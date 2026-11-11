// SPDX-License-Identifier: BSD-3-Clause
#include "dc_port.h"
#include <kos.h>
#include <dc/video.h>
#include <dc/biosfont.h>
#include <s32/video.h>
#include <stdint.h>
#include <string.h>

static int dc_present_frame_width = -1;
static char status[49];
static bool status_off;

void dc_status_off(void) {
    status_off = true;
}

/* The loading's steps as the loading screen shows them (startup.c): "n/8 ..." moves the car on; anything
   else (an error) is written on the screen */
void dc_status(const char *text) {
    if (status_off || !text) return;
    if (text[0] >= '1' && text[0] <= '9' && text[1] == '/') {
        /* (the steps' shares of the loading time: the graphics, read whole at step 2, are most of it) */
        static const int at[9] = { 0, 60, 180, 700, 740, 780, 880, 940, 1000 };
        const int step = text[0] - '0';
        dc_loading_progress(at[step > 8 ? 8 : step], at[step >= 8 ? 8 : step + 1]);
        return;
    }
    if (strncmp(text, "ERROR", 5) == 0)
        dc_loading_message(text);
}

void dc_present_frame(void) {
    int width;
    const uint32_t *src = s32_video_frame(0, &width);
    if (!src || width < 1 || width > 496) {
        dc_status("invalid frame");
        return;
    }

    int scale = (width <= 320) ? 2 : 1;
    int left = (640 - width * scale) / 2;
    int top = (scale == 2) ? 24 : 128;

    if (width != dc_present_frame_width) {
        vid_clear(0, 0, 0);
        dc_present_frame_width = width;
    }

    uint16_t *dst = vram_s + top * 640 + left;
    
    if (scale == 2) {
        for (int y = 0; y < S32_VIDEO_HEIGHT; y++) {
            const uint32_t *row = src + y * S32_VIDEO_MAX_WIDTH;
            uint16_t *d0 = dst + y * 2 * 640;
            uint16_t *d1 = d0 + 640;
            for (int x = 0; x < width; x++) {
                uint32_t c = row[x];
                uint16_t rgb565 = ((c >> 8) & 0xf800u) | ((c >> 5) & 0x07e0u) | ((c >> 3) & 0x001fu);
                d0[x * 2] = rgb565;
                d0[x * 2 + 1] = rgb565;
                d1[x * 2] = rgb565;
                d1[x * 2 + 1] = rgb565;
            }
        }
    } else {
        for (int y = 0; y < S32_VIDEO_HEIGHT; y++) {
            const uint32_t *row = src + y * S32_VIDEO_MAX_WIDTH;
            uint16_t *d = dst + y * 640;
            for (int x = 0; x < width; x++) {
                uint32_t c = row[x];
                d[x] = ((c >> 8) & 0xf800u) | ((c >> 5) & 0x07e0u) | ((c >> 3) & 0x001fu);
            }
        }
    }

    bfont_draw_str(vram_s, 640, true, status);
}
