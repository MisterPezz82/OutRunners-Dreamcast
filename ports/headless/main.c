/**
 *    Name:        main.c
 *    Description: runs the game with no screen or sound, with the controls a script drives, and writes a
 *                 hash of the board state (and of the frames the video draws) after every frame, in the
 *                 format tools/core/system32/reference/s32ref uses
 *    Version:     0.1.1
 *    Created:     11-10-2026
 *    Modified:    07-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Usage: orunners_headless [--frames N] [--hashes FILE] [--script FILE] [--dump-frame N --dump-dir DIR]
 *                             [--video-hashes FILE] [--video-dump N ... --video-dir DIR] [--sound-hashes FILE]
 *                             [--sprite-census FILE] [--tile-census FILE] [--memory-census FILE]
 *                             [--assets FOLDER]
 *    The game's data is its assets (game/assets.h), the files extract_assets.py writes: --assets, or the
 *    folder 'assets' beside the executable, or the one in the current folder (a run from the repository).
 *    The scaffold (ORUNNERS_SCAFFOLD) runs the original code, so it reads the ROM set in rom/ instead.
 *
 *    The board runs with frame timing, as s32ref does by default, and the hashes are the ones s32ref
 *    writes, so the two compare byte by byte. --video-hashes and --video-dump draw every frame of both
 *    screens with the board's video (runtime/src/video.c): the hash of each frame, and frames as BMP, the
 *    left screen beside the right. --sound-hashes runs the sound board too (the decompiled sound program
 *    and the chips, runtime/src/sound_cpu.c and sound_chips.c), and writes for each frame the hash of its
 *    samples and of the sound board's memory. --sprite-census and --tile-census write what the sprites and
 *    the tilemaps draw, with the colors each pen shows, for the asset extractor (tools/palette_census.py).
 *    --screens 1 draws the left screen alone, as the console's window does in one player games
 *    (s32_video_show_screens): the right one keeps its first frame.
 *    --memory-census writes what memory each mem_read and mem_write of the game's C reached, by its line
 *    (runtime/src/memory_census.c), in a build with ORUNNERS_MEMORY_CENSUS (tools/memory_census.py).
 */
#include "s32/bus.h"
#include "s32/machine.h"
#include "s32/program.h"
#include "s32/script.h"
#include "s32/workers.h"
#include "s32/video.h"
#include "sound/chips.h"
#ifdef ORUNNERS_CONSOLE
#include "console.h"
#endif
#ifdef ORUNNERS_SCAFFOLD
#include "game/data.h"
#include "game/extract.h"
#else
#include "game/assets.h"
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef ORUNNERS_CONSOLE
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>    /* Sleep: --real-time */
#else
#include <unistd.h>     /* usleep */
#endif
#endif

#define USAGE "Usage: orunners_headless [--frames N] [--hashes FILE] [--script FILE] [--dump-frame N --dump-dir DIR]" \
	" [--video-hashes FILE] [--video-dump N ... --video-dir DIR] [--sound-hashes FILE]" \
	" [--sprite-census FILE] [--tile-census FILE] [--memory-census FILE] [--assets FOLDER|PACK.assets]" \
	" [--write-pack PACK.assets] [--workers N] [--screens 1|2] [--sound-wav FILE]" \
	" (orunners_console_headless: [--actions FILE] [--wide | --wide-20-9 | --wide-16-10] [--autopilot]" \
	" [--ghost-folder DIR] [--pause-dump N] [--side-map] [--real-time] [--name NAME] [--network-log FRAMES]" \
	" [--restart N])\n"
#define MAX_VIDEO_DUMPS 64
#define CENSUS_SLOTS (1u << 22)     /* open addressing for the censuses' hashes */
#define TILE_CENSUS_EVERY 30        /* the tilemaps are looked at every this many frames */

static uint64_t frames_wanted = 600;
static int screens = 2;               /* --screens: the screens the video draws */
static FILE *hashes;
static s32_script *script;
static uint64_t dump_frame = UINT64_MAX;
static const char *dump_dir;
static FILE *video_hashes;
static uint64_t video_dumps[MAX_VIDEO_DUMPS];
static int video_dump_count;
static const char *video_dir;
#ifdef ORUNNERS_CONSOLE
static uint64_t pause_dump_frame;    /* --pause-dump: the frame drawn with the pause's words over it */
static bool real_time;               /* --real-time: 60 frames a second, as a window's (two games on the network) */
static uint64_t network_log_every;   /* --network-log: the network's state every that many frames */
#endif
static FILE *sound_hashes;
static FILE *sound_wav;            /* --sound-wav: the mix the window plays, 16-bit stereo */
static uint32_t sound_wav_bytes;
static FILE *sprite_census;
static FILE *tile_census;
static uint64_t *census_seen;       /* hashes of what the censuses wrote (0 = free) */

/* Whether a census line with this hash was written already; notes it if not */
static bool census_written(uint64_t hash)
{
	uint32_t slot;

	if (hash == 0)
		hash = 1;
	for (slot = (uint32_t)hash & (CENSUS_SLOTS - 1); census_seen[slot] != 0; slot = (slot + 1) & (CENSUS_SLOTS - 1))
		if (census_seen[slot] == hash)
			return true;
	census_seen[slot] = hash;
	return false;
}

/* --sprite-census: every sprite of the ROM the list draws, with the colors its pens show on its screen, once
   per set of colors and way of drawing it: "ADDRESS WORDS ROWS BITS OPAQUE SCREEN COLOR..." (hex; OPAQUE 1
   when it draws the end code and goes on past it; colors RGB555, one a pen) */
static void on_sprite_drawn(int screen, uint32_t address, int words, int rows, bool bits8, bool opaque,
	const uint16_t *pixels)
{
	const int pens = bits8 ? 256 : 16;
	uint16_t colors[256];
	uint64_t hash = 0xcbf29ce484222325ull;

	for (int pen = 0; pen < pens; pen++)
		colors[pen] = s32_video_sprite_pixel_color(screen, pixels[pen]);
	hash = (hash ^ (address | (uint64_t)screen << 32)) * 0x100000001b3ull;
	hash = (hash ^ (uint64_t)((uint32_t)words << 16 | (uint32_t)rows << 2 | (opaque ? 2u : 0u) | (bits8 ? 1u : 0u)))
		* 0x100000001b3ull;
	for (int pen = 0; pen < pens; pen++)
		hash = (hash ^ colors[pen]) * 0x100000001b3ull;
	if (census_written(hash))
		return;
	fprintf(sprite_census, "%06X %d %d %d %d %d", address, words, rows, bits8 ? 8 : 4, opaque ? 1 : 0, screen);
	for (int pen = 0; pen < pens; pen++)
		fprintf(sprite_census, " %04X", colors[pen]);
	fputc('\n', sprite_census);
}

/* --tile-census: every tile the tilemaps that are on show (every TILE_CENSUS_EVERY frames), with the colors its
   pens show on each screen that is on (the screens share the tilemaps, not the palettes), once per set of
   colors: "TILE COLOR TILEMAP SCREEN COLOR..." (hex; colors RGB555, one a pen) */
static void on_tile_shown(int tilemap, uint32_t tile, uint16_t color)
{
	if (program_frames() % TILE_CENSUS_EVERY != 0)
		return;
	for (int screen = 0; screen < S32_SCREENS; screen++)
	{
		uint16_t colors[16];
		uint64_t hash = 0x84222325cbf29ce4ull;

		if (!s32_display_enabled(screen))
			continue;
		for (int pen = 0; pen < 16; pen++)
			colors[pen] = s32_video_tile_pen_color(screen, tilemap, color, pen);
		hash = (hash ^ (tile | (uint64_t)screen << 32)) * 0x100000001b3ull;
		for (int pen = 0; pen < 16; pen++)
			hash = (hash ^ colors[pen]) * 0x100000001b3ull;
		if (census_written(hash))
			continue;
		fprintf(tile_census, "%04X %03X %d %d", tile, color, tilemap, screen);
		for (int pen = 0; pen < 16; pen++)
			fprintf(tile_census, " %04X", colors[pen]);
		fputc('\n', tile_census);
	}
}

static void write_file(const char *name, const void *data, size_t size)
{
	char path[1024];
	FILE *output;

	snprintf(path, sizeof(path), "%s/%s", dump_dir, name);
	output = fopen(path, "wb");
	if (!output)
	{
		fprintf(stderr, "%s: cannot write\n", path);
		return;
	}
	fwrite(data, 1, size, output);
	fclose(output);
}

static void dump_state(void)
{
	write_file("work_ram.bin", s32_board.work_ram, S32_WORK_RAM_SIZE);
	write_file("video_ram.bin", s32_board.video_ram, sizeof(s32_board.video_ram));
	write_file("sprite_ram.bin", s32_board.sprite_ram, sizeof(s32_board.sprite_ram));
	write_file("palette.bin", s32_board.palette, sizeof(s32_board.palette));
	write_file("mixer.bin", s32_board.mixer, sizeof(s32_board.mixer));
	write_file("shared_ram.bin", s32_board.shared_ram, sizeof(s32_board.shared_ram));
	write_file("comm_ram.bin", s32_board.comm_ram, sizeof(s32_board.comm_ram));
	write_file("eeprom.bin", s32_board.eeprom.data, sizeof(s32_board.eeprom.data));
}

/* The last frames the video drew, as a 24-bit BMP: the left screen, then the right one */
static void write_bmp(const char *path)
{
	int widths[S32_SCREENS] = { 0 };
	const uint32_t *screens[S32_SCREENS];
	int width;
	int row;
	uint32_t size;
	uint8_t *file;
	FILE *output;

	for (int screen = 0; screen < S32_SCREENS; screen++)
		screens[screen] = s32_video_frame(screen, &widths[screen]);
	width = widths[0] + widths[1];
	row = (width * 3 + 3) & ~3;
	size = 54 + (uint32_t)row * S32_VIDEO_HEIGHT;
	file = calloc(size, 1);
	if (!file)
		return;
	file[0] = 'B';
	file[1] = 'M';
	for (int byte = 0; byte < 4; byte++)
	{
		file[2 + byte] = (uint8_t)(size >> (8 * byte));
		file[10 + byte] = (uint8_t)(54 >> (8 * byte));
		file[14 + byte] = (uint8_t)(40 >> (8 * byte));
		file[18 + byte] = (uint8_t)((uint32_t)width >> (8 * byte));
		file[22 + byte] = (uint8_t)((uint32_t)S32_VIDEO_HEIGHT >> (8 * byte));
	}
	file[26] = 1;
	file[28] = 24;
	for (int y = 0; y < S32_VIDEO_HEIGHT; y++)
		for (int x = 0; x < width; x++)
		{
			const int screen = x < widths[0] ? 0 : 1;
			const uint32_t pixel = screens[screen][y * S32_VIDEO_MAX_WIDTH + (screen ? x - widths[0] : x)];
			uint8_t *out = &file[54 + (size_t)(S32_VIDEO_HEIGHT - 1 - y) * (size_t)row + (size_t)x * 3];

			out[0] = (uint8_t)pixel;
			out[1] = (uint8_t)(pixel >> 8);
			out[2] = (uint8_t)(pixel >> 16);
		}
	output = fopen(path, "wb");
	if (output)
	{
		fwrite(file, 1, size, output);
		fclose(output);
	}
	else
		fprintf(stderr, "%s: cannot write\n", path);
	free(file);
}

static void on_irq_changed(bool asserted)
{
	program_irq_line(asserted);
}

static void on_log(const char *message)
{
	fprintf(stderr, "[s32] %s\n", message);
}

#ifdef ORUNNERS_CONSOLE
/* ---- the network, for two games on one computer ---- */

static uint64_t milliseconds(void)
{
	struct timespec now;

	timespec_get(&now, TIME_UTC);
	return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void sleep_for(uint64_t ms)
{
#ifdef _WIN32
	Sleep((DWORD)ms);
#else
	usleep((useconds_t)(ms * 1000u));
#endif
}

/* --real-time: each frame waits for its sixtieth of a second */
static void keep_pace(void)
{
	static uint64_t next_frame;    /* (in 60ths of a millisecond, the frames' own) */
	const uint64_t now = milliseconds() * 60u;

	next_frame += 1000u;
	if (now > next_frame + 15000u)
		next_frame = now;    /* (far behind: start over) */
	if (next_frame > now)
		sleep_for((next_frame - now) / 60u);
}

/* The network's frame, after the game's; while a race waits for a device unheard, its frames alone */
static void network_frame(void)
{
	console_network_frame();
	if (network_log_every && (program_frames() - 1) % network_log_every == 0)
	{
		char text[256];

		console_network_describe(text, sizeof(text));
		printf("%" PRIu64 " %s\n", program_frames() - 1, text);
		fflush(stdout);
	}
	if (real_time)
		keep_pace();
	if (console_network_waiting())
	{
		uint64_t waited = 0;

		while (console_network_waiting())
		{
			sleep_for(16);
			console_network_frame();
			waited++;
		}
		printf("%" PRIu64 " waited %" PRIu64 " frames for a device\n", program_frames() - 1, waited);
		fflush(stdout);
	}
}
#endif

static void on_frame_done(void)
{
	if (hashes)
		fprintf(hashes, "%" PRIu64 " %016" PRIx64 "\n", program_frames() - 1, s32_state_hash());
	if (dump_dir && program_frames() - 1 == dump_frame)
		dump_state();
	if (video_hashes)
		fprintf(video_hashes, "%" PRIu64 " %016" PRIx64 "\n", program_frames() - 1, s32_video_frame_hash());
	if (sound_hashes || sound_wav)
	{
		/* the sound board up to the frame's end, then the samples that start before it */
		if (s32_board.time_passes)
			s32_board.time_passes(s32_board.cycles);
		sound_chips_end_frame(s32_board.cycles);
		if (sound_hashes)
			fprintf(sound_hashes, "%" PRIu64 " %016" PRIx64 " %016" PRIx64 "\n", program_frames() - 1,
				sound_chips_frame_hash(), sound_board_state_hash());
		if (sound_wav)
		{
			size_t count;
			const int16_t *mix = sound_chips_frame_mix(&count);

			if (mix && count)
				sound_wav_bytes += (uint32_t)fwrite(mix, sizeof(int16_t) * 2, count, sound_wav) * 4u;
		}
	}
	for (int index = 0; index < video_dump_count && video_dir; index++)
		if (video_dumps[index] == program_frames() - 1)
		{
			char path[1024];

			snprintf(path, sizeof(path), "%s/frame_%" PRIu64 ".bmp", video_dir, video_dumps[index]);
			write_bmp(path);
		}
#ifdef ORUNNERS_CONSOLE
	if (pause_dump_frame && pause_dump_frame == program_frames() - 1 && video_dir)
	{
		/* the window's pause, its words in the game's letters over the frame as the board stands */
		static uint8_t list[0x1000];
		char path[1024];

		s32_video_overlay_sprites(list, console_pause_sprites(list, sizeof(list), CONSOLE_PAUSE_QUIT));
		s32_video_draw_frame();
		snprintf(path, sizeof(path), "%s/pause_%" PRIu64 ".bmp", video_dir, pause_dump_frame);
		write_bmp(path);
		s32_video_overlay_end();
	}
	network_frame();
#endif
	if (program_frames() >= frames_wanted)
		program_stop();
}

#ifdef ORUNNERS_CONSOLE
/* --actions: the console's menu actions, a line each: the frame, then up, down, left, right, accept, back or
   start ('#' starts a comment) */
#define MAX_ACTIONS 4096

static struct
{
	uint64_t frame;
	uint32_t action;
} actions[MAX_ACTIONS];
static size_t action_count;
static bool autopilot;    /* --autopilot: player 1's wheel keeps the car on the road (console_test_autopilot) */
static uint64_t restart_frame;    /* --restart: the pause's RESTART picked at that frame (console_restart_game) */

static const char *load_actions(const char *path)
{
	static const char *const names[ACTIONS] = { "up", "down", "left", "right", "accept", "back", "start" };
	char line[256], name[32];
	unsigned long long frame;
	FILE *input = fopen(path, "r");

	if (!input)
		return "The actions file cannot be read.";
	while (fgets(line, sizeof(line), input) && action_count < MAX_ACTIONS)
	{
		int action;

		if (line[0] == '#' || sscanf(line, "%llu %31s", &frame, name) != 2)
			continue;
		for (action = 0; action < ACTIONS && strcmp(name, names[action]) != 0; action++)
			;
		if (action == ACTIONS)
		{
			fclose(input);
			return "An action is not one of up, down, left, right, accept, back, start.";
		}
		actions[action_count].frame = frame;
		actions[action_count].action = ACTION_BIT(action);
		action_count++;
	}
	fclose(input);
	return NULL;
}
#endif

static void on_controls_update(void)
{
	/* VBLANK of the board's frame is starting */
	s32_script_apply(script, s32_board.frame);
#ifdef ORUNNERS_CONSOLE
	for (size_t index = 0; index < action_count; index++)
		if (actions[index].frame == s32_board.frame)
			console_actions(actions[index].action);
	s32_video_set_wide_side(console_wide_side());
	if (autopilot)
		s32_board.input.analog[0] = (uint8_t)console_test_autopilot();    /* (player 1's wheel) */
	if (restart_frame != 0u && s32_board.frame == restart_frame)
		console_restart_game();
#endif
}

#ifndef ORUNNERS_SCAFFOLD
static char beside_layout[1100];

/* The game's assets (game/assets.h): 'given', else the folder 'assets' beside the executable, else the one
   in the current folder (a run from the repository); NULL, or what failed */
static const char *load_assets(const char *program, const char *given)
{
	const char *slash = strrchr(program, '/'), *backslash = strrchr(program, '\\');
	const char *last = slash > backslash ? slash : backslash;
	char folder[1024];
	FILE *probe;

	if (given)
	{
		const size_t length = strlen(given);

		/* (a pack, made by --write-pack: one file) */
		return length > 7 && strcmp(given + length - 7, ".assets") == 0 ? assets_load_pack(given) : assets_load(given);
	}
	snprintf(folder, sizeof(folder), "%.*sassets", last ? (int)(last - program + 1) : 0, program);
	snprintf(beside_layout, sizeof(beside_layout), "%s/data/layout.json", folder);
	if ((probe = fopen(beside_layout, "rb")) != NULL)
	{
		fclose(probe);
		return assets_load(folder);
	}
	return assets_load("assets");
}
#endif

int main(int argc, char **argv)
{
	const char *hashes_path = NULL;
	const char *script_path = NULL;
	const char *video_hashes_path = NULL;
	const char *sound_hashes_path = NULL;
	const char *sprite_census_path = NULL;
	const char *tile_census_path = NULL;
	const char *memory_census_path = NULL;
	const char *assets_path = NULL;
	const char *pack_path = NULL;    /* --write-pack FILE */
	bool frames_given = false;

	for (int index = 1; index < argc; index++)
	{
		if (strcmp(argv[index], "--frames") == 0 && index + 1 < argc)
		{
			frames_wanted = strtoull(argv[++index], NULL, 10);
			frames_given = true;
		}
		else if (strcmp(argv[index], "--hashes") == 0 && index + 1 < argc)
			hashes_path = argv[++index];
		else if (strcmp(argv[index], "--script") == 0 && index + 1 < argc)
			script_path = argv[++index];
		else if (strcmp(argv[index], "--dump-frame") == 0 && index + 1 < argc)
			dump_frame = strtoull(argv[++index], NULL, 10);
		else if (strcmp(argv[index], "--dump-dir") == 0 && index + 1 < argc)
			dump_dir = argv[++index];
		else if (strcmp(argv[index], "--video-hashes") == 0 && index + 1 < argc)
			video_hashes_path = argv[++index];
		else if (strcmp(argv[index], "--video-dump") == 0 && index + 1 < argc && video_dump_count < MAX_VIDEO_DUMPS)
			video_dumps[video_dump_count++] = strtoull(argv[++index], NULL, 10);
		else if (strcmp(argv[index], "--video-dir") == 0 && index + 1 < argc)
			video_dir = argv[++index];
		else if (strcmp(argv[index], "--sound-hashes") == 0 && index + 1 < argc)
			sound_hashes_path = argv[++index];
		else if (strcmp(argv[index], "--sound-wav") == 0 && index + 1 < argc)
		{
			sound_wav = fopen(argv[++index], "wb");
			if (!sound_wav)
			{
				fprintf(stderr, "%s: cannot write\n", argv[index]);
				return 1;
			}
			fseek(sound_wav, 44, SEEK_SET);    /* (the header once the size is known) */
		}
		else if (strcmp(argv[index], "--sprite-census") == 0 && index + 1 < argc)
			sprite_census_path = argv[++index];
		else if (strcmp(argv[index], "--tile-census") == 0 && index + 1 < argc)
			tile_census_path = argv[++index];
		else if (strcmp(argv[index], "--memory-census") == 0 && index + 1 < argc)
			memory_census_path = argv[++index];
		else if (strcmp(argv[index], "--assets") == 0 && index + 1 < argc)
			assets_path = argv[++index];
		else if (strcmp(argv[index], "--write-pack") == 0 && index + 1 < argc)
			pack_path = argv[++index];
		else if (strcmp(argv[index], "--workers") == 0 && index + 1 < argc)
			s32_workers_start(atoi(argv[++index]));    /* (the video's rows on that many more threads) */
		else if (strcmp(argv[index], "--screens") == 0 && index + 1 < argc)
			screens = atoi(argv[++index]);
#ifdef ORUNNERS_CONSOLE
		else if (strcmp(argv[index], "--wide") == 0)
			console_window_settings.wide = WIDE_16_9;    /* (OPTIONS > VIDEO > WIDE) */
		else if (strcmp(argv[index], "--wide-20-9") == 0)
			console_window_settings.wide = WIDE_20_9;
		else if (strcmp(argv[index], "--wide-16-10") == 0)
			console_window_settings.wide = WIDE_16_10;
		else if (strcmp(argv[index], "--autopilot") == 0)
			autopilot = true;
		else if (strcmp(argv[index], "--restart") == 0 && index + 1 < argc)
			restart_frame = strtoull(argv[++index], NULL, 10);
		else if (strcmp(argv[index], "--side-map") == 0)
			console_side_map_wanted(true);    /* (a tall screen's room: the route map beside the race) */
		else if (strcmp(argv[index], "--pause-dump") == 0 && index + 1 < argc)
			pause_dump_frame = strtoull(argv[++index], NULL, 10);
		else if (strcmp(argv[index], "--ghost-folder") == 0 && index + 1 < argc)
			console_ghost_folder(argv[++index]);    /* (TIME ATTACK's ghosts: the current folder else) */
		else if (strcmp(argv[index], "--real-time") == 0)
			real_time = true;
		else if (strcmp(argv[index], "--name") == 0 && index + 1 < argc)
			snprintf(console_window_settings.name, sizeof(console_window_settings.name), "%s", argv[++index]);
		else if (strcmp(argv[index], "--network-log") == 0 && index + 1 < argc)
			network_log_every = strtoull(argv[++index], NULL, 10);
		else if (strcmp(argv[index], "--actions") == 0 && index + 1 < argc)
		{
			const char *problem = load_actions(argv[++index]);

			if (problem)
			{
				fprintf(stderr, "%s\n", problem);
				return 1;
			}
		}
#endif
		else
		{
			fprintf(stderr, USAGE);
			return 1;
		}
	}
#ifdef ORUNNERS_SCAFFOLD
	(void)assets_path;
	(void)pack_path;
	{
		const char *problem = game_rom_set_load("rom");

		if (problem)
		{
			fprintf(stderr, "rom: %s\n", problem);
			return 1;
		}
	}
#else
	{
		const char *problem = load_assets(argv[0], assets_path);

		if (problem)
		{
			fprintf(stderr, "%s\n", problem);
			return 1;
		}
		if (pack_path)
		{
			/* --write-pack: the assets loaded, as the pack the devices' builds load */
			if ((problem = assets_pack_write(pack_path)) != NULL)
			{
				fprintf(stderr, "%s: %s\n", pack_path, problem);
				return 1;
			}
			printf("%s written\n", pack_path);
			return 0;
		}
	}
	s32_board.program_at = data_byte_at;
	if (getenv("ORUNNERS_DUMP_ASSETS") && !assets_dump(getenv("ORUNNERS_DUMP_ASSETS")))
		fprintf(stderr, "%s cannot be written\n", getenv("ORUNNERS_DUMP_ASSETS"));
	if (getenv("ORUNNERS_DUMP_UNITS") && !assets_dump_units(getenv("ORUNNERS_DUMP_UNITS")))
		fprintf(stderr, "%s cannot be written\n", getenv("ORUNNERS_DUMP_UNITS"));
#endif
	if (script_path)
	{
		char error[256];

		script = s32_script_load(script_path, error, sizeof(error));
		if (!script)
		{
			fprintf(stderr, "%s\n", error);
			return 1;
		}
		if (!frames_given && s32_script_frames(script))
			frames_wanted = s32_script_frames(script);
	}
	if (hashes_path)
		hashes = fopen(hashes_path, "w");
	if (video_hashes_path)
		video_hashes = fopen(video_hashes_path, "w");
	if (sprite_census_path || tile_census_path)
	{
		census_seen = calloc(CENSUS_SLOTS, sizeof(census_seen[0]));
		if (!census_seen)
		{
			fprintf(stderr, "no memory for the census\n");
			return 1;
		}
	}
	if (sprite_census_path)
	{
		sprite_census = fopen(sprite_census_path, "w");
		s32_video_sprite_observer = on_sprite_drawn;
	}
	if (tile_census_path)
	{
		tile_census = fopen(tile_census_path, "w");
		s32_video_tile_observer = on_tile_shown;
	}

	s32_board.irq_changed = on_irq_changed;
	s32_board.controls_update = on_controls_update;
	s32_board.log = on_log;
	s32_board.frame_timing = true;
#ifdef ORUNNERS_SCAFFOLD
	s32_init(game_program);
#else
	s32_init(NULL);
#endif
	if (video_hashes || video_dump_count || sprite_census || tile_census)
#ifdef ORUNNERS_SCAFFOLD
		s32_video_init(game_tiles, sizeof(game_tiles), game_sprites, sizeof(game_sprites));
#else
		s32_video_init_graphics(assets_graphics());
#endif
	s32_video_show_screens(screens == 1 ? 1 : 3);
	if (sound_hashes_path || sound_wav)
	{
#ifdef ORUNNERS_SCAFFOLD
		s32_board.sound_rom = game_sound;
		s32_board.pcm_rom = game_pcm;
#else
		s32_board.sound_at = assets_sound_data;
		s32_board.sound_byte_at = assets_sound_byte;
		s32_board.pcm_at = assets_pcm_byte;
#endif
		sound_chips_attach();
		sound_cpu_attach();
		if (sound_hashes_path)
			sound_hashes = fopen(sound_hashes_path, "w");
	}
#ifdef ORUNNERS_CONSOLE
	sound_chips_join_sides(true);
	console_attach();
#endif
	program_reset();
	program_frame_done = on_frame_done;

	program_run();

	if (hashes)
		fclose(hashes);
	if (video_hashes)
		fclose(video_hashes);
	if (sound_hashes)
		fclose(sound_hashes);
	if (sound_wav)
	{
		/* a WAV header: PCM, 2 channels, 16 bits, at the mix's rate */
		const uint32_t rate = sound_chips_mix_rate();
		const uint32_t fields[] = { 0x46464952u, 36u + sound_wav_bytes, 0x45564157u, 0x20746D66u, 16u,
			0x00020001u, rate, rate * 4u, 0x00100004u, 0x61746164u, sound_wav_bytes };

		fseek(sound_wav, 0, SEEK_SET);
		for (size_t field = 0; field < sizeof(fields) / sizeof(fields[0]); field++)
		{
			const uint8_t bytes[4] = { (uint8_t)fields[field], (uint8_t)(fields[field] >> 8),
				(uint8_t)(fields[field] >> 16), (uint8_t)(fields[field] >> 24) };

			fwrite(bytes, 1, 4, sound_wav);
		}
		fclose(sound_wav);
	}
	if (sprite_census)
		fclose(sprite_census);
	if (tile_census)
		fclose(tile_census);
	free(census_seen);
	if (memory_census_path)
	{
#ifdef S32_MEMORY_CENSUS
		FILE *census = fopen(memory_census_path, "w");

		if (census)
		{
			s32_memory_census_write(census);
			fclose(census);
		}
		else
			fprintf(stderr, "%s: cannot be written\n", memory_census_path);
#else
		fprintf(stderr, "--memory-census: this build does not count memory accesses\n");
#endif
	}
	s32_script_free(script);
	printf("frames:            %" PRIu64 "\n", program_frames());
	printf("interrupts taken:  %u\n", program_interrupts());
	printf("sprite lists:      %u\n", s32_board.sprite_lists);
	printf("sound requests:    %u\n", s32_board.sound_requests);
	printf("unmapped reads:    %u\n", s32_board.unmapped_reads);
	printf("unmapped writes:   %u\n", s32_board.unmapped_writes);
	printf("eeprom written:    %s\n", s32_board.eeprom.dirty ? "yes" : "no");
	return 0;
}
