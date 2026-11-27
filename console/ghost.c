/**
 *    Name:        ghost.c
 *    Description: & KNUCKLES > TIME ATTACK's ghost: the fastest run kept, a goal each, and raced again
 *                 beside the player as a see-through car
 *    Version:     0.1.0
 *    Created:     29-10-2026
 *    Modified:    24-11-2026
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

#ifdef ORUNNERS_GHOST_PACKED
/* ---- packed runs (a machine with little memory: the Dreamcast) ----
   A run's frames as the change of each byte's change from the frame before (a counter, a place moving
   at a steady speed: 0), the bytes that are not 0 kept with a mask of 8 groups of 8 and a mask for each
   group with any: about 13 bytes a frame instead of 64 (a 5.5 minute run: 130 KiB), in chunks taken as
   the run goes. Only two runs are kept: the one being recorded and the fastest, raced again; none are
   written anywhere (a ghost is too big for a VMU), so ghosts last until the console is switched off. */
#define PACK_CHUNK 16384
#define PACK_MOST (1 + 8 + RECORDED_BYTES)    /* (a frame's most: every byte changed) */

typedef struct pack_chunk
{
	struct pack_chunk *next;
	uint32_t used;
	uint8_t data[PACK_CHUNK];
} pack_chunk;

typedef struct
{
	pack_chunk *first, *last;
	uint32_t frames, time;
	uint8_t model, car_number;
} packed_run;

typedef struct
{
	uint8_t prev[RECORDED_BYTES], change[RECORDED_BYTES];
} pack_coder;

typedef struct
{
	const pack_chunk *chunk;
	uint32_t at, frame;    /* (frame: the frame in 'coder.prev'; 0xFFFFFFFF none yet) */
	pack_coder coder;
} pack_reader;

static packed_run recorded, best;
static pack_coder recorder;
static bool record_full;    /* (the memory ran out: this run is not kept) */
static pack_reader reader;

static void free_run(packed_run *run)
{
	while (run->first)
	{
		pack_chunk *next = run->first->next;

		free(run->first);
		run->first = next;
	}
	memset(run, 0, sizeof(*run));
}

/* Adds a frame; false when the memory has run out */
static bool pack_frame(packed_run *run, pack_coder *coder, const uint8_t *frame)
{
	uint8_t out[PACK_MOST], values[RECORDED_BYTES];
	uint8_t groups = 0;
	int size = 1;

	for (int group = 0; group < 8; group++)
	{
		uint8_t mask = 0;
		int count = 0;

		for (int index = 0; index < 8; index++)
		{
			const int at = group * 8 + index;
			const uint8_t change = (uint8_t)(frame[at] - coder->prev[at]);
			const uint8_t value = (uint8_t)(change - coder->change[at]);

			coder->prev[at] = frame[at];
			coder->change[at] = change;
			if (value)
			{
				mask |= (uint8_t)(1u << index);
				values[count++] = value;
			}
		}
		if (mask)
		{
			groups |= (uint8_t)(1u << group);
			out[size++] = mask;
			memcpy(&out[size], values, (size_t)count);
			size += count;
		}
	}
	out[0] = groups;
	if (!run->last || run->last->used + (uint32_t)size > PACK_CHUNK)
	{
		pack_chunk *chunk = malloc(sizeof(pack_chunk));

		if (!chunk)
			return false;
		chunk->next = NULL;
		chunk->used = 0;
		if (run->last)
			run->last->next = chunk;
		else
			run->first = chunk;
		run->last = chunk;
	}
	memcpy(&run->last->data[run->last->used], out, (size_t)size);
	run->last->used += (uint32_t)size;
	run->frames++;
	return true;
}

static void reader_start(pack_reader *reader, const packed_run *run)
{
	memset(reader, 0, sizeof(*reader));
	reader->chunk = run->first;
	reader->frame = 0xFFFFFFFFu;
}

/* The next frame into reader->coder.prev */
static void reader_next(pack_reader *reader)
{
	const uint8_t *in;
	uint8_t groups;

	if (reader->chunk && reader->at >= reader->chunk->used)
	{
		reader->chunk = reader->chunk->next;
		reader->at = 0;
	}
	if (!reader->chunk)
		return;
	in = &reader->chunk->data[reader->at];
	groups = *in++;
	for (int group = 0; group < 8; group++)
	{
		const uint8_t mask = (groups >> group) & 1u ? *in++ : 0u;

		for (int index = 0; index < 8; index++)
		{
			const int at = group * 8 + index;

			if ((mask >> index) & 1u)
				reader->coder.change[at] = (uint8_t)(reader->coder.change[at] + *in++);
			reader->coder.prev[at] = (uint8_t)(reader->coder.prev[at] + reader->coder.change[at]);
		}
	}
	reader->at = (uint32_t)(in - reader->chunk->data);
	reader->frame++;
}

/* Frame 'frame' of the run (in order, as a race goes; back to the start when asked for an earlier one) */
static const uint8_t *reader_frame(pack_reader *reader, const packed_run *run, uint32_t frame)
{
	if (reader->frame != 0xFFFFFFFFu && frame < reader->frame)
		reader_start(reader, run);
	while (reader->frame == 0xFFFFFFFFu || reader->frame < frame)
		reader_next(reader);
	return reader->coder.prev;
}

/* ---- the fastest run on a VMU ----
   The packed run is too big for a VMU (130 KiB for 5.5 minutes), so it goes there sampled: every
   SAMPLE_EVERY frames (3 a second) the car's place, speed and the like (the bytes that change smoothly)
   as their change from the sample before, and between samples only the bytes that change now and then (the
   car's pose, its palette, the sprays) where they change. Read back, the frames between samples are filled
   in: the place on the road and across it and in the world along a curve through the samples
   (Catmull-Rom), the rest in a straight line. About 22 KiB for 5.5 minutes, some 45 of a VMU's 200
   blocks. */
#define SAMPLE_EVERY 30
#define SAMPLE_MAGIC 0x5347524Fu    /* "ORGS" */
#define SAMPLE_FIELDS 14
#define POSE_BYTE 26                 /* (0x4A, the car's frame: its own stream) */

/* the smooth fields: their place in the recorded bytes and size (4 or 2 bytes, signed) */
static const uint8_t field_at[SAMPLE_FIELDS] = { 0, 4, 8, 16, 18, 20, 22, 27, 28, 30, 36, 38, 60, 0xFF };
static const uint8_t field_size[SAMPLE_FIELDS] = { 4, 4, 4, 2, 2, 2, 2, 1, 2, 2, 2, 2, 4, 0 };
enum { F_WX, F_WY, F_WZ, F_FRAC, F_SEG, F_ROADX, F_WINDOW, F_SLOT, F_SPEED, F_SIDE, F_PITCH, F_BOUNCE, F_TIME, F_COUNT = 13 };

static bool smooth_byte(int at)
{
	for (int f = 0; f < F_COUNT; f++)
		if (at >= field_at[f] && at < field_at[f] + field_size[f])
			return true;
	return false;
}

static int32_t field_get(const uint8_t *frame, int f)
{
	const uint8_t *p = frame + field_at[f];
	if (field_size[f] == 4) return (int32_t)(p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
	if (field_size[f] == 2) return f == F_FRAC || f == F_SEG || f == F_WINDOW || f == F_SPEED ? (int32_t)(p[0] | p[1] << 8) : (int16_t)(p[0] | p[1] << 8);
	return p[0];
}

static void field_put(uint8_t *frame, int f, int32_t v)
{
	uint8_t *p = frame + field_at[f];
	p[0] = (uint8_t)v;
	if (field_size[f] >= 2) p[1] = (uint8_t)(v >> 8);
	if (field_size[f] == 4) { p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
}

typedef struct { uint8_t *data; size_t size, room; bool failed; } byte_out;

static void out_byte(byte_out *o, uint8_t b)
{
	if (o->failed) return;
	if (o->size == o->room)
	{
		const size_t room = o->room ? o->room * 2 : 8192;
		uint8_t *data = realloc(o->data, room);
		if (!data) { o->failed = true; return; }
		o->data = data; o->room = room;
	}
	o->data[o->size++] = b;
}

static void out_varint(byte_out *o, int32_t v)
{
	uint32_t z = ((uint32_t)v << 1) ^ (uint32_t)(v >> 31);
	while (z >= 0x80) { out_byte(o, (uint8_t)(z | 0x80)); z >>= 7; }
	out_byte(o, (uint8_t)z);
}

static void out_u32(byte_out *o, uint32_t v)
{
	for (int i = 0; i < 4; i++) out_byte(o, (uint8_t)(v >> (8 * i)));
}

typedef struct { const uint8_t *at, *end; bool failed; } byte_in;

static uint8_t in_byte(byte_in *in)
{
	if (in->at >= in->end) { in->failed = true; return 0; }
	return *in->at++;
}

static int32_t in_varint(byte_in *in)
{
	uint32_t z = 0;
	for (int shift = 0; shift < 35; shift += 7)
	{
		const uint8_t b = in_byte(in);
		z |= (uint32_t)(b & 0x7f) << shift;
		if (!(b & 0x80)) break;
	}
	return (int32_t)(z >> 1) ^ -(int32_t)(z & 1);
}

static uint32_t in_u32(byte_in *in)
{
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) v |= (uint32_t)in_byte(in) << (8 * i);
	return v;
}

static uint32_t ghost_generation;    /* (a new fastest run: the VMU's copy is out of date) */

uint32_t console_ghost_generation(void)
{
	return ghost_generation;
}

/* The fastest run out of memory (2 PLAYERS needs it: the machine keeps it elsewhere and gives it back with
   console_ghost_import), and the port's way to read it back when TIME ATTACK starts */
void (*console_ghost_reload)(void);

void console_ghost_release(void)
{
	free_run(&best);
	free_run(&recorded);
	ghost.raced = -1;
}

/* The fastest run, sampled (malloc'd, the caller frees it); 0 none */
size_t console_ghost_export(uint8_t **result)
{
	byte_out o = { 0 }, events = { 0 }, poses = { 0 };
	uint32_t last_pose = 0;
	pack_reader r;
	uint8_t frame[RECORDED_BYTES], before[RECORDED_BYTES];
	int32_t last[F_COUNT] = { 0 };
	uint32_t samples = 0, last_event = 0;

	*result = NULL;
	if (!best.frames)
		return 0;
	reader_start(&r, &best);
	out_u32(&o, SAMPLE_MAGIC);
	out_u32(&o, 2);
	out_u32(&o, best.frames);
	out_u32(&o, best.time);
	out_byte(&o, best.model);
	out_byte(&o, best.car_number);
	out_byte(&o, SAMPLE_EVERY);
	out_byte(&o, 0);
	for (uint32_t i = 0; i < best.frames; i++)
	{
		memcpy(frame, reader_frame(&r, &best, i), RECORDED_BYTES);
		if (i == 0)
		{
			for (int at = 0; at < RECORDED_BYTES; at++) out_byte(&o, frame[at]);
			for (int f = 0; f < F_COUNT; f++) last[f] = field_get(frame, f);
			samples = 1;
		}
		else
		{
			for (int at = 0; at < RECORDED_BYTES; at++)
				if (at == POSE_BYTE && frame[at] != before[at])
				{
					/* the car's pose (it counts through its frames): the frames since the last change and the
					   change, in a byte where they are small */
					const uint32_t gap = i - last_pose;
					const int change = (int)frame[at] - (int)before[at];
					if (gap <= 15 && change >= -8 && change <= 7 && !(gap == 0))
						out_byte(&poses, (uint8_t)(gap << 4 | (unsigned)(change & 15)));
					else
					{
						out_byte(&poses, 0);
						out_varint(&poses, (int32_t)gap);
						out_byte(&poses, frame[at]);
					}
					last_pose = i;
				}
				else if (!smooth_byte(at) && frame[at] != before[at])
				{
					out_varint(&events, (int32_t)(i - last_event) + 1);    /* (0 is the end) */
					out_byte(&events, (uint8_t)at);
					out_byte(&events, frame[at]);
					last_event = i;
				}
			if (i % SAMPLE_EVERY == 0 || i == best.frames - 1)
			{
				for (int f = 0; f < F_COUNT; f++)
				{
					const int32_t v = field_get(frame, f);
					out_varint(&o, v - last[f]);
					last[f] = v;
				}
				samples++;
			}
		}
		memcpy(before, frame, RECORDED_BYTES);
	}
	out_varint(&events, 0);    /* (the end) */
	out_byte(&poses, 0);
	out_varint(&poses, 0);
	out_u32(&o, samples);
	out_u32(&o, (uint32_t)poses.size);
	for (size_t i = 0; i < poses.size; i++) out_byte(&o, poses.data[i]);
	for (size_t i = 0; i < events.size; i++) out_byte(&o, events.data[i]);
	free(events.data);
	free(poses.data);
	if (o.failed || events.failed || poses.failed) { free(o.data); return 0; }
	*result = o.data;
	return o.size;
}

static int32_t curve(int32_t p0, int32_t p1, int32_t p2, int32_t p3, int t, int n)
{
	/* Catmull-Rom at t/n between p1 and p2, in 64-bit (world places are large) */
	const int64_t a = 2 * (int64_t)p1, b = (int64_t)p2 - p0, c = 2 * (int64_t)p0 - 5 * (int64_t)p1 + 4 * (int64_t)p2 - p3,
		d = -(int64_t)p0 + 3 * (int64_t)p1 - 3 * (int64_t)p2 + p3;
	const int64_t n3 = (int64_t)n * n * n;
	const int64_t v = a * n3 + b * t * n * n + c * t * t * n + d * t * t * t;
	return (int32_t)((v >= 0 ? v + n3 : v - n3) / (2 * n3));
}

/* A sampled run (as console_ghost_export writes it) as the fastest run: false if it does not read */
bool console_ghost_import(const uint8_t *data, size_t size)
{
	byte_in in = { data, data + size, false };
	uint8_t frame[RECORDED_BYTES], first[RECORDED_BYTES];
	if (in_u32(&in) != SAMPLE_MAGIC || in_u32(&in) != 2)
		return false;
	const uint32_t frames = in_u32(&in), time = in_u32(&in);
	const uint8_t model = in_byte(&in), car_number = in_byte(&in), every = in_byte(&in);
	(void)in_byte(&in);
	if (in.failed || frames == 0 || frames > GHOST_FRAMES || every == 0)
		return false;
	for (int at = 0; at < RECORDED_BYTES; at++) first[at] = in_byte(&in);
	const uint32_t samples = (frames - 1) / every + 2;    /* (at most: the first, every 'every', the last) */
	int32_t (*keys)[F_COUNT] = malloc((size_t)(samples + 1) * sizeof(*keys));
	uint32_t *key_frame = malloc((size_t)(samples + 1) * sizeof(uint32_t));
	if (!keys || !key_frame) { free(keys); free(key_frame); return false; }
	uint32_t count = 0;
	for (int f = 0; f < F_COUNT; f++) keys[0][f] = field_get(first, f);
	key_frame[count++] = 0;
	for (uint32_t i = 1; i < frames && !in.failed; i++)
		if (i % every == 0 || i == frames - 1)
		{
			if (count > samples) { in.failed = true; break; }
			for (int f = 0; f < F_COUNT; f++) keys[count][f] = keys[count - 1][f] + in_varint(&in);
			key_frame[count++] = i;
		}
	if (in.failed || in_u32(&in) != count) { free(keys); free(key_frame); return false; }
	const uint32_t pose_bytes = in_u32(&in);
	if (in.failed || pose_bytes > (size_t)(in.end - in.at)) { free(keys); free(key_frame); return false; }
	byte_in pose = { in.at, in.at + pose_bytes, false };
	in.at += pose_bytes;
	/* the pose's next change: its frame and value (or none) */
	uint32_t pose_frame = 0;
	int pose_value = -1, pose_change = 0;
	bool pose_full = false;
#define POSE_NEXT() do { \
		const uint8_t b = in_byte(&pose); \
		if (b) { pose_frame += b >> 4; pose_change = ((b & 15) ^ 8) - 8; pose_full = false; pose_value = 0; } \
		else { const int32_t g = in_varint(&pose); if (g <= 0) pose_value = -1; else { pose_frame += (uint32_t)g; pose_value = in_byte(&pose); pose_full = true; } } \
	} while (0)
	POSE_NEXT();
	/* the frames, rebuilt and packed */
	packed_run run = { 0 };
	pack_coder coder;
	memset(&coder, 0, sizeof(coder));
	memcpy(frame, first, RECORDED_BYTES);
	int32_t gap = in_varint(&in);
	uint32_t next_event = gap > 0 ? (uint32_t)(gap - 1) : 0;
	bool ok = true;
	uint32_t k = 0;
	for (uint32_t i = 0; i < frames && ok; i++)
	{
		while (gap != 0 && next_event == i && !in.failed)
		{
			const uint8_t at = in_byte(&in), value = in_byte(&in);
			if (at < RECORDED_BYTES) frame[at] = value;
			gap = in_varint(&in);
			if (gap > 0) next_event += (uint32_t)(gap - 1);
		}
		while (pose_value >= 0 && pose_frame == i && !pose.failed)
		{
			frame[POSE_BYTE] = pose_full ? (uint8_t)pose_value : (uint8_t)(frame[POSE_BYTE] + pose_change);
			POSE_NEXT();
		}
		while (k + 1 < count && key_frame[k + 1] <= i) k++;
		const uint32_t a = key_frame[k], b = k + 1 < count ? key_frame[k + 1] : a;
		const int t = (int)(i - a), n = (int)(b > a ? b - a : 1);
		const int32_t *k0 = keys[k > 0 ? k - 1 : 0], *k1 = keys[k], *k2 = keys[k + 1 < count ? k + 1 : k],
			*k3 = keys[k + 2 < count ? k + 2 : (k + 1 < count ? k + 1 : k)];
		if (t == 0)
			for (int f = 0; f < F_COUNT; f++) field_put(frame, f, k1[f]);
		else
		{
			for (int f = F_WX; f <= F_WZ; f++) field_put(frame, f, curve(k0[f], k1[f], k2[f], k3[f], t, n));
			field_put(frame, F_ROADX, curve(k0[F_ROADX], k1[F_ROADX], k2[F_ROADX], k3[F_ROADX], t, n));
			/* along the road: the segment and its fraction as one distance (128 a segment) */
			const int32_t d0 = k0[F_SEG] * 128 + k0[F_FRAC], d1 = k1[F_SEG] * 128 + k1[F_FRAC],
				d2 = k2[F_SEG] * 128 + k2[F_FRAC], d3 = k3[F_SEG] * 128 + k3[F_FRAC];
			int32_t d = curve(d0, d1, d2, d3, t, n);
			if (d < d1 && d1 <= d2) d = d1;
			if (d > d2 && d1 <= d2) d = d2;
			field_put(frame, F_SEG, d >> 7);
			field_put(frame, F_FRAC, d & 127);
			field_put(frame, F_WINDOW, (d >> 7) + (k1[F_WINDOW] - k1[F_SEG]));
			field_put(frame, F_SLOT, (k1[F_SLOT] + t) & 7);
			for (int f = F_SPEED; f <= F_TIME; f++)
				field_put(frame, f, k1[f] + (int32_t)(((int64_t)(k2[f] - k1[f]) * t) / n));
		}
		ok = pack_frame(&run, &coder, frame);
	}
	free(keys);
	free(key_frame);
	if (!ok || in.failed)
	{
		free_run(&run);
		return false;
	}
	free_run(&best);
	best = run;
	best.time = time;
	best.model = model;
	best.car_number = car_number;
	return true;
}
#endif

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

#ifdef ORUNNERS_GHOST_PACKED
void console_ghost_start(void)
{
	const uint32_t record = race_copy_offsets[GHOST_PLAYER] + GAME_VARIABLES;

#ifdef ORUNNERS_GHOST_TEST
	/* (host checks: a ghost file of the unpacked build's, packed as the race's best before its first race) */
	if (!best.frames && getenv("ORUNNERS_GHOST_FILE"))
	{
		FILE *file = fopen(getenv("ORUNNERS_GHOST_FILE"), "rb");
		uint32_t magic, frames, time, cars;
		uint8_t frame[RECORDED_BYTES];
		pack_coder coder = { { 0 }, { 0 } };

		if (file && get32(file, &magic) && get32(file, &frames) && get32(file, &time) && get32(file, &cars))
		{
			while (best.frames < frames && fread(frame, 1, RECORDED_BYTES, file) == RECORDED_BYTES)
				pack_frame(&best, &coder, frame);
			best.time = time;
			best.model = (uint8_t)cars;
			best.car_number = (uint8_t)(cars >> 8);
			fprintf(stderr, "ghost: %u frames packed\n", (unsigned)best.frames);
			if (getenv("ORUNNERS_GHOST_SAMPLED"))
			{
				uint8_t *sampled = NULL;
				const size_t bytes = console_ghost_export(&sampled);
				packed_run exact = best;
				memset(&best, 0, sizeof(best));
				const bool back = bytes && console_ghost_import(sampled, bytes);
				if (back)
				{
					pack_reader ra, rb;
					uint8_t fa[RECORDED_BYTES];
					reader_start(&ra, &exact);
					reader_start(&rb, &best);
					long hist[4][6] = { { 0 } };
					const int fl[4] = { F_ROADX, F_WX, F_WZ, F_SEG };
					for (uint32_t i = 0; i < exact.frames; i++)
					{
						memcpy(fa, reader_frame(&ra, &exact, i), RECORDED_BYTES);
						const uint8_t *fb = reader_frame(&rb, &best, i);
						for (int at = 0; at < RECORDED_BYTES; at++)
							if (!smooth_byte(at) && fa[at] != fb[at]) { hist[0][5] += 1000000; break; }
						for (int q = 0; q < 4; q++)
						{
							int32_t e = fl[q] == F_SEG ? (field_get(fa, F_SEG) * 128 + field_get(fa, F_FRAC)) - (field_get(fb, F_SEG) * 128 + field_get(fb, F_FRAC))
								: field_get(fa, fl[q]) - field_get(fb, fl[q]);
							if (e < 0) e = -e;
							hist[q][e == 0 ? 0 : e < 4 ? 1 : e < 16 ? 2 : e < 64 ? 3 : e < 256 ? 4 : 5]++;
						}
						if (getenv("ORUNNERS_GHOST_TRACE") && i >= (uint32_t)atoi(getenv("ORUNNERS_GHOST_TRACE")) && i < (uint32_t)atoi(getenv("ORUNNERS_GHOST_TRACE")) + 30)
							fprintf(stderr, "t %u roadx %d %d seg %d/%d %d/%d\n", (unsigned)i, field_get(fa, F_ROADX), field_get(fb, F_ROADX),
								field_get(fa, F_SEG), field_get(fa, F_FRAC), field_get(fb, F_SEG), field_get(fb, F_FRAC));
					}
					for (int q = 0; q < 4; q++)
						fprintf(stderr, "ghost err %d: 0:%ld <4:%ld <16:%ld <64:%ld <256:%ld more:%ld\n", q, hist[q][0], hist[q][1], hist[q][2], hist[q][3], hist[q][4], hist[q][5]);
				}
				free_run(&exact);
				fprintf(stderr, "ghost: sampled %u bytes (%u VMU blocks), read back %d\n", (unsigned)bytes, (unsigned)((bytes + 511) / 512 + 1), back);
				FILE *o = fopen(getenv("ORUNNERS_GHOST_SAMPLED"), "wb");
				if (o) { fwrite(sampled, 1, bytes, o); fclose(o); }
				free(sampled);
			}
		}
		if (file)
			fclose(file);
	}
#endif
	if (!best.frames && console_ghost_reload)
		console_ghost_reload();    /* (released for 2 PLAYERS: from the VMU again) */
	ghost.raced = best.frames ? 0 : -1;
	ghost.frame = 0;
	ghost.started = false;
	ghost.object_count = 0;
	free_run(&recorded);
	memset(&recorder, 0, sizeof(recorder));
	record_full = false;
	if (best.frames)
	{
		reader_start(&reader, &best);
		RACE_SET_MODEL(RACE(record), best.model);
		RACE(record)->car_number = best.car_number;
	}
}

void console_ghost_frame(void)
{
	const uint32_t own = screen->race_copy;
	const uint32_t record = race_copy_offsets[GHOST_PLAYER] + GAME_VARIABLES;

	if (screen->game_mode != GAME_MODE_PLAY)
		return;
	if (!ghost.started && screen->race_started != 0u)
		ghost.started = true;
	if (!ghost.started)
		;
	else if (RACE(own)->state.finished == 0u && !record_full && recorded.frames < GHOST_FRAMES)
	{
		if (!pack_frame(&recorded, &recorder, ram(own + RECORDED_FROM)))
		{
			record_full = true;    /* (out of memory: the run goes on, not kept) */
			free_run(&recorded);
		}
	}
	else if (RACE(own)->state.finished != 0u && recorded.frames)
	{
		/* at the goal: the fastest yet? (then it is the one raced again) */
		const uint32_t time = RACE(own)->state.race_time;

		if (!record_full && (!best.frames || time < best.time))
		{
			const bool racing_best = ghost.raced >= 0;

			free_run(&best);
			best = recorded;
			ghost_generation++;
			best.time = time;
			best.model = RACE_MODEL(RACE(own));
			best.car_number = RACE(own)->car_number;
			memset(&recorded, 0, sizeof(recorded));
			if (racing_best)
				ghost.raced = -1;    /* (the run it was racing is gone: no ghost for the rest of this race) */
		}
		else
			free_run(&recorded);
	}

	if (ghost.raced >= 0 && best.frames)
	{
		const uint32_t at = ghost.frame < best.frames ? ghost.frame : best.frames - 1u;    /* (0 until the start) */
		const uint8_t *frame = reader_frame(&reader, &best, at);
		const uint8_t stage = frame[RACE_STAGE - RECORDED_FROM];
		bool on_route = stage == RACE(own)->state.stage;

		for (uint32_t taken = 0; taken <= player_car->forks_taken && taken < 6u && !on_route; taken++)
			on_route = player_car->stage_history[taken] == stage;
		memcpy(ram(record + RECORDED_FROM), frame, RECORDED_BYTES);
		if (!on_route)
			RACE(record)->state.segment = (uint16_t)(RACE(own)->state.segment - FAR_BEHIND);
		RACE(record)->state.finished = 0u;
	}
	else if (ghost.object_count)
		RACE(record)->state.segment = (uint16_t)(RACE(own)->state.segment - FAR_BEHIND);    /* (its run replaced: out of sight) */
#ifdef ORUNNERS_GHOST_TEST
	if (getenv("ORUNNERS_GHOST_DEBUG") && ghost.frame % 60 == 0)
		fprintf(stderr, "ghost frame %u raced %d objects %d seg %u own %u stage %u own %u\n", (unsigned)ghost.frame, ghost.raced,
			ghost.object_count, (unsigned)RACE(record)->state.segment, (unsigned)RACE(own)->state.segment, (unsigned)RACE(record)->state.stage, (unsigned)RACE(own)->state.stage);
#endif
	if (ghost.started)
		ghost.frame++;
}
#else
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
		RACE_SET_MODEL(RACE(record), ghost.runs[ghost.raced].model);
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
	else if (RACE(own)->state.finished == 0u && ghost.recording.frames < GHOST_FRAMES)
	{
		memcpy(ghost.recording.data[ghost.recording.frames], ram(own + RECORDED_FROM), RECORDED_BYTES);
		ghost.recording.frames++;
	}
	else if (RACE(own)->state.finished != 0u && ghost.recording.frames)
	{
		/* at the goal: the goal's fastest? */
		const int goal = screen->built_stage - (int)FIRST_GOAL;
		ghost_run *kept = goal >= 0 && goal < GOALS ? &ghost.runs[goal] : NULL;
		const uint32_t time = RACE(own)->state.race_time;

		if (kept && (!kept->frames || time < kept->time))
		{
			free(kept->data);
			kept->data = malloc((size_t)ghost.recording.frames * RECORDED_BYTES);
			if (kept->data)
			{
				memcpy(kept->data, ghost.recording.data, (size_t)ghost.recording.frames * RECORDED_BYTES);
				kept->frames = ghost.recording.frames;
				kept->time = time;
				kept->model = RACE_MODEL(RACE(own));
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
		bool on_route = stage == RACE(own)->state.stage;

		/* on the player's route: the stage the player is on, or one the player has taken (behind) */
		for (uint32_t taken = 0; taken <= player_car->forks_taken && taken < 6u && !on_route; taken++)
			on_route = player_car->stage_history[taken] == stage;
		memcpy(ram(record + RECORDED_FROM), run->data[at], RECORDED_BYTES);
		if (!on_route)
			RACE(record)->state.segment = (uint16_t)(RACE(own)->state.segment - FAR_BEHIND);    /* (another route: not drawn) */
		RACE(record)->state.finished = 0u;
	}
	if (ghost.started)
		ghost.frame++;
}
#endif

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
	model = RACE_MODEL(RACE(record)) & 7u;
	car_number = RACE(record)->car_number & 7u;
	car = address_of(start_object());
	OBJECT(car)->handler = FUNCTION_race_car_draw;
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
		OBJECT(object)->handler = FUNCTION_rider_gesture_update;
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
			call_function(ghost.handlers[index]);
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
