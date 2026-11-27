/**
 *    Name:        side_map.c
 *    Description: the race's route map beside it (a tall screen's room under the picture): the right seat,
 *                 unseen with one player, shows the game's close-up route map, its car as far along the
 *                 route as the left seat's car is
 *    Version:     0.1.1
 *    Created:     30-10-2026
 *    Modified:    24-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The scene is the console's own game mode on the right seat's screen (console.c), while the left seat
 *    races alone on the game's route (ARCADE, TIME ATTACK): the close-up map route_map_update drives the
 *    player's car over after a race (its states 3-14), with its tilemap, palette, sea and landmarks (the
 *    game's route_map_landmark objects), the car and the C.P signs the console's objects in the game's
 *    pictures. The map is the one of the route's first branch (its background, start and landmarks are by
 *    it): the east's until the first checkpoint decides it. The drive is route_map_update's, leg by leg:
 *    up from the start, then each level's stage across the map and the branch road after it up or down by
 *    the fork's branch, as far as the left car is into its stage (its course, then its branch road) and
 *    at most a step a frame, as the game's map drives; the car waits at a checkpoint until the fork is
 *    decided. The port draws the screen in the room, the part around the car (console_side_map_shown,
 *    console_side_map_car).
 */
#include "console.h"
#include "console_internal.h"

#include "game.h"

#define SCREEN_CAR          0x1000u    /* the screen's car, beside its block */
#define STEP                256        /* a step of the drive (a frame of route_map_update's), in fractions */
#define LEGS                11         /* each level's stage, then the branch road after it (the goals' none) */
#define LEVELS              6          /* the pre-stage's to the goals' */
#define MAP_X               0xA0       /* the route map object's place at the start: the landmarks' and the */
#define MAP_Y               0x70       /* signs' origin */
#define CAR_X               0x8C       /* the car's place at the start (route_map_car_update) */
#define CAR_Y               0xC3
#define START_SCROLL_Y      0x8C
#define WEST_EDGE           0x10       /* the map's scroll a stage's leg stops it at: the car goes on */
#define EAST_EDGE           0x2A0
#define FRAME_UP            0xCu       /* the car's frame by its way */
#define FRAME_DOWN          6u
#define FRAME_EAST          9u
#define FRAME_WEST          4u
#define CAR_SIZE            0x50u
#define SIGN_SIZE           0x100u
#define SIGN_DEPTH          0x195u
#define SIGN_ABOVE          8          /* a C.P sign over the car's place */
#define LANDMARKS           0x20u
#define SEA_COLOUR          0x7EE0u

/* route_map_update's legs: their kind, steps and pixels a step */
enum
{
	LEG_START,       /* the car up the screen, the map still */
	LEG_SIDEWAYS,    /* the map under the car, east or west by the first branch */
	LEG_BRANCH,      /* up or down by the fork's branch, and a pixel across */
	LEG_STAGE        /* sideways, the map stopping at its edges */
};

static const struct
{
	uint8_t kind, steps, pixels;
} legs[LEGS] = {
	{ LEG_START, 22, 4 }, { LEG_SIDEWAYS, 6, 8 }, { LEG_SIDEWAYS, 11, 5 }, { LEG_BRANCH, 10, 4 },
	{ LEG_STAGE, 15, 7 }, { LEG_BRANCH, 10, 4 }, { LEG_STAGE, 15, 7 }, { LEG_BRANCH, 10, 4 },
	{ LEG_STAGE, 15, 7 }, { LEG_BRANCH, 10, 4 }, { LEG_STAGE, 15, 7 }
};

/* Where the drive is */
typedef struct
{
	int scroll_x, scroll_y;    /* the map's */
	int map_x, map_y;          /* the route map object's place, moved the other way */
	int car_x, car_y;          /* the car's on the screen */
	uint8_t frame;             /* the car's, by its way */
} place;

static struct
{
	bool running;
	int first_branch;          /* the map's (0 east) */
	int progress;              /* the drive's, in STEP fractions of its steps */
	place at;
	int checkpoints;           /* the C.P signs passed, and their places of the map */
	int checkpoint_x[LEVELS], checkpoint_y[LEVELS];
	int signs;                 /* the C.P signs started */
	uint32_t map;              /* the route map object */
} side;

static const struct route_map_registers *map_registers(void)
{
	return &route_map_register_sets[screen->index];
}

/* ---- the drive ---- */

static int scaled(int pixels, int part)
{
	return (pixels * part + STEP / 2) / STEP;
}

/* One step of leg 'leg' from 'at', its part 'part' (STEP: whole) */
static void step(place *at, int leg, int part)
{
	const bool west = side.first_branch != 0;
	const int pixels = scaled(legs[leg].pixels, part);

	switch (legs[leg].kind)
	{
		case LEG_START:
			at->car_x += scaled(1, part);
			at->car_y -= pixels;
			at->frame = FRAME_UP;
			return;
		case LEG_BRANCH:
		{
			/* (the fork's branch, the leg's level's: down where it is the first branch's) */
			const bool down = (SCREEN(SCREEN_1)->progress.route_branches[leg / 2] != 0u) == west;
			const int across = scaled(1, part);

			at->scroll_y += down ? pixels : -pixels;
			at->map_y -= down ? pixels : -pixels;
			at->scroll_x += down ? -across : across;
			at->map_x -= down ? -across : across;
			at->frame = down ? FRAME_DOWN : FRAME_UP;
			return;
		}
		default:
			at->frame = west ? FRAME_WEST : FRAME_EAST;
			if (legs[leg].kind == LEG_STAGE && (west ? at->scroll_x <= WEST_EDGE : at->scroll_x >= EAST_EDGE))
			{
				at->car_x += west ? -pixels : pixels;    /* (the map at its edge) */
				return;
			}
			at->scroll_x += west ? -pixels : pixels;
			at->map_x -= west ? -pixels : pixels;
			return;
	}
}

/* The drive after 'progress', step by step from the start: its place and the checkpoints passed */
static void drive_to(int progress)
{
	place at = { route_map_start_scroll_xs[side.first_branch], START_SCROLL_Y, MAP_X, MAP_Y, CAR_X, CAR_Y,
		FRAME_UP };

	side.checkpoints = 0;
	for (int leg = 0; leg < LEGS && progress > 0; leg++)
	{
		int done = 0;

		for (; done < legs[leg].steps && progress > 0; done++, progress -= STEP)
			step(&at, leg, progress < STEP ? progress : STEP);
		if (done == legs[leg].steps && progress >= 0 && (leg & 1) == 0 && leg != LEGS - 1)
		{
			/* (a level's stage over: its checkpoint) */
			side.checkpoint_x[side.checkpoints] = at.map_x;
			side.checkpoint_y[side.checkpoints] = at.map_y;
			side.checkpoints++;
		}
	}
	side.at = at;
}

/* How far the left car is along the drive, in STEP fractions: its stage's level, and in it its course
   (the stage's length) or the branch road after it, as route_map_update has them at a race's end */
static int race_progress(void)
{
	const uint32_t car = SCREEN_1 + SCREEN_CAR;
	const int level = SCREEN(SCREEN_1)->current_stage_level;
	const int course = SCREEN(SCREEN_1)->stage_lengths[SCREEN(SCREEN_1)->current_stage];
	const int road = SCREEN(SCREEN_1)->branch_road_length;
	int into = (int)CAR(car)->segment - (level == 0 ? 0 : (int)CAR(car)->reached_stage_start);
	int leg = 2 * level, part, before = 0;

	if (level >= LEVELS)
	{
		leg = LEGS - 1;    /* (the goal) */
		into = course;
	}
	if (into < 0)
		into = 0;
	if (into <= course || leg == LEGS - 1)
		part = course > 0 ? into * STEP / course : 0;
	else
	{
		leg++;
		part = road > 0 ? (into - course) * STEP / road : STEP;
		if (SCREEN(SCREEN_1)->progress.forks_decided <= level)
			part = 0;    /* (the fork not passed: the branch unknown) */
	}
	part = part > STEP ? STEP : part;
	for (int earlier = 0; earlier < leg; earlier++)
		before += legs[earlier].steps;
	return before * STEP + part * legs[leg].steps;
}

/* ---- the map ---- */

/* The close-up map as route_map_update's state 3 sets it up, the first branch's, its car and landmarks */
static void map_set_up(void)
{
	const uint32_t index = screen->index;
	const struct route_map_registers *registers = map_registers();
	uint32_t object;

	clear_objects();
	reset_tile_layers();
	clear_sprite_group_count();
	screen->progress.route_branches[0] = (uint8_t)side.first_branch;    /* (the landmarks' set) */
	screen->route_map_signals = 0u;
	set_up_background(rom_pointer(0x4868u + (index * 10u + (uint32_t)side.first_branch * 5u) * 4u));
	queue_palette(route_map_background_palette);
	io_write16(route_map_sea_colour_addresses[index], SEA_COLOUR);
	console_page_select(registers->pages_address, route_map_close_up_pages[index]);
	console_page_select(registers->second_pages_address, 0u);
	VIDEO_REGISTER_WORDS(registers->zoom_x_address)[0] = 0x200u;
	VIDEO_REGISTER_WORDS(registers->zoom_y_address)[0] = 0x200u;
	VIDEO_REGISTER_WORDS(registers->zoom_center_x_address)[0] = 0u;
	VIDEO_REGISTER_WORDS(registers->zoom_center_y_address)[0] = 0u;
	screen->skip_scroll_update = 0xFFu;
	side.map = address_of(start_object());
	OBJECT(side.map)->handler = CONSOLE_CODE_SIDE_MAP;
	object = address_of(start_object());
	OBJECT(object)->handler = CONSOLE_CODE_SIDE_MAP_CAR;
	OBJECT(object)->route_map_car.model = (uint8_t)CAR(SCREEN_1 + SCREEN_CAR)->model;
	OBJECT(object)->route_map_car.route_map = side.map;
	for (uint32_t landmark = 0u; landmark < LANDMARKS; landmark++)
	{
		object = address_of(start_object());
		OBJECT(object)->handler = FUNCTION_route_map_landmark_update;
		OBJECT(object)->route_map_landmark.index_then_half_rate = (uint8_t)landmark;
		OBJECT(object)->route_map_landmark.route_map = side.map;
	}
	side.signs = 0;
	side.at.scroll_x = -1;    /* (the scroll to be set) */
}

/* The map where the drive is: its scroll, the route map object's place, a C.P sign at each checkpoint
   passed */
static void map_show(void)
{
	const struct route_map_registers *registers = map_registers();
	const int scroll_x = side.at.scroll_x, scroll_y = side.at.scroll_y;

	drive_to(side.progress);
	if (side.at.scroll_x != scroll_x || side.at.scroll_y != scroll_y)
	{
		VIDEO_REGISTER_WORDS(registers->scroll_x_address)[0] = (uint16_t)side.at.scroll_x;
		VIDEO_REGISTER_WORDS(registers->register_scroll_x_address)[0] = (uint16_t)side.at.scroll_x;
		VIDEO_REGISTER_WORDS(registers->scroll_y_address)[0] = (uint16_t)side.at.scroll_y;
		VIDEO_REGISTER_WORDS(registers->register_scroll_y_address)[0] = (uint16_t)side.at.scroll_y;
		world->send_video_registers = game->send_video_registers = 0xFFu;
	}
	OBJECT(side.map)->x = (uint16_t)side.at.map_x;
	OBJECT(side.map)->y = (uint16_t)side.at.map_y;
	for (; side.signs < side.checkpoints; side.signs++)
	{
		const uint32_t sign = address_of(start_object());

		OBJECT(sign)->handler = CONSOLE_CODE_SIDE_MAP_SIGN;
		OBJECT(sign)->route_map_sign.route_map = side.map;
		OBJECT(sign)->route_map_sign.x_offset = (int16_t)(MAP_X - side.checkpoint_x[side.signs]);
		OBJECT(sign)->route_map_sign.y_offset = (int16_t)(MAP_Y - side.checkpoint_y[side.signs] - SIGN_ABOVE);
	}
}

/* ---- the objects (console_code) ---- */

void side_map_update(void)
{
	/* (the route map object: its place is the scene's) */
}

void side_map_car_update(void)
{
	if (current_object->state == 0u)
	{
		const uint32_t model = current_object->route_map_car.model & 7u;

		current_object->draw_mode = 0xAu;
		animation_start(route_map_car_animations[model]);
		current_object->palette = route_map_car_palettes[model];
		current_object->width_scale = CAR_SIZE;
		current_object->height_scale = CAR_SIZE;
		current_object->state++;
	}
	current_object->x = (uint16_t)side.at.car_x;
	current_object->y = (uint16_t)side.at.car_y;
	current_object->frame = side.at.frame;
	map_depth_from_y();
	draw_object(0u);
}

/* A C.P sign (route_map_sign_update's kind 0), its place of the map set when it starts */
void side_map_sign_update(void)
{
	const uint32_t map = current_object->route_map_sign.route_map;

	if (current_object->state == 0u)
	{
		current_object->draw_mode = 0xAu;
		animation_start(route_map_sign_animations[0]);
		current_object->palette = route_map_sign_palettes[0];
		current_object->width_scale = SIGN_SIZE;
		current_object->height_scale = SIGN_SIZE;
		current_object->depth_layer = SIGN_DEPTH;
		current_object->state++;
	}
	current_object->x = (uint16_t)(OBJECT(map)->x + current_object->route_map_sign.x_offset);
	current_object->y = (uint16_t)(OBJECT(map)->y + current_object->route_map_sign.y_offset);
	animation_advance(0u);
	draw_object(0u);
}

/* ---- the scene ---- */

bool console_side_map_running(void)
{
	return side.running;
}

void console_side_map_car(int *x, int *y)
{
	*x = side.at.car_x;
	*y = side.at.car_y;
}

int console_side_map_frame(bool due)
{
	const int first_branch = SCREEN(SCREEN_1)->progress.forks_decided > 0u ? SCREEN(SCREEN_1)->progress.route_branches[0] != 0u : 0;

	if (!due)
	{
		if (!side.running)
			return SIDE_MAP_OFF;
		side.running = false;
		clear_objects();
		return SIDE_MAP_OVER;
	}
	if (!side.running || first_branch != side.first_branch)
	{
		if (!side.running)
		{
			side.progress = 0;
			screen->fade_kind = 1u;    /* (from black) */
			screen->flags |= 1u;
		}
		side.running = true;
		side.first_branch = first_branch;
		map_set_up();
	}
	{
		/* at most a step a frame after the car */
		const int target = race_progress();

		if (target > side.progress)
			side.progress += target - side.progress < STEP ? target - side.progress : STEP;
	}
	map_show();
	return SIDE_MAP_ON;
}
