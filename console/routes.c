/**
 *    Name:        routes.c
 *    Description: & KNUCKLES' routes: REVERSE, from a goal the player picks back to the pre-stage, and FULL,
 *                 the 31 stages one after another from the pre-stage to the last goal
 *    Version:     0.1.3
 *    Created:     29-10-2026
 *    Modified:    24-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    The game builds its road from a course (stage_courses: a stage's runs, then its fork's two branches,
 *    each a short branch road that jumps to the next stage's course) and starts the roadside objects from a
 *    stream a stage (road_object_streams), both read through data_at. The routes give it courses of their
 *    own, at addresses the program has no data at (data_elsewhere): each stage's runs as they are, then a
 *    fork whose two branches lead where the route goes, through copies of the game's branch roads; the
 *    game's own course builder then takes the route (the fork's stages into route_stage, its STAGE label,
 *    palettes, arrow), and the roadside objects follow each stage's own stream.
 *
 *    REVERSE: each fork leads to the stage's parents (the stages whose forks lead to it: fork_next_stages);
 *    the goal stages fork where their goal was; the pre-stage finishes where its fork was, with a long run
 *    after it, as the goals do. A goal stage, where REVERSE starts, has a stream of its own here: the
 *    pre-stage's start line among its objects before its goal's approach, then stage 1's fork objects (its
 *    camera, sign, flags, flagman, checkpoint gate) moved to the new fork, and stage 1's branch streams
 *    moved with them. The pre-stage, where it finishes, has its finish where its fork was (the track's
 *    choice), with a goal stage's finish objects in place of the fork's.
 *
 *    FULL: both branches of each fork lead to the next stage by number (1.1, 2.1, 2.2, 3.1 ...), through the
 *    stage's own branch roads; the goal stages fork where their goal was, as REVERSE's do, but the last
 *    (KENYA), which keeps its goal. The game counts the checkpoints passed (the forks 0-4, then the goal 5:
 *    the time each adds, the route, the split times are kept by them) and its times in hundredths of 16
 *    bits; once the car is in a stage, FULL takes the counts back to the stage's level (a goal stage's to the
 *    last fork's: the game's fifth is its goal), and the time to the last checkpoint out of the race's time
 *    into a total of its own, so that the race always is one the game knows. The goal shows the total.
 *
 *    The background of each stage is its own (stage_backgrounds), the pre-stage keeps the one before it (it
 *    has none); each fork adds the time of the stage it leads to; there is no route map after the race and
 *    no course record (the game's are by goal).
 */
#include "console.h"
#include "console_internal.h"

#include <stddef.h>
#include <stdio.h>

#include "game.h"
#include "s32/video.h"

#define ROUTE_DATA          0x900000u    /* the routes' courses and streams, for data_at (data_elsewhere) */
#define ROUTE_DATA_SIZE     0x20000u
#define STAGES              0x1F
#define FIRST_GOAL          0x15
#define LAST_STAGE          0x1E         /* KENYA: FULL's goal */
#define LAST_FORK_STAGE     0x14         /* the last stage with a fork of the game's */
#define GOALS               10
#define GOAL_CHECKPOINT     5            /* the game's checkpoints: the forks 0-4, then the goal */
#define REVERSE_START_EXTRA 59           /* REVERSE's start: 99 seconds, the clock's most beside FINAL STAGE */
#define WATER_LEFT_GOAL     0x15         /* AUSTRALIA: its start has the harbour at the left (REVERSE's start line) */
#define GOAL_FORK           1792         /* a goal stage's goal, where the routes fork */
#define STAGE_FORK          1664         /* the other stages' fork */
#define FORK_OBJECTS_FROM   1408         /* stage 1's stream: its fork camera on */
#define GOAL_APPROACH       1700         /* a goal stage's stream: its goal's approach and finish from here */
#define PRE_STAGE_FORK      710          /* the pre-stage's fork: REVERSE's finish */
#define FORK_OBJECTS_FROM_0 454          /* the pre-stage's stream: its fork camera on */
#define START_OBJECTS_TO    84           /* the pre-stage's stream: its start line (gate, flagman, balloons) */
#define LONG_RUN            16384        /* past a finish, as the goal stages' courses go on */
#define SHARED_RECORDS      0x02D900u    /* the stream records the stages share (fork, checkpoint) from here */
#define START_LINE_KINDS    16           /* the start line's objects' records, at most */

enum
{
	COURSE_END = 0,
	COURSE_JUMP = 1,
	COURSE_FORK = 2,
	COURSE_PITCH_0 = 3,
	COURSE_STAGE = 4,
	COURSE_HEADING_0 = 5,
	COURSE_GOAL = 6
};

enum
{
	STREAM_OBJECT = 0,
	STREAM_STAGE_END = 1,
	STREAM_FORK = 3
};

enum
{
	ROUTE_NONE,
	ROUTE_REVERSE,
	ROUTE_FULL
};

typedef struct
{
	bool built;
	uint32_t courses[STAGES];          /* each stage's course on the route */
	uint32_t goal_streams[GOALS];      /* each goal stage's stream (FULL: but the last's) */
} route;

static struct
{
	uint8_t data[ROUTE_DATA_SIZE];
	uint32_t used;
	bool shared_built;
	uint32_t branch_streams[2];        /* stage 1's, moved to a goal stage's fork */
	route reverse;
	route full;
	uint32_t finish_stream;            /* REVERSE's pre-stage's */
	uint8_t parents[STAGES][2];        /* REVERSE's: by branch (the same twice for one parent) */
	int running;                       /* ROUTE_* of the game started */
	int goal;                          /* REVERSE: the picked goal's stage */
	int given_goal;                    /* REVERSE: the goal its start gave (the pause's RESTART); 0: the map's */
	bool time_given;                   /* REVERSE: the start's time is in the race's time_left */
	int8_t start_difficulty;           /* REVERSE: the TIME setting of the start (the race's master_time_difficulty
	                                      0), while the start's own time is in its place */
	int folded_stage;                  /* FULL: the stage the game's counts were last taken back for */
	uint32_t time_base;                /* FULL: the race's time taken out of it, in hundredths */
	bool total_shown;                  /* FULL: the goal's time is the total */
	struct
	{
		uint32_t record;               /* a start line object's record (its parameter) */
		uint32_t handler;              /* and its own handler */
	} start_line[START_LINE_KINDS];
	int start_line_kinds;
} routes;

/* ---- the routes' data, at ROUTE_DATA ---- */

static const uint8_t *route_data(uint32_t address)
{
	const uint32_t offset = address - ROUTE_DATA;

	return address >= ROUTE_DATA && offset < routes.used ? &routes.data[offset] : NULL;
}

/* (and back: the address of a place of it a pointer the C keeps points at) */
static uint32_t route_address(const void *pointer)
{
	const uintptr_t offset = (uintptr_t)pointer - (uintptr_t)routes.data;

	return offset < routes.used ? ROUTE_DATA + (uint32_t)offset : 0u;
}

static uint32_t here(void)
{
	return ROUTE_DATA + routes.used;
}

static void put8(uint32_t value)
{
	if (routes.used < ROUTE_DATA_SIZE)
		routes.data[routes.used++] = (uint8_t)value;
}

static void put16(uint32_t value)
{
	put8(value);
	put8(value >> 8);
}

static void put32(uint32_t value)
{
	put16(value);
	put16(value >> 16);
}

static uint8_t byte_at(uint32_t address)
{
	return data_at(address)[0];
}

static int16_t s16_at(uint32_t address)
{
	return (int16_t)(byte_at(address) | byte_at(address + 1u) << 8);
}

static uint32_t u32_at(uint32_t address)
{
	return byte_at(address) | (uint32_t)byte_at(address + 1u) << 8 | (uint32_t)byte_at(address + 2u) << 16
		| (uint32_t)byte_at(address + 3u) << 24;
}

static void copy(uint32_t from, uint32_t count)
{
	for (uint32_t index = 0; index < count; index++)
		put8(byte_at(from + index));
}

/* ---- the courses ---- */

/* The size of the course record at 'place' (its command or a run), 0 for the end, a fork, a goal, a jump */
static uint32_t record_size(uint32_t place)
{
	const int16_t length = s16_at(place);

	if (length > 0)
		return 6;    /* a run: its length, pitch and heading steps, width code, road event */
	switch (-length)
	{
		case COURSE_STAGE: return 4;
		case COURSE_PITCH_0:
		case COURSE_HEADING_0: return 6;
		default: return 0;
	}
}

/* A stage's course up to its fork or goal (not those), its jumps followed: copied as it is when 'copying';
   the place of the record it stops at */
static uint32_t course_end(uint32_t course, bool copying)
{
	uint32_t size;

	for (;;)
	{
		if (s16_at(course) == -COURSE_JUMP)
		{
			course = u32_at(course + 2u);
			continue;
		}
		if ((size = record_size(course)) == 0)
			return course;
		if (copying)
			copy(course, size);
		course += size;
	}
}

/* Room for a fork in the course being written: its place */
static uint32_t fork_room(void)
{
	const uint32_t place = here();

	for (int byte = 0; byte < 12; byte++)
		put8(0);
	return place;
}

/* The fork at 'place' (fork_room's): its branches' stages and their branch roads */
static void fork_write(uint32_t place, const uint8_t stages[2], const uint32_t roads[2])
{
	const uint32_t used = routes.used;

	routes.used = place - ROUTE_DATA;
	put16((uint32_t)(uint16_t)-COURSE_FORK);
	put8(stages[0]);
	put8(stages[1]);
	put32(roads[0]);
	put32(roads[1]);
	routes.used = used;
}

/* The pre-stage's course for REVERSE: its runs, then its finish where its fork was (the track's choice), and
   a long run past it to the end, as a goal's */
static void copy_finish_course(uint32_t course)
{
	course_end(course, true);
	put16((uint32_t)(uint16_t)-COURSE_GOAL);
	put16(LONG_RUN);
	put32(0x00050000u);    /* (straight, flat, width code 5, no event) */
	put16(COURSE_END);
}

/* One of 'stage''s fork's branch roads (branch 1 the mirror of branch 0) that leads to 'course' */
static uint32_t branch_road(int stage, int branch, uint32_t course)
{
	const uint32_t fork = course_end(stage_courses[stage], false);
	const uint32_t start = here();
	uint32_t road = u32_at(fork + 2u + 2u + 4u * (uint32_t)branch), size;

	while ((size = record_size(road)) != 0)
	{
		copy(road, size);
		road += size;
	}
	put16((uint32_t)(uint16_t)-COURSE_JUMP);
	put32(course);
	return start;
}

/* ---- the streams ---- */

/* Stage 1's stream, from its first record: the place of its fork command */
static uint32_t stage_1_fork(void)
{
	uint32_t entry = road_object_streams[1];

	while (s16_at(entry + 2u) == STREAM_OBJECT)
		entry += 12u;
	return entry;
}

/* One of stage 1's branch streams (fork signs, the branch not taken), its places moved by 'shift', to its
   end */
static uint32_t branch_stream(int branch, int shift)
{
	const uint32_t start = here();
	uint32_t entry = u32_at(stage_1_fork() + 4u + 4u * (uint32_t)branch);

	for (;;)
	{
		const uint16_t command = (uint16_t)s16_at(entry + 2u);

		put16((uint32_t)(s16_at(entry) + shift));
		put16(command);
		if (command != STREAM_OBJECT)
			break;
		copy(entry + 4u, 8);
		entry += 12u;
	}
	return start;
}

/* One of the pre-stage's start line's objects into a stream, with the console's handler
   (presentation_start_line_update, which runs the object's own and draws it in the pre-stage's colours); its
   own handler kept by its record, as the function's number. The game's stream takes CONSOLE_CODE_START_LINE,
   no function's address, as NO_FUNCTION: road_object_stream_read gives those objects the console's handler
   (all of them, as before handlers were numbers: one whose record START_LINE_KINDS left out does nothing) */
static void put_start_line_object(uint32_t entry)
{
	const uint32_t handler = u32_at(entry + 4u), record = u32_at(entry + 8u);
	int kind;

	for (kind = 0; kind < routes.start_line_kinds && routes.start_line[kind].record != record; kind++)
		;
	if (kind == routes.start_line_kinds && kind < START_LINE_KINDS)
	{
		routes.start_line[kind].record = record;
		routes.start_line[kind].handler = function_number(handler);
		routes.start_line_kinds++;
	}
	copy(entry, 4);
	put32(CONSOLE_CODE_START_LINE);
	put32(record);
}

/* A goal stage's stream to a fork where its goal was: its own objects before its goal's approach (REVERSE,
   which starts there: the pre-stage's start line among them, its gate and lights, flagman, balloons and
   crowd), stage 1's fork objects moved to the fork, and the fork with stage 1's branch streams moved alike */
static uint32_t goal_stream(int stage, bool start_line)
{
	const int shift = GOAL_FORK - STAGE_FORK;
	const uint32_t start = here();
	uint32_t entry = road_object_streams[stage];
	uint32_t line = road_object_streams[0];

	while (s16_at(entry + 2u) == STREAM_OBJECT && s16_at(entry) < GOAL_APPROACH)
	{
		/* (the two streams merged by place: the reader takes them in order) */
		while (start_line && s16_at(line) < START_OBJECTS_TO && s16_at(line) <= s16_at(entry))
		{
			put_start_line_object(line);
			line += 12u;
		}
		copy(entry, 12);
		entry += 12u;
	}
	for (entry = road_object_streams[1]; s16_at(entry + 2u) == STREAM_OBJECT; entry += 12u)
	{
		const uint32_t parameter = u32_at(entry + 8u);

		if (s16_at(entry) < FORK_OBJECTS_FROM || (parameter != 0u && parameter < SHARED_RECORDS))
			continue;    /* (stage 1's own scenery) */
		put16((uint32_t)(s16_at(entry) + shift));
		copy(entry + 2u, 10);
	}
	put16(GOAL_FORK);
	put16(STREAM_FORK);
	put32(routes.branch_streams[0]);
	put32(routes.branch_streams[1]);
	return start;
}

/* The pre-stage's stream for REVERSE, which finishes there: its objects past its start line and before its
   fork's (the fork camera, the WEST/EAST panel, the sign, the checkpoint gate), then a goal stage's finish
   (the flags of its approach, its flagman and gate, the crowd and parade past it) moved to its fork */
static uint32_t finish_stream(void)
{
	const int shift = PRE_STAGE_FORK - GOAL_FORK;
	const uint32_t start = here();
	uint32_t entry;

	for (entry = road_object_streams[0]; s16_at(entry + 2u) == STREAM_OBJECT; entry += 12u)
		if (s16_at(entry) >= START_OBJECTS_TO && s16_at(entry) < FORK_OBJECTS_FROM_0)
			copy(entry, 12);
	for (entry = road_object_streams[FIRST_GOAL];; entry += 12u)
	{
		const uint16_t command = (uint16_t)s16_at(entry + 2u);

		if (s16_at(entry) < GOAL_APPROACH && command == STREAM_OBJECT)
			continue;
		put16((uint32_t)(s16_at(entry) + shift));
		put16(command);
		if (command != STREAM_OBJECT)
			break;    /* (its end: the reader stops there) */
		copy(entry + 4u, 8);
	}
	return start;
}

/* ---- building them, once each ---- */

static int level_of(int stage)
{
	return stage == 0 ? 0 : stage < 3 ? 1 : stage < 7 ? 2 : stage < 0xD ? 3 : stage < FIRST_GOAL ? 4 : 5;
}

static void build_shared(void)
{
	if (routes.shared_built)
		return;
	routes.shared_built = true;
	data_elsewhere = route_data;
	address_elsewhere = route_address;
	for (int branch = 0; branch < 2; branch++)
		routes.branch_streams[branch] = branch_stream(branch, GOAL_FORK - STAGE_FORK);
}

static void build_reverse(void)
{
	route *const reverse = &routes.reverse;
	uint32_t forks[STAGES];
	bool found[STAGES] = { false };

	if (reverse->built)
		return;
	reverse->built = true;
	build_shared();

	/* the parents, from the forward forks */
	for (int stage = 0; stage < FIRST_GOAL; stage++)
		for (int branch = 0; branch < 2; branch++)
		{
			const int child = fork_next_stages[stage * 2 + branch];

			if (child >= STAGES || child == stage)
				continue;
			routes.parents[child][found[child] ? 1 : 0] = (uint8_t)stage;
			if (!found[child])
				routes.parents[child][1] = (uint8_t)stage;
			found[child] = true;
		}

	/* each stage's course, with room for its fork (the pre-stage's ends at its finish) */
	for (int stage = 0; stage < STAGES; stage++)
	{
		reverse->courses[stage] = here();
		if (stage == 0)
		{
			copy_finish_course(stage_courses[0]);
			continue;
		}
		course_end(stage_courses[stage], true);
		forks[stage] = fork_room();
	}
	for (int stage = 1; stage < STAGES; stage++)
	{
		uint32_t roads[2];

		for (int branch = 0; branch < 2; branch++)
			roads[branch] = branch_road(1, branch, reverse->courses[routes.parents[stage][branch]]);
		fork_write(forks[stage], routes.parents[stage], roads);
	}

	for (int goal = 0; goal < GOALS; goal++)
		reverse->goal_streams[goal] = goal_stream(FIRST_GOAL + goal, true);
	routes.finish_stream = finish_stream();
}

static void build_full(void)
{
	route *const full = &routes.full;
	uint32_t forks[STAGES];

	if (full->built)
		return;
	full->built = true;
	build_shared();

	/* each stage's course but the last's, with room for its fork; the last's the game's, to its goal */
	for (int stage = 0; stage < LAST_STAGE; stage++)
	{
		full->courses[stage] = here();
		course_end(stage_courses[stage], true);
		forks[stage] = fork_room();
	}
	full->courses[LAST_STAGE] = stage_courses[LAST_STAGE];
	for (int stage = 0; stage < LAST_STAGE; stage++)
	{
		const uint8_t next[2] = { (uint8_t)(stage + 1), (uint8_t)(stage + 1) };
		uint32_t roads[2];

		/* (a goal stage has no branch roads of its own: stage 1's, as its stream's fork objects) */
		for (int branch = 0; branch < 2; branch++)
			roads[branch] = branch_road(stage <= LAST_FORK_STAGE ? stage : 1, branch, full->courses[stage + 1]);
		fork_write(forks[stage], next, roads);
	}

	for (int goal = 0; goal < GOALS - 1; goal++)
		full->goal_streams[goal] = goal_stream(FIRST_GOAL + goal, false);
}

/* ---- the race ---- */

static bool route_here(void)
{
	return routes.running != ROUTE_NONE && screen->index == LEFT_SEAT
		&& screen->game_mode == GAME_MODE_PLAY;
}

static const route *route_running(void)
{
	return routes.running == ROUTE_FULL ? &routes.full : &routes.reverse;
}

bool console_routes_running(void)
{
	return routes.running != ROUTE_NONE;
}

void console_reverse_start(int goal)
{
	build_reverse();
	routes.goal = 0;    /* (picked on the route map, once the car select is over, or given) */
	routes.given_goal = goal;
	routes.time_given = true;    /* (nothing in the setting until the goal is picked) */
	routes.running = ROUTE_REVERSE;
}

/* What a REVERSE checkpoint gives in the game's own race, in frames: the time of the stage it leads to (one of
   each level, from the last fork's down to the pre-stage's) and that checkpoint's TIME setting */
static int reverse_checkpoint_given(int checkpoint)
{
	static const uint8_t level_stages[GOAL_CHECKPOINT] = { 0x0D, 0x07, 0x03, 0x01, 0x00 };

	return (stage_start_times[level_stages[checkpoint]] + game->race_copies[0].master_time_difficulty[checkpoint]) * 0x1E;
}

/* REVERSE's time at a checkpoint: the game's, less what the start gave beyond the game's start
   (REVERSE_START_EXTRA), taken back from the last checkpoints (the pre-stage's first): the race's whole
   time the game's own, its margin at the start, where the longest course is, and not at the end */
static int reverse_checkpoint_time(int checkpoint)
{
	int later = 0, owed;
	const int given = reverse_checkpoint_given(checkpoint);

	for (int after = checkpoint + 1; after < GOAL_CHECKPOINT; after++)
		later += reverse_checkpoint_given(after);
	owed = REVERSE_START_EXTRA * 0x1E - later;
	return owed <= 0 ? given : (owed >= given ? 0 : given - owed);
}

int console_reverse_goal(void)
{
	return routes.running == ROUTE_REVERSE ? routes.goal : 0;
}

void console_full_start(void)
{
	build_full();
	routes.running = ROUTE_FULL;
	routes.folded_stage = 0;
	routes.time_base = 0;
	routes.total_shown = false;
}

/* The pre-stage's scene colours (its colour block: its place in palette RAM, its length in bytes, the
   colours; the upper half of the window in the board's alternate format) into the left screen's extra palette:
   the start line's gate and stands take theirs from it, the stage's own scenery the board's */
static void start_line_colours(void)
{
	const uint32_t block = scene_palettes[0];
	const uint32_t place = (uint32_t)(byte_at(block) | byte_at(block + 1u) << 8) / 2u;
	const uint32_t count = (uint32_t)(byte_at(block + 2u) | byte_at(block + 3u) << 8) / 2u;

	for (uint32_t index = 0; index < count; index++)
	{
		const uint32_t entry = place + index;
		uint16_t colour = (uint16_t)(byte_at(block + 4u + 2u * index) | byte_at(block + 5u + 2u * index) << 8);

		if (entry & 0x4000u)
		{
			/* (the alternate format, as the board's palette_write takes it) */
			const int r = ((colour >> 12) & 0x01) | ((colour << 1) & 0x1e);
			const int g = ((colour >> 13) & 0x01) | ((colour >> 3) & 0x1e);
			const int b = ((colour >> 14) & 0x01) | ((colour >> 7) & 0x1e);

			colour = (uint16_t)((colour & 0x8000) | (b << 10) | (g << 5) | r);
		}
		s32_video_extra_colour(0, entry, colour);
	}
}

/* A start line object's handler where REVERSE starts: its own, but at a goal with water at the left (AUSTRALIA's
   harbour) the rows drawn at both sides of the road (the stands and the people: scenery_pair and
   animated_scenery_pair) at the right alone, by their one side's (scenery and animated_scenery: the same record
   and steps, the pair's first item, which is at the right) */
static uint32_t start_line_handler(uint32_t handler)
{
	if (routes.running != ROUTE_REVERSE || routes.goal != WATER_LEFT_GOAL)
		return handler;
	if (handler == FUNCTION_scenery_pair_update)
		return FUNCTION_scenery_update;
	if (handler == FUNCTION_animated_scenery_pair_update)
		return FUNCTION_animated_scenery_update;
	return handler;
}

/* A start line object of REVERSE's (a goal stage's stream): its own handler (start_line_handler), then its sprites
   (the entries it put in the list) marked for the extra palette, where the pre-stage's colours are */
void presentation_start_line_update(void)
{
	const uint32_t record = current_object->parameter;
	const uint32_t first = screen->sprite_list_last + 0x10u;

	for (int kind = 0; kind < routes.start_line_kinds; kind++)
		if (routes.start_line[kind].record == record)
		{
			call_function(start_line_handler(routes.start_line[kind].handler));
			break;
		}
	for (uint32_t sprite = first; sprite <= screen->sprite_list_last; sprite += 0x10u)
		if ((SPRITE_WORDS(sprite)[0] >> 14) == 0u)    /* (a sprite, not the group's jump) */
		{
			SPRITE_WORDS(sprite)[1] |= S32_SPRITE_EXTRA_PALETTE;
			SPRITE_WORDS(sprite + 0x3000u)[1] |= S32_SPRITE_EXTRA_PALETTE;
		}
}

/* The checkpoint a stage's fork is, in the game's count (FULL): its level's; a goal stage's the last fork's,
   and the last stage's goal the game's goal */
static int checkpoint_of(int stage)
{
	return stage == LAST_STAGE ? GOAL_CHECKPOINT : stage >= FIRST_GOAL ? GOAL_CHECKPOINT - 1 : level_of(stage);
}

/* The objects whose handler is 'handler': a count of theirs taken back by 'fold' (at 'offset' in them) */
static void fold_objects(uint32_t handler, uint32_t offset, int fold)
{
	for (uint32_t object = console_find_object(handler, 0u); object; object = console_find_object(handler, object))
		ram(object + offset)[0] = (uint8_t)(ram(object + offset)[0] - fold);
}

/* FULL, once the car is in a stage of the route: the game's checkpoint counts taken back to the stage's, and
   the race's time to the last checkpoint taken out into FULL's total */
static void full_fold(void)
{
	const uint32_t race = screen->race, race_copy = screen->race_copy;
	const int stage = screen->route_stage;
	const uint8_t player = screen->player;
	uint32_t time;
	int fold;

	if (screen->current_stage != stage || stage == routes.folded_stage)
		return;
	routes.folded_stage = stage;
	/* (the START banner and its lights never end, and the race's segments, which the game compares as 16 bits
	   with a sign, would have them in front of the car again half way through: over once it has left them) */
	for (uint32_t object = console_find_object(FUNCTION_start_gate_update, 0u); object;
		object = console_find_object(FUNCTION_start_gate_update, object))
	{
		OBJECT(object)->flags = 0u;
		OBJECT(object)->flags_high = 0u;
	}
	fold = RACE(race)->state.stage_count - checkpoint_of(stage);
	if (fold > 0)
	{
		RACE(race)->state.stage_count -= (uint8_t)fold;
		if (race_copy != race)
			RACE(race_copy)->state.stage_count -= (uint8_t)fold;
		screen->progress.counted_stages[player] -= (uint8_t)fold;
		screen->progress.checkpoints_passed[player] -= (uint8_t)fold;
		screen->progress.forks_decided -= (uint8_t)fold;
		screen->route_stage_level -= (uint8_t)fold;
		player_car->forks_reached -= (uint8_t)fold;
		player_car->forks_taken -= (uint8_t)fold;
		player_car->extensions_shown -= (uint8_t)fold;
		fold_objects(FUNCTION_lap_timer_update, offsetof(struct object, lap_timer.checkpoints), fold);
		fold_objects(FUNCTION_checkpoint_board_update, offsetof(struct object, checkpoint_board.checkpoints_seen), fold);
		fold_objects(FUNCTION_stage_end_sounds_update, offsetof(struct object, stage_end_sounds.stage), fold);
		for (int checkpoint = checkpoint_of(stage); checkpoint <= GOAL_CHECKPOINT; checkpoint++)
		{
			screen->progress.counted_passes[checkpoint] = 0u;
			screen->progress.checkpoint_passes[checkpoint] = 0u;
		}
	}
	/* (the race's time in thousandths, its checkpoints' in hundredths: the split times stay as they were) */
	time = player_car->checkpoint_time;
	routes.time_base += time;
	RACE(race)->state.race_time -= time * 10u;
	if (race_copy != race)
		RACE(race_copy)->state.race_time -= time * 10u;
	player_car->race_time -= time * 10u;
	player_car->checkpoint_time = 0u;
}

/* FULL at its goal: the time under CONGRATULATIONS the whole race's (goal_update has written the last
   stage's) */
static void full_total(void)
{
	const uint32_t goal = console_find_object(FUNCTION_goal_update, 0u);
	char text[16];
	uint32_t time;
	int letters;

	if (routes.total_shown || !goal || OBJECT(goal)->state < 2u)
		return;
	routes.total_shown = true;
	time = routes.time_base + screen->progress.checkpoint_times_by_player[screen->player + 0x28u];
	letters = snprintf(text, sizeof text, "%u'%02u\"%02u", (unsigned)(time / 6000u), (unsigned)(time / 100u % 60u),
		(unsigned)(time % 100u));
	for (int letter = 0; letter <= letters; letter++)
		OBJECT_TEXT_CHARACTERS(goal + 0x86u)[letter] = (uint8_t)text[letter];
}

/* On the left seat's frame, before its game mode's code: REVERSE's goal picked on the route map once the
   car select is over and its fade out too (game_mode_play step 5, before the game loads the stage: the game
   waits meanwhile, true), then the race's first stage that goal, and the start line's colours; BACK there
   gives the game up; FULL's counts and its total; over with the game */
bool console_routes_frame(uint32_t mode, uint32_t pressed)
{
	if (routes.running == ROUTE_NONE)
		return false;
	if (mode != GAME_MODE_PLAY)
	{
		routes.running = ROUTE_NONE;
		return false;
	}
	if (routes.running == ROUTE_FULL)
	{
		full_fold();
		full_total();
		return false;
	}
	if (screen->game_step != 5u || (screen->flags & 1u) != 0u)
		return false;
	if (routes.goal == 0)
	{
		if (routes.given_goal == 0)
		{
			if (!console_route_select_running())
				console_route_select_start(RACE_MODEL(RACE(screen->race)));
			switch (console_route_select_frame(pressed))
			{
				case ROUTE_SELECT_START:
					routes.given_goal = FIRST_GOAL + console_route_select_goal();
					break;
				case ROUTE_SELECT_BACK:
					console_quit_game();
					return true;
				default:
					return true;
			}
		}
		routes.goal = routes.given_goal;
		/* (the race's start: REVERSE_START_EXTRA more in the setting the car adds to its 40 seconds) */
		routes.start_difficulty = game->race_copies[0].master_time_difficulty[0];
		game->race_copies[0].master_time_difficulty[0] = (int8_t)(routes.start_difficulty + REVERSE_START_EXTRA);
		routes.time_given = false;
	}
	screen->first_stage = (uint8_t)routes.goal;
	screen->route_stage = (uint8_t)routes.goal;
	screen->built_stage = (uint8_t)routes.goal;
	start_line_colours();
	return false;
}

/* ---- the game's functions the routes put their own in place of ---- */

/* The race's course: the route's for its first stage */
void player_car_start_course(void)
{
	uint16_t width_code;
	uint32_t course;

	if (!route_here())
	{
		original_player_car_start_course();
		return;
	}
	/* (as the game's, from the route's course) */
	move_halfwords(address_of(screen) + 0x2000u, zero_block_ADDRESS, 0x200u);
	course = route_running()->courses[screen->first_stage];
	player_car->course = course;
	width_code = COURSE_START(course)->width_code;
	player_car->width_code = (uint8_t)width_code;
	player_car->road_width = width_code * 0xB0u;
	player_car->prebuilt_segments = screen->draw_distance >> 7;
	do
	{
		player_car_build_segment(0xFFFu);
	} while ((int16_t)player_car->built_segments < (int16_t)player_car->prebuilt_segments);
	screen->current_stage_level = screen->built_stage_level;
	player_car->stage_history[0] = screen->route_stage;
}

/* The roadside objects: REVERSE's goal stage, where it starts, from its own stream; its pre-stage, where it
   finishes, too; FULL's goal stages, which it reaches at a fork (the game's state 2 takes the next stage's
   stream), from theirs */
void road_object_stream_read(void)
{
	const int stage = screen->built_stage;
	const bool reverse = route_here() && routes.running == ROUTE_REVERSE;
	const bool full = route_here() && routes.running == ROUTE_FULL;

	if (reverse && current_object->state == 0u && stage >= FIRST_GOAL && stage < STAGES)
	{
		current_object->state = 1u;
		current_object->road_object_stream.stream = routes.reverse.goal_streams[stage - FIRST_GOAL];
	}
	if (current_object->state == 2u && current_object->road_object_stream.stage == stage
		&& ((reverse && stage == 0) || (full && stage >= FIRST_GOAL && stage < LAST_STAGE)))
	{
		current_object->state = 1u;
		current_object->road_object_stream.stage_start = player_car->built_segments;
		current_object->road_object_stream.stream = reverse ? routes.finish_stream
			: routes.full.goal_streams[stage - FIRST_GOAL];
	}
	original_road_object_stream_read();
	for (uint32_t object = console_find_object(NO_FUNCTION, 0u); object; object = console_find_object(NO_FUNCTION, object))
		OBJECT(object)->handler = CONSOLE_CODE_START_LINE;    /* (a start line's, from the console's stream) */
}

/* The background's page word, as the game writes it: its two halves, the upper pages the low one */
static uint32_t page_word(const struct tilemap_pages *pages)
{
	return pages->upper_pages | (uint32_t)pages->lower_pages << 16u;
}

static void set_page_word(struct tilemap_pages *pages, uint32_t word)
{
	pages->upper_pages = (uint16_t)word;
	pages->lower_pages = (uint16_t)(word >> 16u);
}

/* WIDE's background through a change: the game's own, its last two swaps once WIDE's sides are past them too.
   A change's page words are a set of four (background_page_words), one a step of the view's turn away and
   back: the background the race leaves, whole (the first four of the new one's nine pages); with the turn
   past 2A0, the column it has taken out of sight swapped, its upper page for one that joins the two
   backgrounds (the one the view comes back over: the change itself) and its lower page for the new one's;
   back at 160, the other column for the new one's; at 60, the joining page for the new one's own, the same
   in the half still in sight. Those turns are the 320 columns' (the view is 160 at each side of the turn),
   so WIDE's sides showed the last two swaps half done (the clouds cut): here their page words wait until the
   turn is back the side's columns more, where the wider view is past the column too. The first cannot wait
   (the turn reaches about 2A8): the columns a side shows of its column change as the game swaps it, to the
   joining page's, which goes on from the other column as the old one did. On a route's change that takes the
   other set (turn_background_pages) the joining page is in the column it was not drawn for and its swap for
   the new one's shows in the 320 columns whenever it comes: that one does not wait, the middle as 4:3's */
static struct waiting_page_word
{
	uint32_t object;    /* the background whose page word waits (its address), or 0 */
	uint32_t word;      /* the page word the game wrote */
	int turn;           /* the turn it waits for: this at most */
	bool other_set;     /* the change's set is the turn's, not its background's own */
} wide_page_words[2];   /* by screen */

/* 'starting': the background was being set up (a race's first frame); 'changing': it was in a change, its
   page word 'pages_before' then; 'other_set': the turn took the change's other set this frame */
static void wide_background_pages(bool starting, bool changing, bool other_set, uint32_t pages_before)
{
	const uint32_t written = current_object->race_background.next_page_words - 4u;
	struct tilemap_pages *const pages = TILEMAP_PAGES(current_object->race_background.page_word_address);
	struct waiting_page_word *const waiting = &wide_page_words[screen->index & 1u];
	const int16_t signed_turn = (int16_t)((uint32_t)player_car->view_yaw << 4) >> 4;
	const int turn = signed_turn < 0 ? -signed_turn : signed_turn;
	const int side = console_wide_side();

	if (starting || waiting->object != address_of(current_object))
		waiting->object = 0u;    /* (a race's that is gone) */
	if (side != 0 && changing && current_object->state == 2u && page_word(pages) != pages_before
		&& written >= background_page_words_ADDRESS && written < background_page_words_ADDRESS + 8u * 4u * 4u)
	{
		const uint32_t step = (written - background_page_words_ADDRESS) / 4u % 4u;

		if (step < 2u)
			waiting->other_set = other_set;
		else if (step == 2u || !waiting->other_set)
		{
			waiting->object = address_of(current_object);
			waiting->word = page_word(pages);
			waiting->turn = (step == 2u ? 0x160 : 0x60) - side;
			set_page_word(pages, pages_before);
		}
	}
	/* (at once where the change is over, or WIDE was turned off meanwhile) */
	if (waiting->object != 0u && (turn <= waiting->turn || current_object->state != 2u || side == 0))
	{
		set_page_word(pages, waiting->word);
		waiting->object = 0u;
	}
}

/* A change's columns swapped in the order the view's turn takes them out of sight, on a route's (FULL and
   REVERSE: glitches at their stages' changes, a player's report). The game picks the set of page words by
   the fork's branch, as its road turns the view the branch's way: the branch a route gives for its stage's
   background (fork_background_update) need not be the way the view turns, and the column in sight changed
   at once. The set the turn needs instead (a turn to the right takes the left column first, the branch 0
   set), from its second step on: the first is the same in both. Returns whether that is the other set */
static bool turn_background_pages(uint32_t pages_before)
{
	const uint32_t written = current_object->race_background.next_page_words - 4u;
	const int16_t turn = (int16_t)((uint32_t)player_car->view_yaw << 4) >> 4;
	uint32_t set;

	if (!route_here() || page_word(TILEMAP_PAGES(current_object->race_background.page_word_address)) == pages_before
		|| written < background_page_words_ADDRESS || written >= background_page_words_ADDRESS + 8u * 4u * 4u
		|| (written - background_page_words_ADDRESS) / 4u % 4u != 1u)
		return false;
	set = background_page_words_ADDRESS + (current_object->race_background.page_half * 2u + (turn < 0 ? 1u : 0u)) * 4u * 4u;
	set_page_word(TILEMAP_PAGES(current_object->race_background.page_word_address),
		BACKGROUND_PAGE_WORD_STEPS(set + 4u)[0]);
	current_object->race_background.next_page_words = set + 8u;
	return set != written - 4u;
}

/* The background at a fork: the stage's own (stage_backgrounds; the game takes the fork's stage and the
   branch, which lead forward to its own choice), and none new for the pre-stage, which has none. There
   REVERSE finishes with AUSTRALIA's goal (finish_stream), whose kangaroos take their colours from
   AUSTRALIA's scene palette (at 4C00, the pre-stage's own at 4A00): put after the pre-stage's
   (queue_stage_load), as the game puts it at AUSTRALIA (they were not there, a player's screenshot) */
static void fork_background_update(void)
{
	const uint8_t built_stage = screen->built_stage;
	const uint8_t branch = player_car->branch;
	uint8_t background;

	if (!route_here() || current_object->state != 1u || screen->stage_background_change != 0u
		|| player_car->new_branch == 0u)
	{
		original_race_background_update();
		return;
	}
	if (screen->route_stage == 0u)
	{
		queue_palette(scene_palettes[FIRST_GOAL]);
		player_car->new_branch = 0u;
		original_race_background_update();
		return;
	}
	background = stage_backgrounds[screen->route_stage];
	screen->built_stage = background >> 1;    /* (the game's choice is built_stage * 2 + branch) */
	player_car->branch = background & 1u;
	original_race_background_update();
	screen->built_stage = built_stage;
	player_car->branch = branch;
}

/* Whether 'stage''s course changes its background within the stage (a run's road event 4 or 5: the
   background 2A + stage, road_surface_update) */
static bool background_change_within(int stage)
{
	uint32_t course = stage_courses[stage], size;

	for (;;)
	{
		if (s16_at(course) == -COURSE_JUMP)
		{
			course = u32_at(course + 2u);
			continue;
		}
		if ((size = record_size(course)) == 0)
			return false;
		if (size == 6 && s16_at(course) > 0 && (byte_at(course + 5u) == 4u || byte_at(course + 5u) == 5u))
			return true;
		course += size;
	}
}

/* 16 colours (at 'colours') into the screen's palette at once, 'blocks' times from 'place' on, as
   upload_palette_list puts each entry */
static void palette_blocks_now(uint32_t colours, uint32_t place, uint32_t blocks)
{
	const uint32_t index = screen->index;
	uint32_t address = LINKED((index << 19) | (index >> 13) | 0x600000u, place);
	const uint16_t *source = rom_pointer(colours);

	for (; blocks != 0u; blocks--)
		for (int colour = 0; colour < 16; colour += 2, address += 4u)
			io_write32(address, source[colour] | (uint32_t)source[colour + 1] << 16);
}

/* A list of colour blocks (queue_palette_list's) at once */
static void palette_list_now(uint32_t list)
{
	const uint32_t entries = list != 0u ? (PALETTE_BLOCK_LIST(list)->length
		| (uint32_t)PALETTE_BLOCK_LIST(list)->length_high << 16) >> 3 : 0u;

	for (uint32_t entry = 0; entry < entries; entry++)
	{
		const struct palette_block_entry *block = PALETTE_BLOCK_ENTRY(list + 4u + entry * 8u);

		palette_blocks_now(block->colours, block->place, block->blocks);
	}
}

/* A list of palette fades (start_palette_fades') at its end at once: each one's second palette */
static void palette_fades_ended(uint32_t fade_list)
{
	const uint32_t fades = fade_list != 0u ? PALETTE_FADE_LIST(fade_list)->length / 0xCu : 0u;

	for (uint32_t fade = 0; fade < fades; fade++)
	{
		const struct palette_fade_entry *entry = PALETTE_FADE_ENTRY(fade_list + 4u + fade * 0xCu);

		palette_blocks_now(entry->to, entry->palette_offset, entry->blocks);
	}
}

/* The palettes a route's background stands on (FULL's and REVERSE's: the sky black over a hill, the road's
   colours another stage's, a player's report and screenshots). A change of the game's puts only the
   colours that are not already the stage before's, the one its fork comes from: those of the way there
   stay from the changes before it (the sky of stage 7, Spain, is the first change's, the pre-stage's to
   stage 1). A route comes from another stage, or starts at a goal, so the palette is put as the game's
   own way to 'stage' leaves it: from the race's start at the pre-stage, each change to the next stage of
   the way (stage_backgrounds; its stage before is the background's built_stage, background >> 1) and
   each change within a stage of it (2A + stage, where its course has one): their colours, and their fades
   at their end (the backgrounds', the road's and those of the backgrounds leaving), until the change to
   'stage' itself, which the game puts as ever ('stage' not the pre-stage, which has no change). Returns the road's palette fades the way leaves
   (screen road_palette_fades: the last change with road colours') */
static uint32_t forward_palettes(int stage)
{
	uint8_t way[2 * STAGES];
	int steps = 0, at = stage_backgrounds[stage] >> 1;
	const uint32_t *start_road = rom_pointer(stage_road_palettes[0]);
	uint32_t road_fades = start_road[0];

	for (;;)
	{
		if (background_change_within(at))
			way[steps++] = (uint8_t)(0x2A + at);
		if (at == 0)
			break;
		way[steps++] = stage_backgrounds[at];
		at = stage_backgrounds[at] >> 1;
	}
	palette_list_now(no_background_palettes_ADDRESS);    /* (the pre-stage's start: race_background_update) */
	palette_list_now(stage_road_palettes[0] + 4u);       /* (and road_surface_start) */
	while (steps-- > 0)
	{
		const uint8_t background = way[steps];
		const uint32_t road = background_road_palettes[background];

		palette_list_now(background_palettes[background]);
		palette_fades_ended(background_leaving_fades[background]);
		if (road != 0u)
		{
			road_fades = ROAD_PALETTE_RECORD(road)->road_fades;
			palette_fades_ended(road + 4u);
		}
		palette_fades_ended(background_palette_fades[background]);
	}
	return road_fades;
}

/* The background: the route's own at its forks on the palettes the game's way there leaves, its columns
   swapped the way the view turns, and out of WIDE's sides' sight too */
void race_background_update(void)
{
	const bool starting = current_object->state == 0u;
	const bool changing = current_object->state == 2u;
	const bool first_step = changing && current_object->race_background.change_step == 0u;
	const uint32_t pages_before = changing
		? page_word(TILEMAP_PAGES(current_object->race_background.page_word_address)) : 0u;

	bool other_set = false;

	fork_background_update();
	/* (the palettes under the game's own, which it queues for the vertical blank: the race's first frame,
	   and the first step of a fork's change, as it puts the new background's) */
	if (route_here() && starting && screen->first_stage != 0u)
		forward_palettes(screen->first_stage);    /* (REVERSE's goal; FULL starts at the pre-stage, as the game) */
	else if (route_here() && first_step && current_object->race_background.change_step != 0u
		&& screen->stage_background_change == 0u && screen->route_stage != 0u)
		screen->road_palette_fades = forward_palettes(screen->route_stage);
	if (changing && current_object->state == 2u)
		other_set = turn_background_pages(pages_before);
	wide_background_pages(starting, changing, other_set, pages_before);
}

/* The race's checkpoints: the time a fork adds the route's next stage's (the game's is by the stage its
   forward fork leads to, and has no goal stage's), from a goal stage as from the last fork's (the game
   reads its forks by the stage). REVERSE's time is the game's turned round (Sebas): the whole race's at the
   start, its first course a goal's, the longest, and nothing at the checkpoints, as the game's own race
   gathers it course by course. CLASSIC's seconds more on the game's own route (console_classic_time) */
void race_progress_update(uint32_t *p_host_record, uint32_t *p_racer_copy)
{
	const uint32_t race = screen->race, race_copy = screen->race_copy;
	uint8_t stage, read_stage, passes;

	if (!route_here())
	{
		passes = RACE(race)->state.pass_count;
		original_race_progress_update(p_host_record, p_racer_copy);
		console_classic_time(passes);
		return;
	}
	if (routes.running == ROUTE_REVERSE && !routes.time_given && RACE(race)->state.time_left != 0u)
	{
		/* (the start's time set by the car, player_car_update, with REVERSE_START_EXTRA in the setting: the
		   setting back) */
		game->race_copies[0].master_time_difficulty[0] = routes.start_difficulty;
		routes.time_given = true;
	}
	stage = RACE(race_copy)->state.stage;
	read_stage = stage <= LAST_FORK_STAGE ? stage : LAST_FORK_STAGE;
	passes = RACE(race)->state.pass_count;
	RACE(race_copy)->state.stage = read_stage;
	original_race_progress_update(p_host_record, p_racer_copy);
	RACE(race_copy)->state.stage = stage;
	if (screen->progress.leader_stage == read_stage)
		screen->progress.leader_stage = stage;    /* (one player: the leader) */
	if (RACE(race)->state.pass_count != passes && RACE(race)->state.pass_order == 0u && RACE(race)->state.pass_checkpoint != GOAL_CHECKPOINT
		&& stage < STAGES)
	{
		const uint8_t checkpoint = RACE(race)->state.pass_checkpoint, branch = RACE(race)->state.pass_branch;
		const int next = routes.running == ROUTE_FULL ? stage + 1 : routes.parents[stage][branch & 1u];
		const int given = (int8_t)(stage_start_times[fork_next_stages[read_stage * 2 + branch]]
			+ game->race_copies[0].master_time_difficulty[checkpoint]) * 0x1E;
		const int due = routes.running == ROUTE_REVERSE ? reverse_checkpoint_time(checkpoint)
			: (int8_t)(stage_start_times[next] + game->race_copies[0].master_time_difficulty[checkpoint]) * 0x1E;

		RACE(race)->state.time_left = (uint16_t)(RACE(race)->state.time_left + due - given);
	}
}

/* The STAGE label: the stage's own level (the game's counts the forks taken, PRE-STAGE to FINAL STAGE) */
void stage_indicator_draw(void)
{
	if (!route_here())
	{
		original_stage_indicator_draw();
		return;
	}
	draw_text(rom_pointer(stage_level_texts[level_of(screen->route_stage)]));
	draw_object(0u);
}

/* After the race: no route map (it draws the forward route, by its forks) */
void route_map_update(void)
{
	if (!route_here())
	{
		original_route_map_update();
		return;
	}
	if (screen->game_step == 14u)
	{
		screen->game_step = 16u;
		current_object->flags = 0u;
		current_object->flags_high = 0u;
	}
}

/* No course record: the game keeps them by goal, and the routes' are not its (the game's hands back its zero
   flag: clear when beaten) */
uint32_t course_record_beaten(void)
{
	return route_here() ? 1u : original_course_record_beaten();
}

uint32_t course_records_update(uint32_t record_entry)
{
	return routes.running == ROUTE_NONE ? original_course_records_update(record_entry) : record_entry;
}
