/*
 * Copyright (c) 2024 Dreamcast Streaming Media Project
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DC_STREAM_MEDIA_H
#define DC_STREAM_MEDIA_H

#include <stdint.h>
#include <stddef.h>

int dc_media_open(const char *folder);
void dc_media_close(void);
const uint8_t* dc_media_tile(uint32_t tile);
uint32_t dc_media_sprite_word(uint32_t word);
uint8_t dc_media_pcm_byte(uint32_t place);
size_t dc_media_cache_bytes(void);
int dc_media_failed(void);
#include <stdbool.h>
int dc_media_sprite_row(uint32_t word, int count, uint32_t *out);
void dc_media_pump(void);
void dc_media_update_stats(void);
void dc_media_probe_heap(void);
uint32_t dc_media_unpack_us_max(void);    /* (the slowest page unpack since the last call) */

#endif