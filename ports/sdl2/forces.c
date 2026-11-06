/**
 *    Name:        forces.c
 *    Description: the forces the controllers give back: a racing wheel's centring spring, and the cabinet's
 *                 wheel motor as a wheel's shake or a game controller's rumble
 *    Version:     0.1.0
 *    Created:     06-11-2026
 *    Modified:    06-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The cabinet's wheel came back to the centre on a spring of its own: the game never pushes it there. The
 *    one force the board drives is the wheel's motor, on or off (output port D bit 4 of each seat's I/O chip,
 *    while the test mode's STEERING WHEEL REACTION is on), which shakes the wheel in a crash and off the road
 *    (src/race/player_car.c, player_car_off_road_shake). A racing wheel with force feedback (a Thrustmaster
 *    T300RS, a Logitech G29) gives no force the game does not ask for, so with nothing asked it turns loose:
 *    this asks it for the cabinet's spring, and for the motor's shake while the motor runs.
 *    - the spring: SDL's spring effect (SDL_HAPTIC_SPRING) at 'centring' percent of the wheel's strongest,
 *      round the wheel's centre, for as long as the game runs; where the wheel has none, its autocentre
 *      (SDL_HAPTIC_AUTOCENTER) at that strength.
 *    - the shake: a sine (SDL_HAPTIC_SINE, SHAKE_PERIOD) at 'shake' percent of the wheel's strongest while the
 *      motor runs. A device with no sine (a game controller: an Xbox, a DualSense...) rumbles instead.
 *    A device is looked at once, the first frame it is a player's game controller with a force wanted: its
 *    haptic is kept where it has one of those effects, and closed where it has none (a game controller's,
 *    whose rumble is SDL's game controller rumble). SDL opening a haptic turns the device's own autocentre off,
 *    so with CENTRING at 0 and VIBRATION at 0 the haptic is not opened at all and the wheel is as its driver
 *    leaves it. The effects go along the wheel's one axis (SDL_HAPTIC_CARTESIAN, 1: DirectInput takes no polar
 *    direction for a device of one axis).
 */
#include "forces.h"

#include <stdio.h>

#define FORCES_PLAYERS 2
#define SHAKE_PERIOD   40     /* ms: the shake's, 25 a second */
#define RUMBLE_SPAN    100    /* ms: a rumble asked for each frame while the motor runs, lasting past the next */

typedef struct
{
	SDL_JoystickID pad;      /* the player's game controller looked at, or -1 */
	bool looked;             /* its haptic opened (or found to have nothing) */
	SDL_Haptic *haptic;      /* its haptic, where it has a spring, an autocentre or a sine */
	unsigned int kinds;      /* the haptic's effects (SDL_HAPTIC_*) */
	int spring;              /* the spring effect, or -1 */
	int centring;            /* the spring's strength (percent), as asked of the haptic */
	int sine;                /* the shake's effect, or -1 */
	int shake;               /* its strength (percent), as asked of the haptic */
	bool shaking;            /* the shake running */
	bool rumbling;           /* a rumble asked for, not yet stopped */
} player_forces;

static player_forces players[FORCES_PLAYERS];
static bool haptics;    /* SDL's haptic part started */

static void forget(player_forces *forces)
{
	forces->pad = -1;
	forces->looked = false;
	forces->haptic = NULL;
	forces->kinds = 0;
	forces->spring = -1;
	forces->centring = 0;
	forces->sine = -1;
	forces->shake = 0;
	forces->shaking = false;
	forces->rumbling = false;
}

void forces_start(void)
{
	for (int player = 0; player < FORCES_PLAYERS; player++)
		forget(&players[player]);
	haptics = SDL_InitSubSystem(SDL_INIT_HAPTIC) == 0;
}

/* The effects' direction: along the wheel's axis */
static void wheel_direction(SDL_HapticDirection *direction)
{
	direction->type = SDL_HAPTIC_CARTESIAN;
	direction->dir[0] = 1;
}

/* The spring round the wheel's centre at 'centring' percent: as hard each way, with no dead band */
static void make_spring(SDL_HapticEffect *effect, int centring)
{
	const Sint16 coefficient = (Sint16)(0x7fff * centring / 100);

	SDL_zerop(effect);
	effect->type = SDL_HAPTIC_SPRING;
	wheel_direction(&effect->condition.direction);
	effect->condition.length = SDL_HAPTIC_INFINITY;
	for (int axis = 0; axis < 3; axis++)
	{
		effect->condition.right_sat[axis] = 0xffff;
		effect->condition.left_sat[axis] = 0xffff;
		effect->condition.right_coeff[axis] = coefficient;
		effect->condition.left_coeff[axis] = coefficient;
	}
}

/* The motor's shake at 'shake' percent */
static void make_sine(SDL_HapticEffect *effect, int shake)
{
	SDL_zerop(effect);
	effect->type = SDL_HAPTIC_SINE;
	wheel_direction(&effect->periodic.direction);
	effect->periodic.length = SDL_HAPTIC_INFINITY;
	effect->periodic.period = SHAKE_PERIOD;
	effect->periodic.magnitude = (Sint16)(0x7fff * shake / 100);
}

/* The device's haptic, kept where it has an effect of ours */
static void look(player_forces *forces, SDL_Joystick *joystick)
{
	SDL_Haptic *haptic;
	unsigned int kinds;

	forces->looked = true;
	if (!haptics || SDL_JoystickIsHaptic(joystick) != 1)
		return;
	haptic = SDL_HapticOpenFromJoystick(joystick);
	if (!haptic)
	{
		fprintf(stderr, "forces: %s: %s\n", SDL_JoystickName(joystick), SDL_GetError());
		return;
	}
	kinds = SDL_HapticQuery(haptic);
	if (!(kinds & (SDL_HAPTIC_SPRING | SDL_HAPTIC_AUTOCENTER | SDL_HAPTIC_SINE)))
	{
		SDL_HapticClose(haptic);
		return;
	}
	forces->haptic = haptic;
	forces->kinds = kinds;
	fprintf(stderr, "forces: %s:%s%s%s\n", SDL_JoystickName(joystick),
		(kinds & SDL_HAPTIC_SPRING) ? " spring" : ((kinds & SDL_HAPTIC_AUTOCENTER) ? " autocentre" : ""),
		(kinds & SDL_HAPTIC_SINE) ? " sine" : "", (kinds & SDL_HAPTIC_GAIN) ? " gain" : "");
}

/* The spring at 'centring' percent (0: none), where it changed */
static void centre(player_forces *forces, int centring)
{
	SDL_HapticEffect effect;

	if (centring == forces->centring)
		return;
	forces->centring = centring;
	if (forces->kinds & SDL_HAPTIC_SPRING)
	{
		if (centring == 0)
		{
			if (forces->spring >= 0)
				SDL_HapticDestroyEffect(forces->haptic, forces->spring);
			forces->spring = -1;
			return;
		}
		make_spring(&effect, centring);
		if (forces->spring >= 0)
		{
			SDL_HapticUpdateEffect(forces->haptic, forces->spring, &effect);
			return;
		}
		forces->spring = SDL_HapticNewEffect(forces->haptic, &effect);
		if (forces->spring < 0 || SDL_HapticRunEffect(forces->haptic, forces->spring, 1) != 0)
			fprintf(stderr, "forces: the spring: %s\n", SDL_GetError());
	}
	else if (forces->kinds & SDL_HAPTIC_AUTOCENTER)
		SDL_HapticSetAutocenter(forces->haptic, centring);
}

/* The shake at 'shake' percent while 'on', stopped otherwise */
static void shake_wheel(player_forces *forces, int shake, bool on)
{
	SDL_HapticEffect effect;

	if (!on)
	{
		if (forces->shaking)
			SDL_HapticStopEffect(forces->haptic, forces->sine);
		forces->shaking = false;
		return;
	}
	if (forces->sine < 0 || shake != forces->shake)
	{
		make_sine(&effect, shake);
		if (forces->sine < 0)
			forces->sine = SDL_HapticNewEffect(forces->haptic, &effect);
		else
			SDL_HapticUpdateEffect(forces->haptic, forces->sine, &effect);
		forces->shake = shake;
		if (forces->sine < 0)
			fprintf(stderr, "forces: the shake: %s\n", SDL_GetError());
	}
	if (forces->sine >= 0 && !forces->shaking)
		forces->shaking = SDL_HapticRunEffect(forces->haptic, forces->sine, 1) == 0;
}

/* A game controller's rumble at 'shake' percent while 'on' (each frame, to last past the next), stopped
   otherwise */
static void rumble(player_forces *forces, SDL_GameController *pad, int shake, bool on)
{
	const Uint16 low = (Uint16)(0xffff * shake / 100);

	if (on)
		forces->rumbling = SDL_GameControllerRumble(pad, low, low / 2, RUMBLE_SPAN) == 0;
	else if (forces->rumbling)
	{
		SDL_GameControllerRumble(pad, 0, 0, 0);
		forces->rumbling = false;
	}
}

/* The player's haptic closed (its effects with it), their rumble stopped */
static void let_go(player_forces *forces, SDL_GameController *pad)
{
	if (forces->haptic)
		SDL_HapticClose(forces->haptic);
	if (forces->rumbling && pad)
		SDL_GameControllerRumble(pad, 0, 0, 0);
	forget(forces);
}

void forces_frame(int player, SDL_GameController *pad, int centring, int shake, bool motor)
{
	player_forces *forces;
	SDL_Joystick *joystick = pad ? SDL_GameControllerGetJoystick(pad) : NULL;
	const SDL_JoystickID id = joystick ? SDL_JoystickInstanceID(joystick) : -1;

	if (player < 0 || player >= FORCES_PLAYERS)
		return;
	forces = &players[player];
	if (id != forces->pad)
	{
		/* (another controller: the one before closed already, forces_release) */
		let_go(forces, NULL);
		forces->pad = id;
	}
	if (id < 0)
		return;
	if (centring == 0 && shake == 0)
	{
		/* (nothing wanted: the device as its driver has it) */
		if (forces->looked)
		{
			let_go(forces, pad);
			forces->pad = id;
		}
		return;
	}
	if (!forces->looked)
		look(forces, joystick);
	if (forces->haptic)
		centre(forces, centring);
	if (forces->haptic && (forces->kinds & SDL_HAPTIC_SINE))
		shake_wheel(forces, shake, motor && shake > 0);
	else
		rumble(forces, pad, shake, motor && shake > 0);
}

void forces_hold(void)
{
	for (int player = 0; player < FORCES_PLAYERS; player++)
	{
		player_forces *forces = &players[player];

		if (forces->shaking)
			shake_wheel(forces, forces->shake, false);
		forces->rumbling = false;    /* (a rumble ends by itself, RUMBLE_SPAN after the last frame's) */
	}
}

void forces_release(SDL_GameController *pad)
{
	SDL_Joystick *joystick = pad ? SDL_GameControllerGetJoystick(pad) : NULL;
	const SDL_JoystickID id = joystick ? SDL_JoystickInstanceID(joystick) : -1;

	for (int player = 0; player < FORCES_PLAYERS; player++)
		if (id >= 0 && players[player].pad == id)
			let_go(&players[player], pad);
}

void forces_stop(void)
{
	for (int player = 0; player < FORCES_PLAYERS; player++)
		let_go(&players[player], NULL);
}
