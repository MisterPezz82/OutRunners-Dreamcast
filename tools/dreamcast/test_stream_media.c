/*
 * Copyright (c) 2024 Dreamcast Streaming Media Project
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "game/assets.h"
#include "../../ports/dreamcast/stream_media.h"

#define TILE_COUNT 32768
#define TILE_BYTES 256
#define SPRITE_WORDS 4194304
#define PCM_BYTES 4194304
#define EXPECTED_CACHE 1835184

int main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <original_assets> <stream_folder>\n", argv[0]);
		return 1;
	}

	const char *original = argv[1];
	const char *stream = argv[2];

	const char *err = assets_load(original);
	if (err) {
		fprintf(stderr, "assets_load failed: %s\n", err);
		return 1;
	}

	if (dc_media_open(stream) != 0) {
		fprintf(stderr, "dc_media_open failed\n");
		return 1;
	}

	if (dc_media_failed()) {
		fprintf(stderr, "dc_media_open reported failure\n");
		dc_media_close();
		return 1;
	}

	const s32_graphics *gfx = assets_graphics();
	if (!gfx) {
		fprintf(stderr, "assets_graphics returned NULL\n");
		dc_media_close();
		return 1;
	}

	/* Verify Cache Size */
	if (dc_media_cache_bytes() != EXPECTED_CACHE) {
		fprintf(stderr, "Cache size mismatch: expected %d, got %zu\n", EXPECTED_CACHE, dc_media_cache_bytes());
		dc_media_close();
		return 1;
	}

	/* Compare Tiles */
	for (uint32_t t = 0; t < TILE_COUNT; t++) {
		const uint8_t *orig = gfx->tile_pens[t];
		const uint8_t *dc = dc_media_tile(t);
		if (!orig || !dc) {
			fprintf(stderr, "Tile %u: NULL pointer\n", t);
			dc_media_close();
			return 1;
		}
		for (int b = 0; b < TILE_BYTES; b++) {
			if (orig[b] != dc[b]) {
				fprintf(stderr, "Mismatch tile %u byte %d: expected %u, got %u\n", t, b, orig[b], dc[b]);
				dc_media_close();
				return 1;
			}
		}
	}

	/* Compare Sprites */
	for (uint32_t w = 0; w < SPRITE_WORDS; w++) {
		uint32_t orig = gfx->sprite_word(w);
		uint32_t dc = dc_media_sprite_word(w);
		if (orig != dc) {
			fprintf(stderr, "Mismatch sprite word %u: expected 0x%08x, got 0x%08x\n", w, orig, dc);
			dc_media_close();
			return 1;
		}
	}

	/* Compare PCM */
	for (uint32_t p = 0; p < PCM_BYTES; p++) {
		uint8_t orig = assets_pcm_byte(p);
		uint8_t dc = dc_media_pcm_byte(p);
		if (orig != dc) {
			fprintf(stderr, "Mismatch pcm byte %u: expected %u, got %u\n", p, orig, dc);
			dc_media_close();
			return 1;
		}
	}

	/* Out of bounds checks */
	if (dc_media_sprite_word(SPRITE_WORDS) != 0xffffffff) {
		fprintf(stderr, "Sprite OOB check failed\n");
		dc_media_close();
		return 1;
	}
	
	const uint8_t *tile0 = gfx->tile_pens[0];
	const uint8_t *tile_wrap = dc_media_tile(TILE_COUNT);
	if (!tile_wrap || memcmp(tile0, tile_wrap, TILE_BYTES) != 0) {
		fprintf(stderr, "Tile wrap check failed\n");
		dc_media_close();
		return 1;
	}

	uint8_t pcm0 = assets_pcm_byte(0);
	if (dc_media_pcm_byte(PCM_BYTES) != pcm0) {
		fprintf(stderr, "PCM wrap check failed\n");
		dc_media_close();
		return 1;
	}

	if (dc_media_failed()) {
		fprintf(stderr, "dc_media_failed reported error during checks\n");
		dc_media_close();
		return 1;
	}

	dc_media_close();

	if (!dc_media_failed()) {
		fprintf(stderr, "closed media unexpectedly reported open\n");
		return 1;
	}

	printf("{\"PASS\":true,\"tile_bytes\":8388608,\"sprite_bytes\":16777216,\"pcm_bytes\":4194304,\"cache_bytes\":1835184}\n");
	return 0;
}
