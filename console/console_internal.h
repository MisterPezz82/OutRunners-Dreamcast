/**
 *    Name:        console_internal.h
 *    Description: what the console build's files share among themselves
 *    Version:     0.1.1
 *    Created:     29-10-2026
 *    Modified:    24-11-2026
 *    Author:      Sebastian Toyos
 */
#ifndef ORUNNERS_CONSOLE_INTERNAL_H
#define ORUNNERS_CONSOLE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The MultiPCM's samples by what they are (console/pcm_categories.c, made by tools/pcm_categories.py from
   pcm_samples.txt), for SOUND's MUSIC, EFFECTS and VOICE (issue 57) */
enum { PCM_MUSIC, PCM_EFFECTS, PCM_VOICE };
typedef struct
{
	uint32_t offset;     /* in the pcm region */
	uint32_t length;
	uint8_t category;    /* PCM_* */
} console_pcm_sample;
extern const console_pcm_sample console_pcm_samples[];
extern const size_t console_pcm_sample_count;

#define LEFT_SEAT                   0u    /* screen index: the left seat's screen (screen 1); the right one's is 1 */

/* The game modes (screen game_mode) */
#define GAME_MODE_POWER_ON          0u
#define GAME_MODE_RESTART           1u    /* the title from its start */
#define GAME_MODE_TITLE             2u
#define GAME_MODE_CAR_PRESENTATION  3u
#define GAME_MODE_DEMO_RACE         4u
#define GAME_MODE_RANKING           5u
#define GAME_MODE_PLAY              6u
#define GAME_MODE_ENTRY_INVITATION  7u
#define GAME_MODE_CONSOLE_SCENE     9u    /* none of the game's: its code runs nothing (the console's scene) */

/* What the menus ask of the left seat's frame (console_menu_frame) */
enum
{
	MENU_NOTHING,
	MENU_START_ONE,     /* a game of one player (ARCADE) */
	MENU_START_TWO,     /* a game of both seats (2 PLAYERS) */
	MENU_TIME_ATTACK,   /* a game of one player without the time limit (& KNUCKLES > TIME ATTACK) */
	MENU_FULL,          /* a game of one player through the 31 stages (& KNUCKLES > FULL) */
	MENU_REVERSE,       /* a game of one player from a goal back to the start (& KNUCKLES > REVERSE) */
	MENU_CLASSIC,       /* the arcade's game as it is, the right seat free to join (& KNUCKLES > CLASSIC) */
	MENU_TITLE          /* the title from its start (game mode 1) */
};

typedef struct
{
	uint32_t pressed;       /* the actions pressed since the left seat's last frame */
	bool quit;
	bool two_players;       /* 2 PLAYERS: both screens shown, the right seat in the race */
	bool join_right;        /* the right seat's START, pressed once it is in the entry (game mode 7) */
	bool time_attack;       /* DELUXE > TIME ATTACK: the left seat's race has no time limit */
	bool classic;           /* DELUXE > CLASSIC: the right seat free to join while the left one chooses */
	bool classic_start;     /* CLASSIC: its seconds more given at the start (console_classic_time) */
	bool side_map_wanted;   /* the port has room for the route map beside the race (console_side_map_wanted) */
	uint8_t quit_seats;     /* the pause's QUIT GAME: the seats (bit 0 the left) still to go back to the title */
} console_state;

extern console_state console;

/* The game's variables the decompilation keeps in the host's memory (struct world: player_count, link_off,
   link_failed, car_numbers, send_video_registers, wheel_motor) the console sets in both places, world->X =
   game->X = value: world is what the game's C reads, and the work RAM's copy (game) is what a frame's end
   projects for the state's hash and, after the port's frame code, takes back into world */

/* A race record's car model and car choice, which the program keeps in one halfword (race state
   car_choice_and_model: the choice its low byte, the model its high one) */
#define RACE_MODEL(race) ((uint8_t)((race)->state.car_choice_and_model >> 8u))
#define RACE_CAR_CHOICE(race) ((uint8_t)(race)->state.car_choice_and_model)
#define RACE_SET_MODEL(race, model) ((race)->state.car_choice_and_model = \
	(uint16_t)(((race)->state.car_choice_and_model & 0x00FFu) | ((uint32_t)(uint8_t)(model) << 8u)))
#define RACE_SET_CAR_CHOICE(race, choice) ((race)->state.car_choice_and_model = \
	(uint16_t)(((race)->state.car_choice_and_model & 0xFF00u) | (uint8_t)(choice)))

/* The console's own objects' code, at addresses the program has none at (console_code) */
#define CONSOLE_CODE_PRESENTATION_LOGO  0xFE0000u
#define CONSOLE_CODE_PRESENTATION_CAR   0xFE0001u
#define CONSOLE_CODE_START_LINE         0xFE0002u    /* REVERSE's start line (routes.c) */
#define CONSOLE_CODE_GHOST              0xFE0003u    /* TIME ATTACK's ghost's objects (ghost.c) */
#define CONSOLE_CODE_ROUTE_CAR          0xFE0004u    /* the car on REVERSE's route map (route_select.c) */
#define CONSOLE_CODE_SIDE_MAP           0xFE0005u    /* the route map beside the race (side_map.c): its place */
#define CONSOLE_CODE_SIDE_MAP_CAR       0xFE0006u    /* and its car */
#define CONSOLE_CODE_SIDE_MAP_SIGN      0xFE0007u    /* and a C.P sign */

/* The menus' sound, a line changed or picked: the test mode's sound 57 */
#define MENU_SOUND          0x4Eu

void presentation_logo_update(void);
void presentation_car_update(void);
void presentation_start_line_update(void);
void ghost_object_update(void);
void route_select_car_update(void);
void side_map_update(void);
void side_map_car_update(void);
void side_map_sign_update(void);

/* TIME ATTACK's ghost (ghost.c): a game starts; the left seat's frame during it */
void console_ghost_start(void);
void console_ghost_frame(void);

/* The SEGA presentation (presentation.c) on the left seat's frame at power-on, with the actions pressed:
   true while it lasts (the game mode's code waits), false once it is over */
bool console_presentation_frame(uint32_t pressed);


/* & KNUCKLES' routes (routes.c): REVERSE started (from 'goal', the stage; 0: picked on the route map after
   the car select), FULL, and their part of the left seat's frame before the game mode's code, with the
   actions pressed */
void console_reverse_start(int goal);
void console_full_start(void);
int console_reverse_goal(void);    /* (a REVERSE game's goal stage, once picked; 0 otherwise) */
bool console_routes_frame(uint32_t mode, uint32_t pressed);    /* (true: the left seat's game waits) */
bool console_routes_running(void);    /* (a game of FULL or REVERSE: its route is not the game's) */

/* The route map beside the race (side_map.c), on the right seat's frame: its scene while it is 'due'
   (SIDE_MAP_ON: the right seat's game waits), and once it is over (SIDE_MAP_OVER: the right seat back to
   its title) */
enum
{
	SIDE_MAP_OFF,
	SIDE_MAP_ON,
	SIDE_MAP_OVER
};

int console_side_map_frame(bool due);
bool console_side_map_running(void);

/* REVERSE's goal on the route map (route_select.c), once the car select is over: started with the car
   model chosen, on while the goal is picked, the left seat's frame meanwhile (its game waits) and the goal
   picked (0-9, the last one picked when it starts again) */
enum
{
	ROUTE_SELECT_NOTHING,
	ROUTE_SELECT_START,     /* the goal picked, the screen dark again: REVERSE starts there */
	ROUTE_SELECT_BACK       /* the game given up: back to the title */
};

void console_route_select_start(int model);
bool console_route_select_running(void);
int console_route_select_frame(uint32_t pressed);
int console_route_select_goal(void);

/* A tilemap's page select at 'address' (a route map's registers' pages): its four pages, two halfwords, as
   route_map_update writes them (console.c) */
void console_page_select(uint32_t address, uint32_t pages);

/* DIFFICULTY and TIME, as OPTIONS leaves them, into the race records (console.c); and with them the rest of
   machine 1's settings, as network_check_result puts them in its first record for every machine's races */
void console_settings_to_race(void);
void console_master_settings(void);

/* CLASSIC's seconds more, after race_progress_update (console.c): 'passes' the race's pass_count before it */
void console_classic_time(uint8_t passes);

/* The network game (network.c), for the menus: a room hosted (HOST), the rooms looked for and one joined (JOIN),
   left; the players of this device in its races (1, or 2: both seats) */
void console_network_host(void);
void console_network_search(void);
void console_network_join(int room);
void console_network_leave(void);
int console_network_players(void);
void console_network_set_players(int players);
bool console_network_in_room(void);     /* (joining one too) */
bool console_network_joining(void);
bool console_network_searching(void);
bool console_network_hosting(void);
bool console_network_linked(void);      /* (the link set up for the room's races) */

/* JOIN's list of rooms: how many, and a line for each (its host's name, its devices); full ones unjoinable */
int console_network_room_count(void);
void console_network_room(int room, char *text, size_t size);
bool console_network_room_full(int room);

/* The room's devices, a line for each (its number, its player's name, 2P) */
int console_network_member_count(void);
void console_network_member(int index, char *text, size_t size);

/* The host's START (two devices or more, none of them racing), and the race it starts on the left seat's frame:
   the seats to start (0 none, 1, or 2), once, in the game's attract modes */
bool console_network_can_start(void);
void console_network_start(void);
bool console_network_start_pending(void);
int console_network_take_start(void);

/* What went wrong with the network (the room gone, no network), once: NULL if nothing */
const char *console_network_problem(void);
void console_network_problem_seen(void);

/* The menus on the left seat's frame, before its game mode's code ('mode'): what it asks for (MENU_*) */
int console_menu_frame(uint32_t mode);

/* The objects of the screen running: the first live one at or after 'from' whose handler is 'handler' (0:
   none); from 0 the first */
uint32_t console_find_object(uint32_t handler, uint32_t from);

#endif
