/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <kos.h>
#include <dc/video.h>
#include <dc/biosfont.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <host/tasks.h>
#include <s32/workers.h>
#include <s32/machine.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static int test_failures = 0;
static int task_counter = 0;
static host_task *main_task;

typedef struct {
    int counter;
    int valid;
} worker_ctx_t;

static void worker_rows_cb(void *context, int first, int last) {
    worker_ctx_t *ctx = (worker_ctx_t *)context;
    if (ctx && first <= last) {
        ctx->counter++;
        ctx->valid = 1;
    }
}

static void worker_task(void) {
    while (1) {
        ++task_counter;
        host_task_switch(main_task);
    }
}

static void draw_line(int y, const char *text) {
    char buf[49];
    memset(buf, ' ', 48);
    size_t len = strlen(text);
    if (len > 48) len = 48;
    memcpy(buf, text, len);
    buf[48] = '\0';
    bfont_draw_str(vram_s + y * 640 + 24, 640, true, buf);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    vid_set_mode(DM_640x480, PM_RGB565);
    vid_clear(0, 0, 0);
    bfont_set_foreground_color(0xffff);
    bfont_set_background_color(0);

    draw_line(0, "OutRunners Dreamcast - diagnostic");
    draw_line(24, "NOT A PLAYABLE GAME");

    worker_ctx_t wctx = {0, 0};
    s32_workers_rows(worker_rows_cb, &wctx, 10, 5);
    if (wctx.counter != 0) {
        test_failures++;
        draw_line(72, "Workers: FAIL (empty range)");
    } else {
        draw_line(72, "Workers: PASS (empty)");
    }

    wctx.counter = 0;
    wctx.valid = 0;
    s32_workers_rows(worker_rows_cb, &wctx, 0, 10);
    if (wctx.counter == 1 && wctx.valid) {
        draw_line(72, "Workers: PASS");
    } else {
        test_failures++;
        draw_line(72, "Workers: FAIL");
    }

    main_task = host_task_main();
    host_task *worker = host_task_create(worker_task);
    for (int i = 0; i < 1000; i++) {
        host_task_switch(worker);
    }

    if (task_counter == 1000 && host_task_current() == main_task) {
        draw_line(96, "Tasks: PASS");
    } else {
        test_failures++;
        draw_line(96, "Tasks: FAIL");
    }

    char size_buf[64];
    snprintf(size_buf, sizeof(size_buf), "sizeof(s32_machine): %zu", sizeof(s32_machine));
    draw_line(144, size_buf);

    draw_line(192, "Controller: ");
    draw_line(216, "");
    draw_line(240, "Press Start to exit");

    while (1) {
        maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if (dev) {
            cont_state_t *state = maple_dev_status(dev);
            if (state) {
                char line1[49];
                char line2[49];
                snprintf(line1, sizeof(line1), "JoyX:%d JoyY:%d", state->joyx, state->joyy);
                snprintf(line2, sizeof(line2), "L:%d R:%d A:%d B:%d", state->ltrig, state->rtrig, state->buttons & CONT_A, state->buttons & CONT_B);
                draw_line(192, line1);
                draw_line(216, line2);
                if (state->buttons & CONT_START) {
                    return test_failures ? 1 : 0;
                }
            }
        } else {
            draw_line(192, "No Controller");
            draw_line(216, "");
        }
        vid_waitvbl();
    }
}
