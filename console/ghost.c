/**
 *    Name:        ghost.c
 *    Description: & KNUCKLES > TIME ATTACK's ghost: the fastest run kept, a goal each, and raced again
 *                 beside the player as a see-through car
 *    Version:     0.1.0
 *    Created:     29-10-2026
 *    Modified:    29-10-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The game draws each car of a race (race_cars_start: a race_car, its driver and passenger and their two
 *    gestures) from its player's race record copy (world place, segment, speed, animation, frame, sprays):
 *    in a race of two seats the other seat's car is drawn that way. A TIME ATTACK run keeps the car part of
 *    the left seat's record (0x30-0x6F) at each of its frames from the start (race_started), and the run that
 *    reaches a goal faster than the one kept for it is written to that goal's file (ghost_N.bin, in the
 *    game's folder). The next TIME ATTACK races the fastest of them again: one more race_car and its riders,
 *    from the record copy of a player no race on one cabinet has (player 2), which the ghost's frames fill in
 *    time with the race; it is not one of the race's players, so it neither collides, nor decides the route,
 *    nor has a place. Its objects run through a handler of the console's that runs theirs and marks the
 *    sprites they put in the list as a ghost (S32_SPRITE_GHOST: the video draws them half see-through). On a
 *    stage the ghost is not on (another route) it is not drawn.
 */
#include "console_internal.h"

#include "game.h"
#include "s32/video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GHOST_PLAYER        2u           /* the record copy it is drawn from: no player of one cabinet's */
#define RECORDED_FROM       0x30u        /* the race record's car part */
#define RECORDED_BYTES      0x40u
#define GHOST_FRAMES        18000        /* ten minutes at the screen's 30 frames a second */
#define GOALS               10
#define FIRST_GOAL          0x15u
#define GHOST_OBJECTS       5            /* the car, its driver and passenger, their two gestures */
#define MAGIC               0x4847524Fu  /* "ORGH" */
#define FAR_BEHIND          0x400u       /* segments behind the player: not drawn */
#define RACE_STAGE          0x62u        /* the record's stage (the one being built), within the recorded part */
#define RACE_SEGMENT        0x42u
#define RACE_FINISHED       0x5Eu

typedef struct
{
	uint32_t frames;
	uint32_t time;                       /* the run's time, ms (race race_time) */
	uint8_t model, car_number;
	uint8_t (*data)[RECORDED_BYTES];
} ghost_run;

static struct
{
	char folder[1024];
	bool loaded;
	ghost_run runs[GOALS];
	ghost_run recording;
	int raced;                           /* the run raced again (its goal), or -1 */
	uint32_t frame;                      /* the race's frame since its start */
	bool started;
	uint32_t objects[GHOST_OBJECTS];     /* the ghost's objects, and their own handlers */
	uint32_t handlers[GHOST_OBJECTS];
	int object_count;
} ghost = { .raced = -1 };

/* ---- the files ---- */

void console_ghost_folder(const char *folder)
{
	snprintf(ghost.folder, sizeof(ghost.folder), "%s", folder ? folder : "");
}

static void path_of(int goal, char *path, size_t size)
{
	snprintf(path, size, "%sghost_%d.bin", ghost.folder, goal);
}

static void put32(FILE *file, uint32_t value)
{
	const uint8_t bytes[4] = { (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24) };

	fwrite(bytes, 1, 4, file);
}

static bool get32(FILE *file, uint32_t *value)
{
	uint8_t bytes[4];

	if (fread(bytes, 1, 4, file) != 4)
		return false;
	*value = bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
	return true;
}

/* A goal's file: ORGH, its frames, its time, the model and car number, then the frames' recorded bytes */
static void load(int goal)
{
	ghost_run *run = &ghost.runs[goal];
	char path[1100];
	uint32_t magic, frames, time, cars;
	FILE *file;

	path_of(goal, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return;
	if (get32(file, &magic) && magic == MAGIC && get32(file, &frames) && frames <= GHOST_FRAMES && get32(file, &time)
		&& get32(file, &cars))
	{
		run->data = malloc((size_t)frames * RECORDED_BYTES);
		if (run->data && fread(run->data, RECORDED_BYTES, frames, file) == frames)
		{
			run->frames = frames;
			run->time = time;
			run->model = (uint8_t)cars;
			run->car_number = (uint8_t)(cars >> 8);
		}
		else
		{
			free(run->data);
			run->data = NULL;
		}
	}
	fclose(file);
}

static void save(int goal)
{
	const ghost_run *run = &ghost.runs[goal];
	char path[1100];
	FILE *file;

	path_of(goal, path, sizeof(path));
	file = fopen(path, "wb");
	if (!file)
		return;
	put32(file, MAGIC);
	put32(file, run->frames);
	put32(file, run->time);
	put32(file, run->model | (uint32_t)run->car_number << 8);
	fwrite(run->data, RECORDED_BYTES, run->frames, file);
	fclose(file);
}

static void load_all(void)
{
#ifdef ORUNNERS_NO_GHOST
	/* (a build without the memory for a run's recording: TIME ATTACK alone, no ghost) */
	return;
#endif
	if (ghost.loaded)
		return;
	ghost.loaded = true;
	for (int goal = 0; goal < GOALS; goal++)
		load(goal);
	if (!ghost.recording.data)
		ghost.recording.data = malloc((size_t)GHOST_FRAMES * RECORDED_BYTES);
}

/* ---- the race ---- */

/* The fastest run kept, or -1 */
static int fastest(void)
{
	int found = -1;

	for (int goal = 0; goal < GOALS; goal++)
		if (ghost.runs[goal].frames && (found < 0 || ghost.runs[goal].time < ghost.runs[found].time))
			found = goal;
	return found;
}

/* A TIME ATTACK game starts: the ghost to race again, its car's model and number into its record copy before
   the race's cars start (race_cars_start) */
void console_ghost_start(void)
{
	const uint32_t record = race_copy_offsets[GHOST_PLAYER] + GAME_VARIABLES;

	load_all();
	ghost.raced = fastest();
	ghost.frame = 0;
	ghost.started = false;
	ghost.object_count = 0;
	ghost.recording.frames = 0;
	if (ghost.raced >= 0)
	{
		RACE(record)->model = ghost.runs[ghost.raced].model;
		RACE(record)->car_number = ghost.runs[ghost.raced].car_number;
	}
}

/* On the left seat's frame during a TIME ATTACK game: the run kept frame by frame from the race's start, the
   ghost's record copy filled in from its run (far behind where it is not on the player's stage), and at the
   goal the run written if it was the goal's fastest */
void console_ghost_frame(void)
{
	const uint32_t own = screen->race_copy;
	const uint32_t record = race_copy_offsets[GHOST_PLAYER] + GAME_VARIABLES;

	if (!ghost.recording.data || screen->game_mode != GAME_MODE_PLAY)
		return;
	if (!ghost.started && screen->race_started != 0u)
		ghost.started = true;
	if (!ghost.started)
		;    /* (before the start: nothing kept yet; the ghost waits on its first frame, below) */
	else if (RACE(own)->finished == 0u && ghost.recording.frames < GHOST_FRAMES)
	{
		memcpy(ghost.recording.data[ghost.recording.frames], ram(own + RECORDED_FROM), RECORDED_BYTES);
		ghost.recording.frames++;
	}
	else if (RACE(own)->finished != 0u && ghost.recording.frames)
	{
		/* at the goal: the goal's fastest? */
		const int goal = screen->built_stage - (int)FIRST_GOAL;
		ghost_run *kept = goal >= 0 && goal < GOALS ? &ghost.runs[goal] : NULL;
		const uint32_t time = RACE(own)->race_time;

		if (kept && (!kept->frames || time < kept->time))
		{
			free(kept->data);
			kept->data = malloc((size_t)ghost.recording.frames * RECORDED_BYTES);
			if (kept->data)
			{
				memcpy(kept->data, ghost.recording.data, (size_t)ghost.recording.frames * RECORDED_BYTES);
				kept->frames = ghost.recording.frames;
				kept->time = time;
				kept->model = RACE(own)->model;
				kept->car_number = RACE(own)->car_number;
				save(goal);
			}
		}
		ghost.recording.frames = 0;    /* (kept once) */
	}

	if (ghost.raced >= 0)
	{
		const ghost_run *run = &ghost.runs[ghost.raced];
		const uint32_t at = ghost.frame < run->frames ? ghost.frame : run->frames - 1u;    /* (0 until the start) */

		const uint8_t stage = run->data[at][RACE_STAGE - RECORDED_FROM];
		bool on_route = stage == RACE(own)->stage;

		/* on the player's route: the stage the player is on, or one the player has taken (behind) */
		for (uint32_t taken = 0; taken <= player_car->forks_taken && taken < 6u && !on_route; taken++)
			on_route = player_car->stage_history[taken] == stage;
		memcpy(ram(record + RECORDED_FROM), run->data[at], RECORDED_BYTES);
		if (!on_route)
			RACE(record)->segment = (uint16_t)(RACE(own)->segment - FAR_BEHIND);    /* (another route: not drawn) */
		RACE(record)->finished = 0u;
	}
	if (ghost.started)
		ghost.frame++;
}

/* ---- its objects ---- */

static void keep_object(uint32_t object)
{
	if (ghost.object_count >= GHOST_OBJECTS)
		return;
	ghost.objects[ghost.object_count] = object;
	ghost.handlers[ghost.object_count] = OBJECT(object)->handler;
	OBJECT(object)->handler = CONSOLE_CODE_GHOST;
	ghost.object_count++;
}

/* The race's cars (the game's), then the ghost's: as race_cars_start starts a player's, from the ghost's record
   copy, each object through the console's handler */
void race_cars_start(void)
{
	const uint32_t record = race_copy_offsets[GHOST_PLAYER] + GAME_VARIABLES;
	uint32_t car, object, model, car_number, rider_places;

	original_race_cars_start();
	if (!console.time_attack || ghost.raced < 0 || screen->index != LEFT_SEAT)
		return;
	model = RACE(record)->model & 7u;
	car_number = RACE(record)->car_number & 7u;
	car = address_of(start_object());
	OBJECT(car)->handler = race_car_draw_ADDRESS;
	OBJECT(car)->race_car.player = GHOST_PLAYER;
	OBJECT(car)->race_car.record = record;
	OBJECT(car)->race_car.model = model;
	OBJECT(car)->race_car.first_body_piece = race_car_first_body_pieces[model];
	OBJECT(car)->race_car.first_body_animation = race_car_first_body_animations[model];
	OBJECT(car)->race_car.driver_animation = race_car_driver_animations[model];
	OBJECT(car)->race_car.driver_palette = race_car_driver_palettes[model];
	OBJECT(car)->race_car.driver_frames = race_car_driver_frame_counts[model];
	OBJECT(car)->race_car.driver_gesture_mask = race_car_driver_gesture_masks[model];
	OBJECT(car)->race_car.passenger_animation = race_car_passenger_animations[model];
	OBJECT(car)->race_car.passenger_palette = race_car_passenger_palettes[model];
	OBJECT(car)->race_car.passenger_frames = race_car_passenger_frame_counts[model];
	OBJECT(car)->race_car.passenger_gesture_mask = race_car_passenger_gesture_masks[model];
	rider_places = race_car_rider_places[model];
	OBJECT(car)->race_car.driver_places = rider_places;
	OBJECT(car)->race_car.passenger_places = rider_places + 4u;
	OBJECT(car)->race_car.spray_2_animations = race_car_spray_2_animations_ADDRESS;
	OBJECT(car)->race_car.spray_2_places = rider_places + 8u;
	OBJECT(car)->race_car.spray_2_effect = record + 0x52u;
	OBJECT(car)->race_car.spray_2_side = 2u;
	OBJECT(car)->race_car.spray_1_animations = race_car_spray_1_animations_ADDRESS;
	OBJECT(car)->race_car.spray_1_places = rider_places + 0xCu;
	OBJECT(car)->race_car.spray_1_effect = record + 0x51u;
	OBJECT(car)->race_car.spray_1_side = 1u;
	OBJECT(car)->race_car.tag_animation = race_car_tag_animations[car_number];
	OBJECT(car)->race_car.tag_palette = race_car_tag_palettes[model];
	OBJECT(car)->race_car.blink_counter = race_car_blink_starts[car_number];
	OBJECT(car)->race_car.rival = car_number - RACE(screen->race_copy)->car_number;
	keep_object(car);
	object = address_of(start_object());
	OBJECT(object)->handler = race_car_driver_handlers[model];
	OBJECT(object)->race_car_occupant.car = car;
	OBJECT(object)->race_car_occupant.occupant = (uint8_t)model;
	OBJECT(object)->race_car_occupant.body_back = 0xFFu;
	keep_object(object);
	object = address_of(start_object());
	OBJECT(object)->handler = race_car_passenger_handlers[model];
	OBJECT(object)->race_car_occupant.car = car;
	OBJECT(object)->race_car_occupant.occupant = (uint8_t)(model + 8u);
	OBJECT(object)->race_car_occupant.body_back = 0u;
	keep_object(object);
	for (uint32_t rider = model; rider <= model + 8u; rider += 8u)
	{
		object = address_of(start_object());
		OBJECT(object)->handler = rider_gesture_update_ADDRESS;
		OBJECT(object)->rider_gesture.car = car;
		OBJECT(object)->rider_gesture.rider = (uint8_t)rider;
		OBJECT(object)->rider_gesture.model = (uint8_t)model;
		OBJECT(object)->rider_gesture.player = GHOST_PLAYER;
		keep_object(object);
	}
}

/* One of the ghost's objects: its own handler, then the sprites it put in the list marked as a ghost */
void ghost_object_update(void)
{
	const uint32_t first = screen->sprite_list_last + 0x10u;

	for (int index = 0; index < ghost.object_count; index++)
		if (ghost.objects[index] == address_of(current_object))
		{
			current_object->handler = ghost.handlers[index];
			call_code(ghost.handlers[index]);
			if ((current_object->flags & 1u) != 0u)
			{
				ghost.handlers[index] = current_object->handler;    /* (a handler it took on) */
				current_object->handler = CONSOLE_CODE_GHOST;
			}
			break;
		}
	for (uint32_t sprite = first; sprite <= screen->sprite_list_last; sprite += 0x10u)
		if ((SPRITE_WORDS(sprite)[0] >> 14) == 0u)    /* (a sprite, not the group's jump) */
		{
			SPRITE_WORDS(sprite)[7] |= S32_SPRITE_GHOST;
			SPRITE_WORDS(sprite + 0x3000u)[7] |= S32_SPRITE_GHOST;
		}
}
