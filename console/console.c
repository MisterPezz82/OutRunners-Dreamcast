/**
 *    Name:        console.c
 *    Description: the console build's own versions of the game functions console/replaced.txt lists, and
 *                 what it tells the port
 *    Version:     0.1.5
 *    Created:     29-10-2026
 *    Modified:    24-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The console build runs the arcade game's program with these in place of the game's own (whose
 *    definitions tools/decompile.py writes as CONSOLE_ORIGINAL(name): original_name here, for these to call):
 *    one player on the left seat's screen, free play, one sound for both speakers, and the title's menu
 *    (menu.c) in place of the start button.
 */
#include "console.h"
#include "console_internal.h"

#include "game.h"
#include "s32/video.h"

#include <string.h>

#define COIN_SETTINGS      0x1Au    /* the test mode's COIN/CREDIT SETTING: 26 */
#define START_BUTTON        0x40u    /* screen buttons_pressed: START */
#define TIME_ATTACK_TIME_LEFT (99u * 30u)    /* race time_left, in 30ths of a second */
#define CLASSIC_EXTRA       5u       /* CLASSIC's seconds more, at the start and at each checkpoint */
#define SCREEN_CAR          0x1000u  /* the screen's car, beside its block (the register player_car) */
#define TRANSMISSION_MANUAL 1u       /* car transmission (car_select_wheel_transmissions): 0 automatic */
#define WHEEL_CENTRE        0x80u    /* screen wheel: the middle (the car select's turntable stays) */
#define WHEEL_MANUAL        0x20u    /* screen wheel: where the car select picks the manual transmission */

console_state console;

/* The pause's RESTART: the start of the game being played as the menus made it, and what a restart takes
   again */
static struct
{
	int started;                     /* MENU_START_ONE to MENU_CLASSIC; MENU_NOTHING: the network's, or none */
	bool pending;                    /* RESTART picked: the same start once the left seat is at the title */
	int goal;                        /* REVERSE's goal stage */
	uint8_t choosing;                /* the seats (bit 0 the left) whose car select takes the choices again */
	uint8_t models[2];               /* by seat: the car, and its transmission */
	uint8_t transmissions[2];
} restart;

/* ---- the coins ---- */

/* The settings as the EEPROM has them (the game's own load, with its repairs), then the coins set to free
   play as the test mode's COIN/CREDIT SETTING leaves them on FREE PLAY (the setting whose record has no
   coins), and written back as the game's own repairs write them */
void load_settings(uint32_t checksum)
{
	uint32_t setting;

	original_load_settings(checksum);
	if (game->eeprom.settings.free_play != 0u)
		return;
	for (setting = 0; setting < COIN_SETTINGS - 1u; setting++)
		if ((int8_t)coin_setting_records[setting].coins < 0)
			break;
	game->eeprom.settings.coin_setting = (uint8_t)setting;
	game->eeprom.settings.coins_per_credit = 1u;
	game->eeprom.settings.bonus_adder = 0xFFu;
	game->eeprom.settings.first_chute_multiplier = 1u;
	game->eeprom.settings.second_chute_multiplier = 1u;
	game->eeprom.settings.free_play = 0xFFu;
	eeprom_write_settings();
}

/* The credits line under the prompt: none (with free play the game's says FREE PLAY) */
void credit_display_update(void)
{
	current_object->flags = 0u;
	current_object->flags_high = 0u;
}

/* The prompt's message (INSERT COIN(S), TURN THE IGNITION KEY) and the ignition key's picture, which the
   prompt starts every 30 frames: ended at once, unseen; PRESS START is the console's (menu.c). The little
   car that crosses the prompt still does */
void coin_prompt_message_update(void)
{
	current_object->flags = 0u;
	current_object->flags_high = 0u;
}

void ignition_key_prompt_update(void)
{
	current_object->flags = 0u;
	current_object->flags_high = 0u;
}

/* ---- the machine ---- */

/* The settings OPTIONS changes, into the first race record's master fields as network_check_result puts
   them at boot (the races read the copies the link makes of them) */
void console_settings_to_race(void)
{
	const uint32_t race = RACE_1;

	RACE(race)->master_game_difficulty = game->eeprom.settings.game_difficulty;
	memcpy(RACE(race)->master_time_difficulty, game->eeprom.settings.time_difficulty, sizeof(RACE(race)->master_time_difficulty));
}

void console_master_settings(void)
{
	const uint32_t race = RACE_1;

	RACE(race)->master_advertise_sound = game->eeprom.settings.advertise_sound;
	RACE(race)->master_steering_wheel_reaction = game->eeprom.settings.steering_wheel_reaction;
	RACE(race)->master_coins_per_credit = game->eeprom.settings.coins_per_credit;
	RACE(race)->master_first_chute_multiplier = game->eeprom.settings.first_chute_multiplier;
	RACE(race)->master_bonus_adder = game->eeprom.settings.bonus_adder;
	RACE(race)->master_coin_setting = game->eeprom.settings.coin_setting;
	RACE(race)->master_free_play = game->eeprom.settings.free_play;
	console_settings_to_race();
}

/* The machine alone (stand alone, or the link failed), as the game's network_check_stand_alone and
   network_check_result set it up as machine 1, without NETWORK CHECK and its result on the screens for
   128 frames: the SEGA presentation is the first thing seen */
void network_check_stand_alone(void)
{
	const uint32_t race = RACE_1;

	world->link_off = game->link_off = 1u;
	world->player_count = game->player_count = 2u;
	game->machine_count = 1u;
	SCREEN(SCREEN_1)->car_number = 0u;
	RACE(RACE_1)->car_number = 0u;
	SCREEN(SCREEN_2)->car_number = 1u;
	RACE(RACE_2)->car_number = 1u;
	RACE(RACE_1)->machine = 1u;
	RACE(RACE_2)->machine = 1u;
	RACE(RACE_1)->player = 0u;
	RACE(RACE_2)->player = 1u;
	SCREEN(SCREEN_1)->player = 0u;
	SCREEN(SCREEN_2)->player = 1u;
	SCREEN(SCREEN_1)->race_copy = race_copy_offsets[0] + GAME_VARIABLES;
	SCREEN(SCREEN_2)->race_copy = race_copy_offsets[1] + GAME_VARIABLES;
	SCREEN(SCREEN_1)->race = RACE_1;
	SCREEN(SCREEN_2)->race = RACE_2;
	console_master_settings();
	move_halfwords(race + 0x30u, BOOKKEEPING_COURSE_RECORDS_ADDRESS, 0x1Eu);
	clear_text_layer();
}

/* ---- the picture ---- */

/* The sprites put in the list from 'first' on (the object's, this frame) marked as a ghost's (S32_SPRITE_GHOST:
   half their colour, half the colour under them), on both screens' lists */
static void sprites_see_through(uint32_t first)
{
	for (uint32_t sprite = first; sprite <= screen->sprite_list_last; sprite += 0x10u)
		if ((SPRITE_WORDS(sprite)[0] >> 14) == 0u)    /* (a sprite, not the group's jump) */
		{
			SPRITE_WORDS(sprite)[7] |= S32_SPRITE_GHOST;
			SPRITE_WORDS(sprite + 0x3000u)[7] |= S32_SPRITE_GHOST;
		}
}

/* The sprites on the background that the game draws every other frame so that they look faint (the spray of
   Niagara's falls, the pyramids of stages 13 and 20): two of the screen's frames on and two off, which shows
   as a blink (a player's report). Here they are drawn every frame and see-through: the count kept odd for the
   game's handler, which draws when its next is even, and the sprites it puts in the list marked as a ghost's,
   what the frames on and off add up to */
void backdrop_translucent_update(void)
{
	const uint32_t first = screen->sprite_list_last + 0x10u;

	current_object->backdrop_translucent.frame_count |= 1u;
	original_backdrop_translucent_update();
	sprites_see_through(first);
}

/* The northern lights over stage 26 (issue 32), drawn by the game every other frame (when their countdown is odd)
   so that they look faint, which blinks as the spray did. Their countdown also times their animations, so it is
   left as it is: the frames the game leaves out draw the same picture again (draw_object, no animation_advance:
   their animation at its own pace), and every frame's sprites are see-through, as the spray's */
void aurora_update(void)
{
	const uint32_t first = screen->sprite_list_last + 0x10u;

	original_aurora_update();
	if ((current_object->aurora.countdown & 1u) == 0u)
		draw_object(0u);    /* (a frame the game leaves out: the same picture) */
	sprites_see_through(first);
}

/* The close-up route map's one picture at half its rate (its record's half_rate, issue 32): the game steps its
   animation and draws it every other frame (toggle), which blinks; here the frames it leaves out draw the same
   picture again (its animation at half its rate still), and every frame's sprites are see-through, as the
   spray's. The others, and the frame it ends at, as the game's */
void route_map_landmark_update(void)
{
	const uint32_t first = screen->sprite_list_last + 0x10u;
	const bool half_rate = current_object->state == 1u && current_object->route_map_landmark.index_then_half_rate != 0u;

	original_route_map_landmark_update();
	if (!half_rate || current_object->flags == 0u)
		return;
	if (current_object->route_map_landmark.toggle != 0u)
	{
		map_depth_from_y();
		draw_object(0u);    /* (a frame the game leaves out: the same picture) */
	}
	sprites_see_through(first);
}

/* WIDE's sides as the game's edges read them (support.h's SCREEN_MORE_LEFT and SCREEN_MORE_RIGHT): where the
   game takes an object away, stops drawing it, turns it back or puts it in at a side, past the wider screen's
   side; 0 with WIDE off and in 2 PLAYERS. console_set_sides sets them each frame. */
int screen_more_left, screen_more_right;

/* Whether a sprite on the background is wholly off the screen, with WIDE's sides: past the wider screen's side,
   so that WIDE shows the spray of Niagara's falls, the pyramids and the landmarks at the sides too (the
   decompilation keeps the function's numbers: its A0 is both the screen's middle and how far each edge is from
   it). While the sprite waits to come in (state 0: off the screen first, so that it does not pop up in view)
   and with no sides it is the game's own, so that when it comes in, and all the board's 320 columns show, are
   the board's. */
uint32_t backdrop_off_screen(void)
{
	const int16_t centre = (int16_t)(current_object->x - 0xA0u);
	const int16_t half_width = (int16_t)current_object->backdrop.half_width;

	if (current_object->state == 0u || (screen_more_left == 0 && screen_more_right == 0))
		return original_backdrop_off_screen();
	if ((int16_t)(centre - half_width) > 0xA0 + screen_more_right
		|| (int16_t)(0 - centre - half_width) > 0xA0 + screen_more_left)
	{
		current_object->flags &= 0xDFu;    /* (as the game's) */
		return 1u;
	}
	return 0u;
}

/* ---- the sound ---- */

#define RADIO_SONGS 11    /* radio_songs' records: the cars' ten and Jingle Bell */

/* True for the sound of one of the radio's songs (radio_songs: those the cars start with and << and >> go
   through) */
static bool radio_song(uint8_t sound)
{
	for (int song = 0; song < RADIO_SONGS; song++)
		if ((uint8_t)(radio_songs[song] >> 24) == sound)
			return true;
	return false;
}

/* A screen's sound requests, in the vertical blank: the left seat's always; the right one's only while it
   plays a game (game mode 6), the rest of the time dropped, so that what its screen would play unseen (the
   entry countdown's DJ, its attract mode) is not heard and takes none of the 24 voices the two seats share.
   Nor are the right seat's radio's songs heard (issue 22: two cabinets have a radio and speakers each, one
   screen's two songs go to the same speakers) unless OPTIONS > 2P MUSIC asks for both: a song's request
   leaves its ring as the game takes it, one a vertical blank, and is not sent; its other sounds are */
void send_sound_requests(void)
{
	if (screen->index != LEFT_SEAT && screen->game_mode != GAME_MODE_PLAY)
	{
		screen->sound_queue_read = screen->sound_queue_write;
		return;
	}
	if (screen->index != LEFT_SEAT && !console_window_settings.both_radios
		&& screen->sound_queue_read != screen->sound_queue_write
		&& radio_song(screen->sound_queue[screen->sound_queue_read & 0x1Fu]))
	{
		screen->sound_queue_read = (uint8_t)((screen->sound_queue_read + 1u) & 0x1Fu);
		return;
	}
	original_send_sound_requests();
}

/* ---- starting games ---- */

/* The game's own start (check_game_start) on the screen running, as if its START had just been pressed:
   with free play it starts a game there (game mode 6), or joins the race being entered from game mode 7 */
static uint32_t press_start(uint32_t mode, uint32_t seat_credits)
{
	screen->buttons_pressed |= START_BUTTON;
	return original_check_game_start(mode, seat_credits);
}

/* A game the menus start ('start', MENU_START_ONE to MENU_CLASSIC), or a RESTART of it: its own set-up,
   then the left seat's START (REVERSE: from 'goal', 0 for the one the route map picks) */
static uint32_t start_game(int start, int goal, uint32_t mode, uint32_t seat_credits)
{
	restart.started = start;
	switch (start)
	{
		case MENU_START_TWO:
			console.two_players = true;
			console.join_right = true;
			break;
		case MENU_TIME_ATTACK:
			console.time_attack = true;
			console_ghost_start();
			break;
		case MENU_FULL:
			console_full_start();
			break;
		case MENU_CLASSIC:
			console.classic = true;
			console.classic_start = false;
			break;
		case MENU_REVERSE:
			console_reverse_start(goal);
			break;
		default:
			break;
	}
	return press_start(mode, seat_credits);
}

/* The steps of game mode 6 (and of the demo's race, game mode 4) from the race's set-up to its fade out
   (game_mode_play): the race on the screen */
static bool racing(uint32_t screen_block)
{
	const uint8_t mode = SCREEN(screen_block)->game_mode;
	const uint8_t step = SCREEN(screen_block)->game_step;

	if (mode != GAME_MODE_PLAY && mode != GAME_MODE_DEMO_RACE)
		return false;
	return step == 5u || step == 6u || step == 7u || step == 8u || step == 11u || (step >= 23u && step <= 27u);
}

/* The route map beside the race on the right seat's screen (side_map.c), its game mode 'mode': where the port
   has room for it, while the left seat races alone on the game's route and the right seat is idle (CLASSIC's
   too: its right seat joins while the race takes entries, before the race and its map) */
static bool side_map_due(uint32_t mode)
{
	return console.side_map_wanted && !console.two_players && !console_routes_running()
		&& SCREEN(SCREEN_1)->game_mode == GAME_MODE_PLAY && racing(SCREEN_1)
		&& mode != GAME_MODE_PLAY && mode != GAME_MODE_ENTRY_INVITATION;
}

/* Each screen's frame before its game mode's code. The right seat starts only to join a 2 PLAYERS race, or a
   CLASSIC one with its own START, which shows its screen (its START is let alone otherwise), and shows the
   route map beside a race of the left one where the port has room for it; the left one's START opens the
   title's menu, whose ARCADE and 2 PLAYERS start the game as the arcade's START does (menu.c) */
uint32_t check_game_start(uint32_t mode, uint32_t seat_credits)
{
	int menu;

	if (console.quit_seats & (1u << screen->index))
	{
		/* (the pause's QUIT GAME: a game being played ends, back to the title, with the sound and the wheel
		   motor off as the ranking's first step leaves them before the title: game mode 1 does not do it, and
		   the engine kept its revs and the race's song its sequence, issue 39) */
		console.quit_seats &= (uint8_t)~(1u << screen->index);
		if (mode == GAME_MODE_PLAY)
		{
			world->wheel_motor[screen->index] = game->wheel_motor[screen->index] = 0u;
			reset_screen_sound();
			AS32(screen->game_mode) = GAME_MODE_RESTART;
			return GAME_MODE_RESTART;
		}
	}
	if (screen->index != LEFT_SEAT)
	{
		switch (console_side_map_frame(side_map_due(mode)))
		{
			case SIDE_MAP_ON:
				return GAME_MODE_CONSOLE_SCENE;
			case SIDE_MAP_OVER:
				AS32(screen->game_mode) = GAME_MODE_RESTART;
				return GAME_MODE_RESTART;
			default:
				break;
		}
		if (console.join_right && mode == GAME_MODE_ENTRY_INVITATION)
		{
			console.join_right = false;
			return press_start(mode, seat_credits);
		}
		if (console.classic && mode == GAME_MODE_ENTRY_INVITATION)
		{
			mode = original_check_game_start(mode, seat_credits);
			if (mode == GAME_MODE_PLAY)
				console.two_players = true;    /* (a challenger) */
		}
		return mode;
	}
	if (mode == GAME_MODE_POWER_ON && console_presentation_frame(console.pressed))
	{
		console.pressed = 0;
		return GAME_MODE_CONSOLE_SCENE;    /* (the SEGA presentation first) */
	}
	if (console.time_attack)
	{
		/* TIME ATTACK: the race's time left kept at 99 seconds (the race's time is the HUD's lap and its
		   end's); over with the game */
		if (mode == GAME_MODE_PLAY)
		{
			RACE(screen->race)->state.time_left = TIME_ATTACK_TIME_LEFT;
			console_ghost_frame();
		}
		else
			console.time_attack = false;
	}
	if (console_routes_frame(mode, console.pressed))
	{
		console.pressed = 0;
		return GAME_MODE_CONSOLE_SCENE;    /* (REVERSE's goal on the route map: the game waits) */
	}
	if (console.two_players && mode != GAME_MODE_PLAY && SCREEN(SCREEN_2)->game_mode != GAME_MODE_PLAY
		&& SCREEN(SCREEN_2)->game_mode != GAME_MODE_ENTRY_INVITATION && !console.join_right)
		console.two_players = false;    /* (the race of both is over) */
	if (console.classic && mode != GAME_MODE_PLAY && SCREEN(SCREEN_2)->game_mode != GAME_MODE_PLAY)
		console.classic = false;
	if (console_network_start_pending() && mode >= GAME_MODE_TITLE && mode <= GAME_MODE_RANKING)
	{
		/* the network room's START: an ARCADE race of all its devices (network.c), both seats of one with
		   two players as 2 PLAYERS starts them */
		if (console_network_take_start() == 2)
		{
			console.two_players = true;
			console.join_right = true;
		}
		console_menu_frame(GAME_MODE_PLAY);    /* (its pages closed) */
		restart.started = MENU_NOTHING;    /* (no RESTART) */
		return press_start(mode, seat_credits);
	}
	if (restart.pending && mode >= GAME_MODE_TITLE && mode <= GAME_MODE_RANKING)
	{
		/* the pause's RESTART, back at the title: the same game again */
		restart.pending = false;
		console_menu_frame(GAME_MODE_PLAY);    /* (its pages closed) */
		return start_game(restart.started, restart.goal, mode, seat_credits);
	}
	switch (menu = console_menu_frame(mode))
	{
		case MENU_START_ONE:
		case MENU_START_TWO:
		case MENU_TIME_ATTACK:
		case MENU_FULL:
		case MENU_CLASSIC:
		case MENU_REVERSE:
			return start_game(menu, 0, mode, seat_credits);
		case MENU_TITLE:
			AS32(screen->game_mode) = GAME_MODE_RESTART;
			return GAME_MODE_RESTART;
		default:
			return mode;
	}
}

/* CLASSIC: CLASSIC_EXTRA seconds more than the arcade's in every part of the race (Sebas: friendlier, and
   apart from ARCADE), in the host's record: once the car has given the start's (player_car_update: 40
   seconds and the TIME setting, before the race starts; the record holds the entries' count before it; the
   copies of it the screens read, host_record, and their time displays' for the countdown, car time_left,
   too), and
   with each checkpoint's that the game adds (the first past it, before the goal) */
void console_classic_time(uint8_t passes)
{
	const uint32_t race = screen->race;
	const uint16_t start_time = (uint16_t)((game->race_copies[0].master_time_difficulty[0] + 0x28u) * 0x1Eu);

	if (!console.classic || screen->is_host == 0u)
		return;
	if (!console.classic_start && screen->race_started == 0u && RACE(race)->state.time_left == start_time)
	{
		RACE(race)->state.time_left += CLASSIC_EXTRA * 0x1Eu;
		for (uint32_t seat = SCREEN_1; seat <= SCREEN_2; seat += SCREEN_2 - SCREEN_1)
		{
			if (SCREEN(seat)->in_game == 0u || SCREEN(seat)->host_record == 0u)
				continue;
			if (RACE(SCREEN(seat)->host_record)->state.time_left == start_time)
				RACE(SCREEN(seat)->host_record)->state.time_left += CLASSIC_EXTRA * 0x1Eu;    /* (the copy it reads) */
			if (CAR(seat + SCREEN_CAR)->time_left == start_time)
				CAR(seat + SCREEN_CAR)->time_left += CLASSIC_EXTRA * 0x1Eu;
		}
		console.classic_start = true;
	}
	if (RACE(race)->state.pass_count != passes && RACE(race)->state.pass_order == 0u && RACE(race)->state.pass_checkpoint != 5u)
		RACE(race)->state.time_left += CLASSIC_EXTRA * 0x1Eu;
}

/* The car select's WAITING FOR CHALLENGERS! (then JUST A MOMENT PLEASE) once the player's choices are made,
   while the race takes entries: with one player (no 2 PLAYERS, no network, no challenger in CLASSIC) none,
   and the entries closed at once (player 0's record's entry_frames), as no one else comes. CLASSIC's
   challenger joins while the left seat chooses: the arcade's 10 seconds after it, the right seat's screen
   unseen, were a wait for no one (Sebas) */
void waiting_banner_show(void)
{
	if (console.two_players || console_network_linked())
	{
		original_waiting_banner_show();
		return;
	}
	if (RACE(RACE_1)->entry_frames > 1u)
		RACE(RACE_1)->entry_frames = 1u;
	current_object->flags = 0u;
	current_object->flags_high = 0u;
}

/* ---- the port ---- */

/* The console's own objects' code, at addresses the program has none at */
static bool console_code(uint32_t address)
{
	switch (address)
	{
		case CONSOLE_CODE_PRESENTATION_LOGO:
			presentation_logo_update();
			return true;
		case CONSOLE_CODE_PRESENTATION_CAR:
			presentation_car_update();
			return true;
		case CONSOLE_CODE_START_LINE:
			presentation_start_line_update();
			return true;
		case CONSOLE_CODE_GHOST:
			ghost_object_update();
			return true;
		case CONSOLE_CODE_ROUTE_CAR:
			route_select_car_update();
			return true;
		case CONSOLE_CODE_SIDE_MAP:
			side_map_update();
			return true;
		case CONSOLE_CODE_SIDE_MAP_CAR:
			side_map_car_update();
			return true;
		case CONSOLE_CODE_SIDE_MAP_SIGN:
			side_map_sign_update();
			return true;
		default:
			return false;
	}
}

void console_attach(void)
{
	call_code_elsewhere = console_code;
	s32_video_use_extra_palette(true);    /* (REVERSE's start line) */
}

static int screen_wide;    /* WIDE AUTO's, as the port says the screen is (WIDE_OFF till it does) */

/* A WIDE of a width of its own (not OFF, not AUTO), else WIDE_OFF */
static int wide_width_kind(int kind)
{
	return kind > WIDE_OFF && kind < WIDE_KINDS && kind != WIDE_AUTO ? kind : WIDE_OFF;
}

void console_set_screen_wide(int kind)
{
	screen_wide = wide_width_kind(kind);
}

int console_wide_in_effect(void)
{
	const int wide = console_window_settings.wide;

	if (wide == WIDE_AUTO)
		return screen_wide;
	return wide_width_kind(wide);
}

int console_wide_side(void)
{
	if (console.two_players)
		return 0;    /* (2 PLAYERS: both screens alike) */
	switch (console_wide_in_effect())
	{
		case WIDE_16_10: return S32_VIDEO_WIDE_SIDE_16_10;
		case WIDE_16_9: return S32_VIDEO_WIDE_SIDE_16_9;
		case WIDE_20_9: return S32_VIDEO_WIDE_SIDE_20_9;
		case WIDE_21_9: return S32_VIDEO_WIDE_SIDE_21_9;
		default: return 0;
	}
}

void console_set_sides(void)
{
	const int side = console_wide_side();

	s32_video_set_wide_side(side);
	screen_more_left = side;
	screen_more_right = side;
}

bool console_both_screens(void)
{
	return console.two_players;
}

int console_test_autopilot(void)
{
	/* the wheel back toward the middle of the road, by the left seat's place across it (race road_x) */
	const int across = (int16_t)RACE(RACE_1)->state.road_x;
	const int wheel = 0x80 - across / 6;

	return wheel < 0x10 ? 0x10 : (wheel > 0xF0 ? 0xF0 : wheel);
}

void console_actions(uint32_t pressed)
{
	console.pressed |= pressed;
}

bool console_can_pause(void)
{
	/* (not over REVERSE's map, in the race's step 5: its START picks the goal; nor in a race of the network's,
	   where the others race on: it held this device's car alone, issue 34) */
	return SCREEN(SCREEN_1)->game_mode == GAME_MODE_PLAY && racing(SCREEN_1) && !console_route_select_running()
		&& !console_network_linked();
}

/* ---- the radio: MUSIC round it all, and its hidden song ---- */

#define LAST_RADIO_SONG     9u    /* Last Wave: the game's >> goes on from it to the first, Magical Sound Shower */
#define FIRST_RADIO_SONG    0u
#define HIDDEN_RADIO_SONG   10u   /* Jingle Bell, radio_songs' eleventh */
#define RADIO_ON_SONGS      1u    /* the radio's states 0 and 1 play the car's song (car_radio_update); 2 on, the station */

/* The seat's radio (the object car_radio_read_buttons runs while its car races; its objects are the 0x7C00
   bytes before its block, as console_find_object's of the screen), or 0 */
static uint32_t seat_radio(uint32_t seat)
{
	for (uint32_t object = seat - 0x7C00u; object < seat; object += 0x100u)
		if ((OBJECT(object)->flags & 1u) != 0u && OBJECT(object)->handler == FUNCTION_car_radio_read_buttons)
			return object;
	return 0u;
}

void console_radio_buttons(int player, bool *music, bool *previous, bool *next)
{
	static bool held[CONTROL_PLAYERS][3];        /* the seat's << , >> and MUSIC the frame before */
	static bool music_steps[CONTROL_PLAYERS];    /* MUSIC, as it was pressed, held as >> */
	const uint32_t seat = player == 0 ? SCREEN_1 : SCREEN_2;
	const bool on_race = SCREEN(seat)->game_mode == GAME_MODE_PLAY && racing(seat);
	bool previous_pressed, next_pressed;
	uint8_t song;

	if (*music && !held[player][2])
	{
		/* (MUSIC newly pressed: on a song but the last, >> while it is held; on the station back to the first
		   song, which any of its buttons goes back to, from the car's) */
		const uint32_t radio = on_race ? seat_radio(seat) : 0u;

		music_steps[player] = false;
		if (radio && OBJECT(radio)->state <= RADIO_ON_SONGS)
			music_steps[player] = CAR(seat + SCREEN_CAR)->song != LAST_RADIO_SONG;
		else if (radio)
			CAR(seat + SCREEN_CAR)->song = FIRST_RADIO_SONG;
	}
	held[player][2] = *music;
	if (*music && music_steps[player])
	{
		*music = false;
		*next = true;
	}
	previous_pressed = *previous && !held[player][0];
	next_pressed = *next && !held[player][1];
	held[player][0] = *previous;
	held[player][1] = *next;
	if (previous_pressed == next_pressed || !on_race)
		return;
	song = CAR(seat + SCREEN_CAR)->song;
	if ((next_pressed && song == LAST_RADIO_SONG) || (previous_pressed && song == FIRST_RADIO_SONG))
	{
		/* (both pressed: the game plays Jingle Bell, and leaves the car's song as it was, which its next << or
		   >> steps from: 10, between Last Wave and Magical Sound Shower) */
		*previous = *next = true;
		CAR(seat + SCREEN_CAR)->song = HIDDEN_RADIO_SONG;
	}
}

bool console_manual_gears(int player)
{
	const uint32_t seat = player == 0 ? SCREEN_1 : SCREEN_2;

	return SCREEN(seat)->game_mode == GAME_MODE_PLAY && racing(seat) && !console_route_select_running()
		&& CAR(seat + SCREEN_CAR)->transmission == TRANSMISSION_MANUAL;
}

void console_quit_game(void)
{
	console.quit_seats = 3u;
}

/* ---- the pause's RESTART ---- */

bool console_can_restart(void)
{
	return console_can_pause() && restart.started != MENU_NOTHING;
}

void console_restart_game(void)
{
	if (!console_can_restart())
		return;
	restart.models[0] = CAR(SCREEN_1 + SCREEN_CAR)->model;
	restart.transmissions[0] = CAR(SCREEN_1 + SCREEN_CAR)->transmission;
	restart.models[1] = CAR(SCREEN_2 + SCREEN_CAR)->model;
	restart.transmissions[1] = CAR(SCREEN_2 + SCREEN_CAR)->transmission;
	restart.goal = console_reverse_goal();
	restart.choosing = restart.started == MENU_START_TWO ? 3u : 1u;    /* (CLASSIC's challenger joins again) */
	restart.pending = true;
	console_quit_game();
}

/* The car select (game_mode_play step 3), on a seat a RESTART started: the turntable at the car chosen before
   from its start, as the game puts its default car there, then that car and the transmission chosen before
   confirmed as the player's START would, the wheel held where it picks them; the game's own elsewhere */
void car_select_run(void)
{
	const uint8_t seat = (uint8_t)(1u << screen->index);
	const uint32_t state = current_object->state;
	const uint8_t wheel = screen->wheel;
	const uint32_t pressed = screen->buttons_pressed;

	if ((restart.choosing & seat) == 0u)
	{
		original_car_select_run();
		return;
	}
	if (state == 4u || state == 5u)
	{
		/* (4 the car, the turntable still; 5 the transmission) */
		screen->wheel = (uint8_t)(state == 5u && restart.transmissions[screen->index] == TRANSMISSION_MANUAL
			? WHEEL_MANUAL : WHEEL_CENTRE);
		screen->buttons_pressed |= START_BUTTON;
	}
	original_car_select_run();
	screen->wheel = wheel;
	screen->buttons_pressed = pressed;
	if (state == 1u && current_object->state != 1u)
	{
		/* (as state 1 sets its default car up) */
		const uint32_t model = restart.models[screen->index];

		player_car->model = (uint8_t)model;
		VIDEO_REGISTER_WORDS(current_object->car_select.scroll_x_address)[0] = (uint16_t)(0u - (model << 7));
		current_object->car_select.angle = model << 9;
	}
	if (current_object->car_select.transmission_chosen != 0u)
		restart.choosing &= (uint8_t)~seat;
}

void console_side_map_wanted(bool wanted)
{
	console.side_map_wanted = wanted;
}

bool console_side_map_shown(void)
{
	return console_side_map_running();
}

void console_page_select(uint32_t address, uint32_t pages)
{
	PAGE_SELECT(address)[0] = (uint16_t)pages;
	PAGE_SELECT(address)[1] = (uint16_t)(pages >> 16u);
}

bool console_right_seat_joins(void)
{
	return console.classic && SCREEN(SCREEN_2)->game_mode == GAME_MODE_ENTRY_INVITATION;
}

bool console_quit_requested(void)
{
	return console.quit;
}
