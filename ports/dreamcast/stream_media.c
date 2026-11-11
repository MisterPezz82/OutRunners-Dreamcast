/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * stream_media.c: the game's tile and sprite graphics, kept whole in main RAM in compressed pages
 * (tiles4.z, sprites.z: tools/dreamcast/pack_compressed.py) and unpacked a page at a time into small
 * caches of recently used pages. Nothing is read from the disc while playing: a missing page costs a
 * predictable unpack (runtime/src/inflate.c) instead of a disc read, whose time on real hardware (GDEMU
 * included) is neither short nor predictable. The PCM samples alone are still read from the disc (a
 * small cache of pcm.bin; the AICA's sample banks have their own loader, aica_pcm.c).
 */

#include "stream_media.h"
#include "page_cache.h"
#include "../../runtime/src/inflate.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#if defined(_WIN32)
#define aligned_alloc(alignment, size) malloc(size)    /* (the alignment is for the Dreamcast's disc DMA; Windows
                                                         has no aligned_alloc that free() takes) */
#endif
#include <stdbool.h>
#ifdef _arch_dreamcast
#include <arch/timer.h>
#define ROW_CLOCK() timer_us_gettime64()
#else
#define ROW_CLOCK() 0
#endif

/* ---- a compressed file in RAM, and its cache of unpacked pages ---- */

typedef struct zmedia
{
	uint8_t *file;                 /* the whole .z file */
	const uint32_t *offsets;       /* page count + 1, into data */
	const uint8_t *data;
	uint32_t page_size, page_count, size;
	uint32_t slots, ways, clock;
	uint8_t *payload;              /* slots pages */
	uint32_t *tags, *stamps;
	uint32_t unpacks, failures;
	uint32_t unpack_us_max;
} zmedia;

static zmedia s_tiles, s_sprites;
static dc_page_cache *s_pcm_cache = NULL;
static int s_failed;

/* the whole file, read 64 KiB at a time into a 32-byte aligned buffer (the disc's DMA) */
static uint8_t *read_whole(const char *path, uint32_t *size_out)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) return NULL;
	setvbuf(fp, NULL, _IONBF, 0);
	long size = -1;
	if (fseek(fp, 0, SEEK_END) == 0) size = ftell(fp);
	if (size <= 16 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
	uint8_t *buf = (uint8_t *)aligned_alloc(32, ((size_t)size + 31) & ~(size_t)31);
	if (!buf) { fclose(fp); return NULL; }
	for (long at = 0; at < size; )
	{
		const size_t part = size - at > 65536 ? 65536 : (size_t)(size - at);
		if (fread(buf + at, 1, part, fp) != part) { free(buf); fclose(fp); return NULL; }
		at += (long)part;
	}
	fclose(fp);
	*size_out = (uint32_t)size;
	return buf;
}

static int zmedia_open(zmedia *z, const char *path, uint32_t slots, uint32_t ways)
{
	memset(z, 0, sizeof(*z));
	uint32_t size = 0;
	z->file = read_whole(path, &size);
	if (!z->file) return -1;
	uint32_t head[4];
	memcpy(head, z->file, 16);
	z->page_size = head[1]; z->page_count = head[2]; z->size = head[3];
	if (memcmp(z->file, "ORZ1", 4) != 0 || z->page_size == 0 || z->page_count == 0 ||
		16u + 4u * (z->page_count + 1u) > size)
		goto fail;
	z->offsets = (const uint32_t *)(z->file + 16);
	z->data = z->file + 16 + 4 * (z->page_count + 1);
	if (z->offsets[z->page_count] > size - (uint32_t)(z->data - z->file)) goto fail;
	z->slots = slots; z->ways = ways;
	z->payload = (uint8_t *)aligned_alloc(32, (size_t)slots * z->page_size);
	z->tags = (uint32_t *)malloc(slots * sizeof(uint32_t));
	z->stamps = (uint32_t *)calloc(slots, sizeof(uint32_t));
	if (!z->payload || !z->tags || !z->stamps) goto fail;
	for (uint32_t i = 0; i < slots; i++) z->tags[i] = 0xffffffffu;
	return 0;
fail:
	free(z->file); free(z->payload); free(z->tags); free(z->stamps);
	memset(z, 0, sizeof(*z));
	return -1;
}

static void zmedia_close(zmedia *z)
{
	free(z->file); free(z->payload); free(z->tags); free(z->stamps);
	memset(z, 0, sizeof(*z));
}

/* A page unpacked, or NULL (may_unpack false and the page not unpacked yet, or a bad page) */
static const uint8_t *zmedia_page(zmedia *z, uint32_t page, bool may_unpack)
{
	if (!z->file || page >= z->page_count) return NULL;
	const uint32_t sets = z->slots / z->ways, base = (page % sets) * z->ways;
	for (uint32_t w = 0; w < z->ways; w++)
		if (z->tags[base + w] == page)
		{
			z->stamps[base + w] = ++z->clock;
			return z->payload + (size_t)(base + w) * z->page_size;
		}
	if (!may_unpack) return NULL;
	uint32_t slot = base;
	for (uint32_t w = 1; w < z->ways; w++)
		if (z->stamps[base + w] < z->stamps[slot]) slot = base + w;
	uint8_t *out = z->payload + (size_t)slot * z->page_size;
	const uint32_t at = z->offsets[page], len = z->offsets[page + 1] - at;
	const uint64_t t0 = ROW_CLOCK();
	int ok;
	if (len == z->page_size) { memcpy(out, z->data + at, len); ok = 1; }
	else ok = inflate_raw(z->data + at, len, out, z->page_size);
	const uint32_t us = (uint32_t)(ROW_CLOCK() - t0);
	if (us > z->unpack_us_max) z->unpack_us_max = us;
	if (!ok) { z->tags[slot] = 0xffffffffu; z->failures++; s_failed = 1; return NULL; }
	z->tags[slot] = page;
	z->stamps[slot] = ++z->clock;
	z->unpacks++;
	return out;
}

/* ---- tiles ---- */

#define UNPACKED 64
static uint8_t s_unpacked[UNPACKED][256];
static uint32_t s_unpacked_tile[UNPACKED];
static int s_unpacked_next;
static uint8_t s_empty_tile[256];

const uint8_t *dc_media_tile(uint32_t tile)
{
	const uint32_t index = tile % 32768u;
	for (int i = 0; i < UNPACKED; i++)
		if (s_unpacked_tile[i] == index) return s_unpacked[i];
	const uint32_t at = index * 128u;    /* (two pens a byte: tiles4) */
	const uint8_t *page = zmedia_page(&s_tiles, at / s_tiles.page_size, true);
	if (!page) return s_empty_tile;
	const uint8_t *in = page + at % s_tiles.page_size;
	uint8_t *out = s_unpacked[s_unpacked_next];
	s_unpacked_tile[s_unpacked_next] = index;
	s_unpacked_next = (s_unpacked_next + 1) % UNPACKED;
	for (int i = 0; i < 128; i++)
	{
		out[2 * i] = in[i] & 15u;
		out[2 * i + 1] = in[i] >> 4;
	}
	return out;
}

/* ---- sprites ---- */

/* Pages a frame may unpack for the renderer (a few milliseconds each on the console); beyond them a
   sprite waits a frame, drawn meanwhile with its earlier texture where render_emit has one */
/* (It was 6 a frame. On the console that was what left sprites out at scene changes: once the six were
   spent every new sprite waited, while the frame sat idle waiting for the PowerVR. Sprite builds are
   already bounded by time, in rt_prepare by the wait itself, so the count is now only a backstop.) */
#define SPRITE_UNPACKS_A_FRAME 64
static int s_sprite_unpacks_left = SPRITE_UNPACKS_A_FRAME;

uint32_t dc_media_sprite_word(uint32_t word)
{
	if (word >= 4194304u) return 0xffffffffu;
	const uint32_t at = word * 4u;
	const uint8_t *page = zmedia_page(&s_sprites, at / s_sprites.page_size, true);
	if (!page) return 0xffffffffu;
	const uint8_t *p = page + at % s_sprites.page_size;
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

volatile uint32_t dc_row_us_frame;    /* (time in sprite-row reads this frame, for the capture) */

/* A row of sprite words: 1 done, -1 a page it needs is not unpacked yet and this frame's unpacks are
   spent (tried again next frame), 0 out of range */
int dc_media_sprite_row(uint32_t word, int count, uint32_t *out)
{
	const uint64_t t = ROW_CLOCK();
	int result = 1;
	if (!s_sprites.file || word + (uint32_t)count > 4194304u)
		result = 0;
	else
	{
		uint32_t offset = word * 4u, bytes = (uint32_t)count * 4u;
		uint8_t *dst = (uint8_t *)out;
		while (bytes)
		{
			const uint32_t page_no = offset / s_sprites.page_size;
			const uint32_t room = s_sprites.page_size - offset % s_sprites.page_size;
			const uint32_t part = bytes < room ? bytes : room;
			const uint8_t *page = zmedia_page(&s_sprites, page_no, false);
			if (!page && s_sprite_unpacks_left > 0)
			{
				s_sprite_unpacks_left--;
				page = zmedia_page(&s_sprites, page_no, true);
			}
			if (!page) { result = -1; break; }
			memcpy(dst, page + offset % s_sprites.page_size, part);
			dst += part; offset += part; bytes -= part;
		}
	}
	dc_row_us_frame += (uint32_t)(ROW_CLOCK() - t);
	return result;
}

/* Once a frame: the renderer's unpack allowance again */
void dc_media_pump(void)
{
	s_sprite_unpacks_left = SPRITE_UNPACKS_A_FRAME;
}

/* ---- PCM (still on the disc: rarely read) ---- */

uint8_t dc_media_pcm_byte(uint32_t place)
{
	if (s_pcm_cache == NULL) return 0xff;
	const uint8_t *span = dc_cache_span(s_pcm_cache, place % 4194304u, 1);
	return span ? span[0] : 0xff;
}

/* ---- open, close, evidence ---- */

int dc_media_open(const char *folder)
{
	char path[512];
	memset(s_empty_tile, 0x0f, sizeof(s_empty_tile));
	for (int i = 0; i < UNPACKED; i++) s_unpacked_tile[i] = 0xffffffffu;
	s_failed = 0;
	/* tiles: 4 KiB pages (32 tiles), 32 of them unpacked; sprites: 8 KiB pages, 128 of them (1 MiB) */
	snprintf(path, sizeof(path), "%s/tiles4.z", folder);
	if (zmedia_open(&s_tiles, path, 32, 4) != 0) return -1;
	snprintf(path, sizeof(path), "%s/sprites.z", folder);
	if (zmedia_open(&s_sprites, path, 128, 4) != 0) { zmedia_close(&s_tiles); return -1; }
	snprintf(path, sizeof(path), "%s/pcm.bin", folder);
	s_pcm_cache = dc_cache_open_assoc(path, 8192, 8, 4);
	if (!s_pcm_cache) { zmedia_close(&s_tiles); zmedia_close(&s_sprites); return -1; }
	return 0;
}

void dc_media_close(void)
{
	zmedia_close(&s_tiles);
	zmedia_close(&s_sprites);
	if (s_pcm_cache) { dc_cache_close(s_pcm_cache); s_pcm_cache = NULL; }
}

size_t dc_media_cache_bytes(void)
{
	return (size_t)s_tiles.slots * s_tiles.page_size + (size_t)s_sprites.slots * s_sprites.page_size +
		(s_pcm_cache ? dc_cache_bytes(s_pcm_cache) : 0);
}

int dc_media_failed(void)
{
	return !s_tiles.file || !s_sprites.file || !s_pcm_cache || s_failed || dc_cache_failed(s_pcm_cache);
}

/* Debug evidence for the Flycast capture: tile and sprite page unpacks, PCM page misses */
volatile uint32_t dc_media_misses[3];

void dc_media_update_stats(void)
{
	dc_media_misses[0] = s_tiles.unpacks;
	dc_media_misses[1] = s_sprites.unpacks;
	if (s_pcm_cache) dc_media_misses[2] = (uint32_t)dc_cache_misses(s_pcm_cache);
}

uint32_t dc_media_unpack_us_peek(void)    /* (the same, without starting a new window) */
{
	return s_tiles.unpack_us_max > s_sprites.unpack_us_max ? s_tiles.unpack_us_max : s_sprites.unpack_us_max;
}

uint32_t dc_media_unpack_us_max(void)
{
	const uint32_t m = s_tiles.unpack_us_max > s_sprites.unpack_us_max ? s_tiles.unpack_us_max : s_sprites.unpack_us_max;
	s_tiles.unpack_us_max = s_sprites.unpack_us_max = 0;
	return m;
}

/* Debug evidence: the largest heap block still free */
volatile uint32_t dc_heap_largest = 0;

void dc_media_probe_heap(void)
{
	uint32_t lo = 0, hi = 16u << 20;
	while (hi - lo > 4096u)
	{
		uint32_t mid = lo + (hi - lo) / 2u;
		void *p = malloc(mid);
		if (p) { free(p); lo = mid; } else { hi = mid; }
	}
	dc_heap_largest = lo;
}
