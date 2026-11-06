/**
 *    Name:        wheel_setup.c
 *    Description: OPTIONS > STEERING's SET UP WHEEL: a racing wheel's axes and buttons, as the player shows
 *                 them, made a game controller layout for SDL, kept in gamecontrollerdb.txt
 *    Version:     0.1.0
 *    Created:     05-11-2026
 *    Modified:    05-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The game reads each player's controls as SDL's game controller (main.c): its left stick the wheel, its
 *    triggers the pedals, its buttons CONTROLS'. A device SDL has no layout for is no game controller, and the
 *    game never sees it: a Thrustmaster T300RS on Windows, which SDL knows for a wheel but not where its
 *    pedals are. The page asks for its controls one by one (console/menu.c); this watches every device's axes
 *    and buttons meanwhile:
 *    - the wheel: the axis moved furthest from where it was, of those resting near their middle (a pedal's
 *      rests at an end), turned left: SDL's left stick goes left to its negative end, the axis inverted where
 *      it goes the other way. Its device is the one set up.
 *    - a pedal: of that device's other axes, the one pressed furthest and let go again. Where it rests says
 *      how it goes: at an end, the whole axis is the trigger's travel (inverted where it rests at its positive
 *      end); near its middle, it is the wheel's combined pedals, the gas one half and the brake the other.
 *    - shift up, shift down and start: the first button pressed, of those not taken: the game controller's
 *      right and left shoulders and START, CONTROLS' own for them.
 *    The device's other buttons are the game controller's others in turn (A, B, X, Y...: CONTROLS binds them),
 *    its first hat the pad. The layout, SDL's line for the device, is added to SDL (SDL_GameControllerAddMapping:
 *    the device a game controller from then on) and to the file, in place of the device's line before; the
 *    file's other lines (a layout made elsewhere, SDL's community's) stay as they were.
 */
#include "wheel_setup.h"

#include "console.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEVICES          8
#define AXES             16       /* of a device, watched */
#define BUTTONS          32
#define TURNED           8192     /* the wheel turned: that far from where it was (of the axis' 65535) */
#define CENTRED          16384    /* an axis resting within this of its middle: a wheel's (a pedal's rests at an end) */
#define PRESSED          16384    /* a pedal pressed: that far from where it rests */
#define PEDALS_MIDDLE    8192     /* a pedal resting within this of its middle: the combined pedals' axis */
#define MAPPING_SIZE     1024

static SDL_Joystick *devices[DEVICES];
static char file_path[1024];

/* The page's step being watched, and what the steps before it took */
static struct
{
	int step;                       /* -1: none */
	int device;                     /* the device set up (the wheel turned): its slot of devices, or -1 */
	Sint16 rest[DEVICES][AXES];     /* each axis where the step found it */
	int pressed[AXES];              /* a pedal's step: the furthest each axis went from there, signed */
	Uint8 held[BUTTONS];            /* a button's step: the buttons held the frame before */
	int wheel_axis;
	bool wheel_inverted;
	int pedal_axis[2];              /* the gas', the brake's */
	int pedal_half[2];              /* 0: the whole axis; 1 or -1: its half that way (the combined pedals) */
	bool pedal_inverted[2];
	int buttons[3];                 /* shift up's, shift down's, start's */
} setup = { .step = -1, .device = -1 };

/* the game controller's buttons the device's others are, in turn: those CONTROLS binds first, the pad's
   where the device has no hat (GUIDE left out: SDL holds its presses back) */
static const SDL_GameControllerButton other_buttons[] = {
	SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
	SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
	SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_LEFTSTICK,
	SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_MISC1, SDL_CONTROLLER_BUTTON_PADDLE1,
	SDL_CONTROLLER_BUTTON_PADDLE2, SDL_CONTROLLER_BUTTON_PADDLE3, SDL_CONTROLLER_BUTTON_PADDLE4,
	SDL_CONTROLLER_BUTTON_TOUCHPAD,
};
static const SDL_GameControllerButton chosen_buttons[3] = { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
	SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_START };

static int axes_of(SDL_Joystick *joystick)
{
	const int count = SDL_JoystickNumAxes(joystick);

	return count < 0 ? 0 : (count > AXES ? AXES : count);
}

static int buttons_of(SDL_Joystick *joystick)
{
	const int count = SDL_JoystickNumButtons(joystick);

	return count < 0 ? 0 : (count > BUTTONS ? BUTTONS : count);
}

static bool is_pad(SDL_GameControllerButton button)
{
	return button >= SDL_CONTROLLER_BUTTON_DPAD_UP && button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
}

void wheel_setup_open(const char *path)
{
	SDL_strlcpy(file_path, path, sizeof(file_path));
}

void wheel_setup_device_added(int device_index)
{
	const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device_index);
	int free_slot = -1;

	for (int slot = 0; slot < DEVICES; slot++)
	{
		if (devices[slot] && SDL_JoystickInstanceID(devices[slot]) == id)
			return;    /* (open already) */
		if (!devices[slot] && free_slot < 0)
			free_slot = slot;
	}
	if (free_slot >= 0)
		devices[free_slot] = SDL_JoystickOpen(device_index);
}

void wheel_setup_device_removed(SDL_JoystickID which)
{
	for (int slot = 0; slot < DEVICES; slot++)
		if (devices[slot] && SDL_JoystickInstanceID(devices[slot]) == which)
		{
			if (slot == setup.device)
			{
				setup.device = -1;
				console_wheel_setup_cancel();    /* (the wheel being set up unplugged) */
			}
			SDL_JoystickClose(devices[slot]);
			devices[slot] = NULL;
		}
}

/* A step starts: every device's axes where they are, and the buttons held of the device set up */
static void start_step(int step)
{
	setup.step = step;
	if (step == WHEEL_SETUP_TURN)
		setup.device = -1;
	for (int slot = 0; slot < DEVICES; slot++)
		for (int axis = 0; devices[slot] && axis < axes_of(devices[slot]); axis++)
			setup.rest[slot][axis] = SDL_JoystickGetAxis(devices[slot], axis);
	memset(setup.pressed, 0, sizeof(setup.pressed));
	memset(setup.held, 0, sizeof(setup.held));
	if (setup.device >= 0)
		for (int button = 0; button < buttons_of(devices[setup.device]); button++)
			setup.held[button] = SDL_JoystickGetButton(devices[setup.device], button);
}

/* TURN: the axis turned furthest of any device's, of those resting near their middle */
static bool take_wheel(void)
{
	int best_slot = -1, best_axis = 0, best_moved = 0;

	for (int slot = 0; slot < DEVICES; slot++)
		for (int axis = 0; devices[slot] && axis < axes_of(devices[slot]); axis++)
		{
			const int rest = setup.rest[slot][axis];
			const int moved = SDL_JoystickGetAxis(devices[slot], axis) - rest;

			if (abs(rest) < CENTRED && abs(moved) > abs(best_moved))
			{
				best_slot = slot;
				best_axis = axis;
				best_moved = moved;
			}
		}
	if (best_slot < 0 || abs(best_moved) < TURNED)
		return false;
	setup.device = best_slot;
	setup.wheel_axis = best_axis;
	setup.wheel_inverted = best_moved > 0;    /* (turned left, it went up) */
	return true;
}

/* GAS, BRAKE ('pedal' 0, 1): of the device's axes but the wheel's (and the gas', but its other half where it
   is the combined pedals'), the one pressed furthest, once it is let go */
static bool take_pedal(int pedal)
{
	SDL_Joystick *device = devices[setup.device];
	const int axes = axes_of(device);
	int best = -1, value, rest;

	for (int axis = 0; axis < axes; axis++)
	{
		int moved;

		if (axis == setup.wheel_axis || (pedal == 1 && axis == setup.pedal_axis[0] && setup.pedal_half[0] == 0))
			continue;
		moved = SDL_JoystickGetAxis(device, axis) - setup.rest[setup.device][axis];
		if (pedal == 1 && axis == setup.pedal_axis[0] && (moved > 0 ? 1 : -1) == setup.pedal_half[0])
			moved = 0;    /* (the gas' half) */
		if (abs(moved) > abs(setup.pressed[axis]))
			setup.pressed[axis] = moved;
		if (best < 0 || abs(setup.pressed[axis]) > abs(setup.pressed[best]))
			best = axis;
	}
	if (best < 0 || abs(setup.pressed[best]) < PRESSED)
		return false;
	rest = setup.rest[setup.device][best];
	value = SDL_JoystickGetAxis(device, best);
	if (abs(value - rest) > abs(setup.pressed[best]) / 4)
		return false;    /* (still pressed) */
	setup.pedal_axis[pedal] = best;
	setup.pedal_half[pedal] = abs(rest) < PEDALS_MIDDLE ? (setup.pressed[best] > 0 ? 1 : -1) : 0;
	setup.pedal_inverted[pedal] = setup.pedal_half[pedal] == 0 && rest > 0;
	return true;
}

/* SHIFT UP, SHIFT DOWN, START ('which' 0-2): the device's first button pressed, of those not taken */
static bool take_button(int which)
{
	SDL_Joystick *device = devices[setup.device];

	for (int button = 0; button < buttons_of(device); button++)
	{
		const Uint8 down = SDL_JoystickGetButton(device, button);
		const bool fresh = down && !setup.held[button];
		bool taken = false;

		setup.held[button] = down;
		for (int other = 0; other < which; other++)
			taken = taken || setup.buttons[other] == button;
		if (fresh && !taken)
		{
			setup.buttons[which] = button;
			return true;
		}
	}
	return false;
}

static void append(char *mapping, const char *format, ...)
{
	const size_t used = strlen(mapping);
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(mapping + used, MAPPING_SIZE - used, format, arguments);
	va_end(arguments);
}

static void append_pedal(char *mapping, SDL_GameControllerAxis trigger, int pedal)
{
	const char *name = SDL_GameControllerGetStringForAxis(trigger);

	if (setup.pedal_half[pedal] != 0)
		append(mapping, "%s:%ca%d,", name, setup.pedal_half[pedal] > 0 ? '+' : '-', setup.pedal_axis[pedal]);
	else
		append(mapping, "%s:a%d%s,", name, setup.pedal_axis[pedal], setup.pedal_inverted[pedal] ? "~" : "");
}

/* The device's line: its GUID, its name (no commas), what the steps took, its other buttons and its hat */
static void make_mapping(SDL_Joystick *device, char *mapping, char *guid, size_t guid_size)
{
	const char *name = SDL_JoystickName(device);
	const bool hat = SDL_JoystickNumHats(device) > 0;
	size_t other = 0;

	SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(device), guid, (int)guid_size);
	snprintf(mapping, MAPPING_SIZE, "%s,", guid);
	for (const char *letter = name ? name : "Racing wheel"; *letter; letter++)
		append(mapping, "%c", *letter == ',' ? ' ' : *letter);
	append(mapping, ",%s:a%d%s,", SDL_GameControllerGetStringForAxis(SDL_CONTROLLER_AXIS_LEFTX), setup.wheel_axis,
		setup.wheel_inverted ? "~" : "");
	append_pedal(mapping, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
	append_pedal(mapping, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1);
	for (int which = 0; which < 3; which++)
		append(mapping, "%s:b%d,", SDL_GameControllerGetStringForButton(chosen_buttons[which]), setup.buttons[which]);
	for (int button = 0; button < buttons_of(device); button++)
	{
		if (button == setup.buttons[0] || button == setup.buttons[1] || button == setup.buttons[2])
			continue;
		while (other < SDL_arraysize(other_buttons) && hat && is_pad(other_buttons[other]))
			other++;
		if (other == SDL_arraysize(other_buttons))
			break;
		append(mapping, "%s:b%d,", SDL_GameControllerGetStringForButton(other_buttons[other++]), button);
	}
	if (hat)
		append(mapping, "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,");
	append(mapping, "platform:%s,", SDL_GetPlatform());
}

/* True for the file's line of the device (its GUID) on this system (its platform, or none) */
static bool same_device(const char *line, size_t length, const char *guid, const char *platform_field)
{
	const size_t guid_length = strlen(guid);
	char copy[MAPPING_SIZE * 2];

	if (length <= guid_length || SDL_strncasecmp(line, guid, guid_length) != 0 || line[guid_length] != ',')
		return false;
	if (length >= sizeof(copy))
		return true;
	memcpy(copy, line, length);
	copy[length] = '\0';
	return strstr(copy, platform_field) != NULL || strstr(copy, "platform:") == NULL;
}

/* The file with the device's line in place of the one it had: read whole, written back */
static void write_mapping(const char *mapping, const char *guid)
{
	FILE *file = fopen(file_path, "rb");
	char *text = NULL, platform_field[64];
	size_t length = 0;

	snprintf(platform_field, sizeof(platform_field), "platform:%s,", SDL_GetPlatform());
	if (file)
	{
		char chunk[4096];
		size_t got;

		while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0)
		{
			char *grown = realloc(text, length + got);

			if (!grown)
				break;
			text = grown;
			memcpy(text + length, chunk, got);
			length += got;
		}
		fclose(file);
	}
	if ((file = fopen(file_path, "wb")) == NULL)
	{
		fprintf(stderr, "%s cannot be written\n", file_path);
		free(text);
		return;
	}
	if (length == 0)
		fputs("# Game controller layouts in SDL's format, a line a device, read as OutRunners starts. OPTIONS >\n"
			"# STEERING > SET UP WHEEL writes its own here; a layout made elsewhere can be added too.\n", file);
	for (size_t start = 0; start < length;)
	{
		const char *end = memchr(text + start, '\n', length - start);
		const size_t line_length = end ? (size_t)(end - (text + start)) : length - start;

		if (!same_device(text + start, line_length, guid, platform_field))
		{
			fwrite(text + start, 1, line_length, file);
			fputc('\n', file);
		}
		start += line_length + 1;
	}
	fprintf(file, "%s\n", mapping);
	fclose(file);
	free(text);
}

/* The steps all taken: the device's layout made, added to SDL and kept */
static SDL_JoystickID finish(void)
{
	SDL_Joystick *device = devices[setup.device];
	char mapping[MAPPING_SIZE], guid[64];

	make_mapping(device, mapping, guid, sizeof(guid));
	fprintf(stderr, "SET UP WHEEL: %s\n", mapping);
	if (SDL_GameControllerAddMapping(mapping) < 0)
	{
		fprintf(stderr, "SET UP WHEEL: SDL takes no layout: %s\n", SDL_GetError());
		return -1;
	}
	write_mapping(mapping, guid);
	return SDL_JoystickInstanceID(device);
}

SDL_JoystickID wheel_setup_frame(void)
{
	const int step = console_wheel_setup_step();
	SDL_JoystickID done = -1;
	bool took;

	if (step < 0)
	{
		setup.step = -1;
		return -1;
	}
	if (step != setup.step)
	{
		start_step(step);
		return -1;
	}
	if (step != WHEEL_SETUP_TURN && (setup.device < 0 || !devices[setup.device]))
	{
		console_wheel_setup_cancel();
		return -1;
	}
	switch (step)
	{
		case WHEEL_SETUP_TURN:
			took = take_wheel();
			break;
		case WHEEL_SETUP_GAS:
		case WHEEL_SETUP_BRAKE:
			took = take_pedal(step - WHEEL_SETUP_GAS);
			break;
		default:
			took = take_button(step - WHEEL_SETUP_SHIFT_UP);
			break;
	}
	if (!took)
		return -1;
	if (step == WHEEL_SETUP_STEPS - 1)
		done = finish();
	console_wheel_setup_took();
	return done;
}
