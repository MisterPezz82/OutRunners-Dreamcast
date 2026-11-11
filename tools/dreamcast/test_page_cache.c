/*
 * Copyright (c) 2024 Dreamcast Streaming Media Project
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <stdint.h>
#include "../../ports/dreamcast/page_cache.h"

static uint8_t expected_byte(uint32_t i) { return (uint8_t)((i * 73 + 11) & 255); }

int main(void) {
    char tmpl[] = "/tmp/dc_cache_test_XXXXXX";
    int fd = mkstemp(tmpl);
    assert(fd >= 0);
    FILE *f = fdopen(fd, "wb");
    assert(f);

    const size_t FILE_SIZE = 100003;
    const size_t PAGE_SIZE = 4096;
    const size_t SLOTS = 2;

    for (size_t i = 0; i < FILE_SIZE; i++) fputc(expected_byte(i), f);
    fclose(f);

    /* Test 1: Basic open, hits, eviction */
    dc_page_cache *c = dc_cache_open(tmpl, PAGE_SIZE, SLOTS);
    assert(c != NULL);
    assert(dc_cache_bytes(c) == 2 * (PAGE_SIZE + sizeof(uint32_t)));
    
    uint64_t m0 = dc_cache_misses(c);
    const uint8_t *p0 = dc_cache_span(c, 0, 100);
    assert(p0 != NULL);
    assert(dc_cache_misses(c) == m0 + 1);
    
    const uint8_t *p0b = dc_cache_span(c, 0, 100);
    assert(p0b == p0);
    assert(dc_cache_misses(c) == m0 + 1);

    /* Evict page 0 by loading page 2 */
    const uint8_t *p2 = dc_cache_span(c, 8192, 100);
    assert(p2 != NULL);
    assert(dc_cache_misses(c) == m0 + 2);

    /* Page 0 evicted, refill reuses slot */
    const uint8_t *p0c = dc_cache_span(c, 0, 100);
    assert(p0c != NULL);
    assert(p0c == p0);
    assert(dc_cache_misses(c) == m0 + 3);
    
    for (int i = 0; i < 100; i++) assert(p0c[i] == expected_byte(i));
    dc_cache_close(c);

    /* Test 2: Random accesses bounded to page */
    c = dc_cache_open(tmpl, PAGE_SIZE, SLOTS);
    assert(c != NULL);
    
    unsigned int seed = 12345;
    for (int i = 0; i < 1000; i++) {
        seed = seed * 1103515245 + 12345;
        uint32_t off = seed % FILE_SIZE;
        size_t max_cnt = PAGE_SIZE - (off % PAGE_SIZE);
        if (max_cnt > 17) max_cnt = 17;
        if (off + max_cnt > FILE_SIZE) max_cnt = FILE_SIZE - off;
        
        const uint8_t *ptr = dc_cache_span(c, off, max_cnt);
        assert(ptr != NULL);
        for (size_t j = 0; j < max_cnt; j++) {
            assert(ptr[j] == expected_byte(off + j));
        }
    }

    /* Last page: computed offset */
    uint32_t last_off = (FILE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
    size_t last_cnt = FILE_SIZE - last_off;
    const uint8_t *last_ptr = dc_cache_span(c, last_off, last_cnt);
    assert(last_ptr != NULL);
    for (size_t j = 0; j < last_cnt; j++) {
        assert(last_ptr[j] == expected_byte(last_off + j));
    }
    dc_cache_close(c);

    /* Test 3: Invalid span (cross-page) causes sticky failure */
    c = dc_cache_open(tmpl, PAGE_SIZE, SLOTS);
    assert(c != NULL);
    const uint8_t *bad = dc_cache_span(c, 4000, 100);
    assert(bad == NULL);
    assert(dc_cache_failed(c) == 1);
    
    /* Sticky failure: subsequent valid span also NULL */
    const uint8_t *bad2 = dc_cache_span(c, 0, 10);
    assert(bad2 == NULL);
    dc_cache_close(c);

    /* Test 4: Edge cases */
    /* Count 0 is invalid per contract (positive count required) */
    c = dc_cache_open(tmpl, PAGE_SIZE, SLOTS);
    assert(c != NULL);
    const uint8_t *z = dc_cache_span(c, 0, 0);
    assert(z == NULL);
    assert(dc_cache_failed(c) == 1);
    dc_cache_close(c);

    /* Out of file */
    c = dc_cache_open(tmpl, PAGE_SIZE, SLOTS);
    assert(c != NULL);
    const uint8_t *oob = dc_cache_span(c, FILE_SIZE, 10);
    assert(oob == NULL);
    assert(dc_cache_failed(c) == 1);
    dc_cache_close(c);

    /* SIZE_MAX count rejection */
    c = dc_cache_open(tmpl, PAGE_SIZE, SLOTS);
    assert(c != NULL);
    const uint8_t *max_cnt = dc_cache_span(c, 0, SIZE_MAX);
    assert(max_cnt == NULL);
    assert(dc_cache_failed(c) == 1);
    dc_cache_close(c);

    /* Bad params */
    assert(dc_cache_open(tmpl, 0, 2) == NULL);
    assert(dc_cache_open(tmpl, 4096, 0) == NULL);
    assert(dc_cache_open("/nonexistent_file_xyz", 4096, 2) == NULL);

    /* Test 5: Truncate errors */
    c = dc_cache_open(tmpl, PAGE_SIZE, SLOTS);
    assert(c != NULL);
    assert(truncate(tmpl, 0) == 0);
    
    const uint8_t *trunc = dc_cache_span(c, 0, 10);
    assert(trunc == NULL);
    assert(dc_cache_failed(c) == 1);
    
    uint64_t m_before = dc_cache_misses(c);
    const uint8_t *trunc2 = dc_cache_span(c, 0, 10);
    assert(trunc2 == NULL);
    assert(dc_cache_misses(c) == m_before);
    dc_cache_close(c);

    /* Test 6: 2-way set-associative LRU: pages 0 and 2 share a set and both stay */
    assert(dc_cache_open_assoc(tmpl, PAGE_SIZE, 3, 2) == NULL);    /* slots % ways != 0 */
    {
        FILE *g = fopen(tmpl, "wb");
        assert(g);
        for (size_t i = 0; i < FILE_SIZE; i++) fputc(expected_byte(i), g);
        fclose(g);
    }
    c = dc_cache_open_assoc(tmpl, PAGE_SIZE, 4, 2);    /* 2 sets x 2 ways */
    assert(c != NULL);
    m0 = dc_cache_misses(c);
    assert(dc_cache_span(c, 0, 10) != NULL);            /* page 0, set 0 */
    assert(dc_cache_span(c, 2 * 4096, 10) != NULL);     /* page 2, set 0 */
    assert(dc_cache_misses(c) == m0 + 2);
    assert(dc_cache_span(c, 0, 10) != NULL);            /* hit; page 2 now least recent */
    assert(dc_cache_misses(c) == m0 + 2);
    assert(dc_cache_span(c, 4 * 4096, 10) != NULL);     /* page 4 evicts page 2 */
    assert(dc_cache_misses(c) == m0 + 3);
    const uint8_t *q0 = dc_cache_span(c, 0, 10);        /* page 0 still resident */
    assert(q0 != NULL && dc_cache_misses(c) == m0 + 3);
    for (int i = 0; i < 10; i++) assert(q0[i] == expected_byte(i));
    const uint8_t *q2 = dc_cache_span(c, 2 * 4096 + 5, 10);    /* page 2 reloads */
    assert(q2 != NULL && dc_cache_misses(c) == m0 + 4);
    for (int i = 0; i < 10; i++) assert(q2[i] == expected_byte(2 * 4096 + 5 + i));
    dc_cache_close(c);

    /* Test 7: asynchronous loading: every page arrives through the loader with the right bytes */
    c = dc_cache_open_assoc(tmpl, PAGE_SIZE, 8, 4);
    assert(c != NULL);
    assert(dc_cache_start_loader(c, 2) == 0);
    {
        const uint32_t pages = (uint32_t)((FILE_SIZE + PAGE_SIZE - 1) / PAGE_SIZE);
        for (uint32_t pg = 0; pg < pages; pg++) {
            const uint32_t off = pg * (uint32_t)PAGE_SIZE + 7;
            const size_t cnt = off + 9 <= FILE_SIZE ? 9 : FILE_SIZE - off;
            const uint8_t *q = NULL;
            int pending = 0, spins = 0;
            do {
                q = dc_cache_span_nowait(c, off, cnt, &pending);
                if (!q) { struct timespec ts = { 0, 1000000 }; dc_cache_pump(c); nanosleep(&ts, NULL); }
                assert(++spins < 5000);
            } while (!q);
            assert(!pending);
            for (size_t j = 0; j < cnt; j++) assert(q[j] == expected_byte(off + j));
        }
        assert(dc_cache_pages_loaded(c) >= pages - 8);
        assert(dc_cache_failed(c) == 0);
    }
    dc_cache_close(c);

    unlink(tmpl);
    printf("PASS\n");
    return 0;
}
