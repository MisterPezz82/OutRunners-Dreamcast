/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * aica_pcm.c: the board's MultiPCM played by the Dreamcast's AICA.
 *
 * The emulated MultiPCM (runtime/src/multipcm.c) still takes every register write, so its voice state
 * (sample header, pitch, envelope, level, pan) is decoded exactly; samples are not synthesised there
 * (S32_SOUND_SILENT). After each write the voice is mirrored onto one of 28 AICA channels:
 *  - samples in the chip's fixed first MiB play from 4-bit ADPCM streams (aica_fixed.bin, one per
 *    sample start, made by tools/make_aica_pcm.py; looping restores the decoder state, PCMS 2);
 *  - samples in the two banked 512 KiB windows play as 8-bit from copies of the banked ROM pages, which
 *    are reloaded from pcm.bin when the game switches banks (rare: game start, race music, ending).
 * Conversions (see PVR-RENDERER-DESIGN.md "Audio"): level 0.375 dB steps on both chips; pan 3 dB steps;
 * envelope rate = MultiPCM effective rate (0-63) / 2; decay level x 2; pitch x 44642.86/44100.
 * Level glides are stepped once a frame. LFOs are not mapped yet.
 */
#include "aica_pcm.h"
#include "page_cache.h"
#include <kos.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <dc/spu.h>
#include <dc/g2bus.h>
#include <s32/machine.h>
#include <s32/multipcm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHAN_BASE 0xa0700000u
#define VOICES MULTIPCM_VOICES
#define PAGE (512u * 1024u)

typedef struct { uint32_t chip, offset, samples; } stream_entry;

static stream_entry *streams;
static int stream_count;
static uint32_t fixed_base;                  /* sound RAM offset of aica_fixed.bin */
static uint32_t window_base[2];              /* the banked windows' page copies */
static int window_page[2] = { -1, -1 };
static int chan[VOICES];
static uint32_t reg0[VOICES];                /* each channel's register 0 as last written */
static int playing_window[VOICES];           /* -1 fixed, 0/1 window */
static int tl_now[VOICES];                   /* level as sent (0 loudest, 127), for glides */
static char pcm_path[256];
static bool ready;
volatile uint32_t dc_aica_stats[8];          /* key-ons, unmapped samples, page loads, glide steps,
                                                load failures, sound RAM free, voices sounding, reserved */

static void reg_write(int ch, uint32_t offset, uint32_t value)
{
	g2_write_32(CHAN_BASE + (uint32_t)ch * 0x80u + offset, value);
}

static int load_file(const char *path, uint32_t *offset_out, uint32_t *size_out)
{
	FILE *fp = fopen(path, "rb");
	if (fp) setvbuf(fp, NULL, _IONBF, 0);
	if (!fp) return -1;
	fseek(fp, 0, SEEK_END);
	const long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	const uint32_t at = snd_mem_malloc((size_t)size);
	if (!at) { fclose(fp); return -1; }
	static uint8_t chunk[32768] __attribute__((aligned(32)));
	long done = 0;
	while (done < size)
	{
		const size_t n = fread(chunk, 1, sizeof(chunk), fp);
		if (n == 0) { fclose(fp); return -1; }
		spu_memload(at + (uint32_t)done, chunk, (n + 3) & ~3u);
		done += (long)n;
	}
	fclose(fp);
	*offset_out = at;
	*size_out = (uint32_t)size;
	return 0;
}

int dc_aica_pcm_start(const char *folder)
{
	char path[256];
	uint32_t size;

	if (snd_init() < 0) return -1;
	spu_master_mixer(15, 1);
	snprintf(path, sizeof(path), "%s/aica_fixed.bin", folder);
	if (load_file(path, &fixed_base, &size) != 0) return -2;
	snprintf(path, sizeof(path), "%s/aica_index.bin", folder);
	FILE *fp = fopen(path, "rb");
	if (fp) setvbuf(fp, NULL, _IONBF, 0);
	if (!fp) return -3;
	fseek(fp, 0, SEEK_END);
	stream_count = (int)(ftell(fp) / 12);
	fseek(fp, 0, SEEK_SET);
	streams = malloc((size_t)stream_count * sizeof(stream_entry));
	if (!streams || fread(streams, 12, (size_t)stream_count, fp) != (size_t)stream_count) { fclose(fp); return -4; }
	fclose(fp);
	window_base[0] = snd_mem_malloc(PAGE);
	window_base[1] = snd_mem_malloc(PAGE);
	if (!window_base[0] || !window_base[1]) return -5;
	for (int v = 0; v < VOICES; v++)
	{
		chan[v] = snd_sfx_chn_alloc();
		if (chan[v] < 0) return -6;
		reg0[v] = 0;
		playing_window[v] = -1;
	}
	snprintf(pcm_path, sizeof(pcm_path), "%s/pcm.bin", folder);
	dc_aica_stats[5] = 0;    /* (sound RAM left after loading: 548384 bytes; slot now the slowest window copy) */
	ready = true;
	return 0;
}

static void key_off(int v, bool immediate)
{
	const int ch = chan[v];
	if (immediate)
		reg_write(ch, 0x14, 0x1f);    /* release at once */
	reg0[v] = (reg0[v] & ~0x4000u);
	reg_write(ch, 0x00, reg0[v] | 0x8000u);
}

/* The window page the game's bank register selects, copied to sound RAM a 32 KiB piece a frame (a whole
   512 KiB page at once stalled the game for seconds): true once it is all there. Notes on a window still
   loading are left silent. */
static dc_page_cache *pcm_pages;    /* pcm.bin, 32 KiB pages read by a loader thread */
static int window_loading[2] = { -1, -1 };
static uint32_t window_done[2];

/* (for the overlay: the window's page, or 9 while one is loading) */
int dc_aica_window_state(int window) { return window_page[window] >= 0 ? window_page[window] : 9; }

static bool ensure_page(int window)
{
	const int page = window == 0 ? (s32_board.pcm_bank & 7) : ((s32_board.pcm_bank >> 3) & 7);
	if (window_page[window] == page)
		return true;
	if (window_loading[window] != page)
	{
		/* (notes playing from the page being replaced stop) */
		for (int v = 0; v < VOICES; v++)
			if (playing_window[v] == window && (reg0[v] & 0x4000u))
				key_off(v, true);
		window_page[window] = -1;
		window_loading[window] = page;
		window_done[window] = 0;
	}
	if (!pcm_pages)
	{
		pcm_pages = dc_cache_open_assoc(pcm_path, 32768, 4, 4);
		if (!pcm_pages || dc_cache_start_loader(pcm_pages, 2) != 0) { dc_aica_stats[4]++; return false; }
	}
	/* the next piece, when the loader has it (asked for otherwise): the game never waits for the disc */
	while (window_done[window] < PAGE)
	{
		const uint32_t done = window_done[window];
		int pending = 0;
		const uint64_t t0 = timer_us_gettime64();
		const uint8_t *piece = dc_cache_span_nowait(pcm_pages, (uint32_t)page * PAGE + done, 32768, &pending);
		if (!piece) { if (!pending) dc_aica_stats[4]++; return false; }
		spu_memload(window_base[window] + done, (void *)piece, 32768);
		const uint32_t us = (uint32_t)(timer_us_gettime64() - t0);
		if (us > dc_aica_stats[5]) dc_aica_stats[5] = us;    /* (for the capture: the slowest piece's copy) */
		window_done[window] = done + 32768;
		/* (the next piece asked for at once, so the disc keeps reading) */
	}
	if (window_done[window] < PAGE)
		return false;
	window_page[window] = page;
	window_loading[window] = -1;
	dc_aica_stats[2]++;
	return true;
}

static const stream_entry *find_stream(uint32_t chip)
{
	int lo = 0, hi = stream_count;
	while (lo < hi)
	{
		const int mid = (lo + hi) / 2;
		if (streams[mid].chip == chip) return &streams[mid];
		if (streams[mid].chip < chip) lo = mid + 1; else hi = mid;
	}
	return NULL;
}

static uint32_t pitch_reg(const multipcm_voice *voice)
{
	/* the MultiPCM plays (1 + pitch/1024) * 2^(octave - 1) samples a sample at 10 MHz / 224; the AICA
	   (1 + FNS/1024) * 2^OCT at 44100 Hz */
	int octave = voice->octave & 0xf;
	if (octave & 8) octave -= 16;
	double ratio = (1.0 + voice->pitch / 1024.0) * ldexp(1.0, octave - 1) * (10000000.0 / 224.0) / 44100.0;
	int oct = (int)floor(log2(ratio));
	if (oct < -8) oct = -8;
	if (oct > 7) oct = 7;
	int fns = (int)lround((ratio / ldexp(1.0, oct) - 1.0) * 1024.0);
	if (fns < 0) fns = 0;
	if (fns > 1023) fns = 1023;
	return (uint32_t)(oct & 0xf) << 11 | (uint32_t)fns;
}

/* the MultiPCM's envelope rate (envelope_calc, get_rate: 0-63) as an AICA 5-bit rate */
static uint32_t rate_reg(const multipcm_voice *voice, int value)
{
	if (value == 0) return 0;
	if (value == 0xf) return 0x1f;
	int octave = voice->octave & 0xf;
	if (octave & 8) octave -= 16;
	const int scale = voice->key_rate_scale != 0xf ? (octave + voice->key_rate_scale) * 2 + ((voice->pitch >> 9) & 1) : 0;
	int r = 4 * value + scale;
	r = r < 0 ? 0 : (r > 0x3f ? 0x3f : r);
	return (uint32_t)(r >> 1);
}

/* The board is two cabinets with one sound board: the program pans each cabinet's sounds hard to one
   output (left for one seat, right for the other), so with one console and one player a race's music
   came from one speaker. As the PC port does (sound_chips_join_sides), both sides go to both speakers:
   every voice is centred. DC_AICA_CABINET_PAN: the board's own panning. */
static uint32_t pan_reg(const multipcm_voice *voice)
{
#ifndef DC_AICA_CABINET_PAN
	(void)voice;
	return 0;
#endif
	const int pan = (int)(voice->pan & 0xf);
	if (pan == 0) return 0;
	if (pan < 8) return (pan & 7) == 7 ? 0x0f : (uint32_t)pan * 1u;          /* left quieter */
	return (pan & 7) == 1 ? 0x1f : 0x10u | (uint32_t)(16 - pan);              /* right quieter */
}

/* OPTIONS > VOLUME (0-10, 5 the game's own) as steps of the AICA's level (0.375 dB each: the PC port's
   volume / 5 of the mix), and the pause's silence */
static int volume_steps;
static bool volume_mute, paused_mute;
static bool pan_silent[VOICES];

static void send_level(int v)
{
	int level = tl_now[v] + volume_steps;
	level = level < 0 ? 0 : (level > 0xff ? 0xff : level);
	if (pan_silent[v] || volume_mute || paused_mute) level = 0xff;
	reg_write(chan[v], 0x28, (uint32_t)level << 8 | 0x24);    /* TL, filter off (as KOS's driver sets it) */
}

static void write_level(int v, int tl, const multipcm_voice *voice)
{
	tl_now[v] = tl;
	pan_silent[v] = (voice->pan & 0xf) == 8;    /* (pan 8 silences both sides) */
	send_level(v);
}

void dc_aica_set_volume(int volume)
{
	static const int8_t steps[11] = { 0, 37, 21, 12, 5, 0, -4, -8, -11, -14, -16 };
	volume = volume < 0 ? 0 : (volume > 10 ? 10 : volume);
	volume_steps = steps[volume];
	volume_mute = volume == 0;
	if (ready) for (int v = 0; v < VOICES; v++) send_level(v);
}

void dc_aica_pause(bool paused)
{
	paused_mute = paused;
	if (ready) for (int v = 0; v < VOICES; v++) send_level(v);
}

static void key_on(int v, const multipcm_voice *voice)
{
	const int ch = chan[v];
	uint32_t sa, format;
	const uint32_t start = voice->start & 0x1fffff;
	if (start < 0x100000)
	{
		const stream_entry *e = find_stream(start);
		if (!e) { dc_aica_stats[1]++; return; }
		sa = fixed_base + e->offset;
		format = 2;                       /* ADPCM */
		playing_window[v] = -1;
	}
	else
	{
		const int window = start >= 0x180000 ? 1 : 0;
		if (!ensure_page(window)) { dc_aica_stats[1]++; return; }    /* (still loading: silent) */
		sa = window_base[window] + (start & 0x7ffffu);
		format = 1;                       /* 8-bit */
		playing_window[v] = window;
	}
	uint32_t end = voice->end > 0xffff ? 0xffff : voice->end;
	uint32_t loop = voice->loop < end ? voice->loop : 0;
	key_off(v, true);
	reg_write(ch, 0x04, sa & 0xffff);
	reg_write(ch, 0x08, loop);
	reg_write(ch, 0x0c, end);
	reg_write(ch, 0x10, rate_reg(voice, voice->decay2_reg) << 11 | rate_reg(voice, voice->decay1_reg) << 6 |
		rate_reg(voice, voice->attack_reg));
	reg_write(ch, 0x14, 0xfu << 10 | (uint32_t)(voice->decay_level_reg * 2) << 5 | rate_reg(voice, voice->release_reg));
	reg_write(ch, 0x18, pitch_reg(voice));
	reg_write(ch, 0x1c, 0);                  /* (LFOs not mapped yet) */
	reg_write(ch, 0x20, 0);
	reg_write(ch, 0x24, 0xfu << 8 | pan_reg(voice));
	write_level(v, (int)voice->dest_total_level, voice);
	reg0[v] = 0x4000u | 0x0200u | format << 7 | (sa >> 16 & 0x7f);    /* key on, loop, format, address */
	reg_write(ch, 0x00, reg0[v] | 0x8000u);
	dc_aica_stats[0]++;
}

void dc_aica_pcm_written(const multipcm *chip, int offset, uint8_t data)
{
	(void)data;
	if (!ready || offset != 0 || chip->current_voice < 0 || chip->current_voice >= VOICES)
		return;
	const int v = chip->current_voice;
	const multipcm_voice *voice = &chip->voices[v];
	switch (chip->address)
	{
		case 0:    /* pan */
			reg_write(chan[v], 0x24, 0xfu << 8 | pan_reg(voice));
			write_level(v, tl_now[v], voice);
			break;
		case 1:    /* a new sample: restarts a sounding voice */
			if (voice->playing && (reg0[v] & 0x4000u)) key_on(v, voice);
			break;
		case 2:
		case 3:
			reg_write(chan[v], 0x18, pitch_reg(voice));
			break;
		case 4:
			if (data & 0x80) key_on(v, voice);
			else if (reg0[v] & 0x4000u) key_off(v, voice->release_reg == 0xf);
			break;
		case 5:    /* a level: set at once, or glided to (stepped each frame) */
			if (data & 1) write_level(v, (int)voice->dest_total_level, voice);
			break;
		default:   /* 6-10: LFOs and envelope take effect at the next key on */
			break;
	}
}

void dc_aica_pcm_frame(const multipcm *chip)
{
	if (!ready) return;
	int sounding = 0;
	for (int v = 0; v < VOICES; v++)
	{
		const multipcm_voice *voice = &chip->voices[v];
		sounding += (reg0[v] & 0x4000u) != 0;
		const int dest = (int)voice->dest_total_level;
		if (tl_now[v] == dest) continue;
		/* the MultiPCM glides the full 128 steps in 78.2 ms down, twice that up: per 1/60 s */
		int t = tl_now[v];
		t = dest < t ? (t - 27 < dest ? dest : t - 27) : (t + 14 > dest ? dest : t + 14);
		write_level(v, t, voice);
		dc_aica_stats[3]++;
	}
	dc_aica_stats[6] = (uint32_t)sounding;
	/* windows follow the bank register even between key-ons */
	/* (finished pieces into the cache each frame: one nobody asks for again would hold the loader) */
	if (pcm_pages) dc_cache_pump(pcm_pages);
	(void)ensure_page(0);
	(void)ensure_page(1);
	/* (for the capture: pieces loaded, and window 0's progress in 32 KiB pieces with its page) */
	dc_aica_stats[7] = (uint32_t)dc_cache_pages_loaded(pcm_pages) | (window_done[0] >> 15) << 16 | (uint32_t)(window_loading[0] & 15) << 24;
}
