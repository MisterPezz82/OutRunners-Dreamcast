/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * diag_screen.c: what went wrong, on the console's own screen. A crash (CPU exception, exit, abort,
 * stack overflow) or a freeze (no game frame for three seconds, found by a watchdog thread) writes a
 * page of numbers over the picture being shown, to photograph: the build, where each thread was, the
 * frame, and the fault's registers. Map the PC and PR to functions with build/outrunners-prototype.map.
 */
#include "diag_screen.h"
#include <kos.h>
#include <dc/biosfont.h>
#include <stdio.h>
#include <string.h>

volatile char dc_where = '-';          /* the main thread's place in the frame (diag_screen.h) */
volatile char dc_submit_where = '-';   /* the PowerVR submit thread's */
extern volatile uint32_t dc_frame_count, dc_boot_stage;
extern volatile uint32_t dc_vmu_state;
extern volatile uint32_t dc_pvr_stats[16];

volatile int dc_paused;                /* the console build's pause: no game frames, no freeze */

static int line;

static void put(uint16_t *fb, int width, const char *text)
{
	bfont_draw_str(fb + (8 + line * 24) * width + 8, width, true, text);
	line++;
}

void dc_diag_screen(const char *title, uint32_t code, uint32_t pc, uint32_t pr, uint32_t sp)
{
	/* the frame buffer on screen now, whatever its size (FB_R_SOF1, FB_R_SIZE) */
	const uint32_t sof = *(volatile uint32_t *)0xa05f8050 & 0x7fffff;
	const uint32_t size = *(volatile uint32_t *)0xa05f805c;
	int width = (int)((size & 1023) + 1) * 2, height = (int)((size >> 10) & 1023) + 1;
	if (width < 320 || width > 640) width = 320;
	if (height < 240 || height > 480) height = 240;
	uint16_t *fb = (uint16_t *)(0xa5000000u + sof);
	for (int i = 0; i < width * height; i++) fb[i] = 0x3000;    /* (dark red) */
	bfont_set_background_color(0x3000);
	bfont_set_foreground_color(0xffff);
	char text[64];
	line = 0;
	put(fb, width, title);
	snprintf(text, sizeof(text), "%s %s", __DATE__, __TIME__);    /* (the build: 320 pixels hold 26 characters) */ put(fb, width, text);
	snprintf(text, sizeof(text), "code %08lx", (unsigned long)code); put(fb, width, text);
	snprintf(text, sizeof(text), "pc %08lx", (unsigned long)pc); put(fb, width, text);
	snprintf(text, sizeof(text), "pr %08lx", (unsigned long)pr); put(fb, width, text);
	snprintf(text, sizeof(text), "sp %08lx", (unsigned long)sp); put(fb, width, text);
	snprintf(text, sizeof(text), "frame %lu stage %lu", (unsigned long)dc_frame_count, (unsigned long)dc_boot_stage); put(fb, width, text);
	snprintf(text, sizeof(text), "where %c submit %c", dc_where, dc_submit_where); put(fb, width, text);
	snprintf(text, sizeof(text), "vram %lu vmu %lx", (unsigned long)dc_pvr_stats[7], (unsigned long)dc_vmu_state); put(fb, width, text);
	{ extern volatile uint32_t dc_unaligned_count; snprintf(text, sizeof(text), "misaligned %lu", (unsigned long)dc_unaligned_count); put(fb, width, text); }
}

/* With interrupts still on, the PowerVR may yet show another frame buffer: the page is drawn again
   every quarter second, for good */
void dc_diag_screen_hold(const char *title, uint32_t code, uint32_t pc, uint32_t pr, uint32_t sp)
{
	for (;;)
	{
		dc_diag_screen(title, code, pc, pr, sp);
		const uint64_t until = timer_us_gettime64() + 250000;
		while (timer_us_gettime64() < until)
			asm volatile("nop");
	}
}

/* ---- the watchdog ---- */

static void *watchdog(void *arg)
{
	(void)arg;
	uint32_t last = 0, still = 0;
	for (;;)
	{
		thd_sleep(1000);
		if (dc_boot_stage < 8 || dc_paused) { still = 0; continue; }
		if (dc_frame_count != last) { last = dc_frame_count; still = 0; continue; }
		if (++still >= 3)
			dc_diag_screen("FROZEN: no frame for 3 s", 0, 0, 0, 0);    /* (again each second) */
	}
	return NULL;
}

void dc_diag_start_watchdog(void)
{
	kthread_attr_t attr = { 0 };
	attr.stack_size = 8 * 1024;
	attr.prio = PRIO_DEFAULT - 2;    /* (above everything of the game's, so a stuck frame cannot hide it) */
	attr.label = "watchdog";
	thd_create_ex(&attr, watchdog, NULL);
}
