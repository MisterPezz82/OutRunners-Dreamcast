/* SPDX-License-Identifier: BSD-3-Clause */
#include <kos.h>
#include <dc/video.h>
#include <dc/biosfont.h>
#include <game/assets.h>
#include <s32/machine.h>
#include <s32/program.h>
#include <s32/video.h>
#include <s32/workers.h>
#include <sound/chips.h>
#include "stream_media.h"
#include "dc_port.h"
#include "aica_pcm.h"
#include "vmu_save.h"
#include "diag_screen.h"
#include <s32/script.h>
#include <stdio.h>
#include <string.h>

/* Test builds: a control script on the disc (assets/autoplay.txt, the headless tests' format) drives
   the controls as the scripted tests do; without the file the pad does */
static s32_script *autoplay;

#ifdef ORUNNERS_CONSOLE
/* Test builds: the console menus' actions from the disc as well (assets/actions.txt, the console headless's
   --actions format: a frame and up, down, left, right, accept, back or start a line; and pause, the pause
   for four seconds) */
#include "console.h"
#define TEST_ACTIONS 64
static struct { uint32_t frame; uint32_t bits; bool pause; } test_actions[TEST_ACTIONS];
static int test_action_count;
static bool test_pause_due;

static void test_actions_load(void) {
    static const char *const names[ACTIONS] = { "up", "down", "left", "right", "accept", "back", "start" };
    char line[96], name[32];
    unsigned long frame;
    FILE *in = fopen("/cd/assets/actions.txt", "r");
    if (!in) return;
    while (fgets(line, sizeof(line), in) && test_action_count < TEST_ACTIONS) {
        if (line[0] == '#' || sscanf(line, "%lu %31s", &frame, name) != 2) continue;
        test_actions[test_action_count].frame = (uint32_t)frame;
        test_actions[test_action_count].pause = strcmp(name, "pause") == 0;
        for (int a = 0; a < ACTIONS; a++)
            if (strcmp(name, names[a]) == 0) test_actions[test_action_count].bits = ACTION_BIT(a);
        test_action_count++;
    }
    fclose(in);
}

static void test_actions_apply(void) {
    for (int i = 0; i < test_action_count; i++)
        if (test_actions[i].frame == (uint32_t)s32_board.frame) {
            if (test_actions[i].pause) test_pause_due = true;
            else console_actions(test_actions[i].bits);
        }
}
#endif

static void controls_with_script(void) {
    static bool idle_set;
#ifdef ORUNNERS_CONSOLE
    test_actions_apply();
#endif
    if (!autoplay) {
        dc_controls_update();
        return;
    }
    /* (the script holds its presses across frames: the inputs are set to idle once, then left to it) */
    if (!idle_set) {
        dc_controls_update();
        idle_set = true;
    }
    s32_script_apply(autoplay, s32_board.frame);
}
#ifndef DC_SOFTWARE_VIDEO
#include "pvr/drawlist.h"
#include "pvr/pvr_backend.h"
#define DC_PVR 1
#endif
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
volatile uint32_t dc_heap_free_total;
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

int dc_fault_install(void);
int dc_task_stack_check(int full);
extern volatile uint32_t dc_fault_code;

KOS_INIT_FLAGS(INIT_DEFAULT);

volatile uint32_t dc_frame_count = 0;

#ifdef ORUNNERS_CONSOLE
/* ---- the console build: the PC version's menus (console/), its options and its pause ---- */
#include "console.h"
#include <dc/maple.h>
#include <dc/maple/purupuru.h>

extern volatile int dc_paused, dc_fps_on;

/* the options the menus change, as the VMU keeps them (vmu_save.h): byte 0 marks them set */
static void options_to_save(void) {
    const console_window *s = &console_window_settings;
    dc_options[0] = 1;
    dc_options[1] = (uint8_t)s->volume;
    dc_options[2] = (uint8_t)s->dead_zone;
    dc_options[3] = (uint8_t)s->sensitivity;
    dc_options[4] = (uint8_t)s->steering_speed;
    dc_options[5] = (uint8_t)s->vibration;
    dc_options[6] = (uint8_t)s->fps;
}

static void options_from_save(void) {
    console_window *s = &console_window_settings;
    if (dc_options[0] != 1) return;
    if (dc_options[1] <= 10) s->volume = dc_options[1];
    if (dc_options[2] <= DEAD_ZONE_MOST) s->dead_zone = dc_options[2] / DEAD_ZONE_STEP * DEAD_ZONE_STEP;
    if (dc_options[3] >= 1 && dc_options[3] <= STEERING_LEVELS) s->sensitivity = dc_options[3];
    if (dc_options[4] >= 1 && dc_options[4] <= STEERING_LEVELS) s->steering_speed = dc_options[4];
    if (dc_options[5] <= 100) s->vibration = dc_options[5] / FORCE_STEP * FORCE_STEP;
    s->fps = dc_options[6] != 0;
}

static void options_apply(void) {
    dc_aica_set_volume(console_window_settings.volume);
    dc_fps_on = console_window_settings.fps;
}

/* The cabinet's wheel motor (output port D, bit 4: a crash, off the road) on a vibration pack, as hard as
   OPTIONS > STEERING's VIBRATION says: an effect sent again while the motor runs, stopped when it stops */
static void rumble_frame(bool motor) {
    static bool running;
    static uint32_t frames;
    const int percent = console_window_settings.vibration;
    const bool on = motor && percent > 0;
    if (!on && !running) return;
    if (on && running && ++frames % 20 != 0) return;
    maple_device_t *pack = maple_enum_type(0, MAPLE_FUNC_PURUPURU);
    if (!pack) { running = false; return; }
    purupuru_effect_t effect = { .raw = 0 };
    effect.motor = 1;
    if (on) {
        effect.fpow = (uint32_t)(1 + percent * 6 / 100);
        effect.freq = 26;
        effect.inc = 40;
    }
    purupuru_rumble(pack, &effect);
    running = on;
    frames = 0;
}

/* The pause (Start in a race): the game and its sound held, the frame it stopped at dimmed under PAUSE
   and its lines in the game's letters (console_pause_sprites, through the drawlist's overlay). Start or B
   goes on, as CONTINUE does; RESTART starts the same game again; QUIT GAME goes back to the title. */
static void run_pause(void) {
#ifdef DC_PVR
    static uint8_t list[0x800];
    int choice = CONSOLE_PAUSE_CONTINUE;

    dc_paused = 1;
    dc_aica_pause(true);
    rumble_frame(false);
    (void)dc_menu_actions();
    for (int frames = 0;; frames++) {
        if (test_action_count && frames >= 240) break;    /* (a test's pause ends by itself) */
        dl_overlay(list, console_pause_sprites(list, sizeof(list), choice));
        dc_pvr_frame(dl_build());
        const uint32_t pressed = dc_menu_actions();
        if (pressed & (ACTION_BIT(ACTION_UP) | ACTION_BIT(ACTION_DOWN))) {
            const int step = (pressed & ACTION_BIT(ACTION_UP)) ? CONSOLE_PAUSE_ITEMS - 1 : 1;
            do
                choice = (choice + step) % CONSOLE_PAUSE_ITEMS;
            while (choice == CONSOLE_PAUSE_RESTART && !console_can_restart());
        }
        else if (pressed & ACTION_BIT(ACTION_ACCEPT)) {    /* (A, or Start) */
            if (choice == CONSOLE_PAUSE_RESTART) console_restart_game();
            else if (choice == CONSOLE_PAUSE_QUIT) console_quit_game();
            break;
        }
        else if (pressed & ACTION_BIT(ACTION_BACK))
            break;
        if (dc_exit_requested()) break;
    }
    dl_overlay(NULL, 0);
    dc_aica_pause(false);
    dc_paused = 0;
#endif
}

static void console_frame(void) {
    if (console_window_settings.changed) {
        console_window_settings.changed = false;
        options_apply();
        options_to_save();    /* (to the VMU with the settings: vmu_save.c) */
    }
    rumble_frame((s32_board.outputs[0][0] & 0x10) != 0);
    if ((dc_pause_wanted() || test_pause_due) && console_can_pause())
        run_pause();
    test_pause_due = false;
}
#endif
volatile uint32_t dc_boot_stage = 0;
static bool io_failed = false;

/* Frame-time evidence for the Flycast capture: microseconds per frame, averaged over 64 game frames
   (index 0 whole frame, 1 video compose, 2 sprite drawing, 3 sound chips end of frame, 4 present,
   5 all time_passes calls (sound CPU and chips during the frame, 3 included), 6 PowerVR wait,
   7 PowerVR frame work). Emulator
   timing is not hardware timing; real figures need a Dreamcast. */
volatile uint32_t dc_time_avg_us[8];
static uint64_t time_sum[8];
static void (*real_time_passes)(uint64_t);
static uint64_t frame_pass_us, frame_video_us;    /* (this frame's, for the window's worst frame) */

static void timed_time_passes(uint64_t until) {
    const uint64_t t = timer_us_gettime64();
    dc_where = 'T';
    real_time_passes(until);
    dc_where = 'G';
    frame_pass_us += timer_us_gettime64() - t;
}
static uint64_t last_frame_us;
static void (*real_frame_shown)(void);
static void (*real_sprites_due)(bool, bool);

#ifdef DC_PVR
/* the board's picture on the PowerVR (pvr/): the frame as the drawlist describes it */
/* Frame skip: the game and its sound run a frame at a time, 60 a second, and when a frame's work does
   not fit in 16.7 ms everything slows, the music with it. A frame that starts more than half a frame
   behind its time is not drawn (nothing waits for the display either), so the game catches up: the
   picture drops a frame, the speed and the sound hold. Never two in a row; after a long stall (a
   load) the schedule starts again. */
volatile uint32_t dc_frames_skipped;
volatile uint32_t dc_sound_us_total;
static void pvr_frame_shown(void) {
    static uint64_t due;
    static bool skipped_last;
    const uint64_t now = timer_us_gettime64();
    if (due == 0 || now < due || now - due > 100000) due = now;    /* (on time, or starting again) */
    const uint32_t late = (uint32_t)(now - due);
    due += 16683;
    if (late > 8300 && !skipped_last) {
        skipped_last = true;
        dc_frames_skipped++;
        return;
    }
    skipped_last = false;
    dc_pvr_frame(dl_build());
}
#endif

static void timed_frame_shown(void) {
    const uint64_t t = timer_us_gettime64();
    real_frame_shown();
    frame_video_us += timer_us_gettime64() - t;
}

static void timed_sprites_due(bool erase, bool draw) {
    const uint64_t t = timer_us_gettime64();
    real_sprites_due(erase, draw);
    (void)t;
}

static void log_callback(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void frame_callback(void) {
    dc_frame_count++;
    dc_where = 'F';
#ifdef DC_DIAG_TEST
    if (dc_frame_count == 600) exit(7);    /* (test: the stopped screen) */
#endif
#ifndef DC_NO_VMU
    dc_vmu_frame(dc_frame_count);
#endif
    if (dc_frame_count == 900) {
        /* (measurement, once in play: the largest free block, and all the free heap) */
        extern volatile uint32_t dc_heap_free_total;
        dc_media_probe_heap();
        dc_heap_free_total = (uint32_t)mallinfo().fordblks;
    }
    dc_media_pump();    /* (the renderer's sprite page unpacks for the frame) */
    dc_media_update_stats();
    if (dc_task_stack_check(1)) {
        dc_fault_code = 0x57ac;
        dc_diag_screen_hold("CRASHED: task stack overflow", 0x57ac, 0, 0, 0);
        dc_status("ERROR - game task stack overflow");
        for (;;) {
            asm volatile("nop");
        }
    }
    uint64_t t = timer_us_gettime64();
    if (s32_board.time_passes) {
        s32_board.time_passes(s32_board.cycles);
    }
    dc_where = 'S';
    sound_chips_end_frame(s32_board.cycles);
    dc_aica_pcm_frame(sound_chips_multipcm());
    const uint64_t sound_us = timer_us_gettime64() - t;
#ifndef DC_PVR
    dc_present_frame();
#endif
    t = timer_us_gettime64();
    if (last_frame_us != 0) {
        time_sum[0] += t - last_frame_us;
        /* (slots 2-5: the window's worst frame, and of it the frame display (drawlist and PVR), the
           PVR waits, and board time with sound's end of frame) */
        if (t - last_frame_us > time_sum[3]) {
            time_sum[3] = t - last_frame_us;
            time_sum[2] = frame_video_us;
#ifdef DC_PVR
            time_sum[4] = dc_pvr_wait_us;
            time_sum[1] = sound_us;    /* (slot 1: of its board time, sound's end of frame and the AICA) */
#endif
            time_sum[5] = frame_pass_us + sound_us;
        }
    }
    dc_sound_us_total += (uint32_t)(frame_pass_us + sound_us);    /* (for the overlay: the sound board's time) */
    frame_pass_us = frame_video_us = 0;
#ifdef DC_PVR
    time_sum[6] += dc_pvr_wait_us;
    time_sum[7] += dc_pvr_work_us;
#endif
    last_frame_us = t;
    if ((dc_frame_count & 63u) == 0u) {
        for (int i = 0; i < 8; i++) {
            dc_time_avg_us[i] = (uint32_t)(i >= 1 && i <= 5 ? time_sum[i] : time_sum[i] / 64u);
            time_sum[i] = 0;
        }
    }

#ifdef ORUNNERS_CONSOLE
    console_frame();
    last_frame_us = timer_us_gettime64();    /* (a pause is no slow frame) */
#endif
    if (dc_media_failed()) {
        io_failed = true;
        dc_status("ERROR - media read failed");
        program_stop();
    } else if (dc_exit_requested()) {
        program_stop();
    }
#ifndef DC_PVR
    vid_waitvbl();    /* (the PowerVR's scene flips at VBLANK and pvr_wait_ready paces it) */
#endif
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    dc_startup_screen();    /* (credits, and the output: 240p or 480p) */
    if (dc_fault_install() != 0) {
        dc_status("fault handler setup failed");
        return 1;
    }
    dc_status("loading");

#ifdef DC_READ_BENCH
    /* (measurement: disc read speed. fault_pc gets 1 MiB of 32 KiB sequential reads in microseconds,
       exit_caller 16 scattered 32 KiB reads; both are otherwise zero while nothing has failed) */
    {
        extern volatile uint32_t dc_fault_pc, dc_exit_caller;
        static uint8_t buf[32768] __attribute__((aligned(32)));
        int fd = open("/cd/assets/pcm.bin", O_RDONLY);
        if (fd >= 0) {
            uint64_t t = timer_us_gettime64();
            for (int i = 0; i < 32; i++) read(fd, buf, sizeof(buf));
            dc_fault_pc = (uint32_t)(timer_us_gettime64() - t);
            t = timer_us_gettime64();
            for (int i = 0; i < 16; i++) { lseek(fd, (off_t)((i * 7) % 128) * 32768, SEEK_SET); read(fd, buf, sizeof(buf)); }
            dc_exit_caller = (uint32_t)(timer_us_gettime64() - t);
            close(fd);
        }
    }
#endif
    dc_boot_stage = 1;
    dc_status("1/8 - loading core tables");
    const char *err = assets_load_pack("/cd/assets/core.assets");
    if (err) {
        dc_status("ERROR - asset load failed");
        fprintf(stderr, "Asset load failed: %s\n", err);
        thd_sleep(10000);
        return 1;
    }

    dc_boot_stage = 2;
    dc_status("2/8 - opening streamed media");
    if (dc_media_open("/cd/assets") != 0) {
        dc_status("ERROR - media open failed");
        fprintf(stderr, "Media open failed\n");
        thd_sleep(10000);
        return 1;
    }

    /* No program image is resident on Dreamcast: the board reads program ROM through the
       core pack, as the headless and SDL2 ports do. Leaving this NULL made the first ROM read
       call address 0 (the boot ROM), which silently rebooted the console after stage 8. */
    s32_board.program_at = data_byte_at;
    s32_board.irq_changed = program_irq_line;
    {
        char error[128];
        FILE *probe = fopen("/cd/assets/autoplay.txt", "r");
        if (probe) {
            fclose(probe);
            autoplay = s32_script_load("/cd/assets/autoplay.txt", error, sizeof(error));
        }
#ifdef ORUNNERS_CONSOLE
        test_actions_load();
#endif
    }
    s32_board.controls_update = controls_with_script;
    s32_board.log = log_callback;
    s32_board.frame_timing = true;
    dc_boot_stage = 3;
    dc_status("3/8 - resetting board");
    s32_init(NULL);

    static const s32_graphics graphics = {
        .tile_pens = NULL,
        .tile_count = 32768,
        .tile_at = dc_media_tile,
        .sprite_word = dc_media_sprite_word,
        .sprite_banks = 4
    };
    dc_boot_stage = 4;
    dc_status("4/8 - initializing renderer");
#ifdef DC_PVR
    dl_init(&graphics);
    dl_set_sprite_row_reader(dc_media_sprite_row);
    real_frame_shown = pvr_frame_shown;
    real_sprites_due = dl_sprites_due;
#else
    s32_video_init_graphics(&graphics);
    s32_video_show_screens(1);
    real_frame_shown = s32_board.frame_shown;
    real_sprites_due = s32_board.sprites_due;
#endif
    if (real_frame_shown) s32_board.frame_shown = timed_frame_shown;
    if (real_sprites_due) s32_board.sprites_due = timed_sprites_due;
    s32_workers_start(0);

    dc_media_probe_heap();
    s32_board.sound_at = assets_sound_data;
    s32_board.sound_byte_at = assets_sound_byte;
    s32_board.pcm_at = dc_media_pcm_byte;
    dc_boot_stage = 5;
    dc_status("5/8 - attaching sound chips");
    sound_chips_attach();
    {
        /* the MultiPCM on the AICA (aica_pcm.c); without it the game runs silent */
        const int aica = dc_aica_pcm_start("/cd/assets");
        if (aica == 0)
            sound_chips_pcm_observer = dc_aica_pcm_written;
        else
            fprintf(stderr, "AICA sample setup failed (%d): no sample sound\n", aica);
    }
    dc_boot_stage = 6;
    dc_status("6/8 - attaching sound CPU");
    sound_cpu_attach();
#ifdef ORUNNERS_CONSOLE
    options_to_save();    /* (the defaults, for a VMU without them) */
#endif
#ifndef DC_NO_VMU
    dc_vmu_load();
#endif
#ifdef ORUNNERS_CONSOLE
    if (dc_options_loaded) options_from_save();
    options_apply();
    console_attach();
#endif    /* (the records and settings a VMU has, before the game starts from them) */
    real_time_passes = s32_board.time_passes;
    if (real_time_passes) s32_board.time_passes = timed_time_passes;

    dc_boot_stage = 7;
    dc_status("7/8 - resetting game");
    program_reset();
    program_frame_done = frame_callback;

    dc_boot_stage = 8;
    dc_status("8/8 - starting the game");
    dc_loading_finish();    /* (the loading screen has had its time; the display is the game's from here) */
#ifdef DC_PVR
    if (dc_pvr_start() != 0) {
        dc_status("ERROR - PowerVR init failed");
        thd_sleep(10000);
        return 1;
    }
    dc_status_off();    /* (the PowerVR owns the frame buffers from here) */
    dc_diag_start_watchdog();
#endif
    program_run();
    dc_media_close();

    return io_failed ? 1 : 0;
}
