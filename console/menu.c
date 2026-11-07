/**
 *    Name:        menu.c
 *    Description: the console's menus on the left seat's screen: PRESS START in the attract mode, the title's
 *                 menu, DELUXE, NETWORK with its rooms, and OPTIONS with its pages
 *    Version:     0.1.4
 *    Created:     29-10-2026
 *    Modified:    07-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The menus are the game's sprite letters (draw.c) over the title: its menu under the logo, where the
 *    prompt was, with a shadow behind it; OPTIONS over the title's opening (the palm beach and the cars
 *    going by, before the logo comes), held there and darkened. They run on the left seat's frame, at 30 a
 *    second, from the actions the port hands (console_actions).
 */
#include "console.h"
#include "console_internal.h"
#include "console_draw.h"

#include "game.h"
#include "s32/video.h"

#include <stdio.h>

#define SCREEN_WIDTH        320
#define ITEM_COLOUR         0x0090u    /* the font's colours on the title: white, as the game's credits line */
#define CHOSEN_COLOUR       0x00F0u    /* orange to yellow */
#define TITLE_COLOUR        0x0100u    /* light yellow */
#define NETWORK_COLOUR      0x00E0u    /* NETWORK's pages (Sebas: not white, yellow and orange): white to pink */
#define NETWORK_CHOSEN      0x00C0u    /* and their line chosen: white to red */
#define PROMPT_Y            186        /* PRESS START, where the game's prompt is */
#define MENU_Y              124        /* the title's menu, under the logo */
#define ITEM_STEP           13         /* a menu line, the font as it is */
#define PAGE_Y              40         /* OPTIONS' pages: their first line */
#define PAGE_STEP           14
#define VERSION_Y           188        /* OPTIONS: the game's version, at the bottom right (over the title's (C) SEGA 1992) */
#define VERSION_MARGIN      3          /* and its right edge, to the screen's */
#define TITLE_HELD          0x10u      /* the title's opening frames, kept from running out (title_screen frames) */
#define TITLE_TIME          0x1C2u     /* the title's 15 seconds (mode_timer) */
#define DIFFICULTIES        8
#define TIME_STAGES         6
#define TIME_RANGE          5          /* seconds more or less */
#define SOUND_STOP          0x1Fu      /* the sound program's command: every sound off */
#define FIRST_SOUND_SET     0xD0u      /* the sound program's command: sound set 0, its own (init_parts) */
#define MENU_SONG           0          /* the menu's music, of the radio's songs: Magical Sound Shower */
#define INPUT_CELLS         16         /* INPUT TEST: a control's bar, as the test mode's input test draws it */
#define INPUT_BAR_X         96
#define INPUT_WHEEL_END     0x10       /* the wheel's travel (MAME's 10-F0) is EF-0F as the game sees it: its ends the bar's */
#define CONTROLS_Y          38         /* CONTROLS' PLAYER line */
#define CONTROLS_HEADING_Y  14
#define SIGNATURE           "@cheviman"    /* the port's author, under the title's (C) SEGA 1992 (Sebas) */
#define COPYRIGHT_PICTURE   0              /* title_pictures' (C) SEGA 1992 */
#define SIGNATURE_DROP      6              /* the signature's top, under the picture's middle */

enum
{
	PAGE_NONE,
	PAGE_MENU,
	PAGE_DELUXE,
	PAGE_OPTIONS,
	PAGE_TIME,
	PAGE_SOUND_TEST,
	PAGE_VIDEO,
	PAGE_STEERING,
	PAGE_INPUT_TEST,    /* STEERING's: the wheel and the pedals as the game sees them */
	PAGE_WHEEL_SETUP,   /* STEERING's SET UP WHEEL: a racing wheel's controls, one by one */
	PAGE_CONTROLS,
	PAGE_NETWORK,
	PAGE_NAME,
	PAGE_ROOMS,         /* JOIN: the rooms found */
	PAGE_ROOM           /* the room hosted or joined */
};

static const char *const menu_items[] = { "ARCADE", "2 PLAYERS", "NETWORK", "& KNUCKLES", "OPTIONS", "EXIT" };
static const char *const deluxe_items[] = { "CLASSIC", "TIME ATTACK", "FULL", "REVERSE", "BACK" };
static const char *const options_items[] = { "CONTROLS", "DIFFICULTY", "TIME", "VIDEO", "VOLUME", "SOUND TEST", "STEERING", "FPS",
	"BACK" };
static const char *const network_items[] = { "HOST", "JOIN", "NAME", "PLAYERS", "BACK" };
/* the letters NAME goes through: the font's (a space shown as _ while it is written) */
static const char name_letters[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-";
static const char *const time_stages[TIME_STAGES] = { "PRE-STAGE", "STAGE 1", "STAGE 2", "STAGE 3", "STAGE 4",
	"FINAL STAGE" };
static const char *const pixel_kinds[PIXELS_KINDS] = { "SHARP", "SMOOTH", "BILINEAR", "CRT" };
static const char *const wide_kinds[WIDE_KINDS] = { "OFF", "16:9", "20:9" };
static const char *const control_names[CONTROLS] = { "STEER LEFT", "STEER RIGHT", "GAS", "BRAKE", "SHIFT UP",
	"SHIFT DOWN", "MUSIC", "SONG <<", "SONG >>", "START" };
/* the test mode's GAME DIFFICULTY, from EASY to HARD on its bar of 8 (the fourth, the game's own) */
static const char *const difficulties[DIFFICULTIES] = { "VERY EASY", "EASIER", "EASY", "NORMAL", "HARD", "HARDER",
	"VERY HARD", "HARDEST" };

#define COUNT(list) ((int)(sizeof(list) / sizeof((list)[0])))

enum { MENU_ARCADE, MENU_TWO_PLAYERS, MENU_NETWORK, MENU_DELUXE, MENU_OPTIONS, MENU_EXIT };
enum { DELUXE_CLASSIC, DELUXE_TIME_ATTACK, DELUXE_FULL, DELUXE_REVERSE, DELUXE_BACK };
enum { OPTIONS_CONTROLS, OPTIONS_DIFFICULTY, OPTIONS_TIME, OPTIONS_VIDEO, OPTIONS_VOLUME, OPTIONS_SOUND_TEST,
	OPTIONS_STEERING, OPTIONS_FPS, OPTIONS_BACK };
enum { NETWORK_HOST, NETWORK_JOIN, NETWORK_NAME, NETWORK_PLAYERS, NETWORK_BACK };

/* The lines a build shows of the title's menu and of OPTIONS: a computer's, all of them. A game device's
   (ORUNNERS_HANDHELD) has no VIDEO, its picture fixed (ports/sdl2/main.c); nor CONTROLS where its controls are
   fixed (the Switch's), but where they may need putting right (ORUNNERS_CONTROLS_PAGE: a handheld's own
   buttons); nor 2 PLAYERS but where a second player has a controller of their own (ORUNNERS_TWO_PLAYERS: the
   Switch's, side by side) or the port offers it (console_offer_two_players: a tablet's screen, the two facing
   each other); nor EXIT where the system leaves games (ORUNNERS_SYSTEM_EXIT: the Switch's HOME) */
static int menu_lines[MENU_EXIT + 1];
static int menu_line_count;
#if !defined(ORUNNERS_HANDHELD) || defined(ORUNNERS_TWO_PLAYERS)
static bool two_players_offered = true;
#else
static bool two_players_offered;
#endif

static void set_menu_lines(void)
{
	menu_line_count = 0;
	menu_lines[menu_line_count++] = MENU_ARCADE;
	if (two_players_offered)
		menu_lines[menu_line_count++] = MENU_TWO_PLAYERS;
	menu_lines[menu_line_count++] = MENU_NETWORK;
	menu_lines[menu_line_count++] = MENU_DELUXE;
	menu_lines[menu_line_count++] = MENU_OPTIONS;
#ifndef ORUNNERS_SYSTEM_EXIT
	menu_lines[menu_line_count++] = MENU_EXIT;
#endif
}

static const int options_lines[] = {
#if !defined(ORUNNERS_HANDHELD) || defined(ORUNNERS_CONTROLS_PAGE)
	OPTIONS_CONTROLS,
#endif
	OPTIONS_DIFFICULTY, OPTIONS_TIME,
#ifndef ORUNNERS_HANDHELD
	OPTIONS_VIDEO,
#endif
	OPTIONS_VOLUME, OPTIONS_SOUND_TEST, OPTIONS_STEERING, OPTIONS_FPS, OPTIONS_BACK
};

/* NETWORK's lines: PLAYERS where a second player may play (2 PLAYERS offered) */
static int network_lines[NETWORK_BACK + 1];
static int network_line_count;

static void set_network_lines(void)
{
	network_line_count = 0;
	network_lines[network_line_count++] = NETWORK_HOST;
	network_lines[network_line_count++] = NETWORK_JOIN;
	network_lines[network_line_count++] = NETWORK_NAME;
	if (two_players_offered)
		network_lines[network_line_count++] = NETWORK_PLAYERS;
	network_lines[network_line_count++] = NETWORK_BACK;
	if (!two_players_offered)
		console_network_set_players(1);
}
enum { VIDEO_WINDOW, VIDEO_FULL_SCREEN, VIDEO_PIXELS, VIDEO_WIDE, VIDEO_VSYNC, VIDEO_TWO_PLAYERS, VIDEO_BACK,
	VIDEO_ITEMS };
/* STEERING's lines: CENTRING and SET UP WHEEL a computer's (a racing wheel's; a device's controls are its
   own) */
enum { STEERING_DEAD_ZONE, STEERING_SENSITIVITY, STEERING_ROTATION,
#ifndef ORUNNERS_HANDHELD
	STEERING_CENTRING,
#endif
	STEERING_SPEED, STEERING_VIBRATION,
#ifndef ORUNNERS_HANDHELD
	STEERING_SET_UP,
#endif
	STEERING_INPUT_TEST, STEERING_BACK, STEERING_ITEMS };
enum { INPUT_WHEEL, INPUT_GAS, INPUT_BRAKE, INPUT_BACK = INPUT_BRAKE + 2 };
/* CONTROLS' lines: PLAYER, the controls, DEFAULTS, BACK */
enum { CONTROLS_PLAYER, CONTROLS_FIRST, CONTROLS_DEFAULTS = CONTROLS_FIRST + CONTROLS, CONTROLS_BACK,
	CONTROLS_LINES };

static struct
{
	int page;
	int choice;             /* the line chosen on the page */
	int options_choice;     /* OPTIONS' line, kept while one of its pages is open */
	int blink;              /* frames, for PRESS START */
	int sound;              /* SOUND TEST's number: the test mode's list of sound codes */
	bool settings_changed;  /* DIFFICULTY or TIME: saved into the EEPROM when OPTIONS closes */
	int controls_player;    /* CONTROLS: whose */
	int controls_waiting;   /* CONTROLS: the control waiting for its key or button, or -1 */
	bool controls_defaults; /* CONTROLS: DEFAULTS picked, for the port */
	int wheel_setup_step;   /* SET UP WHEEL: the control it waits for (WHEEL_SETUP_*) */
	bool music;             /* the menu's song asked for */
	char name[CONSOLE_NAME_LENGTH + 1];    /* NAME: the name being written */
	int name_at;            /* NAME: the letter being changed */
} menu = { .controls_waiting = -1 };

console_window console_window_settings = { .window_scale = 2, .pixels = PIXELS_SHARP, .volume = 5, .name = "PLAYER",
	.dead_zone = DEAD_ZONE_NORMAL, .sensitivity = STEERING_NORMAL, .wheel_rotation = WHEEL_ROTATION_GAME,
	.steering_speed = STEERING_NORMAL, .centring = CENTRING_NORMAL, .vibration = VIBRATION_NORMAL, .vsync = true };

const int console_wheel_rotations[WHEEL_ROTATIONS] = { WHEEL_ROTATION_GAME, 360, 450, 540, 720, 900, 1080 };

/* ---- drawing ---- */

static void centred(int y, int scale, uint16_t colour, const char *text)
{
	console_text((SCREEN_WIDTH - console_text_width(text, scale)) / 2, y, scale, colour, text);
}

/* The colours of the page being drawn: its lines', its line chosen's and its heading's; NETWORK's pages have
   their own */
static struct
{
	uint16_t item, chosen, title;
} colours = { ITEM_COLOUR, CHOSEN_COLOUR, TITLE_COLOUR };

static void page_colours(bool network)
{
	colours.item = network ? NETWORK_COLOUR : ITEM_COLOUR;
	colours.chosen = network ? NETWORK_CHOSEN : CHOSEN_COLOUR;
	colours.title = network ? NETWORK_COLOUR : TITLE_COLOUR;
}

/* A page's line on its row: its name on the left, its value (if any) on the right, the chosen one in its
   colour */
static void line_at(int row, bool chosen, const char *name, const char *value)
{
	const uint16_t colour = chosen ? colours.chosen : colours.item;
	const int y = PAGE_Y + 16 + row * PAGE_STEP;

	console_text(48, y, 1, colour, name);
	if (value)
		console_text(SCREEN_WIDTH - 48 - console_text_width(value, 1), y, 1, colour, value);
}

/* (the line 'index' on its own row) */
static void line(int index, const char *name, const char *value)
{
	line_at(index, index == menu.choice, name, value);
}

/* The page's heading, and the darkened screen its lines are drawn over (drawn last: the list's newest group
   comes first) */
static void page_heading(const char *heading)
{
	centred(PAGE_Y - 8, 2, colours.title, heading);
}

static void darken_page(void)
{
	const int side = console_wide_side();    /* (WIDE: the screen's sides too) */

	console_shade(-side, 0, SCREEN_WIDTH + 2 * side, 224);
}

/* ---- the attract mode's objects ---- */

uint32_t console_find_object(uint32_t handler, uint32_t from)
{
	const uint32_t pool = address_of(screen) - 0x7C00u;
	uint32_t object = from ? from + 0x100u : pool;

	for (; object < address_of(screen); object += 0x100u)
		if ((OBJECT(object)->flags & 1u) != 0u && OBJECT(object)->handler == handler)
			return object;
	return 0u;
}

/* The title kept on: its 15 seconds from running out, and with OPTIONS its opening from giving way to the
   logo */
static void hold_title(bool opening)
{
	if (screen->mode_step == 2u)
		screen->mode_timer = TITLE_TIME;
	if (opening)
	{
		const uint32_t title = console_find_object(title_screen_update_ADDRESS, 0u);

		if (title && OBJECT(title)->state == 1u)
			OBJECT(title)->title_screen.frames = (uint16_t)((OBJECT(title)->title_screen.frames & 0xFF00u) | TITLE_HELD);
	}
}

/* True while the title shows its opening (the title object before its logo) */
static bool title_opening(void)
{
	const uint32_t title = console_find_object(title_screen_update_ADDRESS, 0u);

	return title && OBJECT(title)->state <= 1u;
}

/* ---- the pages ---- */

static int lines_of(int page)
{
	switch (page)
	{
		case PAGE_MENU: return menu_line_count;
		case PAGE_DELUXE: return COUNT(deluxe_items);
		case PAGE_OPTIONS: return COUNT(options_lines);
		case PAGE_TIME: return TIME_STAGES + 1;
		case PAGE_SOUND_TEST: return 2;
		case PAGE_VIDEO: return VIDEO_ITEMS;
		case PAGE_STEERING: return STEERING_ITEMS;
		case PAGE_INPUT_TEST: return 1;  /* (BACK alone: the wheel turns past the arrows there) */
		case PAGE_CONTROLS: return 0;    /* (its grid moves its own way: controls_move) */
		case PAGE_NETWORK: return network_line_count;
		case PAGE_NAME: return 0;        /* (its letters move their own way: name_frame) */
		case PAGE_ROOMS: return console_network_room_count() + 1;
		case PAGE_ROOM: return console_network_hosting() ? 2 : 1;
		default: return 0;
	}
}

static void open_page(int page, int choice)
{
	menu.page = page;
	menu.choice = choice;
}

/* The line of the title's menu or of OPTIONS an item is on, of those the build shows */
static int line_of(const int *lines, int count, int item)
{
	for (int index = 0; index < count; index++)
		if (lines[index] == item)
			return index;
	return 0;
}

#define MENU_LINE(item)    line_of(menu_lines, menu_line_count, item)
#define NETWORK_LINE(item) line_of(network_lines, network_line_count, item)

static int step(int value, int by, int low, int high)
{
	value += by;
	return value < low ? high : (value > high ? low : value);
}

/* LEFT or RIGHT on a line with a value: -1, +1 or 0 */
static int change(uint32_t pressed)
{
	return (pressed & ACTION_BIT(ACTION_LEFT)) ? -1 : ((pressed & ACTION_BIT(ACTION_RIGHT)) ? 1 : 0);
}

/* The settings DIFFICULTY and TIME changed, into the EEPROM as the test mode's exit saves them */
static void save_game_settings(void)
{
	if (!menu.settings_changed)
		return;
	menu.settings_changed = false;
	console_settings_to_race();
	save_settings();
}

static void play_sound_test(void)
{
	const int number = menu.sound;

	queue_sound(SOUND_STOP);
	if (number >= 0x40)
		queue_sound(number >= 0x6B ? 0xD8u : (number >= 0x66 ? 0xDAu : 0xD6u));    /* (its sound set) */
	queue_sound(sound_test_codes[number]);
}

static int sound_test_count(void)
{
	int count = 0;

	while (sound_test_codes[count] != 0u)
		count++;
	return count;
}

/* OPTIONS' lines: LEFT and RIGHT change those with a value */
static void options_frame(uint32_t pressed)
{
	char value[16];
	const int by = change(pressed);
	const int item = options_lines[menu.choice];

	if (by && item == OPTIONS_DIFFICULTY)
	{
		game->game_difficulty = (uint8_t)step(game->game_difficulty, by, 0, DIFFICULTIES - 1);
		menu.settings_changed = true;
	}
	if (by && item == OPTIONS_VOLUME)
	{
		console_window_settings.volume = step(console_window_settings.volume, by, 0, 10);
		console_window_settings.changed = true;
	}
	if (item == OPTIONS_FPS && (by || (pressed & ACTION_BIT(ACTION_ACCEPT))))
	{
		console_window_settings.fps = !console_window_settings.fps;
		console_window_settings.changed = true;
	}
	if (pressed & ACTION_BIT(ACTION_ACCEPT))
		switch (item)
		{
			case OPTIONS_CONTROLS:
				menu.options_choice = menu.choice;
				open_page(PAGE_CONTROLS, CONTROLS_FIRST);
				return;
			case OPTIONS_TIME:
				menu.options_choice = menu.choice;
				open_page(PAGE_TIME, 0);
				return;
			case OPTIONS_SOUND_TEST:
				menu.options_choice = menu.choice;
				open_page(PAGE_SOUND_TEST, 0);
				return;
			case OPTIONS_VIDEO:
				menu.options_choice = menu.choice;
				open_page(PAGE_VIDEO, 0);
				return;
			case OPTIONS_STEERING:
				menu.options_choice = menu.choice;
				open_page(PAGE_STEERING, 0);
				return;
			case OPTIONS_BACK:
				save_game_settings();
				open_page(PAGE_MENU, MENU_LINE(MENU_OPTIONS));
				return;
			default:
				break;
		}
	page_heading("OPTIONS");
	for (int index = 0; index < COUNT(options_lines); index++)
	{
		const char *shown = NULL;

		if (options_lines[index] == OPTIONS_DIFFICULTY)
		{
			snprintf(value, sizeof(value), "< %s >", difficulties[game->game_difficulty & (DIFFICULTIES - 1)]);
			shown = value;
		}
		else if (options_lines[index] == OPTIONS_VOLUME)
		{
			snprintf(value, sizeof(value), "< %d >", console_window_settings.volume);
			shown = value;
		}
		else if (options_lines[index] == OPTIONS_FPS)
			shown = console_window_settings.fps ? "< ON >" : "< OFF >";
		line(index, options_items[options_lines[index]], shown);
	}
	/* the game's version at the bottom right (of WIDE's wider screen too) */
	console_text(SCREEN_WIDTH + console_wide_side() - VERSION_MARGIN - console_text_width("V" ORUNNERS_VERSION, 1), VERSION_Y, 1,
		ITEM_COLOUR, "V" ORUNNERS_VERSION);
	darken_page();
}

static void time_frame(uint32_t pressed)
{
	char value[16];
	const int by = change(pressed);

	if (by && menu.choice < TIME_STAGES)
	{
		game->time_difficulty[menu.choice] = (int8_t)step(game->time_difficulty[menu.choice], by, -TIME_RANGE, TIME_RANGE);
		menu.settings_changed = true;
	}
	if ((pressed & ACTION_BIT(ACTION_ACCEPT)) && menu.choice == TIME_STAGES)
	{
		open_page(PAGE_OPTIONS, menu.options_choice);
		return;
	}
	page_heading("TIME");
	for (int index = 0; index < TIME_STAGES; index++)
	{
		snprintf(value, sizeof(value), "< %+d >", game->time_difficulty[index]);
		line(index, time_stages[index], value);
	}
	line(TIME_STAGES, "BACK", NULL);
	darken_page();
}

static void sound_test_frame(uint32_t pressed)
{
	char value[16];
	const int by = change(pressed);

	if (by && menu.choice == 0)
		menu.sound = step(menu.sound, by, 0, sound_test_count() - 1);
	if (pressed & ACTION_BIT(ACTION_ACCEPT))
	{
		if (menu.choice == 0)
			play_sound_test();
		else
		{
			queue_sound(SOUND_STOP);
			queue_sound(FIRST_SOUND_SET);    /* (the game's sounds from set 0 again) */
			menu.music = false;    /* (the menu's song again) */
			open_page(PAGE_OPTIONS, menu.options_choice);
			return;
		}
	}
	page_heading("SOUND TEST");
	snprintf(value, sizeof(value), "< %03d >", menu.sound + 1);
	line(0, "SOUND", value);
	line(1, "BACK", NULL);
	darken_page();
}

static void video_frame(uint32_t pressed)
{
	console_window *window = &console_window_settings;
	char value[16];
	const int by = change(pressed);
	const bool accept = (pressed & ACTION_BIT(ACTION_ACCEPT)) != 0;

	if (by || accept)
		switch (menu.choice)
		{
			case VIDEO_WINDOW:
				window->window_scale = step(window->window_scale, by ? by : 1, 1, 6);
				window->changed = true;
				break;
			case VIDEO_FULL_SCREEN:
				window->full_screen = !window->full_screen;
				window->changed = true;
				break;
			case VIDEO_PIXELS:
				window->pixels = step(window->pixels, by ? by : 1, 0, PIXELS_KINDS - 1);
				window->changed = true;
				break;
			case VIDEO_WIDE:
				window->wide = step(window->wide, by ? by : 1, 0, WIDE_KINDS - 1);
				window->changed = true;
				break;
			case VIDEO_VSYNC:
				window->vsync = !window->vsync;
				window->changed = true;
				break;
			case VIDEO_TWO_PLAYERS:
				window->vertical = !window->vertical;
				window->changed = true;
				break;
			case VIDEO_BACK:
				if (accept)
				{
					open_page(PAGE_OPTIONS, menu.options_choice);
					return;
				}
				break;
		}
	page_heading("VIDEO");
	snprintf(value, sizeof(value), "< %dX >", window->window_scale);
	line(VIDEO_WINDOW, "WINDOW", value);
	line(VIDEO_FULL_SCREEN, "FULL SCREEN", window->full_screen ? "< ON >" : "< OFF >");
	snprintf(value, sizeof(value), "< %s >", pixel_kinds[window->pixels]);
	line(VIDEO_PIXELS, "PIXELS", value);
	snprintf(value, sizeof(value), "< %s >", wide_kinds[window->wide]);
	line(VIDEO_WIDE, "WIDE", value);
	line(VIDEO_VSYNC, "VSYNC", window->vsync ? "< ON >" : "< OFF >");
	line(VIDEO_TWO_PLAYERS, "2 PLAYERS", window->vertical ? "< VERTICAL >" : "< HORIZONTAL >");
	line(VIDEO_BACK, "BACK", NULL);
	darken_page();
}

/* A force's value on STEERING: its percent, OFF at 0 */
static void force_value(char *value, size_t size, int percent)
{
	if (percent == 0)
		snprintf(value, size, "< OFF >");
	else
		snprintf(value, size, "< %d%% >", percent);
}

/* STEERING: the sticks' dead zone and sensitivity, a racing wheel's rotation and centring, the keys' and pads'
   speed, the wheel motor's vibration (ports/sdl2/main.c, forces.c) */
static void steering_frame(uint32_t pressed)
{
	console_window *window = &console_window_settings;
	char value[16];
	const int by = change(pressed);
	int rotation = 0;

	while (rotation < WHEEL_ROTATIONS - 1 && console_wheel_rotations[rotation] != window->wheel_rotation)
		rotation++;
	switch (by ? menu.choice : STEERING_ITEMS)
	{
		case STEERING_DEAD_ZONE:
			window->dead_zone = step(window->dead_zone, by * DEAD_ZONE_STEP, 0, DEAD_ZONE_MOST);
			window->changed = true;
			break;
		case STEERING_SENSITIVITY:
			window->sensitivity = step(window->sensitivity, by, 1, STEERING_LEVELS);
			window->changed = true;
			break;
		case STEERING_ROTATION:
			window->wheel_rotation = console_wheel_rotations[step(rotation, by, 0, WHEEL_ROTATIONS - 1)];
			window->changed = true;
			break;
		case STEERING_SPEED:
			window->steering_speed = step(window->steering_speed, by, 1, STEERING_LEVELS);
			window->changed = true;
			break;
#ifndef ORUNNERS_HANDHELD
		case STEERING_CENTRING:
			window->centring = step(window->centring, by * FORCE_STEP, 0, 100);
			window->changed = true;
			break;
#endif
		case STEERING_VIBRATION:
			window->vibration = step(window->vibration, by * FORCE_STEP, 0, 100);
			window->changed = true;
			break;
		default:
			break;
	}
	if ((pressed & ACTION_BIT(ACTION_ACCEPT)) && menu.choice == STEERING_INPUT_TEST)
	{
		open_page(PAGE_INPUT_TEST, 0);
		return;
	}
#ifndef ORUNNERS_HANDHELD
	if ((pressed & ACTION_BIT(ACTION_ACCEPT)) && menu.choice == STEERING_SET_UP)
	{
		menu.wheel_setup_step = WHEEL_SETUP_TURN;
		open_page(PAGE_WHEEL_SETUP, 0);
		return;
	}
#endif
	if ((pressed & ACTION_BIT(ACTION_ACCEPT)) && menu.choice == STEERING_BACK)
	{
		open_page(PAGE_OPTIONS, menu.options_choice);
		return;
	}
	page_heading("STEERING");
	snprintf(value, sizeof(value), "< %d%% >", window->dead_zone);
	line(STEERING_DEAD_ZONE, "DEAD ZONE", value);
	snprintf(value, sizeof(value), "< %d >", window->sensitivity);
	line(STEERING_SENSITIVITY, "SENSITIVITY", value);
	snprintf(value, sizeof(value), "< %d >", window->wheel_rotation);
	line(STEERING_ROTATION, "WHEEL ROTATION", value);
#ifndef ORUNNERS_HANDHELD
	force_value(value, sizeof(value), window->centring);
	line(STEERING_CENTRING, "CENTRING", value);
#endif
	snprintf(value, sizeof(value), "< %d >", window->steering_speed);
	line(STEERING_SPEED, "SPEED", value);
	force_value(value, sizeof(value), window->vibration);
	line(STEERING_VIBRATION, "VIBRATION", value);
#ifndef ORUNNERS_HANDHELD
	line(STEERING_SET_UP, "SET UP WHEEL", NULL);
#endif
	line(STEERING_INPUT_TEST, "INPUT TEST", NULL);
	line(STEERING_BACK, "BACK", NULL);
	darken_page();
}

/* An INPUT TEST control: its name, the mark on a bar of INPUT_CELLS ('across': 0 at the left) and how far it
   is, in percent (Sebas: not the test mode's hexadecimal) */
static void input_bar(int row, const char *name, uint8_t across, int percent)
{
	const int y = PAGE_Y + 16 + row * PAGE_STEP;
	const int cell = across * INPUT_CELLS / 0x100;
	char bar[INPUT_CELLS + 3], number[8];

	bar[0] = '[';
	for (int at = 0; at < INPUT_CELLS; at++)
		bar[1 + at] = at == cell ? ' ' : '-';
	bar[INPUT_CELLS + 1] = ']';
	bar[INPUT_CELLS + 2] = '\0';
	snprintf(number, sizeof(number), "%d%%", percent);
	console_text(48, y, 1, colours.item, name);
	console_text(INPUT_BAR_X, y, 1, colours.item, bar);
	console_text(INPUT_BAR_X + (1 + cell) * 8, y, 1, colours.chosen, "#");
	console_text(SCREEN_WIDTH - 32 - console_text_width(number, 1), y, 1, colours.item, number);
}

/* INPUT TEST: player 1's wheel, gas and brake as the game sees them (the screen's, through the calibration),
   as the test mode's input test draws them, to see where a racing wheel reaches its ends and a pedal its
   floor (issue 16: WHEEL ROTATION's); but the wheel the way it turns, its left at the bar's left (the game's
   left is FF) and in percent of its turn, the left's under 0. The arrows do nothing here, the wheel's turn
   passing them */
static void input_test_frame(uint32_t pressed)
{
	const struct screen *seat = SCREEN(SCREEN_1);
	int turn = (0xFF - seat->wheel - INPUT_WHEEL_END) * 0x100 / (0x100 - 2 * INPUT_WHEEL_END);

	if (pressed & ACTION_BIT(ACTION_ACCEPT))
	{
		open_page(PAGE_STEERING, STEERING_INPUT_TEST);
		return;
	}
	turn = turn < 0 ? 0 : (turn > 0xFF ? 0xFF : turn);
	page_heading("INPUT TEST");
	input_bar(INPUT_WHEEL, "WHEEL", (uint8_t)turn, (turn - 0x80) * 100 / 0x7F);
	input_bar(INPUT_GAS, "GAS", seat->gas, seat->gas * 100 / 0xFF);
	input_bar(INPUT_BRAKE, "BRAKE", seat->brake, seat->brake * 100 / 0xFF);
	line_at(INPUT_BACK, true, "BACK", NULL);
	darken_page();
}

/* SET UP WHEEL: the controls it asks for, the one it waits for chosen and those taken OK, and under them how
   to show it; the port watches the devices meanwhile (console_wheel_setup_step), the menus' actions unheard */
static const char *const wheel_setup_names[WHEEL_SETUP_STEPS] = { "WHEEL", "GAS", "BRAKE", "SHIFT UP",
	"SHIFT DOWN", "START" };
static const char *const wheel_setup_hints[WHEEL_SETUP_STEPS] = { "TURN THE WHEEL LEFT",
	"PRESS THE GAS AND LET GO", "PRESS THE BRAKE AND LET GO", "PRESS SHIFT UP", "PRESS SHIFT DOWN",
	"PRESS START" };

static void wheel_setup_frame(void)
{
	const int hint_y = PAGE_Y + 16 + (WHEEL_SETUP_STEPS + 1) * PAGE_STEP;

	page_heading("SET UP WHEEL");
	for (int step = 0; step < WHEEL_SETUP_STEPS; step++)
		line_at(step, step == menu.wheel_setup_step, wheel_setup_names[step],
			step < menu.wheel_setup_step ? "OK" : NULL);
	centred(hint_y, 1, colours.chosen, wheel_setup_hints[menu.wheel_setup_step]);
	centred(hint_y + 2 * PAGE_STEP, 1, colours.item, "ESCAPE CANCELS");
	darken_page();
}

int console_wheel_setup_step(void)
{
	return menu.page == PAGE_WHEEL_SETUP ? menu.wheel_setup_step : -1;
}

void console_wheel_setup_took(void)
{
	if (menu.page != PAGE_WHEEL_SETUP)
		return;
	if (++menu.wheel_setup_step == WHEEL_SETUP_STEPS)
		open_page(PAGE_INPUT_TEST, 0);    /* (the wheel set up: where it reaches, and the pedals) */
}

void console_wheel_setup_cancel(void)
{
#ifndef ORUNNERS_HANDHELD
	if (menu.page == PAGE_WHEEL_SETUP)
		open_page(PAGE_STEERING, STEERING_SET_UP);
#endif
}

/* CONTROLS: PLAYER on top, the controls in two columns of five (each its name, and under it the pictures of
   its keys and button, the port's), DEFAULTS and BACK side by side at the bottom. The arrows move about it */
#define CONTROLS_COLUMN_ROWS  5
#define CONTROLS_CELL_Y       58     /* the first row of controls: their names */
#define CONTROLS_CELL_STEP    30     /* a name, its pictures under it, a gap */
#define CONTROLS_PICTURES_Y   10     /* under the name */
#define CONTROLS_BOTTOM_Y     210    /* DEFAULTS and BACK */

static const int controls_column_x[2] = { 28, 172 };

static void control_place(int control, int *x, int *y)
{
	*x = controls_column_x[control / CONTROLS_COLUMN_ROWS];
	*y = CONTROLS_CELL_Y + (control % CONTROLS_COLUMN_ROWS) * CONTROLS_CELL_STEP;
}

/* The arrows on the grid: up and down within a column (to PLAYER above it, to DEFAULTS or BACK under it),
   left and right across the columns (on PLAYER: the player) */
static void controls_move(uint32_t pressed)
{
	const int choice = menu.choice;
	const int control = choice - CONTROLS_FIRST;
	const bool on_control = choice >= CONTROLS_FIRST && choice < CONTROLS_DEFAULTS;
	const int column = on_control ? control / CONTROLS_COLUMN_ROWS : (choice == CONTROLS_BACK ? 1 : 0);

	if (pressed & ACTION_BIT(ACTION_UP))
	{
		if (choice == CONTROLS_DEFAULTS || choice == CONTROLS_BACK)
			menu.choice = CONTROLS_FIRST + column * CONTROLS_COLUMN_ROWS + CONTROLS_COLUMN_ROWS - 1;
		else if (on_control && control % CONTROLS_COLUMN_ROWS != 0)
			menu.choice = choice - 1;
		else if (on_control)
			menu.choice = CONTROLS_PLAYER;
		else
			menu.choice = CONTROLS_DEFAULTS;    /* (from PLAYER round to the bottom) */
	}
	else if (pressed & ACTION_BIT(ACTION_DOWN))
	{
		if (choice == CONTROLS_PLAYER)
			menu.choice = CONTROLS_FIRST;
		else if (on_control && control % CONTROLS_COLUMN_ROWS != CONTROLS_COLUMN_ROWS - 1)
			menu.choice = choice + 1;
		else if (on_control)
			menu.choice = column ? CONTROLS_BACK : CONTROLS_DEFAULTS;
		else
			menu.choice = CONTROLS_PLAYER;
	}
	else if (change(pressed))
	{
		if (choice == CONTROLS_PLAYER)
			menu.controls_player = step(menu.controls_player, change(pressed), 0, CONTROL_PLAYERS - 1);
		else if (on_control)
			menu.choice = CONTROLS_FIRST + (control + CONTROLS_COLUMN_ROWS) % CONTROLS;
		else
			menu.choice = choice == CONTROLS_DEFAULTS ? CONTROLS_BACK : CONTROLS_DEFAULTS;
	}
}

static void controls_frame(uint32_t pressed)
{
	char value[24];
	int x, y;

	centred(CONTROLS_HEADING_Y, 2, TITLE_COLOUR, "CONTROLS");
	if (menu.controls_waiting >= 0)
	{
		/* the port takes the next key or button (console_controls_bound) */
		control_place(menu.controls_waiting, &x, &y);
		console_text(x, y, 1, CHOSEN_COLOUR, control_names[menu.controls_waiting]);
		centred(CONTROLS_BOTTOM_Y, 1, TITLE_COLOUR, "PRESS A KEY OR BUTTON");
		darken_page();
		return;
	}
	controls_move(pressed);
	if (pressed & ACTION_BIT(ACTION_ACCEPT))
	{
		if (menu.choice >= CONTROLS_FIRST && menu.choice < CONTROLS_DEFAULTS)
		{
			menu.controls_waiting = menu.choice - CONTROLS_FIRST;
			return;
		}
		if (menu.choice == CONTROLS_DEFAULTS)
			menu.controls_defaults = true;
		if (menu.choice == CONTROLS_BACK)
		{
			open_page(PAGE_OPTIONS, menu.options_choice);
			return;
		}
	}
	snprintf(value, sizeof(value), "< PLAYER %d >", menu.controls_player + 1);
	centred(CONTROLS_Y, 1, menu.choice == CONTROLS_PLAYER ? CHOSEN_COLOUR : ITEM_COLOUR, value);
	for (int control = 0; control < CONTROLS; control++)
	{
		control_place(control, &x, &y);
		console_text(x, y, 1, menu.choice == CONTROLS_FIRST + control ? CHOSEN_COLOUR : ITEM_COLOUR,
			control_names[control]);
	}
	console_text(controls_column_x[0], CONTROLS_BOTTOM_Y, 1,
		menu.choice == CONTROLS_DEFAULTS ? CHOSEN_COLOUR : ITEM_COLOUR, "DEFAULTS");
	console_text(controls_column_x[1], CONTROLS_BOTTOM_Y, 1,
		menu.choice == CONTROLS_BACK ? CHOSEN_COLOUR : ITEM_COLOUR, "BACK");
	darken_page();
}

bool console_controls_place(int control, int *x, int *y)
{
	if (menu.page != PAGE_CONTROLS || menu.controls_waiting >= 0 || control < 0 || control >= CONTROLS)
		return false;
	control_place(control, x, y);
	*y += CONTROLS_PICTURES_Y;
	return true;
}

int console_controls_player(void)
{
	return menu.controls_player;
}

int console_controls_waiting(void)
{
	return menu.page == PAGE_CONTROLS ? menu.controls_waiting : -1;
}

void console_controls_bound(void)
{
	menu.controls_waiting = -1;
}

bool console_controls_defaults(void)
{
	const bool picked = menu.controls_defaults;

	menu.controls_defaults = false;
	return picked;
}

/* ---- NETWORK ---- */

/* NETWORK: HOST, JOIN, NAME, PLAYERS, BACK; what went wrong last under them */
static void network_frame(uint32_t pressed)
{
	const int item = network_lines[menu.choice];
	const char *problem = console_network_problem();
	char value[24];

	if (pressed & (ACTION_BIT(ACTION_UP) | ACTION_BIT(ACTION_DOWN) | ACTION_BIT(ACTION_ACCEPT)))
		console_network_problem_seen();
	if (change(pressed) && item == NETWORK_PLAYERS)
		console_network_set_players(console_network_players() == 1 ? 2 : 1);
	if (pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START)))
		switch (item)
		{
			case NETWORK_HOST:
				console_network_host();
				if (console_network_in_room())
					open_page(PAGE_ROOM, 0);
				return;
			case NETWORK_JOIN:
				console_network_search();
				if (console_network_searching())
					open_page(PAGE_ROOMS, 0);
				return;
			case NETWORK_NAME:
				snprintf(menu.name, sizeof(menu.name), "%-*s", CONSOLE_NAME_LENGTH, console_window_settings.name);
				menu.name_at = 0;
				open_page(PAGE_NAME, 0);
				return;
			case NETWORK_PLAYERS:
				console_network_set_players(console_network_players() == 1 ? 2 : 1);
				break;
			case NETWORK_BACK:
				open_page(PAGE_MENU, MENU_LINE(MENU_NETWORK));
				return;
		}
	page_heading("NETWORK");
	for (int index = 0; index < network_line_count; index++)
	{
		const char *shown = NULL;

		if (network_lines[index] == NETWORK_NAME)
			shown = console_window_settings.name;
		else if (network_lines[index] == NETWORK_PLAYERS)
		{
			snprintf(value, sizeof(value), "< %d >", console_network_players());
			shown = value;
		}
		line(index, network_items[network_lines[index]], shown);
	}
	if (problem)
		centred(PAGE_Y + 16 + (network_line_count + 1) * PAGE_STEP, 1, colours.chosen, problem);
	darken_page();
}

/* The place of a letter in name_letters (0, the space, for one not there) */
static int letter_of(char letter)
{
	for (int index = 0; name_letters[index]; index++)
		if (name_letters[index] == letter)
			return index;
	return 0;
}

/* NAME: UP and DOWN change the letter, LEFT and RIGHT go to the one beside it, ACCEPT keeps the name, BACK
   leaves it as it was */
static void name_frame(uint32_t pressed)
{
	const int letters = (int)sizeof(name_letters) - 1;
	int x = (SCREEN_WIDTH - CONSOLE_NAME_LENGTH * 16) / 2;
	char shown[2] = { 0, 0 };

	if (pressed & ACTION_BIT(ACTION_BACK))
	{
		open_page(PAGE_NETWORK, NETWORK_LINE(NETWORK_NAME));
		return;
	}
	if (pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START)))
	{
		int length = CONSOLE_NAME_LENGTH;

		while (length > 0 && menu.name[length - 1] == ' ')
			length--;
		if (length > 0)
		{
			snprintf(console_window_settings.name, sizeof(console_window_settings.name), "%.*s", length, menu.name);
			console_window_settings.changed = true;    /* (the port keeps it) */
		}
		open_page(PAGE_NETWORK, NETWORK_LINE(NETWORK_NAME));
		return;
	}
	if (pressed & (ACTION_BIT(ACTION_UP) | ACTION_BIT(ACTION_DOWN)))
		menu.name[menu.name_at] = name_letters[step(letter_of(menu.name[menu.name_at]),
			(pressed & ACTION_BIT(ACTION_UP)) ? 1 : -1, 0, letters - 1)];
	menu.name_at = step(menu.name_at, change(pressed), 0, CONSOLE_NAME_LENGTH - 1);
	page_heading("NAME");
	for (int at = 0; at < CONSOLE_NAME_LENGTH; at++, x += 16)
	{
		shown[0] = menu.name[at] == ' ' ? '_' : menu.name[at];
		console_text(x, PAGE_Y + 40, 2, at == menu.name_at ? colours.chosen : colours.item, shown);
	}
	darken_page();
}

/* JOIN: the rooms the local network has (their host's name, their devices), and BACK */
static void rooms_frame(uint32_t pressed)
{
	const int rooms = console_network_room_count();
	char text[32];

	if (!console_network_searching())
	{
		open_page(PAGE_NETWORK, NETWORK_LINE(NETWORK_JOIN));
		return;
	}
	if ((pressed & ACTION_BIT(ACTION_BACK))
		|| ((pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START))) && menu.choice == rooms))
	{
		console_network_leave();
		open_page(PAGE_NETWORK, NETWORK_LINE(NETWORK_JOIN));
		return;
	}
	if ((pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START))) && menu.choice < rooms
		&& !console_network_room_full(menu.choice))
	{
		console_network_join(menu.choice);
		open_page(PAGE_ROOM, 0);
		return;
	}
	page_heading("JOIN");
	for (int room = 0; room < rooms; room++)
	{
		console_network_room(room, text, sizeof(text));
		line(room, text, NULL);
	}
	line(rooms, "BACK", NULL);
	if (rooms == 0 && (menu.blink++ & 16) == 0)
		centred(PAGE_Y + 16 + 2 * PAGE_STEP, 1, colours.item, "LOOKING FOR ROOMS");
	darken_page();
}

/* The room: its devices, then the host's START (two devices or more, none racing) and LEAVE */
static void room_frame(uint32_t pressed)
{
	const bool host = console_network_hosting();
	const int members = console_network_member_count();
	const int y = PAGE_Y + 16 + 5 * PAGE_STEP;    /* (under four devices and a gap) */
	char text[32];

	if (!console_network_in_room())
	{
		open_page(PAGE_NETWORK, 0);
		return;
	}
	if ((pressed & ACTION_BIT(ACTION_BACK))
		|| ((pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START))) && menu.choice == (host ? 1 : 0)))
	{
		console_network_leave();
		open_page(PAGE_NETWORK, 0);
		return;
	}
	if ((pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START))) && host && menu.choice == 0
		&& console_network_can_start())
	{
		console_network_start();    /* (the race on the left seat's next frame: check_game_start) */
		return;
	}
	page_heading(console_network_joining() ? "JOINING" : "ROOM");
	for (int index = 0; index < members; index++)
	{
		console_network_member(index, text, sizeof(text));
		console_text(64, PAGE_Y + 16 + index * PAGE_STEP, 1, colours.title, text);
	}
	if (host)
	{
		line_at(5, menu.choice == 0, console_network_can_start() ? "START" : "START: 2 OR MORE", NULL);
		line_at(6, menu.choice == 1, "LEAVE", NULL);
	}
	else
	{
		if ((menu.blink++ & 16) == 0)
			centred(y - PAGE_STEP, 1, colours.item, console_network_joining() ? "WAITING FOR THE ROOM" : "WAITING FOR THE HOST");
		line_at(5, true, "LEAVE", NULL);
	}
	darken_page();
}

/* The title's menu and DELUXE: their lines centred under the logo, over a shadow (the items lines lists, or
   all of them) */
static void list_frame(const char *const *items, const int *lines, int count)
{
	const int height = count * ITEM_STEP;
	int widest = 0;

	for (int index = 0; index < count; index++)
	{
		const char *const name = items[lines ? lines[index] : index];
		const int width = console_text_width(name, 1);

		widest = width > widest ? width : widest;
		centred(MENU_Y + index * ITEM_STEP, 1, index == menu.choice ? CHOSEN_COLOUR : ITEM_COLOUR, name);
	}
	console_shade((SCREEN_WIDTH - widest) / 2 - 24, MENU_Y - 8, widest + 48, height + 11);
}

/* ---- the frame ---- */

/* OPTIONS and the pages it opens */
static bool options_page(int page)
{
	return page == PAGE_OPTIONS || page == PAGE_TIME || page == PAGE_SOUND_TEST || page == PAGE_VIDEO
		|| page == PAGE_STEERING || page == PAGE_INPUT_TEST || page == PAGE_WHEEL_SETUP || page == PAGE_CONTROLS;
}

/* The port's author under the title's (C) SEGA 1992, while the title shows it (not under OPTIONS' pages,
   which the version's corner is: Sebas) */
static void draw_signature(void)
{
	for (uint32_t picture = console_find_object(title_picture_draw_ADDRESS, 0u); picture;
		picture = console_find_object(title_picture_draw_ADDRESS, picture))
		if (OBJECT(picture)->title_picture.index == COPYRIGHT_PICTURE && OBJECT(picture)->state != 0u)
		{
			console_text((int16_t)OBJECT(picture)->x - console_text_width(SIGNATURE, 1) / 2,
				(int16_t)OBJECT(picture)->y + SIGNATURE_DROP, 1, ITEM_COLOUR, SIGNATURE);
			return;
		}
}

void console_offer_two_players(bool offered)
{
	two_players_offered = offered;
}

int console_menu_frame(uint32_t mode)
{
	const uint32_t pressed = console.pressed;
	int lines;

	set_menu_lines();
	set_network_lines();
	lines = lines_of(menu.page);
	if (lines && menu.choice >= lines)
		menu.choice = lines - 1;    /* (2 PLAYERS gone from it, a room gone from JOIN's) */
	console.pressed = 0;
	if (mode == GAME_MODE_PLAY || mode == GAME_MODE_POWER_ON || mode == GAME_MODE_RESTART)
	{
		if (mode == GAME_MODE_PLAY)
		{
			menu.page = PAGE_NONE;
			menu.music = false;    /* (the game's own from its car select on) */
		}
		return MENU_NOTHING;
	}
	if (!options_page(menu.page))
		draw_signature();

	/* in a network room, its page over the title's opening (after a race too) */
	if (console_network_in_room() && menu.page != PAGE_ROOM)
	{
		open_page(PAGE_ROOM, 0);
		if (mode != GAME_MODE_TITLE || !title_opening())
			return MENU_TITLE;
	}

	/* the attract mode: PRESS START, the menu on the title */
	if (menu.page == PAGE_NONE)
	{
		if ((++menu.blink & 16) == 0)
			centred(PROMPT_Y, 1, CHOSEN_COLOUR, "PRESS START");
		if (pressed & (ACTION_BIT(ACTION_START) | ACTION_BIT(ACTION_ACCEPT)))
		{
			open_page(PAGE_MENU, MENU_ARCADE);
			if (mode != GAME_MODE_TITLE)
				return MENU_TITLE;
			queue_sound(MENU_SOUND);
		}
		return MENU_NOTHING;
	}
	if (mode != GAME_MODE_TITLE)
		return MENU_NOTHING;    /* (the title coming back from another mode: the menu stays open) */

	/* the menu's song while it is open, and a sound for each line changed or picked on the title's menu and
	   & KNUCKLES (OPTIONS' pages go without: it would never stop) */
	if (!menu.music)
	{
		menu.music = true;
		queue_sound((uint8_t)(radio_songs[MENU_SONG] >> 24));
	}
	if ((menu.page == PAGE_MENU || menu.page == PAGE_DELUXE)
		&& (pressed & (ACTION_BIT(ACTION_UP) | ACTION_BIT(ACTION_DOWN) | ACTION_BIT(ACTION_ACCEPT)
			| ACTION_BIT(ACTION_START) | ACTION_BIT(ACTION_BACK))))
		queue_sound(MENU_SOUND);

	if (menu.page == PAGE_CONTROLS && menu.controls_waiting >= 0)
	{
		/* (the port takes the keys and buttons meanwhile) */
		hold_title(true);
		controls_frame(0);
		return MENU_NOTHING;
	}
	if (menu.page == PAGE_WHEEL_SETUP)
	{
		/* (the port watches the devices meanwhile: their buttons are no actions) */
		hold_title(true);
		page_colours(false);
		wheel_setup_frame();
		return MENU_NOTHING;
	}
	if (lines)
	{
		if (pressed & ACTION_BIT(ACTION_UP))
			menu.choice = step(menu.choice, -1, 0, lines - 1);
		if (pressed & ACTION_BIT(ACTION_DOWN))
			menu.choice = step(menu.choice, 1, 0, lines - 1);
	}
	switch (menu.page)
	{
		case PAGE_MENU:
			hold_title(false);
			if (pressed & ACTION_BIT(ACTION_BACK))
			{
				open_page(PAGE_NONE, 0);
				fade_out_music();
				menu.music = false;
				return MENU_NOTHING;
			}
			if (pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START)))
				switch (menu_lines[menu.choice])
				{
					case MENU_ARCADE:
						open_page(PAGE_NONE, 0);
						return MENU_START_ONE;
					case MENU_TWO_PLAYERS:
						open_page(PAGE_NONE, 0);
						return MENU_START_TWO;
					case MENU_NETWORK:
						open_page(PAGE_NETWORK, 0);
						return title_opening() ? MENU_NOTHING : MENU_TITLE;
					case MENU_DELUXE:
						open_page(PAGE_DELUXE, 0);
						return MENU_NOTHING;
					case MENU_OPTIONS:
						open_page(PAGE_OPTIONS, 0);
						return title_opening() ? MENU_NOTHING : MENU_TITLE;
					case MENU_EXIT:
						console.quit = true;
						return MENU_NOTHING;
				}
			list_frame(menu_items, menu_lines, menu_line_count);
			return MENU_NOTHING;
		case PAGE_DELUXE:
			hold_title(false);
			if ((pressed & ACTION_BIT(ACTION_BACK))
				|| ((pressed & ACTION_BIT(ACTION_ACCEPT)) && menu.choice == DELUXE_BACK))
			{
				open_page(PAGE_MENU, MENU_LINE(MENU_DELUXE));
				return MENU_NOTHING;
			}
			if ((pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START))) && menu.choice == DELUXE_TIME_ATTACK)
			{
				open_page(PAGE_NONE, 0);
				return MENU_TIME_ATTACK;
			}
			if ((pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START))) && menu.choice == DELUXE_FULL)
			{
				open_page(PAGE_NONE, 0);
				return MENU_FULL;
			}
			if ((pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START))) && menu.choice == DELUXE_CLASSIC)
			{
				open_page(PAGE_NONE, 0);
				return MENU_CLASSIC;
			}
			if ((pressed & (ACTION_BIT(ACTION_ACCEPT) | ACTION_BIT(ACTION_START))) && menu.choice == DELUXE_REVERSE)
			{
				open_page(PAGE_NONE, 0);
				return MENU_REVERSE;    /* (its goal on the route map once the car is chosen) */
			}
			list_frame(deluxe_items, NULL, COUNT(deluxe_items));
			return MENU_NOTHING;
		default:
			break;
	}

	/* NETWORK and its pages (in their own colours), and OPTIONS and its, over the title's opening */
	hold_title(true);
	page_colours(menu.page == PAGE_NETWORK || menu.page == PAGE_NAME || menu.page == PAGE_ROOMS
		|| menu.page == PAGE_ROOM);
	switch (menu.page)
	{
		case PAGE_NETWORK:
			if (pressed & ACTION_BIT(ACTION_BACK))
				open_page(PAGE_MENU, MENU_LINE(MENU_NETWORK));
			else
				network_frame(pressed);
			return MENU_NOTHING;
		case PAGE_NAME:
			name_frame(pressed);
			return MENU_NOTHING;
		case PAGE_ROOMS:
			rooms_frame(pressed);
			return MENU_NOTHING;
		case PAGE_ROOM:
			room_frame(pressed);
			return MENU_NOTHING;
		default:
			break;
	}
	if (pressed & ACTION_BIT(ACTION_BACK))
	{
		if (menu.page == PAGE_OPTIONS)
		{
			save_game_settings();
			open_page(PAGE_MENU, MENU_LINE(MENU_OPTIONS));
		}
		else if (menu.page == PAGE_INPUT_TEST)
			open_page(PAGE_STEERING, STEERING_INPUT_TEST);
		else
		{
			if (menu.page == PAGE_SOUND_TEST)
				queue_sound(SOUND_STOP);
			open_page(PAGE_OPTIONS, menu.options_choice);
		}
		return MENU_NOTHING;
	}
	switch (menu.page)
	{
		case PAGE_OPTIONS: options_frame(pressed); break;
		case PAGE_TIME: time_frame(pressed); break;
		case PAGE_SOUND_TEST: sound_test_frame(pressed); break;
		case PAGE_VIDEO: video_frame(pressed); break;
		case PAGE_STEERING: steering_frame(pressed); break;
		case PAGE_INPUT_TEST: input_test_frame(pressed); break;
		case PAGE_CONTROLS: controls_frame(pressed); break;
		default: break;
	}
	return MENU_NOTHING;
}
