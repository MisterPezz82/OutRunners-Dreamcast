/**
 *    Name:        main.c
 *    Description: the game in a window: SDL2 shows its screens, plays its sound, reads the controls and
 *                 keeps the pace, around the same game and board the headless runs check
 *    Version:     0.1.1
 *    Created:     21-10-2026
 *    Modified:    04-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Usage: "OutRunners Arcade" or OutRunners [--script FILE [--fast]] [--hashes FILE] [--video-hashes FILE]
 *                    [--sound-hashes FILE] [--assets FOLDER]
 *
 *    The game (src/) and the board (runtime/) run as they are: this file only wraps them. Where the board
 *    has its hooks, the port does its part: when VBLANK starts the video draws both screens and the port
 *    puts them in the window, the left screen beside the right (the console version, OutRunners: the left
 *    one alone, both in 2 PLAYERS); when the board reads the controls the port hands it the keyboard and
 *    the game controllers; when the game finishes a frame the port queues the frame's sound and waits for
 *    the frame's time (60 a second) to pass. What the board keeps with the power off is kept in two files,
 *    beside the executable on Windows and in the user's folder for the game elsewhere (saves_folder):
 *    orunners.eeprom the EEPROM (the settings and their copy), orunners.bookkeeping the part of the sound
 *    board's RAM the game keeps on the battery (the bookkeeping, the ten course records and the credits
 *    among it, its checksum and its copy); the console version's are outrunners.eeprom and
 *    outrunners.bookkeeping.
 *
 *    The game's data is its assets (game/assets.h), the files extract_assets.py writes from the player's
 *    ROM set: --assets, or the folder 'assets' beside the executable, beside the .app on macOS, in the
 *    folder of what the game keeps, or in the current folder. Where there is none, the first run takes them
 *    out of the ROM set itself into the folder of what the game keeps (first_run.c, runtime/src/extract.c):
 *    orunners.zip or rom/ beside the game or in that folder, or a set dropped on its window.
 *
 *    The game devices (ORUNNERS_HANDHELD) make nothing from the ROM set: their assets pack (outrunners.assets)
 *    comes with the game, made on a computer, and their picture is fixed (fix_window_settings). On Anbernic's
 *    handhelds it is beside the executable, where the game keeps what it writes; on the Switch it is in the
 *    .nro's or the .nsp's romfs, and what the game writes goes to /switch/outrunners/ on the SD card, with
 *    outrunners.log.
 *
 *    --script plays a control script (runtime/include/s32/script.h) instead of the keyboard, to its end,
 *    from a blank EEPROM and sound RAM that it does not keep; --fast runs it without waiting for the frames'
 *    time.
 *    --hashes, --video-hashes and --sound-hashes write for each frame the hashes ports/headless and
 *    tools/core/system32/reference/s32ref write, so a script run in the window compares with the reference
 *    frame by frame.
 *
 *    Controls (the cabinet's player 1; player 2 on the second game controller):
 *      left, right arrows             the wheel (it comes back to the centre when let go)
 *      up arrow, down arrow           gas, brake
 *      Q, A                           shift up, shift down
 *      space                          DJ (the music)      [ ]    << and >> (the song)
 *      5, 6                           coins 1-2           1, 2   starts 1-2
 *      9                              service             F2     test (the operator's menu)
 *      Escape                         quit                F11 or Alt+Enter   full screen
 *      F3                             smooth or sharp pixels
 *    Game controllers: the left stick is the wheel, the right trigger the gas, the left trigger the brake,
 *    the right and left shoulders shift up and down, Y the DJ, the pad's left and right << and >>, Start
 *    the start, Back a coin.
 */
#include "s32/machine.h"
#include "s32/program.h"
#include "s32/script.h"
#include "s32/video.h"
#include "sound/chips.h"
#include "game/assets.h"
#ifndef ORUNNERS_HANDHELD    /* (a device makes nothing from the ROM set: its pack comes with the game) */
#include "first_run.h"
#include "game/extract.h"
#endif
#include "s32/workers.h"
#ifdef ORUNNERS_CONSOLE
#include "console.h"
#include "controls.h"
#endif
#ifdef ORUNNERS_TOUCH
#include "touch.h"
#endif

#include <SDL.h>

#ifdef __SWITCH__
#include <switch.h>        /* romfsInit: the game's own files, its .nro's or .nsp's */
#include <sys/stat.h>
#define SWITCH_FOLDER "sdmc:/switch/outrunners"    /* the game's folder on the SD card */
#endif
#if defined(ORUNNERS_HANDHELD) && !defined(_WIN32) && !defined(__SWITCH__)
#include <unistd.h>        /* fsync: the log on the card as it is written */
#endif

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The console version (console/, OutRunners) shows the left seat's screen alone, both in 2 PLAYERS, and
   plays both sides' sound on both speakers; the arcade one (OutRunners Arcade) both screens, each side's
   sound on its own speaker. Each keeps its own files */
#ifdef ORUNNERS_CONSOLE
#define TITLE               "OutRunners"
#define FILE_NAME           "outrunners"
#else
#define TITLE               "OutRunners Arcade"
#define FILE_NAME           "orunners"
#endif
#define PLAYERS             2
#define WINDOW_SCALE        2
#define QUEUE_FRAMES_MAX    6          /* sound queued beyond this many frames is dropped */
#define WHEEL_LEFT          0x10       /* the wheel's travel, as MAME has it */
#define WHEEL_CENTRE        0x80
#define WHEEL_RIGHT         0xf0
#define WHEEL_TURN          6          /* a frame, while a key holds it */
#define WHEEL_RETURN        8          /* a frame, back to the centre */
#define PEDAL_PRESS         0x20       /* a frame, while a key holds it */
#define PEDAL_LIFT          0x30
#define STICK_DEAD_ZONE     4000
#define BACKUP_RAM_START    0x1f00     /* what the game keeps of the sound board's RAM: 701F00-701FF9 */
#define ASSETS_PACK         "outrunners.assets"    /* the assets in one file (game/assets.h), the devices' */
#ifdef __SWITCH__
#define VIDEO_WORKERS       2          /* the video's rows on cores 1 and 2 (the game's is 0; 3 is the system's) */
#else
#define VIDEO_WORKERS       SDL_min(SDL_GetCPUCount() - 1, 3)    /* up to three more cores (a handheld's four) */
#endif
#ifdef ORUNNERS_HANDHELD_KEYS
#define HANDHELD_MENU_BUTTON 8         /* the joystick button that leaves the game: the RG40XX H's MENU */
#endif
#define BACKUP_RAM_SIZE     0xfa

#define USAGE "Usage: orunners [--script FILE [--fast]] [--hashes FILE] [--video-hashes FILE] [--sound-hashes FILE]" \
	" [--assets FILE]"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static SDL_AudioDeviceID audio;
static SDL_GameController *controllers[PLAYERS];
static bool smooth;
static uint64_t next_frame;           /* performance counter value the next frame waits for */
static char eeprom_path[1024];
static char backup_ram_path[1024];
static uint8_t backup_ram_saved[BACKUP_RAM_SIZE];    /* as the file has it */
static s32_script *script;
static bool fast;
static FILE *hashes;
static FILE *video_hashes;
static FILE *sound_hashes;

/* The wheels and pedals as the keyboard leaves them, frame to frame */
static int wheel[PLAYERS] = { WHEEL_CENTRE, WHEEL_CENTRE };
static int gas[PLAYERS];
static int brake[PLAYERS];

/* ---- files ---- */

static void save_eeprom(void)
{
	FILE *output;

	if (script || !s32_board.eeprom.dirty)    /* (a script starts from a blank EEPROM: the player's stays) */
		return;
	output = fopen(eeprom_path, "wb");
	if (!output)
		return;
	fwrite(s32_board.eeprom.data, 1, sizeof(s32_board.eeprom.data), output);
	fclose(output);
	s32_board.eeprom.dirty = false;
}

static void load_eeprom(void)
{
	FILE *input = fopen(eeprom_path, "rb");
	uint16_t data[S32_EEPROM_WORDS];

	if (!input)
		return;
	if (fread(data, 1, sizeof(data), input) == sizeof(data))
		memcpy(s32_board.eeprom.data, data, sizeof(data));
	fclose(input);
}

/* The sound board's 8 KB RAM is on the board's battery. The game keeps its bookkeeping in it (the checksum
   at 701F00, the block from 701F02 with the course records at 701F32 and the credits at 701F74, its copy
   from 701F7E: bookkeeping_check, load_course_records); the rest is the sound program's, which clears
   E000-FDFF (700000-701DFF) when it starts, and the game clears 701FFF. MAME keeps none of it */
static void save_backup_ram(void)
{
	const uint8_t *kept = &s32_board.shared_ram[BACKUP_RAM_START];
	FILE *output;

	if (script || memcmp(kept, backup_ram_saved, BACKUP_RAM_SIZE) == 0)    /* (a script's stays apart too) */
		return;
	output = fopen(backup_ram_path, "wb");
	if (!output)
		return;
	if (fwrite(kept, 1, BACKUP_RAM_SIZE, output) == BACKUP_RAM_SIZE)
		memcpy(backup_ram_saved, kept, BACKUP_RAM_SIZE);
	fclose(output);
}

static void load_backup_ram(void)
{
	FILE *input = fopen(backup_ram_path, "rb");
	uint8_t data[BACKUP_RAM_SIZE];

	if (!input)
		return;
	if (fread(data, 1, sizeof(data), input) == sizeof(data))
	{
		memcpy(&s32_board.shared_ram[BACKUP_RAM_START], data, sizeof(data));
		memcpy(backup_ram_saved, data, sizeof(data));
	}
	fclose(input);
}

/* ---- the picture ---- */

/* The screens the window shows, from the left one: the arcade's two; the console's left one, both in
   2 PLAYERS */
static int screens_shown(void)
{
#ifdef ORUNNERS_CONSOLE
	return console_both_screens() ? 2 : 1;
#else
	return 2;
#endif
}

#ifdef ORUNNERS_TOUCH
#define TABLET_SIDE_INCHES 4.0    /* a tablet's screen (an open Fold's too): its short side at least this long */

static bool tablet;               /* the screen is a tablet's: 2 PLAYERS there, the two screens facing each other */

/* A tablet's screen, by its pixels and their density */
static bool tablet_screen(int width, int height)
{
	float horizontal = 0.0f, vertical = 0.0f;
	const int display = window ? SDL_GetWindowDisplayIndex(window) : 0;    /* (before the window, the first one's) */

	if (SDL_GetDisplayDPI(display, NULL, &horizontal, &vertical) != 0 || horizontal <= 0.0f || vertical <= 0.0f)
		return false;
	return SDL_min(width / horizontal, height / vertical) >= TABLET_SIDE_INCHES;
}
#endif

/* 2 PLAYERS on a tablet: the screens facing each other, the second turned for the player across */
static bool screens_facing(void)
{
#ifdef ORUNNERS_TOUCH
	return screens_shown() == 2 && tablet;
#else
	return false;
#endif
}

/* One screen over the other (the console's 2 PLAYERS, VERTICAL, or facing each other) */
static bool screens_stacked(void)
{
#ifdef ORUNNERS_CONSOLE
	return screens_shown() == 2 && (console_window_settings.vertical || screens_facing());
#else
	return false;
#endif
}

/* The window's picture, in the game's pixels: its screens side by side, or one over the other */
/* A screen's picture, in the game's pixels: 320, or with the console's WIDE 16:9 or 20:9 */
#define WIDE_16_9_WIDTH 398    /* 16:9 at 224 rows: the video's 416 less 9 at each side */
#define WIDE_20_9_WIDTH 496    /* the video's 496, whole (20:9 is 498) */

static int screen_width(void)
{
#ifdef ORUNNERS_CONSOLE
	if (console_window_settings.wide == WIDE_16_9)
		return WIDE_16_9_WIDTH;
	if (console_window_settings.wide == WIDE_20_9)
		return WIDE_20_9_WIDTH;
#endif
	return 320;
}

static void picture_size(int *width, int *height)
{
	*width = (screens_stacked() ? 1 : screens_shown()) * screen_width();
	*height = (screens_stacked() ? 2 : 1) * S32_VIDEO_HEIGHT;
}

#if defined(ORUNNERS_CONSOLE) && ((defined(ORUNNERS_SCREEN_WIDE) && defined(__ANDROID__)) || defined(ORUNNERS_TOUCH_TEST))
/* WIDE as the screen's shape asks, the window the whole screen: Android's (OPTIONS has no VIDEO there), and the
   touch controls' check on a computer, whose window is the phone's screen. The screen can change its shape
   mid-game, a Fold opened or closed (Sebas): WIDE is chosen again then, the picture's new width from the next
   frame (on_controls_update sees it) */
#define WIDE_BY_SCREEN

static int shape_width, shape_height;    /* the screen WIDE was last chosen for */

/* WIDE for a screen of 'width' x 'height' (either way up: its long side across): 20:9 where it is twice as wide
   as high or more, 16:9 down to 3:2; a narrower one's none, the game's 4:3 picture filling it (a 4:3 handheld's:
   issue 15), but a tablet's 16:9, the picture at the top and the controls and the route map under it (an open
   Fold's: Sebas) */
static int wide_for_screen(int width, int height)
{
	const int long_side = SDL_max(width, height), short_side = SDL_min(width, height);

	if (long_side >= 2 * short_side)
		return WIDE_20_9;
	return 2 * long_side >= 3 * short_side || tablet_screen(width, height) ? WIDE_16_9 : WIDE_OFF;
}

/* The screen's size at a frame, as the renderer has it: WIDE chosen again where it changed (the log says so) */
static void follow_screen(int width, int height)
{
	if (width <= 0 || height <= 0 || (width == shape_width && height == shape_height))
		return;
	shape_width = width;
	shape_height = height;
	console_window_settings.wide = wide_for_screen(width, height);
	fprintf(stderr, "screen %dx%d: WIDE %s\n", width, height, console_window_settings.wide == WIDE_20_9 ? "20:9"
		: console_window_settings.wide == WIDE_16_9 ? "16:9" : "OFF");
}
#endif

/* ---- the pixels: sharp, smooth ("sharp bilinear": a whole number of times with sharp pixels into 'scaled',
   then that softened the rest of the way, so only the pixels' edges blend), plain bilinear, or CRT (a
   multiplying overlay on smooth pixels, the size the picture is shown at), as SegaSonic's ---- */

#define CRT_SCANLINE_DARK  0.45    /* how much the scanlines' edges darken (their middles keep it all) */
#define CRT_MASK           0.80    /* the other two channels in a phosphor column */
#define CRT_VIGNETTE       0.18    /* how much the corners darken */

static SDL_Texture *scaled;
static int scaled_times;
static SDL_Texture *crt_overlay;
static int crt_width, crt_height, crt_rows;

/* For each pixel of the shown picture: its game row's scanline (bright in the middle, dark at the edges),
   the column's phosphor (red, green, blue in turn), and the vignette toward the edges */
static void make_crt_overlay(int width, int height, int rows)
{
	uint32_t *pixels;
	int pitch = 0;

	if (crt_overlay && crt_width == width && crt_height == height && crt_rows == rows)
		return;
	if (crt_overlay)
		SDL_DestroyTexture(crt_overlay);
	crt_overlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
	crt_width = width;
	crt_height = height;
	crt_rows = rows;
	if (!crt_overlay || SDL_LockTexture(crt_overlay, NULL, (void **)&pixels, &pitch) != 0)
		return;
	for (int y = 0; y < height; y++)
	{
		const double row = (y + 0.5) * rows / height;
		const double scan = 1.0 - CRT_SCANLINE_DARK * (1.0 - sin(3.14159265358979 * (row - floor(row))));
		const double dy = (2.0 * y / height - 1.0);

		for (int x = 0; x < width; x++)
		{
			const double dx = (2.0 * x / width - 1.0);
			const double light = scan * (1.0 - CRT_VIGNETTE * (dx * dx * dy * dy));
			const int phosphor = x % 3;
			const int red = (int)(255.0 * light * (phosphor == 0 ? 1.0 : CRT_MASK));
			const int green = (int)(255.0 * light * (phosphor == 1 ? 1.0 : CRT_MASK));
			const int blue = (int)(255.0 * light * (phosphor == 2 ? 1.0 : CRT_MASK));

			pixels[y * (pitch / 4) + x] = 0xFF000000u | (uint32_t)red << 16 | (uint32_t)green << 8 | (uint32_t)blue;
		}
	}
	SDL_UnlockTexture(crt_overlay);
	SDL_SetTextureBlendMode(crt_overlay, SDL_BLENDMODE_MOD);
}

/* The pixels the window shows: the console's OPTIONS > VIDEO; the arcade's F3, sharp or smooth */
static int pixels_kind(void)
{
#ifdef ORUNNERS_CONSOLE
	return console_window_settings.pixels;
#else
	return smooth ? 1 : 0;
#endif
}

#ifdef ORUNNERS_CONSOLE
/* ---- the pause ---- */

static bool pause_requested;
static bool pause_shown;            /* the window shows pause_pixels, not the game's frame */
static uint32_t pause_pixels[2][S32_VIDEO_MAX_WIDTH * S32_VIDEO_HEIGHT];
static int pause_widths[2];
#endif

#ifdef ORUNNERS_TOUCH
/* The route map beside the race (console_side_map_shown): the right screen's frame, its board's columns, in
   the texture's corner the screens leave free */
#define SIDE_MAP_WIDTH 320
#define SIDE_MAP_CAR_HEIGHT 16    /* the car on the map, above its place */

static const SDL_Rect side_map_source = { S32_VIDEO_MAX_WIDTH, S32_VIDEO_HEIGHT, SIDE_MAP_WIDTH, S32_VIDEO_HEIGHT };

static void upload_side_map(void)
{
	int width = 0;
	const uint32_t *pixels = s32_video_frame(1, &width);

	if (width > SIDE_MAP_WIDTH)
		pixels += (width - SIDE_MAP_WIDTH) / 2;    /* (the middle of a WIDE frame) */
	SDL_UpdateTexture(texture, &side_map_source, pixels, S32_VIDEO_MAX_WIDTH * (int)sizeof(uint32_t));
}

/* And filling the room under the picture, behind the controls (Sebas: none of it black): as large as it
   takes, the part of it shown the one around the car */
static void present_side_map(const SDL_Rect *room)
{
	const double scale = SDL_max((double)room->w / side_map_source.w, (double)room->h / side_map_source.h);
	SDL_Rect part;
	int car_x, car_y;

	console_side_map_car(&car_x, &car_y);
	part.w = SDL_min(side_map_source.w, (int)lround(room->w / scale));
	part.h = SDL_min(side_map_source.h, (int)lround(room->h / scale));
	part.x = side_map_source.x + SDL_clamp(car_x - part.w / 2, 0, side_map_source.w - part.w);
	part.y = side_map_source.y + SDL_clamp(car_y - SIDE_MAP_CAR_HEIGHT / 2 - part.h / 2, 0, side_map_source.h - part.h);
	SDL_RenderCopy(renderer, texture, &part, room);
}
#endif

#ifdef ORUNNERS_CONSOLE
/* ---- OPTIONS > FPS: the frames shown a second, at the picture's top right and in a window's title ---- */

#define FPS_GLYPHS "0123456789 FPS"

/* FPS_GLYPHS' letters, 3 x 5 pixels of the game's: a row each, its high bit the left */
static const uint8_t fps_glyphs[][5] = {
	{ 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
	{ 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 2, 2, 2 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 },
	{ 0, 0, 0, 0, 0 }, { 7, 4, 6, 4, 4 }, { 6, 5, 6, 4, 4 }, { 3, 4, 2, 1, 6 }
};
static int fps_counted = -1;    /* the last half second's frames a second (-1: none counted yet) */

/* A frame shown: the frames a second, each half second; a computer's window says them in its title */
static void count_fps(void)
{
	static uint64_t start;
	static int frames;
	static bool titled;
	const uint64_t now = SDL_GetPerformanceCounter(), frequency = SDL_GetPerformanceFrequency();

	if (!start)
		start = now;
	if (++frames < 2 || now - start < frequency / 2)
		return;
	fps_counted = (int)lround(frames * (double)frequency / (double)(now - start));
	start = now;
	frames = 0;
#ifndef ORUNNERS_HANDHELD
	if (console_window_settings.fps || titled)
	{
		char title[48];

		snprintf(title, sizeof(title), console_window_settings.fps ? TITLE " - %d FPS" : TITLE, fps_counted);
		SDL_SetWindowTitle(window, title);
		titled = console_window_settings.fps;
	}
#endif
}

/* The frames a second at the top right of the picture 'shown', its letters the game's pixels, with a shadow */
static void draw_fps(const SDL_Rect *shown)
{
	SDL_Rect cells[2][14 * 15];
	int count = 0;
	char text[16];
	const int pixel = SDL_max(1, shown->h / S32_VIDEO_HEIGHT);
	int x;

	if (!console_window_settings.fps || fps_counted < 0)
		return;
	snprintf(text, sizeof(text), "%d FPS", SDL_min(fps_counted, 999));
	x = shown->x + shown->w - (4 * (int)strlen(text) + 3) * pixel;
	for (const char *letter = text; *letter; letter++, x += 4 * pixel)
	{
		const uint8_t *rows = fps_glyphs[strchr(FPS_GLYPHS, *letter) - FPS_GLYPHS];

		for (int row = 0; row < 5; row++)
			for (int column = 0; column < 3; column++)
				if (rows[row] & (4 >> column))
				{
					cells[1][count] = (SDL_Rect){ x + column * pixel, shown->y + (4 + row) * pixel, pixel, pixel };
					cells[0][count] = cells[1][count];
					cells[0][count].x += pixel;
					cells[0][count].y += pixel;
					count++;
				}
	}
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderFillRects(renderer, cells[0], count);
	SDL_SetRenderDrawColor(renderer, 255, 224, 0, 255);
	SDL_RenderFillRects(renderer, cells[1], count);
}
#endif

/* The picture (its part 'source' of 'texture') in the window, as large as it fits with its proportions; with
   the screens facing each other, each screen on its half, the second turned half round */
static void present(SDL_Rect source)
{
	const int kind = pixels_kind();
	SDL_Rect shown[2], from[2];
	int pictures = 1;
	int out_width = 0, out_height = 0;
	double scale;
#ifdef ORUNNERS_TOUCH
	SDL_Rect map_room;
	bool map_room_there = false;
#endif

	SDL_GetRendererOutputSize(renderer, &out_width, &out_height);
#ifdef WIDE_BY_SCREEN
	follow_screen(out_width, out_height);    /* (a Fold opened or closed: its WIDE, from the next frame) */
#endif
	scale = SDL_min((double)out_width / source.w, (double)out_height / source.h);
	shown[0].w = (int)(source.w * scale);
	shown[0].h = (int)(source.h * scale);
	shown[0].x = (out_width - shown[0].w) / 2;
	shown[0].y = (out_height - shown[0].h) / 2;
	from[0] = source;
#ifdef ORUNNERS_TOUCH
	/* (each frame, the screen as it is: a Fold opened or closed mid-game changes them all at once) */
	tablet = tablet_screen(out_width, out_height);
	console_offer_two_players(tablet);
	if (screens_facing())
	{
		touch_layout_facing(out_width, out_height, source.w, S32_VIDEO_HEIGHT, shown);
		for (int screen = 0; screen < 2; screen++)
			from[screen] = (SDL_Rect){ source.x, source.y + screen * S32_VIDEO_HEIGHT, source.w, S32_VIDEO_HEIGHT };
		pictures = 2;
		scale = (double)shown[0].w / source.w;
	}
	else
	{
		touch_layout(out_width, out_height, &shown[0]);    /* (a tall screen: the picture at the top, the controls under it) */
		map_room_there = touch_map_room(&map_room);
	}
	console_side_map_wanted(map_room_there);    /* (and between them the route map, from the game's next frame) */
#endif
	SDL_SetTextureScaleMode(texture, kind == 2 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
	SDL_RenderClear(renderer);
	if (kind == 1 || kind == 3)
	{
		const int times = SDL_max(1, (int)scale);
		SDL_Rect whole = { 0, 0, source.w * times, source.h * times };

		if (!scaled || scaled_times != times)
		{
			if (scaled)
				SDL_DestroyTexture(scaled);
			scaled = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
				2 * S32_VIDEO_MAX_WIDTH * times, 2 * S32_VIDEO_HEIGHT * times);
			if (scaled)
				SDL_SetTextureScaleMode(scaled, SDL_ScaleModeLinear);
			scaled_times = times;
		}
		if (scaled)
		{
			SDL_SetRenderTarget(renderer, scaled);
			SDL_RenderCopy(renderer, texture, &source, &whole);
			SDL_SetRenderTarget(renderer, NULL);
			SDL_RenderClear(renderer);
			for (int picture = 0; picture < pictures; picture++)
			{
				const SDL_Rect part = { (from[picture].x - source.x) * times, (from[picture].y - source.y) * times,
					from[picture].w * times, from[picture].h * times };

				SDL_RenderCopyEx(renderer, scaled, &part, &shown[picture], picture == 1 ? 180.0 : 0.0, NULL,
					SDL_FLIP_NONE);
			}
		}
		if (kind == 3)
			for (int picture = 0; picture < pictures; picture++)
			{
				make_crt_overlay(shown[picture].w, shown[picture].h, from[picture].h);
				if (crt_overlay)
					SDL_RenderCopyEx(renderer, crt_overlay, NULL, &shown[picture], picture == 1 ? 180.0 : 0.0, NULL,
						SDL_FLIP_NONE);
			}
	}
	else
		for (int picture = 0; picture < pictures; picture++)
			SDL_RenderCopyEx(renderer, texture, &from[picture], &shown[picture], picture == 1 ? 180.0 : 0.0, NULL,
				SDL_FLIP_NONE);
#ifdef ORUNNERS_TOUCH
	if (map_room_there && console_side_map_shown())
		present_side_map(&map_room);
	{
#ifdef ORUNNERS_TOUCH_TEST
		const bool gears[TOUCH_PLAYERS] = { true, true };    /* (a computer's check: the gears' buttons always, to see them) */
#else
		const bool gears[TOUCH_PLAYERS] = { console_manual_gears(0), console_manual_gears(1) };    /* (the manual transmission's) */
#endif

		touch_draw(renderer, gears);
	}
#endif
#ifdef ORUNNERS_CONSOLE
	count_fps();
	draw_fps(&shown[0]);
#endif
	SDL_RenderPresent(renderer);
}

#ifdef ORUNNERS_CONSOLE
/* ---- the CONTROLS page's keys and buttons: Kenney's Input Prompts Pixel (CC0), 16 x 16 tiles, as
   SegaSonic's ---- */

typedef struct
{
	int what;      /* an SDL_Scancode or an SDL_GameControllerButton */
	uint8_t row, column, tiles;    /* where in the sheet, and how many tiles wide */
} prompt;

static const prompt key_prompts[] = {
	{ SDL_SCANCODE_ESCAPE, 0, 17, 1 }, { SDL_SCANCODE_F1, 0, 18, 1 }, { SDL_SCANCODE_F2, 0, 19, 1 },
	{ SDL_SCANCODE_F3, 0, 20, 1 }, { SDL_SCANCODE_F4, 0, 21, 1 }, { SDL_SCANCODE_F5, 0, 22, 1 },
	{ SDL_SCANCODE_F6, 0, 23, 1 }, { SDL_SCANCODE_F7, 0, 24, 1 }, { SDL_SCANCODE_F8, 0, 25, 1 },
	{ SDL_SCANCODE_F9, 0, 26, 1 }, { SDL_SCANCODE_F10, 0, 27, 1 }, { SDL_SCANCODE_F11, 0, 28, 1 },
	{ SDL_SCANCODE_F12, 0, 29, 1 },
	{ SDL_SCANCODE_1, 1, 17, 1 }, { SDL_SCANCODE_2, 1, 18, 1 }, { SDL_SCANCODE_3, 1, 19, 1 },
	{ SDL_SCANCODE_4, 1, 20, 1 }, { SDL_SCANCODE_5, 1, 21, 1 }, { SDL_SCANCODE_6, 1, 22, 1 },
	{ SDL_SCANCODE_7, 1, 23, 1 }, { SDL_SCANCODE_8, 1, 24, 1 }, { SDL_SCANCODE_9, 1, 25, 1 },
	{ SDL_SCANCODE_0, 1, 26, 1 }, { SDL_SCANCODE_MINUS, 1, 27, 1 }, { SDL_SCANCODE_EQUALS, 1, 29, 1 },
	{ SDL_SCANCODE_BACKSPACE, 1, 32, 2 },
	{ SDL_SCANCODE_Q, 2, 17, 1 }, { SDL_SCANCODE_W, 2, 18, 1 }, { SDL_SCANCODE_E, 2, 19, 1 },
	{ SDL_SCANCODE_R, 2, 20, 1 }, { SDL_SCANCODE_T, 2, 21, 1 }, { SDL_SCANCODE_Y, 2, 22, 1 },
	{ SDL_SCANCODE_U, 2, 23, 1 }, { SDL_SCANCODE_I, 2, 24, 1 }, { SDL_SCANCODE_O, 2, 25, 1 },
	{ SDL_SCANCODE_P, 2, 26, 1 }, { SDL_SCANCODE_LEFTBRACKET, 2, 27, 1 }, { SDL_SCANCODE_RIGHTBRACKET, 2, 28, 1 },
	{ SDL_SCANCODE_BACKSLASH, 2, 31, 1 },
	{ SDL_SCANCODE_A, 3, 18, 1 }, { SDL_SCANCODE_S, 3, 19, 1 }, { SDL_SCANCODE_D, 3, 20, 1 },
	{ SDL_SCANCODE_F, 3, 21, 1 }, { SDL_SCANCODE_G, 3, 22, 1 }, { SDL_SCANCODE_H, 3, 23, 1 },
	{ SDL_SCANCODE_J, 3, 24, 1 }, { SDL_SCANCODE_K, 3, 25, 1 }, { SDL_SCANCODE_L, 3, 26, 1 },
	{ SDL_SCANCODE_APOSTROPHE, 3, 27, 1 }, { SDL_SCANCODE_SEMICOLON, 3, 30, 1 },
	{ SDL_SCANCODE_RETURN, 3, 32, 2 }, { SDL_SCANCODE_KP_ENTER, 3, 32, 2 },
	{ SDL_SCANCODE_Z, 4, 19, 1 }, { SDL_SCANCODE_X, 4, 20, 1 }, { SDL_SCANCODE_C, 4, 21, 1 },
	{ SDL_SCANCODE_V, 4, 22, 1 }, { SDL_SCANCODE_B, 4, 23, 1 }, { SDL_SCANCODE_N, 4, 24, 1 },
	{ SDL_SCANCODE_M, 4, 25, 1 }, { SDL_SCANCODE_COMMA, 4, 26, 1 }, { SDL_SCANCODE_PERIOD, 4, 27, 1 },
	{ SDL_SCANCODE_SLASH, 4, 29, 1 },
	{ SDL_SCANCODE_UP, 4, 30, 1 }, { SDL_SCANCODE_RIGHT, 4, 31, 1 }, { SDL_SCANCODE_DOWN, 4, 32, 1 },
	{ SDL_SCANCODE_LEFT, 4, 33, 1 },
	{ SDL_SCANCODE_LALT, 5, 17, 2 }, { SDL_SCANCODE_RALT, 5, 17, 2 }, { SDL_SCANCODE_TAB, 5, 19, 2 },
	{ SDL_SCANCODE_DELETE, 5, 21, 2 }, { SDL_SCANCODE_END, 5, 23, 2 },
	{ SDL_SCANCODE_LCTRL, 6, 17, 2 }, { SDL_SCANCODE_RCTRL, 6, 17, 2 }, { SDL_SCANCODE_CAPSLOCK, 6, 19, 2 },
	{ SDL_SCANCODE_HOME, 6, 21, 2 }, { SDL_SCANCODE_PAGEUP, 6, 23, 2 }, { SDL_SCANCODE_PAGEDOWN, 6, 25, 2 },
	{ SDL_SCANCODE_SPACE, 6, 32, 2 },
	{ SDL_SCANCODE_LSHIFT, 7, 17, 2 }, { SDL_SCANCODE_RSHIFT, 7, 17, 2 }, { SDL_SCANCODE_INSERT, 7, 19, 2 },
	{ SDL_SCANCODE_PAUSE, 7, 25, 2 },
};

static const prompt button_prompts[] = {
	{ SDL_CONTROLLER_BUTTON_A, 0, 4, 1 }, { SDL_CONTROLLER_BUTTON_B, 0, 5, 1 },
	{ SDL_CONTROLLER_BUTTON_X, 0, 6, 1 }, { SDL_CONTROLLER_BUTTON_Y, 0, 7, 1 },
	{ SDL_CONTROLLER_BUTTON_DPAD_UP, 1, 1, 1 }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 1, 2, 1 },
	{ SDL_CONTROLLER_BUTTON_DPAD_DOWN, 1, 3, 1 }, { SDL_CONTROLLER_BUTTON_DPAD_LEFT, 1, 4, 1 },
	{ SDL_CONTROLLER_BUTTON_BACK, 18, 4, 1 }, { SDL_CONTROLLER_BUTTON_START, 18, 5, 1 },
	{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 18, 7, 1 }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 18, 8, 1 },
	{ CONTROL_LEFT_TRIGGER, 18, 9, 1 }, { CONTROL_RIGHT_TRIGGER, 18, 10, 1 },
	{ SDL_CONTROLLER_BUTTON_LEFTSTICK, 12, 16, 1 }, { SDL_CONTROLLER_BUTTON_RIGHTSTICK, 14, 16, 1 },
	{ SDL_CONTROLLER_BUTTON_GUIDE, 18, 2, 1 }, { SDL_CONTROLLER_BUTTON_MISC1, 18, 6, 1 },
};

#define PROMPT_SIZE  16
#define PROMPT_CLEAR 0xFFFF00FFu    /* magenta: the sheet's see-through */

static SDL_Surface *prompt_sheet;    /* input_prompts.bmp beside the executable, as ARGB */
static uint32_t controls_picture[S32_VIDEO_MAX_WIDTH * S32_VIDEO_HEIGHT];

static void load_prompts(const char *base)
{
	char path[1024];
	SDL_Surface *loaded;

	snprintf(path, sizeof(path), "%sinput_prompts.bmp", base);
	loaded = SDL_LoadBMP(path);
	if (!loaded)
		return;    /* (CONTROLS then shows the names alone) */
	prompt_sheet = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(loaded);
}

static const prompt *find_prompt(const prompt *list, size_t count, int what)
{
	for (size_t i = 0; i < count; i++)
		if (list[i].what == what)
			return &list[i];
	return NULL;
}

/* One prompt into the picture, its top left at (x, y) */
static void draw_prompt(const prompt *icon, int x, int y)
{
	const uint32_t *sheet = (const uint32_t *)prompt_sheet->pixels;
	const int pitch = prompt_sheet->pitch / 4;

	for (int row = 0; row < PROMPT_SIZE; row++)
		for (int column = 0; column < PROMPT_SIZE * icon->tiles; column++)
		{
			const uint32_t color = sheet[(icon->row * PROMPT_SIZE + row) * pitch + icon->column * PROMPT_SIZE + column];

			if ((color | 0xFF000000u) == PROMPT_CLEAR || y + row < 0 || y + row >= S32_VIDEO_HEIGHT
				|| x + column >= S32_VIDEO_MAX_WIDTH)
				continue;
			controls_picture[(y + row) * S32_VIDEO_MAX_WIDTH + x + column] = color | 0xFF000000u;
		}
}

/* The left screen's frame with each of the shown player's controls' keys and first button on its line */
static const uint32_t *with_controls(const uint32_t *pixels)
{
	const control_binding *bindings = control_bindings[console_controls_player()];

	memcpy(controls_picture, pixels, sizeof(controls_picture));
	for (int control = 0; control < CONTROLS; control++)
	{
		const prompt *icon, *first = NULL;
		int shown = 0, x, y;

		if (!console_controls_place(control, &x, &y))
			continue;
		for (int key = 0; key < CONTROL_KEYS && shown < 2; key++)
		{
			icon = find_prompt(key_prompts, SDL_arraysize(key_prompts), bindings[control].keys[key]);
			if (!icon || (first && first->row == icon->row && first->column == icon->column))
				continue;    /* (no picture, or the first key's: Enter and the keypad's) */
			draw_prompt(icon, x + (shown++ ? CONSOLE_CONTROLS_SECOND_X : 0), y);
			first = first ? first : icon;
		}
		if ((icon = find_prompt(button_prompts, SDL_arraysize(button_prompts), bindings[control].buttons[0])) != NULL)
			draw_prompt(icon, x + CONSOLE_CONTROLS_BUTTON_X, y);
	}
	return controls_picture;
}
#endif

/* The frames drawn into the window */
static void show_screens(void)
{
	SDL_Rect source = { 0, 0, 0, 0 };
	const int shown = screens_shown();
	const bool stacked = screens_stacked();

	/* each screen into the texture: the left one at its top left, the right one beside it or under it; with
	   WIDE 16:9 a frame of 416 pixels loses 9 at each side (20:9's 496 are shown whole), one of 320 sits between
	   black bars */
	for (int screen = 0; screen < shown; screen++)
	{
		static uint32_t bars[S32_VIDEO_MAX_WIDTH * S32_VIDEO_HEIGHT];    /* (black) */
		int width = 0;
		const uint32_t *pixels = s32_video_frame(screen, &width);
#ifdef ORUNNERS_CONSOLE
		if (pause_shown)
		{
			pixels = pause_pixels[screen];    /* (the pause's picture) */
			width = pause_widths[screen];
		}
#endif
#ifdef ORUNNERS_CONSOLE
		const int cell = screen_width();
#else
		const int cell = width;    /* (the arcade: the board's frame as it is, 320 or 416) */
#endif
		SDL_Rect place;

#ifdef ORUNNERS_CONSOLE
		int ignored_x, ignored_y;

		if (screen == 0 && prompt_sheet && console_controls_place(0, &ignored_x, &ignored_y))
			pixels = with_controls(pixels);    /* (OPTIONS > CONTROLS: the keys' and buttons' pictures) */
#endif

		place.x = stacked ? 0 : screen * cell;
		place.y = stacked ? screen * S32_VIDEO_HEIGHT : 0;
		place.w = cell;
		place.h = S32_VIDEO_HEIGHT;
		if (width > cell)
			pixels += (width - cell) / 2;    /* (the middle of a wider frame) */
		else if (width < cell)
		{
			SDL_UpdateTexture(texture, &place, bars, S32_VIDEO_MAX_WIDTH * (int)sizeof(uint32_t));
			place.x += (cell - width) / 2;
			place.w = width;
		}
		SDL_UpdateTexture(texture, &place, pixels, S32_VIDEO_MAX_WIDTH * (int)sizeof(uint32_t));
		source.w = stacked ? cell : source.w + cell;
		source.h = stacked ? source.h + S32_VIDEO_HEIGHT : S32_VIDEO_HEIGHT;
	}
#ifdef ORUNNERS_TOUCH
	if (console_side_map_shown())
		upload_side_map();
#endif
	present(source);
}

#ifdef ORUNNERS_HANDHELD
static uint64_t pace_video, pace_showing;    /* a handheld's log: the frames' time drawing them, showing them */
#endif

static void on_frame_shown(void)
{
	/* the video draws the screens the window shows alone (both where the frames' hashes are written, or the
	   right one is the route map beside the race) */
#ifdef ORUNNERS_TOUCH
	s32_video_show_screens(screens_shown() == 2 || video_hashes || console_side_map_shown() ? 3 : 1);
#else
	s32_video_show_screens(screens_shown() == 2 || video_hashes ? 3 : 1);
#endif
#ifdef ORUNNERS_HANDHELD
	const uint64_t start = SDL_GetPerformanceCounter();
	uint64_t drawn;

	s32_video_draw_frame();
	drawn = SDL_GetPerformanceCounter();
	show_screens();
	pace_video += drawn - start;
	pace_showing += SDL_GetPerformanceCounter() - drawn;
#else
	s32_video_draw_frame();
	show_screens();
#endif
}

static void toggle_full_screen(void)
{
	const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;

	SDL_SetWindowFullscreen(window, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#ifdef ORUNNERS_CONSOLE
	console_window_settings.full_screen = full;
	console_window_settings.changed = true;
#endif
}

static void toggle_smooth(void)
{
#ifdef ORUNNERS_CONSOLE
	console_window_settings.pixels = (console_window_settings.pixels + 1) % PIXELS_KINDS;
	console_window_settings.changed = true;
#else
	smooth = !smooth;
#endif
}

#ifdef ORUNNERS_CONSOLE
/* ---- the console's window settings (OPTIONS > VIDEO, VOLUME), kept in outrunners.ini ---- */

static char settings_path[1024];
static int window_width, window_height;    /* the picture the window was last sized for */

static const char *const pixel_names[PIXELS_KINDS] = { "sharp", "smooth", "bilinear", "crt" };

static void save_window_settings(void)
{
	const console_window *settings = &console_window_settings;
	FILE *output;

	if (script)
		return;
	output = fopen(settings_path, "w");
	if (!output)
		return;
	fprintf(output, "window_scale=%d\nfull_screen=%d\npixels=%s\nwide=%d\ntwo_players=%s\nvolume=%d\nname=%s\n"
		"fps=%d\ndead_zone=%d\nsensitivity=%d\nsteering_speed=%d\n",
		settings->window_scale, settings->full_screen, pixel_names[settings->pixels], settings->wide,
		settings->vertical ? "vertical" : "horizontal", settings->volume, settings->name, settings->fps,
		settings->dead_zone, settings->sensitivity, settings->steering_speed);
	fclose(output);
}

static void load_window_settings(void)
{
	console_window *settings = &console_window_settings;
	char line[128], text[32];
	int number;
	FILE *input = fopen(settings_path, "r");

	if (!input)
		return;
	while (fgets(line, sizeof(line), input))
	{
		if (sscanf(line, "window_scale=%d", &number) == 1 && number >= 1 && number <= 6)
			settings->window_scale = number;
		else if (sscanf(line, "full_screen=%d", &number) == 1)
			settings->full_screen = number != 0;
		else if (sscanf(line, "wide=%d", &number) == 1 && number >= 0 && number < WIDE_KINDS)
			settings->wide = number;    /* (0 off, 1 16:9, 2 20:9) */
		else if (sscanf(line, "volume=%d", &number) == 1 && number >= 0 && number <= 10)
			settings->volume = number;
		else if (sscanf(line, "fps=%d", &number) == 1)
			settings->fps = number != 0;
		else if (sscanf(line, "dead_zone=%d", &number) == 1 && number >= 0 && number <= DEAD_ZONE_MOST)
			settings->dead_zone = number / DEAD_ZONE_STEP * DEAD_ZONE_STEP;
		else if (sscanf(line, "sensitivity=%d", &number) == 1 && number >= 1 && number <= STEERING_LEVELS)
			settings->sensitivity = number;
		else if (sscanf(line, "steering_speed=%d", &number) == 1 && number >= 1 && number <= STEERING_LEVELS)
			settings->steering_speed = number;
		else if (sscanf(line, "pixels=%31s", text) == 1)
		{
			for (int kind = 0; kind < PIXELS_KINDS; kind++)
				if (strcmp(text, pixel_names[kind]) == 0)
					settings->pixels = kind;
		}
		else if (sscanf(line, "two_players=%31s", text) == 1)
			settings->vertical = strcmp(text, "vertical") == 0;
		else if (strncmp(line, "name=", 5) == 0 && line[5] != '\n' && line[5] != '\r' && line[5] != '\0')
		{
			/* (NETWORK > NAME: the font's letters, as many as it takes) */
			size_t length = strcspn(line + 5, "\r\n");

			snprintf(settings->name, sizeof(settings->name), "%.*s", (int)length, line + 5);
		}
	}
	fclose(input);
}

#ifdef ORUNNERS_HANDHELD
/* A game device's picture, fixed whatever outrunners.ini says (OPTIONS has no VIDEO there, console/menu.c):
   the whole screen and smooth pixels; WIDE where the screen is wide (ORUNNERS_SCREEN_WIDE: the Switch's 16:9;
   a phone's by its screen's shape, wide_for_screen, as the screen is now: a Fold's opened or closed), else the
   game's 4:3 picture as large as it fits (a handheld's 640 x 480); 2 PLAYERS side by side */
static void fix_window_settings(void)
{
	console_window_settings.full_screen = true;
#if defined(ORUNNERS_SCREEN_WIDE) && defined(__ANDROID__)
	{
		/* (the screen as the renderer has it, once there is one; before it, the display's mode) */
		SDL_DisplayMode mode;
		int width = 0, height = 0;

		if (renderer)
			SDL_GetRendererOutputSize(renderer, &width, &height);
		else if (SDL_GetCurrentDisplayMode(0, &mode) == 0)
		{
			width = mode.w;
			height = mode.h;
		}
		console_window_settings.wide = width > 0 && height > 0 ? wide_for_screen(width, height) : WIDE_16_9;
	}
#elif defined(ORUNNERS_SCREEN_WIDE)
	console_window_settings.wide = WIDE_16_9;
#else
	console_window_settings.wide = WIDE_OFF;
#endif
	console_window_settings.pixels = PIXELS_SMOOTH;
	console_window_settings.vertical = false;
}
#endif

/* The window as the settings say: full screen or not, and out of it the picture times window_scale (the
   largest whole scale under it at which it fits the screen). A game device's window is its screen */
static void apply_window_settings(void)
{
	int width, height;

#ifdef ORUNNERS_HANDHELD
	fix_window_settings();
	picture_size(&width, &height);
#else
	const console_window *settings = &console_window_settings;
	const bool full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;

	picture_size(&width, &height);
	if (full != settings->full_screen)
		SDL_SetWindowFullscreen(window, settings->full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#ifndef ORUNNERS_TOUCH_TEST    /* (the touch controls' check: the window is a phone's screen, the shape it is given) */
	if (!settings->full_screen && (width != window_width || height != window_height || settings->changed))
	{
		SDL_Rect usable;
		const int display = SDL_GetWindowDisplayIndex(window);
		int scale = settings->window_scale;

		if (SDL_GetDisplayUsableBounds(display < 0 ? 0 : display, &usable) == 0)
			while (scale > 1 && (width * scale > usable.w - 64 || height * scale > usable.h - 64))
				scale--;
		SDL_SetWindowSize(window, width * scale, height * scale);
		SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}
#endif
#endif
	window_width = width;
	window_height = height;
}

/* The menus' actions: the arrows, Enter (START too), Escape or Backspace; each game controller's pad or
   left stick, A, B and Start. PAUSE_BIT, with them: what pauses a race and goes on from the pause (Escape,
   Enter, a game controller's Start; not B, the brake) */
#define PAUSE_BIT (1u << 31)

static uint32_t actions_held;
static bool actions_taken;    /* a key or button just bound on CONTROLS: it is no action this frame */

static uint32_t read_actions(const Uint8 *keys)
{
	uint32_t held = 0;

	if (keys[SDL_SCANCODE_UP])
		held |= ACTION_BIT(ACTION_UP);
	if (keys[SDL_SCANCODE_DOWN])
		held |= ACTION_BIT(ACTION_DOWN);
	if (keys[SDL_SCANCODE_LEFT])
		held |= ACTION_BIT(ACTION_LEFT);
	if (keys[SDL_SCANCODE_RIGHT])
		held |= ACTION_BIT(ACTION_RIGHT);
	if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER])
		held |= ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START);
	if (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_BACKSPACE])
		held |= ACTION_BIT(ACTION_BACK);
	if (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER])
		held |= PAUSE_BIT;
#ifdef __ANDROID__
	if (keys[SDL_SCANCODE_AC_BACK])
		held |= ACTION_BIT(ACTION_BACK);    /* (Android's back: the menus' back, never the end of the game) */
#endif
#ifdef ORUNNERS_TOUCH
	{
		const uint32_t touched = touch_actions();

		held |= touched | ((touched & ACTION_BIT(ACTION_START)) ? PAUSE_BIT : 0);    /* (START pauses a race) */
	}
#endif
	for (int player = 0; player < PLAYERS; player++)
	{
		SDL_GameController *pad = controllers[player];
		int stick_x, stick_y;

		if (!pad)
			continue;
		stick_x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
		stick_y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) || stick_y < -16000)
			held |= ACTION_BIT(ACTION_UP);
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || stick_y > 16000)
			held |= ACTION_BIT(ACTION_DOWN);
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || stick_x < -16000)
			held |= ACTION_BIT(ACTION_LEFT);
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || stick_x > 16000)
			held |= ACTION_BIT(ACTION_RIGHT);
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A))
			held |= ACTION_BIT(ACTION_ACCEPT);
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B))
			held |= ACTION_BIT(ACTION_BACK);
		if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START))
			held |= ACTION_BIT(ACTION_START) | PAUSE_BIT;
	}
	return held;
}

/* A trigger takes the control CONTROLS waits for from halfway down (of 32767) */
#define TRIGGER_BOUND  16384

/* The button or trigger pressed on game controller 'which' for the control CONTROLS waits for, when it is
   the shown player's */
static void bind_button(SDL_JoystickID which, SDL_GameControllerButton button)
{
	const int player = console_controls_player();

	if (player < PLAYERS && controllers[player]
		&& SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[player])) == which)
	{
		controls_bind_button(player, console_controls_waiting(), button);
		console_controls_bound();
		actions_taken = true;
	}
}
#endif

/* ---- the controls ---- */

static void read_events(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event))
	{
#ifdef ORUNNERS_TOUCH
		touch_event(&event);
#endif
		switch (event.type)
		{
			case SDL_QUIT:
				program_stop();    /* (back to main, which saves: the game runs on a task's stack, which a
				                      longjmp to main's does not leave on Windows) */
				return;
#ifdef __ANDROID__
			case SDL_APP_WILLENTERBACKGROUND:
				/* (Android may end the game there: what it keeps is written; a race waits, paused) */
				save_eeprom();
				save_backup_ram();
				if (console_can_pause())
					pause_requested = true;
				break;
#endif
#ifdef ORUNNERS_HANDHELD_KEYS
			case SDL_JOYBUTTONDOWN:
				if (event.jbutton.button == HANDHELD_MENU_BUTTON)
				{
					program_stop();    /* (MENU leaves the game, as the firmware's other games) */
					return;
				}
				break;
#endif
			case SDL_KEYDOWN:
				if (event.key.repeat)
					break;
#ifdef ORUNNERS_CONSOLE
				if (console_controls_waiting() >= 0)
				{
					/* CONTROLS waits for a key: this one (Escape leaves the control as it was) */
					if (event.key.keysym.sym != SDLK_ESCAPE)
						controls_bind_key(console_controls_player(), console_controls_waiting(),
							event.key.keysym.scancode);
					console_controls_bound();
					actions_taken = true;
					break;
				}
#endif
#ifndef ORUNNERS_CONSOLE    /* (the console's Escape is the menus' back; its menu's EXIT quits) */
				if (event.key.keysym.sym == SDLK_ESCAPE)
				{
					program_stop();
					return;
				}
#endif
#ifdef ORUNNERS_HANDHELD_KEYS
				if (event.key.keysym.sym == SDLK_VOLUMEUP || event.key.keysym.sym == SDLK_VOLUMEDOWN)
				{
					/* the volume keys turn OPTIONS' VOLUME, a step a press, kept when the game ends */
					const int volume = console_window_settings.volume + (event.key.keysym.sym == SDLK_VOLUMEUP ? 1 : -1);

					console_window_settings.volume = volume < 0 ? 0 : (volume > 10 ? 10 : volume);
					console_window_settings.changed = true;    /* (kept) */
				}
#endif
				if (event.key.keysym.sym == SDLK_F11
					|| (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)))
					toggle_full_screen();
				if (event.key.keysym.sym == SDLK_F3)
					toggle_smooth();
				break;
#ifdef ORUNNERS_CONSOLE
			case SDL_CONTROLLERBUTTONDOWN:
				/* ... or a button of the game controller plugged in for that player */
				if (console_controls_waiting() >= 0)
					bind_button(event.cbutton.which, (SDL_GameControllerButton)event.cbutton.button);
				break;
			case SDL_CONTROLLERAXISMOTION:
				/* ... or one of its triggers, pressed halfway */
				if (console_controls_waiting() >= 0 && event.caxis.value >= TRIGGER_BOUND
					&& (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
					bind_button(event.caxis.which, event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT
						? CONTROL_LEFT_TRIGGER : CONTROL_RIGHT_TRIGGER);
				break;
#endif
			case SDL_CONTROLLERDEVICEADDED:
				for (int player = 0; player < PLAYERS; player++)
					if (!controllers[player])
					{
						controllers[player] = SDL_GameControllerOpen(event.cdevice.which);
						break;
					}
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				for (int player = 0; player < PLAYERS; player++)
					if (controllers[player]
						&& SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[player])) == event.cdevice.which)
					{
						SDL_GameControllerClose(controllers[player]);
						controllers[player] = NULL;
					}
				break;
			default:
				break;
		}
	}
}

static int clamp(int value, int low, int high)
{
	return value < low ? low : (value > high ? high : value);
}

/* the keyboard's wheel: turned while a key holds it, back to the centre when let go; the console's at
   OPTIONS > STEERING's SPEED (STEERING_NORMAL: WHEEL_TURN and WHEEL_RETURN) */
static int steer(int position, bool left, bool right)
{
#ifdef ORUNNERS_CONSOLE
	const int turn = WHEEL_TURN + console_window_settings.steering_speed - STEERING_NORMAL;
#else
	const int turn = WHEEL_TURN;
#endif
	const int back = WHEEL_RETURN * turn / WHEEL_TURN;

	if (left != right)
		return clamp(position + (right ? turn : -turn), WHEEL_LEFT, WHEEL_RIGHT);
	if (position < WHEEL_CENTRE)
		return position + back > WHEEL_CENTRE ? WHEEL_CENTRE : position + back;
	return position - back < WHEEL_CENTRE ? WHEEL_CENTRE : position - back;
}

/* A stick's travel as the wheel's, past its dead zone (false inside it). The console's from the dead zone's
   edge out, as OPTIONS > STEERING's DEAD ZONE and SENSITIVITY have it: under STEERING_NORMAL softer about the
   centre, over it sharper (issue 1: twitchy on some controllers) */
static bool stick_wheel(int stick, int *position)
{
	const int travel = stick < 0 ? -stick : stick;
#ifdef ORUNNERS_CONSOLE
	static const double curves[STEERING_LEVELS] = { 2.2, 1.9, 1.6, 1.3, 1.0, 0.85, 0.72, 0.6, 0.5 };
	const int dead = console_window_settings.dead_zone * 32767 / 100;
	double out;

	if (travel <= dead || travel == 0)
		return false;
	out = SDL_min(1.0, pow((double)(travel - dead) / (32767 - dead), curves[console_window_settings.sensitivity - 1]));
	*position = WHEEL_CENTRE + (int)lround((stick < 0 ? -out : out) * (WHEEL_RIGHT - WHEEL_CENTRE));
	return true;
#else
	if (travel <= STICK_DEAD_ZONE)
		return false;
	*position = clamp(WHEEL_CENTRE + stick * (WHEEL_RIGHT - WHEEL_CENTRE) / 32767, WHEEL_LEFT, WHEEL_RIGHT);
	return true;
#endif
}

static int press(int position, bool held)
{
	return clamp(position + (held ? PEDAL_PRESS : -PEDAL_LIFT), 0, 0xff);
}

/* a trigger's travel (of 32767) as a pedal's */
static int trigger_pedal(int travel)
{
	return travel * 0xff / 32767;
}

/* the state of every control once a frame, where the board reads them (VBLANK) */
#ifdef ORUNNERS_CONSOLE
/* True while one of the player's keys or controller's buttons for 'control' is held, or a finger on the
   player's touch controls (Android's); the pedals' triggers press them as far as they go (trigger_pedal) */
static bool holds_buttons(int player, SDL_GameController *pad, int control)
{
#ifdef ORUNNERS_TOUCH
	if (player < TOUCH_PLAYERS && touch_holds(player, control))
		return true;
#endif
	return controls_holds(player, pad, control);
}

/* ... or one of the control's triggers pressed at all */
static bool holds(int player, SDL_GameController *pad, int control)
{
	return holds_buttons(player, pad, control) || controls_trigger_travel(player, pad, control) > TRIGGER_PRESSED;
}
#endif

static void on_controls_update(void)
{
	const Uint8 *keys;
	s32_inputs *input = &s32_board.input;
	bool shift_up[PLAYERS] = { false }, shift_down[PLAYERS] = { false };
	bool music[PLAYERS] = { false }, previous[PLAYERS] = { false }, next[PLAYERS] = { false };
	bool coin[PLAYERS] = { false }, start[PLAYERS] = { false };
	int wheels[PLAYERS], gases[PLAYERS], brakes[PLAYERS];

	read_events();
	if (script)
	{
		s32_script_apply(script, s32_board.frame);
		return;
	}
	keys = SDL_GetKeyboardState(NULL);
#ifdef ORUNNERS_CONSOLE
	{
		/* the menus' actions newly pressed; EXIT; the settings OPTIONS changed */
		const uint32_t held = read_actions(keys);

		s32_video_set_wide_side(console_wide_side());    /* (before VBLANK's sprites and the frame) */

		if (console_can_pause() && (held & ~actions_held & PAUSE_BIT))
			pause_requested = true;    /* (run_pause, once the frame is done) */
		else if (!actions_taken)
			console_actions(held & ~actions_held & ~PAUSE_BIT);
		actions_taken = false;
		actions_held = held;
		if (console_controls_defaults())
			controls_defaults(console_controls_player());
		if (console_quit_requested())
		{
			program_stop();
			return;
		}
		if (console_window_settings.changed)
		{
			apply_window_settings();
			save_window_settings();
			console_window_settings.changed = false;
		}
		else
		{
			int width, height;

			picture_size(&width, &height);
			if (width != window_width || height != window_height)
				apply_window_settings();    /* (2 PLAYERS: the window takes both screens, and back) */
		}
	}
#endif
#ifdef ORUNNERS_CONSOLE
	/* each seat's keys and game controller buttons, as OPTIONS > CONTROLS binds them (controls.c); player 1's
	   fingers on the touch controls too */
	for (int player = 0; player < PLAYERS; player++)
	{
		SDL_GameController *pad = controllers[player];

		wheel[player] = steer(wheel[player], holds(player, pad, CONTROL_STEER_LEFT),
			holds(player, pad, CONTROL_STEER_RIGHT));
		gas[player] = press(gas[player], holds_buttons(player, pad, CONTROL_GAS));
		brake[player] = press(brake[player], holds_buttons(player, pad, CONTROL_BRAKE));
		shift_up[player] = holds(player, pad, CONTROL_SHIFT_UP);
		shift_down[player] = holds(player, pad, CONTROL_SHIFT_DOWN);
		music[player] = holds(player, pad, CONTROL_MUSIC);
		previous[player] = holds(player, pad, CONTROL_SONG_BACK);
		next[player] = holds(player, pad, CONTROL_SONG_NEXT);
		start[player] = holds(player, pad, CONTROL_START);
	}
#else
	wheel[0] = steer(wheel[0], keys[SDL_SCANCODE_LEFT], keys[SDL_SCANCODE_RIGHT]);
	gas[0] = press(gas[0], keys[SDL_SCANCODE_UP]);
	brake[0] = press(brake[0], keys[SDL_SCANCODE_DOWN]);
	shift_up[0] = keys[SDL_SCANCODE_Q] != 0;
	shift_down[0] = keys[SDL_SCANCODE_A] != 0;
	music[0] = keys[SDL_SCANCODE_SPACE] != 0;
	previous[0] = keys[SDL_SCANCODE_LEFTBRACKET] != 0;
	next[0] = keys[SDL_SCANCODE_RIGHTBRACKET] != 0;
#endif
	for (int player = 0; player < PLAYERS; player++)
	{
		SDL_GameController *pad = controllers[player];

#ifndef ORUNNERS_CONSOLE
		coin[player] = keys[SDL_SCANCODE_5 + player] != 0;
		start[player] = keys[SDL_SCANCODE_1 + player] != 0;
#endif
		wheels[player] = wheel[player];
		gases[player] = gas[player];
		brakes[player] = brake[player];
#ifdef ORUNNERS_TOUCH
		if (player < TOUCH_PLAYERS && touch_wheel(player) != 0)    /* (the touch wheel, as far as it turns) */
			wheels[player] = clamp(WHEEL_CENTRE + touch_wheel(player) * (WHEEL_RIGHT - WHEEL_CENTRE) / TOUCH_WHEEL_FULL,
				WHEEL_LEFT, WHEEL_RIGHT);
#endif
		if (!pad)
			continue;
		/* the controller's stick and triggers, where they are moved, over the keyboard's (the console's
		   triggers, the pedals' that CONTROLS gives them) */
		{
			const int stick = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
#ifdef ORUNNERS_CONSOLE
			const int pad_gas = trigger_pedal(controls_trigger_travel(player, pad, CONTROL_GAS));
			const int pad_brake = trigger_pedal(controls_trigger_travel(player, pad, CONTROL_BRAKE));
#else
			const int pad_gas = trigger_pedal(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
			const int pad_brake = trigger_pedal(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
#endif

			stick_wheel(stick, &wheels[player]);
			if (pad_gas > gases[player])
				gases[player] = pad_gas;
			if (pad_brake > brakes[player])
				brakes[player] = pad_brake;
		}
#ifndef ORUNNERS_CONSOLE    /* (the console's buttons are its bindings') */
		shift_up[player] |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) != 0;
		shift_down[player] |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) != 0;
		music[player] |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y) != 0;
		previous[player] |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0;
		next[player] |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0;
		start[player] |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START) != 0;
		coin[player] |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK) != 0;
#endif
	}

#ifdef ORUNNERS_CONSOLE
	/* the console: no coins, no operator's switches; a seat whose screen is not shown is let alone */
	for (int player = 0; player < PLAYERS; player++)
	{
		coin[player] = false;
		if (player >= screens_shown())
		{
			shift_up[player] = shift_down[player] = music[player] = previous[player] = next[player] = false;
			start[player] = start[player] && player == 1 && console_right_seat_joins();    /* (CLASSIC's challenger) */
			wheels[player] = WHEEL_CENTRE;
			gases[player] = brakes[player] = 0;
		}
		if (console_can_pause())
			start[player] = false;    /* (in a race START pauses) */
	}
#endif
	/* each player's 315-5296 (chip 0 player 1, chip 1 player 2); its ports are active low: a cleared bit
	   is a pressed button */
	for (int player = 0; player < PLAYERS; player++)
	{
		input->port_a[player] = (uint8_t)(0xff & ~(shift_up[player] ? 0x01 : 0) & ~(shift_down[player] ? 0x02 : 0));
		input->port_b[player] = (uint8_t)(0xff & ~(music[player] ? 0x01 : 0) & ~(previous[player] ? 0x02 : 0)
			& ~(next[player] ? 0x04 : 0));
		input->port_e[player] = (uint8_t)(0xff & ~(coin[player] ? 0x04 : 0) & ~(start[player] ? 0x10 : 0));
	}
#ifndef ORUNNERS_CONSOLE
	if (keys[SDL_SCANCODE_9])
		input->port_e[0] &= (uint8_t)~0x01;
	if (keys[SDL_SCANCODE_F2])
		input->port_e[0] &= (uint8_t)~0x02;
#endif
	input->analog[0] = (uint8_t)wheels[0];
	input->analog[1] = (uint8_t)gases[0];
	input->analog[2] = (uint8_t)brakes[0];
	input->analog[3] = (uint8_t)wheels[1];
	input->analog[6] = (uint8_t)gases[1];
	input->analog[7] = (uint8_t)brakes[1];
}

/* ---- the frame ---- */

#ifdef ORUNNERS_CONSOLE
/* The pause's picture: the frame it stopped at in grey, the words of the sprite list 'list' ('size' bytes:
   PAUSE and its lines, or the network's WAIT) in colour over it in the game's letters. The frame is drawn
   again as the board stands, then with the words' sprites over it (s32_video_overlay_sprites): the pixels that
   change are the words' */
static void make_words_picture(const uint32_t frozen[2][S32_VIDEO_MAX_WIDTH * S32_VIDEO_HEIGHT], const uint8_t *list,
	size_t size)
{
	static uint32_t plain[2][S32_VIDEO_MAX_WIDTH * S32_VIDEO_HEIGHT];
	int width;

	s32_video_draw_frame();
	for (int screen = 0; screen < 2; screen++)
		memcpy(plain[screen], s32_video_frame(screen, &width), sizeof(plain[screen]));
	s32_video_overlay_sprites(list, size);
	s32_video_draw_frame();
	for (int screen = 0; screen < 2; screen++)
	{
		const uint32_t *with_words = s32_video_frame(screen, &width);

		for (size_t index = 0; index < S32_VIDEO_MAX_WIDTH * S32_VIDEO_HEIGHT; index++)
		{
			const uint32_t colour = frozen[screen][index];
			const uint32_t level = ((((colour >> 16) & 0xFF) * 77 + ((colour >> 8) & 0xFF) * 150 + (colour & 0xFF) * 29)
				>> 8) * 3 / 5;

			pause_pixels[screen][index] = with_words[index] != plain[screen][index] ? with_words[index]
				: 0xFF000000u | (level << 16) | (level << 8) | level;
		}
	}
	s32_video_overlay_end();
}

static void make_pause_picture(const uint32_t frozen[2][S32_VIDEO_MAX_WIDTH * S32_VIDEO_HEIGHT], int choice)
{
	static uint8_t list[0x1000];

	make_words_picture(frozen, list, console_pause_sprites(list, sizeof(list), choice));
}

/* A network race waiting for a device unheard (console_network_waiting): the game held, its sound too, the
   picture in grey under WAIT, the network's frames running until it goes on */
static void run_network_wait(void)
{
	static uint32_t frozen[2][S32_VIDEO_MAX_WIDTH * S32_VIDEO_HEIGHT];
	static uint8_t list[0x400];

	if (audio)
		SDL_PauseAudioDevice(audio, 1);
	for (int screen = 0; screen < 2; screen++)
		memcpy(frozen[screen], s32_video_frame(screen, &pause_widths[screen]), sizeof(frozen[screen]));
	make_words_picture(frozen, list, console_network_wait_sprites(list, sizeof(list)));
	pause_shown = true;
	while (console_network_waiting())
	{
		read_events();    /* (closing the window ends the game there: program_stop) */
		show_screens();
		SDL_Delay(16);
		console_network_frame();
	}
	pause_shown = false;
	if (audio)
		SDL_PauseAudioDevice(audio, 0);
	next_frame = SDL_GetPerformanceCounter();
}

/* The game stopped, its sound too, the picture in grey under PAUSE: Escape, Enter or a game controller's
   Start goes on, as CONTINUE does; RESTART starts the same game again (console_restart_game), QUIT GAME ends
   the games being played (console_quit_game) */
static void run_pause(void)
{
	static uint32_t frozen[2][S32_VIDEO_MAX_WIDTH * S32_VIDEO_HEIGHT];
	uint32_t held = actions_held;
	int choice = CONSOLE_PAUSE_CONTINUE;

	pause_requested = false;
	if (audio)
		SDL_PauseAudioDevice(audio, 1);
	for (int screen = 0; screen < 2; screen++)
		memcpy(frozen[screen], s32_video_frame(screen, &pause_widths[screen]), sizeof(frozen[screen]));
	make_pause_picture(frozen, choice);
	pause_shown = true;
	show_screens();
	for (;;)
	{
		uint32_t now, pressed;

		read_events();    /* (closing the window ends the game there: program_stop) */
		now = read_actions(SDL_GetKeyboardState(NULL));
		pressed = now & ~held;
		held = now;
		if (pressed & (ACTION_BIT(ACTION_UP) | ACTION_BIT(ACTION_DOWN)))
		{
			/* the next line up or down, round; RESTART where it is offered */
			const int step = (pressed & ACTION_BIT(ACTION_UP)) ? CONSOLE_PAUSE_ITEMS - 1 : 1;

			do
				choice = (choice + step) % CONSOLE_PAUSE_ITEMS;
			while (choice == CONSOLE_PAUSE_RESTART && !console_can_restart());
			make_pause_picture(frozen, choice);
		}
		if (pressed & ACTION_BIT(ACTION_ACCEPT))
		{
			if (choice == CONSOLE_PAUSE_RESTART)
				console_restart_game();
			else if (choice == CONSOLE_PAUSE_QUIT)
				console_quit_game();
			break;
		}
		if (pressed & (PAUSE_BIT | ACTION_BIT(ACTION_BACK)))
			break;
		show_screens();    /* (the line chosen, a window resized or put full screen meanwhile) */
		SDL_Delay(16);
		console_network_frame();    /* (a network race goes on: this device is still heard) */
	}
	pause_shown = false;
	actions_held = held;    /* (the key let go in the race is no action) */
	if (audio)
		SDL_PauseAudioDevice(audio, 0);
	next_frame = SDL_GetPerformanceCounter();
}
#endif

static void wait_for_frame(void)
{
	const uint64_t frequency = SDL_GetPerformanceFrequency();
	uint64_t now = SDL_GetPerformanceCounter();

	next_frame += frequency / S32_FRAME_RATE;
	if (now > next_frame + frequency / 4)
		next_frame = now;    /* far behind (the window was moved, the machine was busy): start over */
	while ((now = SDL_GetPerformanceCounter()) < next_frame)
	{
		const uint64_t left = (next_frame - now) * 1000 / frequency;
		if (left > 2)
			SDL_Delay((Uint32)(left - 1));
	}
}

#ifdef ORUNNERS_HANDHELD
/* ---- a handheld's log (stderr: the launcher's outrunners.log): what SDL found, the frames' pace ---- */

/* Handhelds' own buttons as SDL's game controller has them, where SDL does not know them: Anbernic's H700
   consoles show theirs as a joystick, "ANBERNIC-keys", numbered as Anbernic's SDL 2.0.12 numbers them (as
   SegaSonic's port found them on the RG40XX H: the d-pad a hat, the sticks four axes, MENU button 8) */
static const char *const device_mappings[] = {
	"19000000010000000100000000010000,ANBERNIC-keys,a:b0,b:b1,x:b3,y:b2,leftshoulder:b4,rightshoulder:b10,"
	"lefttrigger:b5,righttrigger:b11,back:b6,start:b7,guide:b8,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,"
	"leftx:a0,lefty:a1,rightx:a2,righty:a3,leftstick:b9,rightstick:b12,platform:Linux,",
};

/* The mappings as a hint before SDL starts: the game controllers it finds then are opened as such */
static void add_device_mappings(void)
{
	static char mappings[2048];

	mappings[0] = '\0';
	for (size_t mapping = 0; mapping < SDL_arraysize(device_mappings); mapping++)
	{
		SDL_strlcat(mappings, device_mappings[mapping], sizeof(mappings));
		SDL_strlcat(mappings, "\n", sizeof(mappings));
	}
	SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG, mappings);
}

/* The log on the card as it is written: a handheld is often left with its reset button */
static void log_written(void)
{
	fflush(stderr);
#if !defined(_WIN32) && !defined(__SWITCH__)
	fsync(fileno(stderr));
#endif
}

static void log_device(void)
{
	SDL_version linked;
	SDL_DisplayMode mode;

	SDL_GetVersion(&linked);
	fprintf(stderr, "SDL %d.%d.%d, video %s, audio %s\n", linked.major, linked.minor, linked.patch,
		SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none",
		SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none");
	if (SDL_GetCurrentDisplayMode(0, &mode) == 0)
		fprintf(stderr, "screen %dx%d at %d Hz\n", mode.w, mode.h, mode.refresh_rate);
	for (int joystick = 0; joystick < SDL_NumJoysticks(); joystick++)
		fprintf(stderr, "controller %d: %s%s\n", joystick, SDL_JoystickNameForIndex(joystick),
			SDL_IsGameController(joystick) ? "" : " (a joystick: no mapping)");
	log_written();
}

/* Every 300 frames, the frames a second and the time a frame's work takes */
static void log_pace(bool work_done)
{
	static uint64_t pace_start, pace_work, pace_most, frame_start;
	static int pace_frames;
	const uint64_t now = SDL_GetPerformanceCounter(), frequency = SDL_GetPerformanceFrequency();

	if (!work_done)
	{
		frame_start = now;
		return;
	}
	if (!pace_start)
		pace_start = now;
	if (frame_start)
	{
		pace_work += now - frame_start;
		if (now - frame_start > pace_most)
			pace_most = now - frame_start;
	}
	if (++pace_frames == 300)
	{
		const double per_frame = 1000.0 / (double)frequency / pace_frames;

		fprintf(stderr, "%.1f frames a second; a frame takes %.1f ms (its video %.1f, showing it %.1f), at most %.1f\n",
			pace_frames * (double)frequency / (double)(now - pace_start), (double)pace_work * per_frame,
			(double)pace_video * per_frame, (double)pace_showing * per_frame, 1000.0 * (double)pace_most / (double)frequency);
		fflush(stderr);    /* (not to the card at once: a write to it would hold a frame) */
		pace_start = now;
		pace_frames = 0;
		pace_work = pace_most = 0;
		pace_video = pace_showing = 0;
	}
}
#endif

static void on_frame_done(void)
{
#ifdef ORUNNERS_HANDHELD
	log_pace(true);
#endif
	const uint64_t frame = program_frames() - 1;
	size_t count;
	const int16_t *mix;

	/* (in the headless runs' order: the board's hashes, then the sound) */
	if (hashes)
		fprintf(hashes, "%" PRIu64 " %016" PRIx64 "\n", frame, s32_state_hash());
	if (video_hashes)
		fprintf(video_hashes, "%" PRIu64 " %016" PRIx64 "\n", frame, s32_video_frame_hash());

	/* the sound board up to the frame's end, then the samples that start before it */
	if (s32_board.time_passes)
		s32_board.time_passes(s32_board.cycles);
	sound_chips_end_frame(s32_board.cycles);
	mix = sound_chips_frame_mix(&count);
#ifdef ORUNNERS_CONSOLE
	if (console_window_settings.volume != 5 && mix)
	{
		/* VOLUME: 0-10, 5 the game's own */
		static int16_t loud[4096];

		if (count * 2 > sizeof(loud) / sizeof(loud[0]))
			count = sizeof(loud) / sizeof(loud[0]) / 2;
		for (size_t index = 0; index < count * 2; index++)
		{
			const int value = mix[index] * console_window_settings.volume / 5;

			loud[index] = (int16_t)(value > 32767 ? 32767 : (value < -32768 ? -32768 : value));
		}
		mix = loud;
	}
#endif
	if (audio && !fast && SDL_GetQueuedAudioSize(audio) < sound_chips_mix_rate() / S32_FRAME_RATE * 4 * QUEUE_FRAMES_MAX)
		SDL_QueueAudio(audio, mix, (Uint32)(count * 2 * sizeof(int16_t)));
	if (sound_hashes)
		fprintf(sound_hashes, "%" PRIu64 " %016" PRIx64 " %016" PRIx64 "\n", frame, sound_chips_frame_hash(),
			sound_board_state_hash());
	if (script && s32_script_frames(script) && program_frames() >= s32_script_frames(script))
		program_stop();

	if (program_frames() % 600 == 0)
	{
		save_eeprom();
		save_backup_ram();
	}
#ifdef ORUNNERS_CONSOLE
	console_network_frame();
	if (console_network_waiting())
		run_network_wait();
	if (pause_requested)
		run_pause();
#endif
	if (!fast)
		wait_for_frame();
#ifdef ORUNNERS_HANDHELD
	log_pace(false);
#endif
}

static void on_irq_changed(bool asserted)
{
	program_irq_line(asserted);
}

static void on_log(const char *message)
{
	(void)message;
}

/* A window with what went wrong: the port has no console */
static int fail(const char *what, const char *detail)
{
	char message[1200];

	snprintf(message, sizeof(message), "%s\n%s", what, detail);
	fprintf(stderr, "%s\n", message);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, TITLE, message, NULL);
	return 1;
}

static FILE *open_hashes(const char *path)
{
	return path ? fopen(path, "w") : NULL;
}

/* Where the game keeps what it writes (the EEPROM, the sound RAM's bookkeeping): beside the executable on
   Windows; elsewhere the user's own folder for the game (SDL_GetPrefPath: ~/.local/share/OutRunners,
   ~/Library/Application Support/OutRunners), as the executable's may not be written (an .app, /usr/bin).
   The caller frees it (SDL_free) */
static char *saves_folder(void)
{
#ifdef __SWITCH__
	mkdir(SWITCH_FOLDER, 0777);
	return SDL_strdup(SWITCH_FOLDER "/");
#elif defined(__ANDROID__)
	return SDL_GetPrefPath("", "OutRunners");    /* (the app's own folder, its internal storage) */
#elif defined(_WIN32) || defined(ORUNNERS_HANDHELD)
	return SDL_GetBasePath();    /* (a handheld's: the game's folder on its card) */
#else
	return SDL_GetPrefPath("", "OutRunners");
#endif
}

/* On macOS, the folder the .app is in (the game's base is the .app's Resources folder: where the player sees
   the game is beside the .app); 0 elsewhere */
#ifndef ORUNNERS_HANDHELD
/* A folder of assets the first run made for another build of the game (its version.txt; extract_assets.py's
   have none) */
static bool assets_of_another_version(const char *folder)
{
	char path[1200];
	unsigned version = 0;
	FILE *file;
	bool other;

	snprintf(path, sizeof(path), "%s/" GAME_ASSETS_VERSION_FILE, folder);
	if ((file = fopen(path, "rb")) == NULL)
		return false;
	other = fscanf(file, "%x", &version) != 1 || version != game_assets_version();
	fclose(file);
	return other;
}
#endif

static int app_folder(const char *base, char *folder, size_t size)
{
#ifdef __APPLE__
	static const char inside[] = ".app/Contents/Resources/";
	const size_t length = base ? strlen(base) : 0, tail = sizeof(inside) - 1;
	size_t end;

	if (length < tail || length >= size || strcmp(base + length - tail, inside) != 0)
		return 0;
	for (end = length - tail; end > 0 && base[end - 1] != '/'; end--)
		;    /* (back over the .app's name) */
	memcpy(folder, base, end);
	folder[end] = '\0';
	return end > 0;
#else
	(void)base;
	(void)folder;
	(void)size;
	return 0;
#endif
}

#ifdef __ANDROID__
static const char android_pack_missing[] = "missing";

/* The pack in the APK's assets, read whole: what the game loads points into it, so it is kept for as long as
   the game runs. NULL, android_pack_missing, or what is wrong with it */
static const char *load_android_pack(void)
{
	SDL_RWops *packed = SDL_RWFromFile(ASSETS_PACK, "rb");
	const Sint64 size = packed ? SDL_RWsize(packed) : -1;
	uint8_t *bytes = size > 0 ? malloc((size_t)size) : NULL;
	size_t got = 0;
	const char *problem;

	if (!packed)
		return android_pack_missing;
	while (bytes && got < (size_t)size)
	{
		/* (its bytes as the asset manager hands them: maybe fewer than asked for at a time) */
		const size_t now = SDL_RWread(packed, bytes + got, 1, (size_t)size - got);

		if (now == 0)
			break;
		got += now;
	}
	SDL_RWclose(packed);
	if (!bytes || got != (size_t)size)
	{
		free(bytes);
		return "the pack cannot be read whole";
	}
	if ((problem = assets_load_pack_bytes(bytes, (size_t)size)) != NULL)
		free(bytes);
	return problem;
}
#endif

int main(int argc, char **argv)
{
	SDL_AudioSpec wanted;
	const char *script_path = NULL;
	const char *hashes_path = NULL, *video_hashes_path = NULL, *sound_hashes_path = NULL;
	const char *assets_path = NULL;
	char *base, *saves;

#ifdef __SWITCH__
	romfsInit();
	/* (what the game says, failures included, goes to outrunners.log in its folder: the Switch has no console
	   and SDL no message boxes there) */
	mkdir(SWITCH_FOLDER, 0777);
	freopen(SWITCH_FOLDER "/outrunners.log", "w", stderr);
	fprintf(stderr, "OutRunners: starting\n");
#endif
#ifdef __ANDROID__
	/* (what the game says goes to outrunners.log in the app's folder on the storage, Android/data/<the
	   app>/files: stderr goes nowhere there) */
	if (SDL_AndroidGetExternalStoragePath())
	{
		char log_path[1024];

		snprintf(log_path, sizeof(log_path), "%s/outrunners.log", SDL_AndroidGetExternalStoragePath());
		freopen(log_path, "w", stderr);
	}
	fprintf(stderr, "OutRunners: starting\n");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");    /* (back is the menus' back, never the end) */
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");          /* (fingers are the touch controls, not the mouse) */
	SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
#ifdef ORUNNERS_TOUCH_TEST
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");    /* (a computer's check of the touch controls: the mouse a finger) */
#endif
#ifdef ORUNNERS_HANDHELD
	add_device_mappings();
#endif
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
		return fail("SDL could not start.", SDL_GetError());
#ifdef ORUNNERS_HANDHELD
	log_device();
#endif
	/* the video's layers, sprites and mixing a band of rows on each core beside the game's (the same frames
	   as on one: runtime/src/workers.c) */
	s32_workers_start(VIDEO_WORKERS);
	fprintf(stderr, "video: %d threads beside the game's\n", s32_workers_count());
	for (int index = 1; index < argc; index++)
	{
		if (strcmp(argv[index], "--script") == 0 && index + 1 < argc)
			script_path = argv[++index];
		else if (strcmp(argv[index], "--hashes") == 0 && index + 1 < argc)
			hashes_path = argv[++index];
		else if (strcmp(argv[index], "--video-hashes") == 0 && index + 1 < argc)
			video_hashes_path = argv[++index];
		else if (strcmp(argv[index], "--sound-hashes") == 0 && index + 1 < argc)
			sound_hashes_path = argv[++index];
		else if (strcmp(argv[index], "--fast") == 0)
			fast = true;
		else if (strcmp(argv[index], "--assets") == 0 && index + 1 < argc)
			assets_path = argv[++index];
		else
			return fail("Unknown option.", USAGE);
	}
	if (script_path)
	{
		char error[256];

		script = s32_script_load(script_path, error, sizeof(error));
		if (!script)
			return fail("The control script cannot be read.", error);
	}
	fast = fast && script;
	hashes = open_hashes(hashes_path);
	video_hashes = open_hashes(video_hashes_path);
	sound_hashes = open_hashes(sound_hashes_path);
	if ((hashes_path && !hashes) || (video_hashes_path && !video_hashes) || (sound_hashes_path && !sound_hashes))
		return fail("A hash file cannot be written.", hashes_path && !hashes ? hashes_path
			: (video_hashes_path && !video_hashes ? video_hashes_path : sound_hashes_path));

#ifdef __SWITCH__
	base = SDL_strdup("romfs:/");    /* (what comes with the game: the pack and the pictures) */
#elif defined(__ANDROID__)
	base = SDL_strdup("");           /* (the APK's assets, which SDL's files read by a relative path) */
#else
	base = SDL_GetBasePath();
#endif
	saves = saves_folder();
	snprintf(eeprom_path, sizeof(eeprom_path), "%s" FILE_NAME ".eeprom", saves ? saves : "");
	snprintf(backup_ram_path, sizeof(backup_ram_path), "%s" FILE_NAME ".bookkeeping", saves ? saves : "");
#ifdef ORUNNERS_CONSOLE
	snprintf(settings_path, sizeof(settings_path), "%s" FILE_NAME ".ini", saves ? saves : "");
	load_window_settings();
#ifdef ORUNNERS_HANDHELD
	fix_window_settings();
#endif
	{
		char controls_path[1024];

		snprintf(controls_path, sizeof(controls_path), "%scontrols.ini", saves ? saves : "");
		controls_open(controls_path);
		console_ghost_folder(saves ? saves : "");
		load_prompts(base ? base : "");    /* (the pictures come with the game, beside it) */
	}
#endif
#ifdef __ANDROID__
	{
		/* the pack in the APK's assets (stored, not packed: android.cmake), read whole */
		const char *problem = load_android_pack();

		if (problem)
			return fail(problem == android_pack_missing ? "The game's assets are missing." : "The game's assets cannot be read.",
				problem == android_pack_missing ? ASSETS_PACK : problem);
	}
#else
	{
		/* the folder of the game's assets: --assets, else beside the executable, beside the .app on macOS, in
		   the folder of what the game keeps, or in the current folder; with none, the first run's, in the
		   folder of what the game keeps */
		char beside_app[1024], folder[1100], layout[1200];
		const char *places[4];
		const char *found = assets_path;
		const char *problem;
		size_t count = 0;
		bool stale = false;
		bool pack = false;

		places[count++] = base ? base : "";
		if (app_folder(base, beside_app, sizeof(beside_app)))
			places[count++] = beside_app;
		if (saves)
			places[count++] = saves;
		places[count++] = "";
		for (size_t place = 0; place < count && !found; place++)
		{
			FILE *probe;

			/* (the pack the devices take, where a player put one) */
			snprintf(folder, sizeof(folder), "%s" ASSETS_PACK, places[place]);
			if ((probe = fopen(folder, "rb")) != NULL)
			{
				fclose(probe);
				found = folder;
				pack = true;
				break;
			}
#ifndef ORUNNERS_HANDHELD
			snprintf(folder, sizeof(folder), "%sassets", places[place]);
			snprintf(layout, sizeof(layout), "%s/data/layout.json", folder);
			if ((probe = fopen(layout, "rb")) != NULL)
			{
				fclose(probe);
				found = folder;
				if (assets_of_another_version(folder))
				{
					stale = true;    /* (the first run's, before an update: taken out again there) */
					break;
				}
			}
#endif
		}
#ifdef ORUNNERS_HANDHELD
		(void)layout;
		(void)stale;
		if (!found)
			return fail("The game's assets are missing.", ASSETS_PACK);
#else
		if (!found || stale)
		{
			if (!stale)
				snprintf(folder, sizeof(folder), "%sassets", saves ? saves : (base ? base : ""));
			if (!first_run(TITLE, base, saves, folder))
			{
				/* (the window closed before a ROM set came) */
				SDL_free(base);
				SDL_free(saves);
				SDL_Quit();
				return 0;
			}
			found = folder;
		}
#endif
		if ((problem = pack || (assets_path && strstr(assets_path, ".assets")) ? assets_load_pack(found) : assets_load(found)) != NULL)
			return fail("The game's assets cannot be read.", problem);
#if defined(ORUNNERS_CONSOLE) && !defined(ORUNNERS_HANDHELD)
		if (!pack)
		{
			/* the pack the devices take (Anbernic's handhelds: beside their game), beside the folder, once */
			char pack_path[1100];
			FILE *probe;

			snprintf(pack_path, sizeof(pack_path), "%s" ASSETS_PACK, saves ? saves : (base ? base : ""));
			if ((probe = fopen(pack_path, "rb")) != NULL)
				fclose(probe);
			else if ((problem = assets_pack_write(pack_path)) != NULL)
				fprintf(stderr, "%s: %s\n", pack_path, problem);
		}
#endif
	}
#endif
	SDL_free(base);
	SDL_free(saves);

#ifdef __SWITCH__
	/* (the Switch's screen, 1280 x 720; docked, the system shows it at the TV's size) */
	window = SDL_CreateWindow(TITLE, 0, 0, 1280, 720, 0);
#elif defined(ORUNNERS_HANDHELD)
	window = SDL_CreateWindow(TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_FULLSCREEN_DESKTOP);
#else
	window = SDL_CreateWindow(TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		screens_shown() * 320 * WINDOW_SCALE, S32_VIDEO_HEIGHT * WINDOW_SCALE, SDL_WINDOW_RESIZABLE);
#endif
#ifdef ORUNNERS_CONSOLE
	if (window)
		apply_window_settings();
#endif
	renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : NULL;
	if (window && !renderer)
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		2 * S32_VIDEO_MAX_WIDTH, 2 * S32_VIDEO_HEIGHT) : NULL;    /* (room for two screens, either way) */
	if (!texture)
		return fail("The window could not be opened.", SDL_GetError());
	SDL_RenderSetIntegerScale(renderer, SDL_FALSE);
	SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
#ifdef ORUNNERS_TOUCH
	touch_open(renderer, "");    /* (their letters come with the game: Android's in the APK's assets) */
#endif

	/* the mix at the YM3438's rate; SDL takes it to the device's */
	SDL_zero(wanted);
	wanted.freq = (int)sound_chips_mix_rate();
	wanted.format = AUDIO_S16SYS;
	wanted.channels = 2;
	wanted.samples = 1024;
	audio = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
	if (audio)
		SDL_PauseAudioDevice(audio, 0);

	/* the board, the video, the sound board and the game, as the headless runs set them up */
	s32_board.irq_changed = on_irq_changed;
	s32_board.controls_update = on_controls_update;
	s32_board.log = on_log;
	s32_board.frame_timing = true;
	s32_board.program_at = data_at;
	s32_init(NULL);
	s32_video_init_graphics(assets_graphics());
	s32_board.frame_shown = on_frame_shown;
	s32_board.sound_at = assets_sound_data;
	s32_board.pcm_at = assets_pcm_byte;
	sound_chips_attach();
	sound_cpu_attach();
#ifdef ORUNNERS_CONSOLE
	sound_chips_join_sides(true);
	console_attach();
#endif
	if (!script)
	{
		load_eeprom();
		load_backup_ram();
	}
	program_reset();
	program_frame_done = on_frame_done;
	next_frame = SDL_GetPerformanceCounter();

	program_run();

	save_eeprom();
	save_backup_ram();
	if (hashes)
		fclose(hashes);
	if (video_hashes)
		fclose(video_hashes);
	if (sound_hashes)
		fclose(sound_hashes);
	if (audio)
		SDL_CloseAudioDevice(audio);
	SDL_Quit();
#ifdef __SWITCH__
	romfsExit();
#endif
#ifdef __ANDROID__
	exit(0);    /* (the app's process ends with the game: Android would start the next one in it, as this one left it) */
#endif
	return 0;
}
