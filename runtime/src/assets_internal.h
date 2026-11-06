/**
 *    Name:        assets_internal.h
 *    Description: what the loaders of the game's assets share (assets.c, assets_media.c)
 *    Version:     0.1.0
 *    Created:     27-10-2026
 *    Modified:    30-10-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 */
#ifndef GAME_ASSETS_INTERNAL_H
#define GAME_ASSETS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* (room for the longest path assets_path makes, 1023 characters, with what is said of it) */
#define ASSETS_PROBLEM_SIZE 2048

/* What failed, for assets_load to hand back */
extern char assets_problem[ASSETS_PROBLEM_SIZE];

/* A whole file, closed by a 0 (free it), or NULL */
char *assets_read_file(const char *path);

/* 'path' in 'folder' (a buffer of its own, until the next call) */
const char *assets_path(const char *folder, const char *path);

/* The media of the assets (assets_media.c): the tilesets, the sprite sheets, the MultiPCM's samples and
   their headers; false with assets_problem set when one cannot be read */
int assets_read_tilesets(const char *folder);
int assets_read_sprites(const char *folder);
int assets_read_samples(const char *folder);

/* The driver's bytes of a sound program's sequence in a MIDI file (assets_midi.c, the way back of
   tools/formats/sound_music.py): into out (room bytes), its length, and the file's effect flags (-1 without
   them); false when it is no such file or its bytes do not fit */
int assets_midi_sequence(const uint8_t *blob, size_t size, uint8_t *out, size_t room, size_t *length, int *flags);

/* The assets pack (assets_pack.c), little endian, its parts 4-byte aligned: the programs' data (assets.c),
   then the media (assets_media.c), each written from what is loaded and read in its place, the loaded
   pointing into the pack's bytes (kept by the caller); false (with assets_problem set when reading) when
   it cannot */
typedef struct
{
	const uint8_t *at;
	const uint8_t *end;
} pack_reader;

int pack_put32(FILE *output, uint32_t value);
int pack_put_bytes(FILE *output, const void *bytes, size_t size);    /* (then up to 4-byte alignment) */
int pack_get32(pack_reader *reader, uint32_t *value);
const uint8_t *pack_get_bytes(pack_reader *reader, size_t size);
int assets_pack_write_data(FILE *output);
int assets_pack_read_data(pack_reader *reader);
int assets_pack_write_media(FILE *output);
int assets_pack_read_media(pack_reader *reader);

#endif
