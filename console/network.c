/**
 *    Name:        network.c
 *    Description: the network game: rooms (HOST, JOIN) of up to four devices, each one of the arcade's linked
 *                 cabinets in the room's races, over arcade-lan's linked game (lan/)
 *    Version:     0.2.0
 *    Created:     30-10-2026
 *    Modified:    06-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The arcade links up to four cabinets of two seats: each one runs its own two cars and sends its two race
 *    records (their first 70 bytes) to the others through the link board, which hands every cabinet all
 *    eight, its own among them, 30 times a second; machine 1's left seat (player 0) takes the race's entries,
 *    the first one in a race (its host) keeps its time and checkpoints, and every screen reads them from their
 *    records. Nothing waits frame by frame for the others: each cabinet takes the newest records it has.
 *
 *    Here each device is a cabinet and the link board is arcade-lan's linked game (LAN_LINK): its rooms (a host's
 *    beacon, JOIN, the room's list, the next device hosting a room whose host is gone) and the game's devices, each
 *    publishing a block every frame that goes to every other, which keeps the newest. The host's START links the
 *    game's devices in the room's order (the host machine 1) and presses each one's START, as an ARCADE race of all
 *    of them. A device's block is the race records it carries (its own two, and those of a machine gone it sends
 *    on), taken as the board would hand them (link_receive_records); what it says of itself (lan_set_status) is
 *    whether a seat of it is in a race, as the host's START waits for none to be.
 */
#include "console.h"
#include "console_internal.h"

#include "arcade_lan.h"
#include "game.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define GAME_VERSION        2          /* the datagrams' and the game's: devices of another version are not heard */
#define BEACON_PORT         27720      /* where the rooms' beacons go, every device's on the network */
#define MACHINES            4          /* the link's cabinets */
#define SLOTS               8          /* their seats, the race records */
#define RECORD_BYTES        0x70u      /* of a race record, those the link carries */
#define RECORD_SIZE         0x80u
#define ENTRIES_CUT         0x28u      /* race_entries_update's entry_frames once every player is in */
#define RACE_PART           0x30u      /* a race record's race (race_record_clear): the car's state, its passes */

/* What a record's numbers index in the game, for those from the network (check_record) */
#define CHECKPOINTS         6u         /* a race's checkpoints, the goal's the last: stage_count 0-6 */
#define LAST_STAGE          0x1Eu      /* the map's stages, 0-1E */
#define CAR_MODELS          8u
#define COURSES             10u        /* course_records: a course each goal */
#define SPRITE_ANIMATIONS   0x22B2u    /* sprite_animations' entries (034138, 8AC8 bytes) */

/* What a device says of itself (lan_set_status) */
#define BUSY                0x01u      /* a seat of it plays a race */

static struct
{
	lan_device *lan;           /* in a room, or looking for one */
	uint32_t frame;
	char problem[32];          /* what went wrong, for the NETWORK page (once) */
	lan_room rooms[LAN_ROOMS];             /* the rooms found (JOIN), as of this frame */
	int room_count;
	lan_member members[LAN_DEVICES];       /* the room's list (the host first), as of this frame */
	int member_count;

	/* the link: this device's machine (1-4), the machines and who they are, the others' records */
	bool linked;
	int machine;
	int machines;
	int players;               /* the players of the race's devices, all of them */
	uint32_t link_ids[MACHINES];
	bool link_lost[MACHINES];
	uint8_t slots[SLOTS][RECORD_BYTES];
	int start_pending;         /* the room's START, for the left seat's frame: 0, 1 or 2 seats */
	bool waiting;              /* a race waits for a device unheard */
} net;

static int players_wanted = 1;    /* NETWORK > PLAYERS */

/* ---- the link ---- */

/* The race records' entries as a link starts them: none taken, none asked for, every game over cleared */
static void clear_entries(uint32_t race)
{
	RACE(race)->entry_request = 0u;
	RACE(race)->entry_frames = 0u;
	RACE(race)->entry_serial_seen = 0u;
	RACE(race)->entry_serial = 0u;
	RACE(race)->entrant = 0xFFu;
	RACE(race)->game_over = 0u;
}

static void clear_screen_entries(uint32_t screen_block)
{
	memset(SCREEN(screen_block)->accepted_players, 0, sizeof(SCREEN(screen_block)->accepted_players));
	SCREEN(screen_block)->entrant_count = 0u;
	SCREEN(screen_block)->entry_accepted = 0u;
	SCREEN(screen_block)->is_host = 0u;
}

/* The two seats as machine 'machine' of 'machines' (network_check_join and network_check_result's set-up,
   with the car numbers the players'), every record's copy and entries cleared; machine 1's settings in its
   first record for the others (the master_ fields) */
static void link_set_up(int machine, int machines)
{
	const uint32_t first = (uint32_t)(machine - 1) * 2u;

	game->link_off = 0u;
	game->link_failed = 0u;
	game->machine_count = (uint8_t)machines;
	game->player_count = (uint8_t)(machines * 2);
	for (int player = 0; player < SLOTS; player++)
		game->car_numbers[player] = (uint8_t)player;
	for (int seat = 0; seat < 2; seat++)
	{
		const uint32_t race = seat ? RACE_2 : RACE_1, screen_block = seat ? SCREEN_2 : SCREEN_1;

		RACE(race)->machine = (uint8_t)machine;
		RACE(race)->player = (uint8_t)(first + seat);
		RACE(race)->car_number = (uint8_t)(first + seat);
		SCREEN(screen_block)->car_number = (uint8_t)(first + seat);
		SCREEN(screen_block)->player = (uint8_t)(first + seat);
		SCREEN(screen_block)->race = race;
		SCREEN(screen_block)->race_copy = race_copy_offsets[first + seat] + GAME_VARIABLES;
		clear_entries(race);
		clear_screen_entries(screen_block);
		memset((uint8_t *)RACE(race) + RACE_PART, 0, RECORD_SIZE - RACE_PART);    /* (race_record_clear's) */
	}
	if (machine == 1)
		console_master_settings();
	memset(RACE(RACE_COPY_1), 0, SLOTS * RECORD_SIZE);
	memset(net.slots, 0, sizeof(net.slots));
	memset(net.link_lost, 0, sizeof(net.link_lost));
	net.linked = true;
	net.machine = machine;
	net.machines = machines;
	net.waiting = false;
}

/* The link off: the machine alone again (network_check_stand_alone) */
static void link_end(void)
{
	if (!net.linked)
		return;
	net.linked = false;
	net.waiting = false;
	network_check_stand_alone();
	clear_entries(RACE_1);
	clear_entries(RACE_2);
	clear_screen_entries(SCREEN_1);
	clear_screen_entries(SCREEN_2);
	memset(RACE(RACE_COPY_1), 0, SLOTS * RECORD_SIZE);
	game->car_numbers[0] = 0u;
	game->car_numbers[1] = 1u;
}

/* The machine whose blocks carry a slot's record: its own, or the first one still heard for one gone */
static int publisher(int slot)
{
	const int owner = slot / 2 + 1;

	if (!net.link_lost[owner - 1])
		return owner;
	for (int machine = 1; machine <= net.machines; machine++)
		if (!net.link_lost[machine - 1])
			return machine;
	return owner;
}

/* True while a seat of this device plays a game */
static bool busy(void)
{
	return SCREEN(SCREEN_1)->game_mode == GAME_MODE_PLAY || SCREEN(SCREEN_2)->game_mode == GAME_MODE_PLAY;
}

/* The room's START, as the game's devices are listed: this device's machine its place in the list */
static void start_race(void)
{
	lan_member members[LAN_DEVICES];
	const int count = lan_game_members(net.lan, members);
	int own = -1;

	if (count < 1 || count > MACHINES)
		return;
	net.players = 0;
	for (int machine = 0; machine < MACHINES; machine++)
		net.link_ids[machine] = machine < count ? members[machine].id : 0u;
	for (int index = 0; index < count; index++)
	{
		net.players += members[index].players;
		if (members[index].own)
			own = index;
	}
	if (own < 0)
		return;
	link_set_up(own + 1, count);
	net.start_pending = members[own].players;
}

/* A record from the network, of the slot 'slot': whose it is as the link has it (its machine, its player, its
   car's number), and its numbers that index the game's tables and arrays (the players', the checkpoints', the
   stages', the cars' and their animations', the course records') kept within them, so that a broken datagram,
   or a device on the network meaning harm, reaches nothing past them. (Its car's frame is not: an animation
   keeps no count of its frames; one past them reads on in the game's data, whose pieces stop the game where
   there is none, as a device that fills the room stops a race) */
static void check_record(struct race *record, int slot)
{
	const int players = net.machines * 2;
	uint8_t *order = (uint8_t *)record + offsetof(struct race, race_order);

	record->machine = (uint8_t)(slot / 2 + 1);
	record->player = (uint8_t)slot;
	record->car_number = (uint8_t)slot;
	if (record->record_model >= CAR_MODELS)
		record->record_model = 0u;
	if (record->racer_count > players)
		record->racer_count = (uint8_t)players;
	for (int index = 0; index < SLOTS; index++)
		if (order[index] >= players)
			order[index] = 0u;
	if (record->entrant != 0xFFu && record->entrant >= players)
		record->entrant = 0xFFu;
	if (record->pass_player >= players)
		record->pass_player = 0u;
	if (record->pass_order >= players)
		record->pass_order = 0u;
	if (record->pass_checkpoint >= CHECKPOINTS)
		record->pass_checkpoint = CHECKPOINTS - 1u;
	record->pass_branch &= 1u;
	if (record->stage_count > CHECKPOINTS)
		record->stage_count = CHECKPOINTS;
	if (record->stage > LAST_STAGE)
		record->stage = LAST_STAGE;
	if (record->model >= CAR_MODELS)
		record->model = 0u;
	if (record->car_choice > CAR_MODELS)
		record->car_choice = 0u;
	if (record->record_course >= COURSES)
		record->record_course = 0u;
	if (record->animation >= SPRITE_ANIMATIONS)
		record->animation = 0u;
}

/* ---- the blocks ---- */

/* This device's block: the records it carries (its own two, and those of a machine gone it sends on), how many,
   then each one's slot and its first RECORD_BYTES */
static void publish(void)
{
	uint8_t block[1 + SLOTS * (1 + RECORD_BYTES)];
	size_t size = 1;
	int count = 0;

	for (int slot = 0; slot < net.machines * 2; slot++)
	{
		if (publisher(slot) != net.machine)
			continue;
		block[size++] = (uint8_t)slot;
		if (slot / 2 + 1 == net.machine)
			memcpy(block + size, RACE(slot & 1 ? RACE_2 : RACE_1), RECORD_BYTES);
		else
			memcpy(block + size, net.slots[slot], RECORD_BYTES);
		size += RECORD_BYTES;
		count++;
	}
	block[0] = (uint8_t)count;
	lan_publish(net.lan, block, size);
}

/* The index of a machine of the link among the game's devices (its id), or -1 (another game's list: a START
   this device was busy for) */
static int game_index(const lan_member *members, int count, int machine)
{
	for (int index = 0; index < count; index++)
		if (members[index].id == net.link_ids[machine - 1])
			return index;
	return -1;
}

/* The link's machines from the game's devices: those gone lost (their records carried by another), true while one
   is unheard and not yet gone; and the records each one carries, from its newest block (only the machine that
   carries a slot, publisher, and that machine only) */
static bool take_blocks(void)
{
	lan_member members[LAN_DEVICES];
	const int count = lan_game_members(net.lan, members);
	bool silent = false;

	for (int machine = 1; machine <= net.machines; machine++)
	{
		const int index = game_index(members, count, machine);

		if (machine == net.machine || net.link_lost[machine - 1])
			continue;
		if (index < 0 || members[index].gone)
			net.link_lost[machine - 1] = true;
		else if (!members[index].heard)
			silent = true;
	}
	for (int machine = 1; machine <= net.machines; machine++)
	{
		const int index = game_index(members, count, machine);
		uint8_t block[LAN_BLOCK_MOST];
		size_t size, at = 1;

		if (machine == net.machine || net.link_lost[machine - 1] || index < 0
			|| (size = lan_block(net.lan, index, block)) == 0)
			continue;
		for (int each = 0; each < block[0] && at + 1 + RECORD_BYTES <= size; each++, at += 1 + RECORD_BYTES)
		{
			const int slot = block[at];
			struct race record;

			if (slot >= net.machines * 2 || publisher(slot) != machine || slot / 2 + 1 == net.machine)
				continue;
			memset(&record, 0, sizeof(record));
			memcpy(&record, block + at + 1, RECORD_BYTES);
			check_record(&record, slot);
			memcpy(net.slots[slot], &record, RECORD_BYTES);
		}
	}
	return silent;
}

/* ---- a machine of the link gone ---- */

/* Its players go from the race as if out of it: another machine sends their records on (publisher), their
   cars out of every screen's sight, and the race's host takes them from its order (racer_count, race_order),
   so that nothing of the race waits for them (the passes, players_not_ready). A race whose host is gone pauses
   until the first of its players still heard (the lowest number) hosts it on with the host's last record: its
   time, the time left, the passes and the order */

#define HIDDEN_SEGMENT      0xFFFFu    /* past every screen's draw distance (race_car_draw) */
#define HOST_TAKEN          0xFFu      /* in a record's link_pad_5A (nothing of the game's reads it): the race
                                          hosted on from a host gone */

/* (a player of the link: one of its machines' seats) */
static bool player_lost(int player)
{
	return player >= 0 && player < net.machines * 2 && net.link_lost[player / 2];
}

static uint32_t copy_of(int player)
{
	return race_copy_offsets[player >= 0 && player < SLOTS ? player : 0] + GAME_VARIABLES;
}

static uint8_t *race_order(uint32_t race)
{
	return (uint8_t *)RACE(race) + offsetof(struct race, race_order);
}

/* The race host's record without the players gone */
static void expel_lost(uint32_t race)
{
	uint8_t *order = race_order(race);
	const int count = RACE(race)->racer_count;
	int kept = 0;

	for (int index = 0; index < count && index < SLOTS; index++)
		if (!player_lost(order[index]))
			order[kept++] = order[index];
	if (kept > 0)
		RACE(race)->racer_count = (uint8_t)kept;
}

/* A screen's player hosts the race on from the host's last record (its copy): race_progress_update's own
   counts of the passes from the screen's (each pass counted there as the host published it) */
static void host_race_on(uint32_t screen_block, uint32_t old)
{
	const uint32_t race = SCREEN(screen_block)->race;

	RACE(race)->racer_count = RACE(old)->racer_count;
	memcpy(race_order(race), race_order(old), SLOTS);
	RACE(race)->race_time = RACE(old)->race_time;
	RACE(race)->time_left = RACE(old)->time_left;
	RACE(race)->pass_count = RACE(old)->pass_count;
	RACE(race)->pass_player = RACE(old)->pass_player;
	RACE(race)->pass_checkpoint = RACE(old)->pass_checkpoint;
	RACE(race)->pass_order = RACE(old)->pass_order;
	RACE(race)->pass_branch = RACE(old)->pass_branch;
	RACE(race)->pass_time = RACE(old)->pass_time;
	RACE(race)->link_pad_5A = HOST_TAKEN;
	memcpy(SCREEN(screen_block)->counted_passes, SCREEN(screen_block)->checkpoint_passes,
		sizeof(SCREEN(screen_block)->counted_passes));
	memcpy(SCREEN(screen_block)->counted_stages, SCREEN(screen_block)->checkpoints_passed,
		sizeof(SCREEN(screen_block)->counted_stages));
	SCREEN(screen_block)->is_host = 0xFFu;
}

/* A seat in a race: its host's players gone from it, or its host gone (another hosts on: true while it waits
   for the new host's record) */
static bool follow_host(uint32_t screen_block)
{
	const int host = SCREEN(screen_block)->host_player;
	const uint32_t old = copy_of(host);
	const uint8_t *order = race_order(old);
	int next = -1;

	if (!SCREEN(screen_block)->in_game || host >= net.machines * 2)
		return false;
	if (!player_lost(host))
	{
		if (SCREEN(screen_block)->is_host)
			expel_lost(SCREEN(screen_block)->race);
		return false;
	}
	for (int index = 0; index < RACE(old)->racer_count && index < SLOTS; index++)
		if (!player_lost(order[index]) && (next < 0 || order[index] < next))
			next = order[index];
	if (next < 0)
		return false;
	if (next == SCREEN(screen_block)->player)
		host_race_on(screen_block, old);
	else if (RACE(copy_of(next))->link_pad_5A != HOST_TAKEN)
		return true;    /* (the new host's record not here yet) */
	SCREEN(screen_block)->host_player = (uint8_t)next;
	SCREEN(screen_block)->host_record = copy_of(next);
	if (SCREEN(screen_block)->is_host)
		expel_lost(SCREEN(screen_block)->race);
	return false;
}

/* The records of a machine gone this one sends on: from their last, the car out of sight, ready, the
   entries seen (as race_entries_update waits for every record's), and player 0's entries closed as its
   countdown would close them */
static void carry_lost_records(void)
{
	const uint8_t entry_serial = RACE(RACE_COPY_1)->entry_serial;

	for (int slot = 0; slot < net.machines * 2; slot++)
	{
		struct race *record = (struct race *)net.slots[slot];

		if (!player_lost(slot) || publisher(slot) != net.machine)
			continue;
		record->segment = HIDDEN_SEGMENT;
		record->ready = 0xFFu;
		record->entry_request = 0u;
		record->entry_serial_seen = entry_serial;
		if (slot == 0 && record->entry_frames != 0u && (net.frame & 1u) == 0u && --record->entry_frames == 0u)
		{
			record->entry_serial++;
			record->entrant = 0xFFu;
		}
	}
}

/* The link's part of the frame: the machines gone, their records, the races' hosts; true while a race of this
   device waits for a new host */
static bool carry_link(void)
{
	bool waiting;

	if (!net.linked)
		return false;
	carry_lost_records();
	waiting = follow_host(SCREEN_1);
	waiting = follow_host(SCREEN_2) || waiting;
	return waiting && busy();
}

/* ---- the port's frame ---- */

/* What went wrong, as the NETWORK page says it */
static const char *problem_text(const char *problem)
{
	static const char *const texts[][2] = {
		{ "NO ANSWER", "THE ROOM DID NOT ANSWER" }, { "ROOM FULL", "THE ROOM IS FULL" },
		{ "HOST GONE", "THE ROOM IS GONE" }, { "LEFT OUT", "LEFT OUT OF THE ROOM" } };

	for (size_t index = 0; index < sizeof(texts) / sizeof(texts[0]); index++)
		if (strcmp(problem, texts[index][0]) == 0)
			return texts[index][1];
	return problem;
}

static void fail(const char *problem)
{
	snprintf(net.problem, sizeof(net.problem), "%s", problem);
	console_network_leave();
}

/* A device on the network for this game, of the player's name and players: false where it has none */
static bool open_lan(void)
{
	const lan_config config = { { 'O', 'R', 'U', 'N' }, GAME_VERSION, BEACON_PORT, 0, 0, 0, LAN_LINK };

	net.problem[0] = '\0';
	if (net.lan)
		lan_close(net.lan);
	net.lan = lan_open(&config, console_window_settings.name, console_network_players());
	net.room_count = 0;
	net.member_count = 0;
	if (!net.lan)
	{
		fail("NO NETWORK");
		return false;
	}
	return true;
}

void console_network_frame(void)
{
	bool silent = false;

	if (!net.lan && !net.linked)
		return;
	net.frame++;
	if (net.lan)
	{
		const char *problem;

		lan_set_status(net.lan, busy() ? BUSY : 0u);
		lan_update(net.lan);
		net.room_count = lan_rooms(net.lan, net.rooms);
		net.member_count = lan_members(net.lan, net.members);
		if ((problem = lan_problem(net.lan)) != NULL)
			fail(problem_text(problem));
		else if (lan_game_started(net.lan, NULL, NULL, NULL) && !busy())
			start_race();
	}
	if (net.lan && net.linked && lan_role_of(net.lan) == LAN_PLAYING)
	{
		silent = take_blocks();
		publish();
		/* everyone in: the entries closed soon after (machine 1, player 0's record) */
		if (net.machine == 1 && RACE(RACE_1)->entry_frames > ENTRIES_CUT && SCREEN(SCREEN_1)->entrant_count >= net.players)
			RACE(RACE_1)->entry_frames = ENTRIES_CUT;
	}
	net.waiting = carry_link() || (silent && busy());
	if (!net.lan && net.linked && !busy())
		link_end();    /* (the room left during a race: once it is over) */
}

bool console_network_waiting(void)
{
	return net.waiting;
}

/* ---- the menu's ---- */

void console_network_host(void)
{
	if (open_lan() && lan_host(net.lan))
		net.member_count = lan_members(net.lan, net.members);    /* (the room's page shows it at once) */
}

void console_network_search(void)
{
	if (open_lan())
		lan_search(net.lan);
}

void console_network_join(int room)
{
	if (net.lan && lan_role_of(net.lan) == LAN_SEARCHING && room >= 0 && room < net.room_count)
		lan_join(net.lan, &net.rooms[room]);
}

void console_network_leave(void)
{
	if (net.lan)
		lan_close(net.lan);    /* (its LEAVE to the room) */
	net.lan = NULL;
	if (busy())
	{
		/* a race of this device goes on alone, the others gone from it; the link off once it is over */
		for (int machine = 1; net.linked && machine <= net.machines; machine++)
			net.link_lost[machine - 1] = machine != net.machine;
	}
	else
		link_end();
	net.member_count = 0;
	net.room_count = 0;
	net.start_pending = 0;
}

static lan_role role(void)
{
	return net.lan ? lan_role_of(net.lan) : LAN_OFF;
}

bool console_network_in_room(void)
{
	return role() == LAN_IN_ROOM || role() == LAN_PLAYING || role() == LAN_JOINING;
}

bool console_network_joining(void)
{
	return role() == LAN_JOINING;
}

bool console_network_searching(void)
{
	return role() == LAN_SEARCHING;
}

bool console_network_hosting(void)
{
	return lan_hosting(net.lan);
}

bool console_network_linked(void)
{
	return net.linked;
}

int console_network_room_count(void)
{
	return net.room_count;
}

void console_network_room(int room, char *text, size_t size)
{
	snprintf(text, size, "%-8s %d/%d", net.rooms[room].name, net.rooms[room].devices, MACHINES);
}

bool console_network_room_full(int room)
{
	return net.rooms[room].devices >= MACHINES;
}

int console_network_member_count(void)
{
	return net.member_count;
}

void console_network_member(int index, char *text, size_t size)
{
	snprintf(text, size, "%d %-8s%s", index + 1, net.members[index].name, net.members[index].players == 2 ? " 2P" : "");
}

/* START: two devices or more, none of them in a race, every one heard */
bool console_network_can_start(void)
{
	if (!console_network_hosting() || net.member_count < 2 || busy())
		return false;
	for (int index = 0; index < net.member_count; index++)
		if (!net.members[index].own && (!net.members[index].heard || (net.members[index].status & BUSY)))
			return false;
	return true;
}

void console_network_start(void)
{
	if (console_network_can_start() && lan_start(net.lan, NULL, 0) && lan_game_started(net.lan, NULL, NULL, NULL))
		start_race();
}

bool console_network_start_pending(void)
{
	return net.start_pending != 0;
}

int console_network_players(void)
{
	return players_wanted;
}

void console_network_set_players(int players)
{
	players_wanted = players == 2 ? 2 : 1;
}

int console_network_take_start(void)
{
	const int seats = net.start_pending;

	net.start_pending = 0;
	return seats;
}

const char *console_network_problem(void)
{
	return net.problem[0] ? net.problem : NULL;
}

void console_network_problem_seen(void)
{
	net.problem[0] = '\0';
}

/* ---- the game's link, in place of the board ---- */

/* Each frame, the race records of all the players into 20FC00 (link_receive_records): this device's own two,
   and the others' as their newest blocks brought them */
void link_receive_records(void)
{
	if (!net.linked)
	{
		original_link_receive_records();
		return;
	}
	for (int slot = 0; slot < net.machines * 2; slot++)
	{
		struct race *copy = RACE(RACE_COPY_1 + (uint32_t)slot * RECORD_SIZE);

		if (slot / 2 + 1 == net.machine)
			memcpy(copy, RACE(slot & 1 ? RACE_2 : RACE_1), RECORD_BYTES);
		else
			memcpy(copy, net.slots[slot], RECORD_BYTES);
	}
}

/* The records go with the port's frame (console_network_frame), not to a link board */
void link_send_records(void)
{
	if (!net.linked)
		original_link_send_records();
}

/* ---- for scripted checks ---- */

void console_network_describe(char *text, size_t size)
{
	const char *const roles[] = { "off", "searching", "joining", "in room", "in room" };
	int at = snprintf(text, size, "%s%s, %d members", roles[role()], console_network_hosting() ? " (host)" : "",
		net.member_count);

	if (net.linked && SCREEN(SCREEN_1)->host_record && at > 0 && (size_t)at < size)
	{
		const uint32_t host = SCREEN(SCREEN_1)->host_record;

		at += snprintf(text + at, size - (size_t)at, ", machine %d of %d, host player %u%s, racers %u, time %u, left %u,"
			" segments", net.machine, net.machines, SCREEN(SCREEN_1)->host_player, SCREEN(SCREEN_1)->is_host ? " (this)" : "",
			RACE(host)->racer_count, RACE(host)->race_time, RACE(host)->time_left);
		for (int slot = 0; slot < net.machines * 2 && at > 0 && (size_t)at < size; slot++)
			at += snprintf(text + at, size - (size_t)at, " %u", RACE(RACE_COPY_1 + (uint32_t)slot * RECORD_SIZE)->segment);
	}
}
