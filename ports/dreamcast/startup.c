/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * startup.c: the loading screen. The disc builder puts it together on the player's machine from the
 * player's own ROM set (assets/splash.bin, tools/dreamcast/make_splash.py): the game's title picture,
 * the credits, and one of the title's cars, which drives along the bottom as the game loads, the
 * loading bar the road behind it. Without the file (an older disc) the credits are plain text.
 *
 * The picture's output: with a VGA box 480p, the game's 320 x 224 shown at exactly twice its size by the
 * video hardware (each pixel and each line doubled: sharp, no scaling shimmer; a VGA box runs at 31 kHz
 * only, so 240p is not possible through it). With an RGB/SCART or composite cable progressive 240p at
 * 60 Hz, never 480i, this screen included, and on PAL consoles too (the game runs a frame per refresh:
 * 50 Hz would slow it down).
 */
#include "dc_port.h"
#include <kos.h>
#include <dc/video.h>
#include <dc/biosfont.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int dc_video_choice = DC_VIDEO_240P;

#define SCREEN_W 320
#define TOP 8                     /* the 224-line picture's first row on the 240-line screen */
#define SHOWN_MS 3500             /* the screen stays at least this long */

static uint16_t *picture;         /* 320 x 224, RGB565: what is behind the car */
static uint16_t *car;             /* car_w x car_h, 0xF81F clear */
static int car_w, car_h, road_y;
static volatile int target;       /* the loading so far, 0-1000 */
static volatile int creep_to;     /* while a step runs the car creeps on towards the next step, up to here */
static int shown;                 /* where the car is, 0-1000 */
static volatile bool running;
static kthread_t *animator;
static uint64_t started_ms;
static bool text_only;

static void text(int x, int y, const char *s)
{
	bfont_draw_str(vram_s + y * SCREEN_W + x, SCREEN_W, true, s);
}

static void centred(int y, const char *s)
{
	const int x = (SCREEN_W - (int)strlen(s) * 12) / 2;
	text(x < 0 ? 0 : x, y, s);
}

static bool load_splash(void)
{
	FILE *f = fopen("/cd/assets/splash.bin", "rb");
	if (!f) return false;
	uint16_t head[4], size[2], road;
	bool ok = fread(head, 2, 4, f) == 4 && memcmp(head, "ORSP", 4) == 0 && head[2] == SCREEN_W && head[3] == 224;
	if (ok)
	{
		picture = aligned_alloc(32, SCREEN_W * 224 * 2);
		ok = picture && fread(picture, 2, SCREEN_W * 224, f) == SCREEN_W * 224 && fread(size, 2, 2, f) == 2 &&
			size[0] > 0 && size[0] <= SCREEN_W && size[1] > 0 && size[1] <= 100;
	}
	if (ok)
	{
		car_w = size[0]; car_h = size[1];
		car = malloc((size_t)car_w * car_h * 2);
		ok = car && fread(car, 2, (size_t)car_w * car_h, f) == (size_t)car_w * car_h && fread(&road, 2, 1, f) == 1 &&
			road >= car_h && road <= 224 - 3;
		road_y = road;
	}
	fclose(f);
	if (!ok) { free(picture); free(car); picture = car = NULL; }
	return ok;
}

/* the strip the car drives in: the picture again, the road so far, the car */
static void draw_car(int progress)
{
	const int y0 = road_y - car_h, rows = car_h + 3;
	/* the car's front runs from the left edge to just off the right one */
	const int front = progress * (SCREEN_W + car_w / 2) / 1000, left = front - car_w;
	static uint16_t strip[(100 + 3) * SCREEN_W];
	memcpy(strip, picture + y0 * SCREEN_W, (size_t)rows * SCREEN_W * 2);
	for (int y = car_h; y < rows; y++)
		for (int x = 0; x < left + car_w / 3 && x < SCREEN_W; x++)
			strip[y * SCREEN_W + x] = y == car_h ? 0xffe8 : (y == car_h + 1 ? 0xfe80 : 0xcb00);    /* (gold, as the panel's line) */
	for (int y = 0; y < car_h; y++)
		for (int x = 0; x < car_w; x++)
		{
			const int sx = left + x;
			const uint16_t c = car[y * car_w + x];
			if (sx >= 0 && sx < SCREEN_W && c != 0xF81F) strip[y * SCREEN_W + sx] = c;
		}
	memcpy(vram_s + (TOP + y0) * SCREEN_W, strip, (size_t)rows * SCREEN_W * 2);
}

static void *animate(void *arg)
{
	(void)arg;
	while (running)
	{
		/* towards the loading so far: quickly when far behind, never a jump */
		const int gap = target - shown;
		if (gap > 0) shown += gap > 240 ? 24 : (gap > 60 ? 12 : (gap > 6 ? 6 : gap));
		else if (shown < creep_to) shown += 2;
		vid_waitvbl();
		draw_car(shown);
		thd_sleep(10);
	}
	return NULL;
}

void dc_startup_screen(void)
{
	const bool vga = vid_check_cable() == CT_VGA;
	dc_video_choice = vga ? DC_VIDEO_480P : DC_VIDEO_240P;
	/* the game's own modes: 320 x 240 doubled on a VGA box, the 262-line progressive mode at 15 kHz */
	vid_set_mode(vga ? DM_320x240_VGA : DM_320x240_NTSC, PM_RGB565);
	vid_clear(0, 0, 0);
	started_ms = timer_ms_gettime64();
	if (!load_splash())
	{
		text_only = true;
		bfont_set_background_color(0);
		bfont_set_foreground_color(0xfd20);
		centred(40, "OutRunners");
		bfont_set_foreground_color(0xffff);
		centred(80, "Dreamcast port");
		centred(104, "by Pezz82");
		centred(136, "Original PC port by");
		centred(160, "Sebastian Toyos");
		return;
	}
	memcpy(vram_s + TOP * SCREEN_W, picture, SCREEN_W * 224 * 2);
	draw_car(0);
	running = true;
	kthread_attr_t attr = { 0 };
	attr.stack_size = 16 * 1024;
	attr.prio = PRIO_DEFAULT - 1;    /* (above the loading: it sleeps between frames) */
	attr.label = "loading-screen";
	animator = thd_create_ex(&attr, animate, NULL);
}

/* how far the loading is, 0-1000 */
void dc_loading_progress(int thousandths, int next)
{
	if (thousandths > target) target = thousandths > 1000 ? 1000 : thousandths;
	creep_to = next - 30;
}

/* a message that must be read (an error): over the screen's bottom rows */
void dc_loading_message(const char *message)
{
	char line[27];
	snprintf(line, sizeof(line), "%-26.26s", message ? message : "");
	if (running) { running = false; thd_join(animator, NULL); animator = NULL; }
	bfont_set_background_color(0);
	bfont_set_foreground_color(0xffff);
	text(4, 240 - 28, line);
}

/* the loading is done: the car drives off, the screen has had its time, and the display is the game's */
void dc_loading_finish(void)
{
	target = 1000;
	creep_to = 1000;
	if (!text_only)
		for (int i = 0; i < 200 && shown < 1000 && running; i++) thd_sleep(16);
	while (timer_ms_gettime64() - started_ms < SHOWN_MS) thd_sleep(16);
	if (running) { running = false; thd_join(animator, NULL); animator = NULL; }
	free(picture); free(car);
	picture = car = NULL;
	vid_clear(0, 0, 0);
}
