/* SPDX-License-Identifier: BSD-3-Clause */
#include "dc_port.h"
#include <s32/machine.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <string.h>
#ifdef ORUNNERS_CONSOLE
#include "console.h"
#include <math.h>

/* The console build (the PC version's menus): the pad's menu actions, the pause, and the wheel as
   OPTIONS > STEERING has it */
static uint32_t actions_held;
static bool pause_wanted;
static int pad_wheel = 128;    /* (the D-pad's wheel: turned while held, back to the centre let go) */

/* the menu actions held now: the D-pad or the stick, A accepts, B goes back, Start */
static uint32_t actions_of(const cont_state_t *pad)
{
	uint32_t held = 0;
	if ((pad->buttons & CONT_DPAD_UP) || pad->joyy < -64) held |= ACTION_BIT(ACTION_UP);
	if ((pad->buttons & CONT_DPAD_DOWN) || pad->joyy > 64) held |= ACTION_BIT(ACTION_DOWN);
	if ((pad->buttons & CONT_DPAD_LEFT) || pad->joyx < -64) held |= ACTION_BIT(ACTION_LEFT);
	if ((pad->buttons & CONT_DPAD_RIGHT) || pad->joyx > 64) held |= ACTION_BIT(ACTION_RIGHT);
	if (pad->buttons & CONT_A) held |= ACTION_BIT(ACTION_ACCEPT);
	if (pad->buttons & CONT_B) held |= ACTION_BIT(ACTION_BACK);
	if (pad->buttons & CONT_START) held |= ACTION_BIT(ACTION_START) | ACTION_BIT(ACTION_ACCEPT);
	return held;
}

uint32_t dc_menu_actions(void)
{
	maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
	cont_state_t *pad = dev ? maple_dev_status(dev) : NULL;
	const uint32_t held = pad ? actions_of(pad) : 0;
	const uint32_t pressed = held & ~actions_held;
	actions_held = held;
	return pressed;
}

bool dc_pause_wanted(void)
{
	const bool wanted = pause_wanted;
	pause_wanted = false;
	return wanted;
}

/* the stick's travel (-128..127) as the wheel's, past its dead zone (false inside it): softer about the
   centre under SENSITIVITY's middle, sharper over it (ports/sdl2/main.c: stick_wheel) */
static bool stick_wheel(int stick, int *position)
{
	static const float curves[STEERING_LEVELS] = { 2.2f, 1.9f, 1.6f, 1.3f, 1.0f, 0.85f, 0.72f, 0.6f, 0.5f };
	static int8_t table[STEERING_LEVELS + 1][128];    /* (by sensitivity, then travel past the dead zone in 127ths) */
	static bool made;
	const int travel = stick < 0 ? -stick : stick;
	const int dead = console_window_settings.dead_zone * 127 / 100;
	int level = console_window_settings.sensitivity;
	if (level < 1 || level > STEERING_LEVELS) level = STEERING_NORMAL;
	if (!made)
	{
		for (int l = 1; l <= STEERING_LEVELS; l++)
			for (int t = 0; t < 128; t++)
				table[l][t] = (int8_t)(powf((float)t / 127.0f, curves[l - 1]) * 127.0f + 0.5f);
		made = true;
	}
	if (travel <= dead || dead >= 127)
		return false;
	int past = (travel - dead) * 127 / (127 - dead);
	if (past > 127) past = 127;
	*position = 128 + (stick < 0 ? -table[level][past] : table[level][past]);
	return true;
}
#endif

static bool exit_flag = false;

void dc_controls_update(void)
{
    s32_inputs *input = &s32_board.input;
    memset(input, 0xff, sizeof(*input));
    memset(input->analog, 0, sizeof(input->analog));
    input->analog[0] = 128;
    input->analog[3] = 128;

    maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!dev) return;

    cont_state_t *pad = maple_dev_status(dev);
    if (!pad) return;

#ifdef ORUNNERS_CONSOLE
    {
        /* the menus' actions newly pressed; in a race Start is the pause (main.c), no cabinet's */
        const uint32_t held = actions_of(pad);
        const uint32_t pressed = held & ~actions_held;
        const bool racing = console_can_pause();
        const bool left = (pad->buttons & CONT_DPAD_LEFT) != 0, right = (pad->buttons & CONT_DPAD_RIGHT) != 0;
        int wheel;

        actions_held = held;
        if (racing && (pressed & ACTION_BIT(ACTION_START)))
            pause_wanted = true;
        else
            console_actions(pressed);
        /* the wheel: the stick past its dead zone; else the D-pad's, turned at STEERING's SPEED */
        {
            const int turn = 12 + 2 * (console_window_settings.steering_speed - STEERING_NORMAL);
            const int back = turn * 4 / 3;
            if (left != right)
                pad_wheel += right ? turn : -turn;
            else if (pad_wheel < 128)
                pad_wheel = pad_wheel + back > 128 ? 128 : pad_wheel + back;
            else
                pad_wheel = pad_wheel - back < 128 ? 128 : pad_wheel - back;
            pad_wheel = pad_wheel < 0 ? 0 : (pad_wheel > 255 ? 255 : pad_wheel);
        }
        wheel = pad_wheel;
        (void)stick_wheel(pad->joyx, &wheel);
        input->analog[0] = (uint8_t)wheel;
        input->analog[1] = (uint8_t)pad->rtrig;
        input->analog[2] = (uint8_t)pad->ltrig;
        if ((pad->buttons & CONT_START) && !racing) input->port_e[0] &= ~0x10;
        if (pad->buttons & CONT_A) input->port_a[0] &= ~0x01;
        if (pad->buttons & CONT_B) input->port_a[0] &= ~0x02;
        if (pad->buttons & CONT_Y) input->port_b[0] &= ~0x01;
        /* the music buttons beside the wheel: previous and next */
        if (pad->buttons & CONT_DPAD_UP) input->port_b[0] &= ~0x02;
        if (pad->buttons & CONT_DPAD_DOWN) input->port_b[0] &= ~0x04;
    }
#else
    /* wheel: the analogue stick, centred at 128, with a small dead zone (sticks rest a few counts off
       centre) and the rest of the travel stretched to full lock; accelerator and brake: the right
       and left triggers, 0-255, as the arcade pedals */
    {
        const int dead = 8;
        int x = pad->joyx;                       /* -128..127 */
        if (x > dead) x = (x - dead) * 127 / (127 - dead);
        else if (x < -dead) x = (x + dead) * 128 / (128 - dead);
        else x = 0;
        input->analog[0] = (uint8_t)(x + 128);
    }
    input->analog[1] = (uint8_t)pad->rtrig;
    input->analog[2] = (uint8_t)pad->ltrig;

    if (pad->buttons & CONT_DPAD_LEFT) input->analog[0] = 0;
    if (pad->buttons & CONT_DPAD_RIGHT) input->analog[0] = 255;

    if (pad->buttons & CONT_START) input->port_e[0] &= ~0x10;
    if (pad->buttons & CONT_X) input->port_e[0] &= ~0x04;
    if (pad->buttons & CONT_A) input->port_a[0] &= ~0x01;
    if (pad->buttons & CONT_B) input->port_a[0] &= ~0x02;
    if (pad->buttons & CONT_Y) input->port_b[0] &= ~0x01;
    /* the music buttons beside the wheel: previous and next */
    if (pad->buttons & CONT_DPAD_UP) input->port_b[0] &= ~0x02;
    if (pad->buttons & CONT_DPAD_DOWN) input->port_b[0] &= ~0x04;
#endif

#ifdef DC_TEST_OVERLAY    /* (test builds only: the pad's buttons are the game's) */
    /* Start + Y together: the stats overlay on or off (pvr_backend.c) */
    {
        extern volatile int dc_overlay_on;
        static bool combo_held;
#ifdef ORUNNERS_CONSOLE    /* (Start is the pause there: X + Y) */
        const bool combo = (pad->buttons & CONT_X) && (pad->buttons & CONT_Y);
#else
        const bool combo = (pad->buttons & CONT_START) && (pad->buttons & CONT_Y);
#endif
        if (combo && !combo_held) dc_overlay_on = !dc_overlay_on;
        combo_held = combo;
    }

    /* test switches, Y held: A the PowerVR safe mode, B paletted sprite textures (pvr_backend.c) */
    {
        extern volatile int dc_safe_mode;
        extern bool rt_pal_enabled;
        static uint32_t held;
        const uint32_t now = pad->buttons & (CONT_Y | CONT_A | CONT_B);
        const uint32_t pressed = now & ~held;
        held = now;
        if ((now & CONT_Y) && (pressed & CONT_A)) dc_safe_mode = !dc_safe_mode;
        if ((now & CONT_Y) && (pressed & CONT_B)) rt_pal_enabled = !rt_pal_enabled;
        {
            /* Y + D-pad left: depth compaction (pvr_backend.c) */
            extern volatile int dc_depth_compact;
            static bool left_held;
            const bool left = (pad->buttons & CONT_DPAD_LEFT) != 0;
            if ((now & CONT_Y) && left && !left_held) dc_depth_compact = !dc_depth_compact;
            left_held = left;
        }
    }
#endif

    if ((pad->buttons & CONT_A) && (pad->buttons & CONT_B) && (pad->buttons & CONT_X) && (pad->buttons & CONT_Y) && (pad->buttons & CONT_START))
        exit_flag = true;
}

bool dc_exit_requested(void)
{
    return exit_flag;
}


