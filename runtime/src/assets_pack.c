/**
 *    Name:        assets_pack.c
 *    Description: the game's assets as one file, the pack: written from the assets loaded, and loaded in
 *                 their place (game/assets.h)
 *    Version:     0.1.0
 *    Created:     30-10-2026
 *    Modified:    30-10-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 *    Format (little endian, its parts 4-byte aligned): "ORAP", u32 the pack's version, u32 the tables'
 *    version (pack_tables_version: the game's tables, their names and addresses, which a build of another
 *    layout reads elsewhere); then the programs' data (assets.c: the V70's pieces, then the sound
 *    program's, each its address, size and bytes) and the media (assets_media.c: which tiles there are and
 *    their pens, the sprite blocks and their words, the MultiPCM's headers and samples). It is made on the
 *    player's computer from the player's ROM set, as the assets are, and never shared.
 */
#include "game/assets.h"

#include "assets_internal.h"

#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#define aligned_alloc(alignment, size) malloc(size)    /* (the alignment is for the Dreamcast's disc DMA; Windows
                                                         has no aligned_alloc that free() takes) */
#endif
#include <string.h>

#define PACK_MAGIC      "ORAP"
#define PACK_VERSION    1u

/* ---- the parts' bytes ---- */

int pack_put32(FILE *output, uint32_t value)
{
	const uint8_t bytes[4] = { (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24) };

	return fwrite(bytes, 1, 4, output) == 4;
}

int pack_put_bytes(FILE *output, const void *bytes, size_t size)
{
	static const uint8_t zeros[4] = { 0 };
	const size_t padding = (4u - size % 4u) % 4u;

	return fwrite(bytes, 1, size, output) == size && fwrite(zeros, 1, padding, output) == padding;
}

int pack_get32(pack_reader *reader, uint32_t *value)
{
	if (reader->end - reader->at < 4)
		return 0;
	*value = reader->at[0] | (uint32_t)reader->at[1] << 8 | (uint32_t)reader->at[2] << 16 | (uint32_t)reader->at[3] << 24;
	reader->at += 4;
	return 1;
}

const uint8_t *pack_get_bytes(pack_reader *reader, size_t size)
{
	const uint8_t *bytes = reader->at;
	const size_t aligned = size + (4u - size % 4u) % 4u;

	if ((size_t)(reader->end - reader->at) < aligned)
		return NULL;
	reader->at += aligned;
	return bytes;
}

/* ---- the pack ---- */

/* The game's tables, their names and addresses (FNV-1a): what a pack is made for */
static uint32_t pack_tables_version(void)
{
	uint32_t hash = 2166136261u;

	for (size_t index = 0; index < game_table_count; index++)
	{
		const char *name = game_tables[index].name;
		const uint32_t address = game_tables[index].address;

		for (size_t letter = 0; name[letter]; letter++)
			hash = (hash ^ (uint8_t)name[letter]) * 16777619u;
		for (int byte = 0; byte < 4; byte++)
			hash = (hash ^ (uint8_t)(address >> (8 * byte))) * 16777619u;
	}
	return hash;
}

const char *assets_pack_write(const char *path)
{
	FILE *output = fopen(path, "wb");
	int written;

	if (!output)
		return "the assets pack cannot be written";
	written = fwrite(PACK_MAGIC, 1, 4, output) == 4 && pack_put32(output, PACK_VERSION)
		&& pack_put32(output, pack_tables_version()) && assets_pack_write_data(output) && assets_pack_write_media(output);
	if (fclose(output) != 0 || !written)
	{
		remove(path);
		return "the assets pack cannot be written";
	}
	return NULL;
}

const char *assets_load_pack_bytes(const uint8_t *bytes, size_t size)
{
	pack_reader reader = { bytes, bytes + size };
	uint32_t version, tables;

	assets_problem[0] = 0;
	if (size < 12 || memcmp(bytes, PACK_MAGIC, 4) != 0)
		return "the assets pack is not one";
	reader.at += 4;
	if (!pack_get32(&reader, &version) || version != PACK_VERSION || !pack_get32(&reader, &tables)
		|| tables != pack_tables_version())
		return "the assets pack is of another version of the game";
	if (!assets_pack_read_data(&reader) || !assets_pack_read_media(&reader))
		return assets_problem[0] ? assets_problem : "the assets pack is cut short";
	return NULL;
}

const char *assets_load_pack(const char *path)
{
	FILE *input = fopen(path, "rb");
	long size;
	uint8_t *bytes;

	if (!input)
		return "the assets pack cannot be read";
	setvbuf(input, NULL, _IONBF, 0);    /* (one read for the whole pack, not one a buffer) */
	if (fseek(input, 0, SEEK_END) != 0 || (size = ftell(input)) <= 0 || fseek(input, 0, SEEK_SET) != 0
		|| !(bytes = aligned_alloc(32, ((size_t)size + 31) & ~(size_t)31)) || fread(bytes, 1, (size_t)size, input) != (size_t)size)
	{
		fclose(input);
		return "the assets pack cannot be read";
	}
	fclose(input);
	return assets_load_pack_bytes(bytes, (size_t)size);    /* (its bytes kept: the assets point into them) */
}
