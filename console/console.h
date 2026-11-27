/**
 *    Name:        console.h
 *    Description: what the console build tells the port
 *    Version:     0.1.7
 *    Created:     29-10-2026
 *    Modified:    24-11-2026
 *    Author:      Sebastian Toyos
 */
#ifndef ORUNNERS_CONSOLE_H
#define ORUNNERS_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sets the console's part of the game up, before the program starts: the code of its own objects, which the
   program reaches through call_code (call_code_elsewhere) */
void console_attach(void);

/* True while the right seat's screen is shown beside the left one's (2 PLAYERS); the port shows the left
   screen alone otherwise */
bool console_both_screens(void);

/* The menus' actions, from any player's keys and game controller: the port hands those newly pressed each of
   its frames, and the menus take them on the left seat's next frame (the game's screens run at 30 a second) */
enum
{
	ACTION_UP,
	ACTION_DOWN,
	ACTION_LEFT,
	ACTION_RIGHT,
	ACTION_ACCEPT,
	ACTION_BACK,
	ACTION_START,
	ACTIONS
};
#define ACTION_BIT(action) (1u << (action))

void console_actions(uint32_t pressed);

/* For scripted checks (orunners_console_headless --autopilot): the left seat's wheel that keeps its car near
   the middle of the road */
int console_test_autopilot(void);

/* True once the menu's EXIT was picked: the port closes the game */
bool console_quit_requested(void);

/* The pause: true while a race of the left seat's runs, but the network's (the others race on), when the port
   may stop the game (its START or BACK; the seats' START is then no cabinet's: the radio's >> has its own
   control); QUIT GAME ends the games being played, back to the title */
bool console_can_pause(void);
void console_quit_game(void);

/* The pause's RESTART (issue 4), where it is offered (a game the menus started, not the network's): the
   games being played end as QUIT GAME's, and the same game starts again, its cars and transmissions chosen
   on the car select as before (REVERSE from the same goal) */
bool console_can_restart(void);
void console_restart_game(void);

/* True while player 'player''s seat (0 the left) races with the manual transmission (the car select's
   choice): the touch controls' gear buttons are shown then alone */
bool console_manual_gears(int player);

/* The radio's MUSIC, << and >> of player 'player''s seat, held this frame, before they go to the board. MUSIC goes
   round it all (Sebas's: one button for the songs, as the space bar is): on a song it is >>, on the last one the
   game's own (the station, the DJ), and on the station the game's own too, back to the first song. Jingle Bell,
   the radio's hidden song, which the game plays for both << and >> pressed together (a game controller's pad
   cannot), is reached with >> past Last Wave and with << before Magical Sound Shower (the other one pressed with
   it) */
void console_radio_buttons(int player, bool *music, bool *previous, bool *next);

/* 2 PLAYERS in the title's menu where the build has none (a phone's): the port offers it each frame where it
   can show both players (a tablet's screen, the two facing each other) */
void console_offer_two_players(bool offered);


/* The pause's words, PAUSE and its lines (CONTINUE, RESTART where console_can_restart, QUIT GAME), in the
   game's letters: sprite entries for s32_video_overlay_sprites into 'list' ('room' bytes), the line 'choice'
   in colour; their size in bytes */
enum
{
	CONSOLE_PAUSE_CONTINUE,
	CONSOLE_PAUSE_RESTART,
	CONSOLE_PAUSE_QUIT,
	CONSOLE_PAUSE_ITEMS
};

size_t console_pause_sprites(uint8_t *list, size_t room, int choice);
size_t console_prompt_sprites(uint8_t *list, size_t room, const char *title, const char *const *items, int count, int choice);

/* The route map beside the race: the port says each frame whether it has room for it (a tall screen's room
   under the picture); the right seat, unseen with one player, shows it then while the left one races, and
   its screen is shown there while true */
void console_side_map_wanted(bool wanted);
bool console_side_map_shown(void);

/* Where the car is on it, in the board's pixels of the screen (for the port to show the part around it) */
void console_side_map_car(int *x, int *y);

/* CLASSIC: true while the right seat may join the race being entered with its START, its screen unseen */
bool console_right_seat_joins(void);

/* The cabinet's controls each seat has, which OPTIONS > CONTROLS binds to keys and buttons (the port keeps
   them, controls.c) */
enum
{
	CONTROL_STEER_LEFT,
	CONTROL_STEER_RIGHT,
	CONTROL_GAS,
	CONTROL_BRAKE,
	CONTROL_SHIFT_UP,
	CONTROL_SHIFT_DOWN,
	CONTROL_MUSIC,         /* the DJ: the radio's song select */
	CONTROL_SONG_BACK,     /* << */
	CONTROL_SONG_NEXT,     /* >> */
	CONTROL_START,
	CONTROLS
};
#define CONTROL_PLAYERS 2

/* OPTIONS > CONTROLS on screen: where, in the left screen's pixels (WIDE's side counted), the pictures of
   'control''s keys and buttons go (their top left; false when the page is not shown): the first key's there,
   the second's and the game controller buttons' that many pixels to the right, a step each */
bool console_controls_place(int control, int *x, int *y);
#define CONSOLE_CONTROLS_SECOND_X    36    /* (room for a key two tiles wide, Enter or Space) */
#define CONSOLE_CONTROLS_BUTTON_X    76
#define CONSOLE_CONTROLS_BUTTON_STEP 20    /* (three in the column's 144) */

/* The player whose controls CONTROLS shows (0, 1) */
int console_controls_player(void);

/* The control CONTROLS waits for a new key or button for, or -1: the port takes the next one pressed, binds
   it (Escape: leaves it as it was) and says so */
int console_controls_waiting(void);
void console_controls_bound(void);

/* DEFAULTS was picked on CONTROLS (true once): the port puts the shown player's keys and buttons back */
bool console_controls_defaults(void);

/* OPTIONS > STEERING's SET UP WHEEL (a computer's): a racing wheel SDL has no game controller layout for (a
   Thrustmaster T300RS on Windows: SDL knows it is a wheel, not where its pedals are), or a wrong one, made one
   from the controls the player shows it in turn. The step the page waits for, or -1: the port watches the
   devices for it and says when it took it (console_wheel_setup_took: on to the next; after the last, the
   port has made the layout and the page gives way to INPUT TEST), or Escape leaves the page, the layout as it
   was (console_wheel_setup_cancel) */
enum
{
	WHEEL_SETUP_TURN,          /* the wheel turned left: its axis, and the device being set up */
	WHEEL_SETUP_GAS,           /* the pedal pressed and let go: its axis, or its half of the pedals' one */
	WHEEL_SETUP_BRAKE,
	WHEEL_SETUP_SHIFT_UP,      /* a button pressed */
	WHEEL_SETUP_SHIFT_DOWN,
	WHEEL_SETUP_START,
	WHEEL_SETUP_STEPS
};

int console_wheel_setup_step(void);
void console_wheel_setup_took(void);
void console_wheel_setup_cancel(void);

/* The window's settings OPTIONS > VIDEO, VOLUME, STEERING and FPS show and change: the port fills them in, and applies and
   saves them when 'changed' is set */
enum
{
	PIXELS_SHARP,       /* the board's pixels, square */
	PIXELS_SMOOTH,      /* sharp bilinear: square pixels with soft edges */
	PIXELS_BILINEAR,    /* plain bilinear, softer */
	PIXELS_CRT,         /* scanlines, a phosphor mask and a little vignette over smooth pixels */
	PIXELS_KINDS
};

enum
{
	WIDE_OFF,           /* the board's 320 pixels, 4:3 */
	WIDE_16_9,          /* 48 more at each side (S32_VIDEO_WIDE_SIDE_16_9) */
	WIDE_20_9,          /* 88 more at each side (S32_VIDEO_WIDE_SIDE_20_9): a phone's screen */
	WIDE_16_10,         /* 32 more at each side (S32_VIDEO_WIDE_SIDE_16_10): a Steam Deck's (after the others:
	                       outrunners.ini keeps the number) */
	WIDE_AUTO,          /* the one the screen's shape asks for (console_set_screen_wide): a computer's default, a
	                       Steam Deck's 16:10 in its hands and 16:9 on a TV (issue 40) */
	WIDE_21_9,          /* 108 more at each side (S32_VIDEO_WIDE_SIDE_21_9): an ultrawide monitor's (issue 44; after
	                       AUTO: outrunners.ini keeps the number) */
	WIDE_KINDS
};

#define CONSOLE_NAME_LENGTH 8    /* the player's name, NETWORK > NAME: letters of the game's font */

/* OPTIONS > STEERING: the sticks' dead zone (percent of their travel, in steps of DEAD_ZONE_STEP), and the
   sticks' sensitivity and the keys' and pads' speed (1 to STEERING_LEVELS, STEERING_NORMAL the game's own);
   past the speed's last level, STEERING_SPEED_INSTANT: the wheel all the way at once while a key or a pad
   holds it, and at the centre at once when let go (issue 45: a keyboard's keys as a digital pad's) */
#define DEAD_ZONE_STEP      5
#define DEAD_ZONE_MOST      50
#define DEAD_ZONE_NORMAL    10
#define STEERING_LEVELS     9
#define STEERING_NORMAL     5
#define STEERING_SPEED_INSTANT (STEERING_LEVELS + 1)

/* OPTIONS > STEERING's WHEEL ROTATION: a racing wheel's turn from end to end, in degrees, as its driver has it;
   the game's wheel turns all the way at WHEEL_ROTATION_GAME's (the arcade's; a stick's whole travel), the rest
   of a wider one's turn past it (issue 16: a G29 at 900) */
#define WHEEL_ROTATION_GAME 270
#define WHEEL_ROTATIONS     7
extern const int console_wheel_rotations[WHEEL_ROTATIONS];    /* WHEEL_ROTATION_GAME up to 1080 */

/* OPTIONS > STEERING's CENTRING and VIBRATION (ports/sdl/forces.c): the spring that brings a racing wheel
   with force feedback back to the centre, as the cabinet's did (a computer's), and how hard a wheel shakes or
   a game controller rumbles while the cabinet's wheel motor runs (in a crash, off the road); in percent of
   the device's strongest, in steps of FORCE_STEP, 0 none */
#define FORCE_STEP          10
#define CENTRING_NORMAL     50
#define VIBRATION_NORMAL    50

typedef struct
{
	int window_scale;      /* 1-6: the window, in times the game's picture */
	bool full_screen;
	int pixels;            /* PIXELS_* */
	int wide;              /* WIDE_*: a wider picture */
	bool vsync;            /* VIDEO's VSYNC: each frame shown at the screen's refresh (a computer's) */
	bool vertical;         /* 2 PLAYERS: one screen over the other (false: side by side) */
	int volume;            /* 0-10: 5 is the game's own */
	int music_volume;      /* SOUND's MUSIC, EFFECTS and VOICE (issue 57): 0-10, 5 the game's own */
	int effects_volume;
	int voice_volume;
	char name[CONSOLE_NAME_LENGTH + 1];    /* the player's, as the network's rooms show it */
	bool both_radios;      /* OPTIONS > 2P MUSIC: the right seat's radio heard too, as the cabinets' (false: the left one's alone) */
	bool fps;              /* OPTIONS > FPS: the frames a second at the picture's top right, and in a window's title */
	int dead_zone;         /* STEERING: DEAD_ZONE_* */
	int sensitivity;       /* STEERING: the sticks', STEERING_LEVELS */
	int wheel_rotation;    /* STEERING: a racing wheel's, one of console_wheel_rotations */
	int steering_speed;    /* STEERING: the keys' and pads', STEERING_LEVELS or STEERING_SPEED_INSTANT */
	int centring;          /* STEERING: a racing wheel's spring, percent (FORCE_STEP) */
	int vibration;         /* STEERING: the wheel motor's shake or rumble, percent (FORCE_STEP) */
	bool phone_vibration;  /* STEERING (Android's): the phone itself vibrates too, for its touch controls */
	bool changed;
} console_window;

extern console_window console_window_settings;

/* OPTIONS > SOUND's MUSIC, EFFECTS and VOICE (issue 57, sound_mix.c): a MultiPCM sample's category by its address as
   the chip has it (for sound_chips_set_categories), and the gains the settings give the sound chips now (for
   sound_chips_set_gains: 'pcm' of MULTIPCM_CATEGORIES); 5 each, the board's sound to the bit */
uint8_t console_sample_category(uint32_t address);
void console_sound_gains(double *fm, int32_t *pcm);

/* WIDE draws the frames wider (s32_video_set_wide_side): the columns more at each side, or 0 (WIDE off, or
   2 PLAYERS, whose screens are both the board's) */
int console_wide_side(void);

/* Each frame, before VBLANK's sprites and the frame: WIDE's side for the video (s32_video_set_wide_side) and for
   the game's edges (screen_more_left and screen_more_right, support.h's SCREEN_MORE_LEFT and SCREEN_MORE_RIGHT) */
void console_set_sides(void);

/* WIDE AUTO: the port says which WIDE the screen's shape asks for (WIDE_OFF till it does), and the one in effect
   is that one with AUTO, else the setting (what draws and sizes the picture reads this, not the setting) */
void console_set_screen_wide(int kind);
int console_wide_in_effect(void);

/* The folder TIME ATTACK keeps its ghosts in (ghost_0.bin to ghost_9.bin, a goal each), with its separator */
void console_ghost_folder(const char *folder);

/* TIME ATTACK's fastest run, for a machine that keeps it elsewhere (the Dreamcast: a VMU file): sampled, a
   few times a second, into a malloc'd buffer (0: none), read back (false: not a run), and a count that
   changes when a new fastest run is kept */
size_t console_ghost_export(uint8_t **result);
bool console_ghost_import(const uint8_t *data, size_t size);
uint32_t console_ghost_generation(void);
void console_ghost_release(void);         /* (its memory back; read again through console_ghost_reload) */
extern void (*console_ghost_reload)(void);

/* The network game (NETWORK: rooms of up to four devices, network.c): the port runs it once each of its frames
   (60 a second), the game's frame run or not. While a race of it waits for a device unheard (true while
   waiting) the port holds the game, its frame not run, runs the network's frames and shows
   console_network_wait_sprites over the frame it stopped at (sprite entries, as console_pause_sprites) */
void console_network_frame(void);
bool console_network_waiting(void);
size_t console_network_wait_sprites(uint8_t *list, size_t room);

/* True while the network is open: rooms looked for, a room's, its races (Android's Wi-Fi multicast lock) */
bool console_network_open(void);

/* For scripted checks: where the network stands, a line of text */
void console_network_describe(char *text, size_t size);

#endif
