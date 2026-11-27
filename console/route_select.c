/**
 *    Name:        route_select.c
 *    Description: & KNUCKLES > REVERSE's goal, picked on the route map: the map of the race's end, whole, its
 *                 ten goals, a car on the one picked
 *    Version:     0.1.2
 *    Created:     29-10-2026
 *    Modified:    24-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The scene is the console's own game mode on the left seat's screen (console.c), as the SEGA
 *    presentation is: its objects are the game's, with handlers of the console's. The map is the one the
 *    race's end shows whole (route_map_update's last view): the routes over the world at 1:1, wider than the
 *    screen: the pre-stage's globe in the middle, the routes east of it to five goals and west of it to the
 *    other five, each half where route_map_whole_scroll_xs puts the screen for it; the WEST / EAST panel
 *    under the globe (route_map_direction_sign_show), moved with the map. It comes once the car select is
 *    over, so the car is the one chosen, as the route map shows it after a race (route_map_car_animations
 *    by its model): it drives from goal to goal as the player picks them, up and down through a side's,
 *    left and right across to the other side's, and the map scrolls after it. START or ACCEPT starts
 *    REVERSE there, BACK gives the game up; either fades the map out first.
 */
#include "console.h"
#include "console_internal.h"
#include "console_draw.h"

#include "game.h"

#define GOALS               10
#define SIDE_GOALS          5
#define EAST                0          /* the pre-stage's branch 0 (route_map_whole_scroll_xs' first) */
#define WEST                1
#define CAR_SIZE            0x50u      /* as the route map's */
#define CAR_FRAME           0xCu
#define CAR_OFFSET          10         /* the car beside the end of the goal's route, away from the globe */
#define MOVE_FRAMES         8          /* the car from goal to goal on a side */
#define CROSS_FRAMES        24         /* and across the map */
#define PANEL_X             0x220      /* the panel's x on the map (its screen x 18 for the east half) */
#define HEADING_Y           12
#define NAME_Y              30
#define SCREEN_WIDTH        320
#define MAP_RIGHT           904        /* the world map's right edge, in the map's columns (WIDE 20:9 reaches past
                                          it at the east half's scroll) */

/* Each goal's place on the map (stages 15-1E: its x, the screen's scroll x added; its y), where its route
   ends: the east's five from the bottom up, the west's from the top down */
static const int16_t goal_places[GOALS][2] = {
	{ 780, 191 }, { 791, 150 }, { 802, 109 }, { 813, 68 }, { 824, 27 },    /* AUSTRALIA to RUSSIA */
	{ 307, 27 }, { 297, 68 }, { 285, 109 }, { 274, 150 }, { 264, 191 }     /* NORTHERN EUROPE to KENYA */
};

/* the goals, as the test mode's BOOKKEEPING names them (stages 15-1E) */
static const char *const goal_names[GOALS] = { "AUSTRALIA", "HONGKONG", "JAPAN", "CHINA", "RUSSIA", "NORTHERN EUROPE",
	"SWITZERLAND", "ATLANTIC OCEAN", "SPAIN", "KENYA" };

static struct
{
	bool running;
	int frame;
	int goal;                      /* the goal picked (0-9) */
	int scroll;                    /* the screen's place on the map */
	int car_x, car_y;              /* the car on the map, driving to the goal's place */
	int from_x, from_y, moving, move_frames;
	int model;                     /* the car chosen */
	int leaving;                   /* ROUTE_SELECT_START or _BACK once picked, while the map fades out */
} pick;

static int side_of(int goal)
{
	return goal < SIDE_GOALS ? EAST : WEST;
}

/* From the top down on its side */
static int rank_of(int goal)
{
	return side_of(goal) == EAST ? SIDE_GOALS - 1 - goal : goal - SIDE_GOALS;
}

static int goal_at(int side, int rank)
{
	return side == EAST ? SIDE_GOALS - 1 - rank : SIDE_GOALS + rank;
}

static int car_place_x(int goal)
{
	return goal_places[goal][0] + (side_of(goal) == EAST ? CAR_OFFSET : -CAR_OFFSET);
}

/* ---- the map ---- */

static const struct route_map_registers *map_registers(void)
{
	return &route_map_register_sets[screen->index];
}

/* The map's scroll (the routes' tilemap and the world's under it, as route_map_update has them) and the
   panel's place with it */
static void map_scroll(int scroll)
{
	const struct route_map_registers *registers = map_registers();
	const uint32_t panel = console_find_object(FUNCTION_route_map_direction_sign_show, 0u);

	pick.scroll = scroll;
	VIDEO_REGISTER_WORDS(registers->scroll_x_address)[0] = (uint16_t)scroll;
	VIDEO_REGISTER_WORDS(registers->register_scroll_x_address)[0] = (uint16_t)scroll;
	VIDEO_REGISTER_WORDS(registers->second_scroll_x_address)[0] = (uint16_t)(scroll - 0x88);
	VIDEO_REGISTER_WORDS(registers->second_register_scroll_x_address)[0] = (uint16_t)(scroll - 0x88);
	world->send_video_registers = game->send_video_registers = 0xFFu;
	if (panel)
		OBJECT(panel)->x = (uint16_t)(PANEL_X - scroll);
}

/* The whole map of the race's end: its tilemaps, palette, zoom and scrolls as route_map_update leaves them
   (states 0, 1 and 16), the sea black, the WEST / EAST panel */
static void map_set_up(int side)
{
	const uint32_t index = screen->index;
	const struct route_map_registers *registers = map_registers();

	screen->progress.route_branches[0] = (uint8_t)side;    /* (the panel's first place) */
	set_up_background(rom_pointer(0x4868u + (index * 10u + (uint32_t)side * 5u) * 4u));
	queue_palette(route_map_background_palette);
	io_write16(route_map_sea_colour_addresses[index], 0u);
	console_page_select(registers->pages_address, route_map_routes_pages[index]);
	console_page_select(registers->second_pages_address, route_map_world_pages[index]);
	VIDEO_REGISTER_WORDS(registers->zoom_x_address)[0] = 0x200u;
	VIDEO_REGISTER_WORDS(registers->zoom_y_address)[0] = 0x200u;
	VIDEO_REGISTER_WORDS(registers->zoom_center_x_address)[0] = 0xA0u;
	VIDEO_REGISTER_WORDS(registers->zoom_center_y_address)[0] = 0x70u;
	VIDEO_REGISTER_WORDS(registers->scroll_y_address)[0] = 0x70u;
	VIDEO_REGISTER_WORDS(registers->register_scroll_y_address)[0] = 0x70u;
	VIDEO_REGISTER_WORDS(registers->second_scroll_y_address)[0] = 0u;
	VIDEO_REGISTER_WORDS(registers->second_register_scroll_y_address)[0] = 0u;
	start_object()->handler = FUNCTION_route_map_direction_sign_show;
	map_scroll(route_map_whole_scroll_xs[side]);
}

/* ---- the car (console_code) ---- */

void route_select_car_update(void)
{
	if (current_object->state == 0u)
	{
		current_object->draw_mode = 0xAu;
		animation_start(route_map_car_animations[pick.model]);
		current_object->palette = route_map_car_palettes[pick.model];
		current_object->width_scale = CAR_SIZE;
		current_object->height_scale = CAR_SIZE;
		current_object->frame = CAR_FRAME;
		current_object->state++;
	}
	current_object->x = (uint16_t)(pick.car_x - pick.scroll);
	current_object->y = (uint16_t)pick.car_y;
	map_depth_from_y();
	draw_object(0u);
}

/* ---- the scene ---- */

void console_route_select_start(int model)
{
	pick.running = true;
	pick.frame = 0;
	pick.model = model & 7;
	pick.moving = 0;
	pick.leaving = ROUTE_SELECT_NOTHING;
}

bool console_route_select_running(void)
{
	return pick.running;
}

int console_route_select_goal(void)
{
	return pick.goal;
}

int console_route_select_frame(uint32_t pressed)
{
	const int side = side_of(pick.goal), rank = rank_of(pick.goal);
	int choice = pick.goal;

	if (pick.frame++ == 0)
	{
		/* as the title clears its screen, from black */
		clear_objects();
		reset_tile_layers();
		clear_sprite_group_count();
		clear_text_layer();
		map_set_up(side);
		pick.car_x = car_place_x(pick.goal);
		pick.car_y = goal_places[pick.goal][1];
		start_object()->handler = CONSOLE_CODE_ROUTE_CAR;
		screen->fade_kind = 1u;
		screen->flags |= 1u;
		return ROUTE_SELECT_NOTHING;
	}
	if (pick.leaving != ROUTE_SELECT_NOTHING && (screen->flags & 1u) == 0u)
	{
		/* (the fade out over: the map's objects go, the game goes on; the zoom's center back at 0, as the
		   game's route map leaves it (route_map_update) and the race's background reads it: at A0, 70 the
		   race's background was 112 lines lower, the sky over a hill black, a player's screenshot) */
		const struct route_map_registers *registers = map_registers();

		VIDEO_REGISTER_WORDS(registers->zoom_center_x_address)[0] = 0u;
		VIDEO_REGISTER_WORDS(registers->zoom_center_y_address)[0] = 0u;
		world->send_video_registers = game->send_video_registers = 0xFFu;
		pick.running = false;
		clear_objects();
		return pick.leaving;
	}
	if (pick.leaving == ROUTE_SELECT_NOTHING && (pressed & (ACTION_BIT(ACTION_START) | ACTION_BIT(ACTION_ACCEPT)
		| ACTION_BIT(ACTION_BACK))))
	{
		queue_sound(MENU_SOUND);
		pick.leaving = (pressed & ACTION_BIT(ACTION_BACK)) ? ROUTE_SELECT_BACK : ROUTE_SELECT_START;
		screen->fade_kind = 0u;
		screen->flags |= 1u;
		pressed = 0;
	}
	if (pick.leaving != ROUTE_SELECT_NOTHING)
		pressed = 0;
	if (pressed & ACTION_BIT(ACTION_UP))
		choice = goal_at(side, rank > 0 ? rank - 1 : SIDE_GOALS - 1);
	else if (pressed & ACTION_BIT(ACTION_DOWN))
		choice = goal_at(side, rank < SIDE_GOALS - 1 ? rank + 1 : 0);
	else if (pressed & (ACTION_BIT(ACTION_LEFT) | ACTION_BIT(ACTION_RIGHT)))
		choice = goal_at(1 - side, rank);
	if (choice != pick.goal)
	{
		queue_sound(MENU_SOUND);
		pick.goal = choice;
		pick.from_x = pick.car_x;
		pick.from_y = pick.car_y;
		pick.move_frames = side_of(choice) != side ? CROSS_FRAMES : MOVE_FRAMES;
		pick.moving = pick.move_frames;
	}
	if (pick.moving)
	{
		const int done = pick.move_frames - --pick.moving;

		pick.car_x = pick.from_x + (car_place_x(pick.goal) - pick.from_x) * done / pick.move_frames;
		pick.car_y = pick.from_y + (goal_places[pick.goal][1] - pick.from_y) * done / pick.move_frames;
	}
	{
		/* the map after the car, from one side's place to the other's, the picture's right edge on the map */
		const int west = route_map_whole_scroll_xs[WEST];
		const int east_most = MAP_RIGHT - SCREEN_WIDTH - console_wide_side();
		const int east = route_map_whole_scroll_xs[EAST] < east_most ? route_map_whole_scroll_xs[EAST] : east_most;
		int scroll = pick.car_x - SCREEN_WIDTH / 2;

		scroll = scroll < west ? west : (scroll > east ? east : scroll);
		if (scroll != pick.scroll)
			map_scroll(scroll);
	}
	{
		const char *name = goal_names[pick.goal];

		console_text((SCREEN_WIDTH - console_text_width("REVERSE", 2)) / 2, HEADING_Y, 2, 0x0100u, "REVERSE");
		console_text((SCREEN_WIDTH - console_text_width(name, 1)) / 2, NAME_Y, 1, 0x00F0u, name);
	}
	return ROUTE_SELECT_NOTHING;
}
