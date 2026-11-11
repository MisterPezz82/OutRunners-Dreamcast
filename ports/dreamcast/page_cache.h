/*
 * Copyright (c) 2024 Dreamcast Streaming Media Project
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DC_PAGE_CACHE_H
#define DC_PAGE_CACHE_H

#include <stdint.h>
#include <stddef.h>

typedef struct dc_page_cache dc_page_cache;

dc_page_cache* dc_cache_open(const char *path, size_t page_size, size_t slots);
/* 'ways'-way set-associative with least-recently-used replacement; slots % ways == 0 */
dc_page_cache* dc_cache_open_assoc(const char *path, size_t page_size, size_t slots, size_t ways);
const uint8_t* dc_cache_span(dc_page_cache* cache, uint32_t offset, size_t count);
void dc_cache_close(dc_page_cache* cache);
size_t dc_cache_bytes(const dc_page_cache* cache);
uint64_t dc_cache_misses(const dc_page_cache* cache);
int dc_cache_failed(const dc_page_cache* cache);

/* Asynchronous loading: a thread reads missing pages into 'staging' buffers of their own. After it
   starts, dc_cache_span_nowait returns NULL with *pending set for a page not here yet (and asks for
   it); dc_cache_pump puts finished pages into the cache. Both, like dc_cache_span, only from the
   cache's owner thread. */
int dc_cache_start_loader(dc_page_cache *cache, int staging);
const uint8_t *dc_cache_span_nowait(dc_page_cache *cache, uint32_t offset, size_t count, int *pending);
void dc_cache_pump(dc_page_cache *cache);
uint64_t dc_cache_pages_loaded(const dc_page_cache *cache);

#endif