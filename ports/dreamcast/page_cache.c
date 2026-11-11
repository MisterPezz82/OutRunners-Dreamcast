/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "page_cache.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#define aligned_alloc(alignment, size) malloc(size)    /* (the alignment is for the Dreamcast's disc DMA; Windows
                                                         has no aligned_alloc that free() takes) */
#endif
#include <string.h>
#include <limits.h>

struct dc_page_cache {
    FILE *fp;
    uint8_t *payload;
    uint32_t *tags;
    uint32_t *stamps;     /* last use, for least-recently-used replacement within a set */
    uint32_t clock;
    size_t page_size;
    size_t slots;
    size_t ways;          /* slots per set; 1 = direct-mapped */
    uint64_t file_size;
    uint64_t misses;
    int failed;
    char path[256];
    struct dc_loader *loader;    /* asynchronous loading, when started */
};

dc_page_cache* dc_cache_open(const char *path, size_t page_size, size_t slots) {
    return dc_cache_open_assoc(path, page_size, slots, 1);
}

dc_page_cache* dc_cache_open_assoc(const char *path, size_t page_size, size_t slots, size_t ways) {
    if (!path || page_size == 0 || slots == 0 || ways == 0 || slots % ways != 0) return NULL;

    FILE *fp = fopen(path, "rb");
    if (fp) setvbuf(fp, NULL, _IONBF, 0);
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long size_l = ftell(fp);
    if (size_l <= 0 || (uint64_t)size_l > UINT32_MAX) {
        fclose(fp);
        return NULL;
    }

    uint64_t file_size = (uint64_t)size_l;

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    if (slots > (SIZE_MAX / page_size)) {
        fclose(fp);
        return NULL;
    }

    size_t payload_bytes = slots * page_size;
    size_t tag_bytes = slots * sizeof(uint32_t);

    if (payload_bytes > SIZE_MAX - tag_bytes) {
        fclose(fp);
        return NULL;
    }


    /* (32-byte aligned: KOS reads the disc by DMA straight into such buffers, sector by sector into others) */
    uint8_t *payload = (uint8_t*)aligned_alloc(32, (payload_bytes + 31) & ~(size_t)31);
    if (!payload) {
        fclose(fp);
        return NULL;
    }

    uint32_t *tags = (uint32_t*)malloc(tag_bytes);
    if (!tags) {
        free(payload);
        fclose(fp);
        return NULL;
    }

    memset(tags, 0xFF, tag_bytes);

    uint32_t *stamps = (uint32_t*)calloc(slots, sizeof(uint32_t));
    if (!stamps) {
        free(tags);
        free(payload);
        fclose(fp);
        return NULL;
    }

    dc_page_cache *cache = (dc_page_cache*)malloc(sizeof(dc_page_cache));
    if (!cache) {
        free(stamps);
        free(tags);
        free(payload);
        fclose(fp);
        return NULL;
    }

    cache->fp = fp;
    cache->payload = payload;
    cache->tags = tags;
    cache->stamps = stamps;
    cache->clock = 0;
    cache->ways = ways;
    cache->page_size = page_size;
    cache->slots = slots;
    cache->file_size = file_size;
    cache->misses = 0;
    cache->failed = 0;
    cache->loader = NULL;
    snprintf(cache->path, sizeof(cache->path), "%s", path);

    return cache;
}

const uint8_t* dc_cache_span(dc_page_cache* cache, uint32_t offset, size_t count) {
    if (!cache || cache->failed) return NULL;
    if (count == 0) {
        cache->failed = 1;
        return NULL;
    }

    if ((uint64_t)offset > cache->file_size || count > cache->file_size - offset) {
        cache->failed = 1;
        return NULL;
    }

    uint32_t page_num = offset / cache->page_size;
    uint32_t offset_in_page = offset % cache->page_size;

    if (count > cache->page_size - offset_in_page) {
        cache->failed = 1;
        return NULL;
    }

    const size_t sets = cache->slots / cache->ways;
    const size_t base = (page_num % sets) * cache->ways;
    size_t slot = base;
    int hit = 0;
    for (size_t w = 0; w < cache->ways; w++) {
        if (cache->tags[base + w] == page_num) {
            slot = base + w;
            hit = 1;
            break;
        }
        if (cache->stamps[base + w] < cache->stamps[slot]) slot = base + w;
    }
    cache->stamps[slot] = ++cache->clock;
    uint8_t *page_ptr = cache->payload + (slot * cache->page_size);

    if (!hit) {
        cache->tags[slot] = 0xFFFFFFFFu;    /* invalid until the read succeeds */
        uint64_t page_start = (uint64_t)page_num * cache->page_size;
        if (page_start > LONG_MAX) {
            cache->failed = 1;
            return NULL;
        }

        if (fseek(cache->fp, (long)page_start, SEEK_SET) != 0) {
            cache->failed = 1;
            return NULL;
        }

        size_t bytes_to_read = cache->page_size;
        if (page_start + cache->page_size > cache->file_size) {
            bytes_to_read = cache->file_size - page_start;
        }

        if (fread(page_ptr, 1, bytes_to_read, cache->fp) != bytes_to_read) {
            cache->failed = 1;
            return NULL;
        }

        if (bytes_to_read < cache->page_size) {
            memset(page_ptr + bytes_to_read, 0xFF, cache->page_size - bytes_to_read);
        }

        cache->tags[slot] = page_num;
        cache->misses++;
    }

    return page_ptr + offset_in_page;
}

/* ---- asynchronous loading ----
   A loader thread reads requested pages, each into a staging buffer of its own, with a FILE of its
   own; the cache itself is changed only by its owner (dc_cache_pump, dc_cache_span_nowait), so the
   two share only the staging slots' states, under one lock. */

#ifdef _arch_dreamcast
#include <kos/thread.h>
#include <kos/sem.h>
#include <kos/mutex.h>
typedef mutex_t lock_t;
#define LOCK_INIT(l) mutex_init(l, MUTEX_TYPE_NORMAL)
#define LOCK(l) mutex_lock(l)
#define UNLOCK(l) mutex_unlock(l)
typedef semaphore_t signal_t;
#define SIGNAL_INIT(s) sem_init(s, 0)
#define SIGNAL_POST(s) sem_signal(s)
#define SIGNAL_WAIT(s) sem_wait(s)
#else
#include <pthread.h>
#include <semaphore.h>
typedef pthread_mutex_t lock_t;
#define LOCK_INIT(l) pthread_mutex_init(l, NULL)
#define LOCK(l) pthread_mutex_lock(l)
#define UNLOCK(l) pthread_mutex_unlock(l)
typedef sem_t signal_t;
#define SIGNAL_INIT(s) sem_init(s, 0, 0)
#define SIGNAL_POST(s) sem_post(s)
#define SIGNAL_WAIT(s) sem_wait(s)
#endif

enum { STAGE_FREE, STAGE_QUEUED, STAGE_LOADING, STAGE_DONE, STAGE_FAILED };

typedef struct dc_stage {
    volatile int state;
    uint32_t page;
    uint8_t *buffer;
} dc_stage;

struct dc_loader {
    dc_page_cache *cache;
    FILE *fp;
    lock_t lock;
    signal_t work;
    int count;
    dc_stage *stages;
    volatile int quit;
#ifdef _arch_dreamcast
    kthread_t *thread;
#else
    pthread_t thread;
#endif
    uint64_t pages_loaded;
    uint64_t requests_refused;   /* every staging buffer busy */
};

static void *loader_main(void *arg) {
    struct dc_loader *l = (struct dc_loader *)arg;
    for (;;) {
        SIGNAL_WAIT(&l->work);
        if (l->quit) break;
        for (;;) {
            int pick = -1;
            LOCK(&l->lock);
            for (int i = 0; i < l->count; i++)
                if (l->stages[i].state == STAGE_QUEUED) { pick = i; l->stages[i].state = STAGE_LOADING; break; }
            UNLOCK(&l->lock);
            if (pick < 0) break;
            dc_stage *st = &l->stages[pick];
            const uint64_t start = (uint64_t)st->page * l->cache->page_size;
            size_t bytes = l->cache->page_size;
            if (start + bytes > l->cache->file_size) bytes = (size_t)(l->cache->file_size - start);
            int ok = start <= LONG_MAX && fseek(l->fp, (long)start, SEEK_SET) == 0 &&
                fread(st->buffer, 1, bytes, l->fp) == bytes;
            if (ok && bytes < l->cache->page_size)
                memset(st->buffer + bytes, 0xFF, l->cache->page_size - bytes);
            LOCK(&l->lock);
            st->state = ok ? STAGE_DONE : STAGE_FAILED;
            UNLOCK(&l->lock);
        }
    }
    return NULL;
}

#ifdef _arch_dreamcast
static void *loader_entry(void *arg) { return loader_main(arg); }
#endif

int dc_cache_start_loader(dc_page_cache *cache, int staging) {
    if (!cache || cache->loader || staging <= 0) return -1;
    struct dc_loader *l = (struct dc_loader *)calloc(1, sizeof(*l));
    if (!l) return -1;
    l->cache = cache;
    l->count = staging;
    l->stages = (dc_stage *)calloc((size_t)staging, sizeof(dc_stage));
    l->fp = fopen(cache->path, "rb");
    if (l->fp) setvbuf(l->fp, NULL, _IONBF, 0);
    if (!l->stages || !l->fp) { if (l->fp) fclose(l->fp); free(l->stages); free(l); return -1; }
    for (int i = 0; i < staging; i++) {
        l->stages[i].buffer = (uint8_t *)aligned_alloc(32, (cache->page_size + 31) & ~(size_t)31);
        if (!l->stages[i].buffer) return -1;
    }
    LOCK_INIT(&l->lock);
    SIGNAL_INIT(&l->work);
#ifdef _arch_dreamcast
    kthread_attr_t attr = {0};
    attr.stack_size = 16 * 1024;
    attr.create_detached = false;
    attr.label = "page-loader";
    /* at the game's own priority: it spends its time waiting for the disc, and below the game it was
       starved (the frame no longer waits for the PowerVR, which was when it used to run) */
    attr.prio = PRIO_DEFAULT;
    l->thread = thd_create_ex(&attr, loader_entry, l);
    if (!l->thread) return -1;
#else
    if (pthread_create(&l->thread, NULL, loader_main, l) != 0) return -1;
#endif
    cache->loader = l;
    return 0;
}

/* the slot a page goes to (least recently used of its set), its tag invalid until filled */
static size_t choose_slot(dc_page_cache *cache, uint32_t page_num) {
    const size_t sets = cache->slots / cache->ways;
    const size_t base = (page_num % sets) * cache->ways;
    size_t slot = base;
    for (size_t w = 1; w < cache->ways; w++)
        if (cache->stamps[base + w] < cache->stamps[slot]) slot = base + w;
    return slot;
}

static const uint8_t *find_page(dc_page_cache *cache, uint32_t page_num) {
    const size_t sets = cache->slots / cache->ways;
    const size_t base = (page_num % sets) * cache->ways;
    for (size_t w = 0; w < cache->ways; w++)
        if (cache->tags[base + w] == page_num) {
            cache->stamps[base + w] = ++cache->clock;
            return cache->payload + (base + w) * cache->page_size;
        }
    return NULL;
}

void dc_cache_pump(dc_page_cache *cache) {
    struct dc_loader *l = cache ? cache->loader : NULL;
    if (!l) return;
    LOCK(&l->lock);
    for (int i = 0; i < l->count; i++) {
        dc_stage *st = &l->stages[i];
        if (st->state == STAGE_FAILED) { cache->failed = 1; st->state = STAGE_FREE; continue; }
        if (st->state != STAGE_DONE) continue;
        if (!find_page(cache, st->page)) {
            const size_t slot = choose_slot(cache, st->page);
            memcpy(cache->payload + slot * cache->page_size, st->buffer, cache->page_size);
            cache->tags[slot] = st->page;
            cache->stamps[slot] = ++cache->clock;
            cache->misses++;
            l->pages_loaded++;
        }
        st->state = STAGE_FREE;
    }
    UNLOCK(&l->lock);
}

const uint8_t *dc_cache_span_nowait(dc_page_cache *cache, uint32_t offset, size_t count, int *pending) {
    *pending = 0;
    if (!cache || cache->failed) return NULL;
    if (!cache->loader) return dc_cache_span(cache, offset, count);
    if (count == 0 || (uint64_t)offset > cache->file_size || count > cache->file_size - offset) {
        cache->failed = 1;
        return NULL;
    }
    const uint32_t page_num = offset / cache->page_size;
    const uint32_t in_page = offset % cache->page_size;
    if (count > cache->page_size - in_page) { cache->failed = 1; return NULL; }
    const uint8_t *page = find_page(cache, page_num);
    if (page) return page + in_page;
    /* not here: finished loading (installed now), on its way, or asked for */
    struct dc_loader *l = cache->loader;
    *pending = 1;
    LOCK(&l->lock);
    int free_stage = -1, known = 0;
    for (int i = 0; i < l->count; i++) {
        if (l->stages[i].state != STAGE_FREE && l->stages[i].page == page_num) known = 1;
        else if (l->stages[i].state == STAGE_FREE && free_stage < 0) free_stage = i;
    }
    if (!known && free_stage >= 0) {
        l->stages[free_stage].page = page_num;
        l->stages[free_stage].state = STAGE_QUEUED;
    } else if (!known) {
        l->requests_refused++;
    }
    UNLOCK(&l->lock);
    if (!known && free_stage >= 0) SIGNAL_POST(&l->work);
    if (known) {
        dc_cache_pump(cache);
        page = find_page(cache, page_num);
        if (page) { *pending = 0; return page + in_page; }
    }
    return NULL;
}

uint64_t dc_cache_pages_loaded(const dc_page_cache *cache) {
    return cache && cache->loader ? cache->loader->pages_loaded : 0;
}

static void loader_stop(struct dc_loader *l) {
    l->quit = 1;
    SIGNAL_POST(&l->work);
#ifdef _arch_dreamcast
    thd_join(l->thread, NULL);
#else
    pthread_join(l->thread, NULL);
#endif
    fclose(l->fp);
    for (int i = 0; i < l->count; i++) free(l->stages[i].buffer);
    free(l->stages);
    free(l);
}

static void loader_stop(struct dc_loader *l);

void dc_cache_close(dc_page_cache* cache) {
    if (!cache) return;
    if (cache->loader) loader_stop(cache->loader);
    if (cache->fp) fclose(cache->fp);
    if (cache->payload) free(cache->payload);
    if (cache->tags) free(cache->tags);
    if (cache->stamps) free(cache->stamps);
    free(cache);
}

size_t dc_cache_bytes(const dc_page_cache* cache) {
    if (!cache) return 0;
    return (cache->slots * cache->page_size) + (cache->slots * sizeof(uint32_t));
}

uint64_t dc_cache_misses(const dc_page_cache* cache) {
    if (!cache) return 0;
    return cache->misses;
}

int dc_cache_failed(const dc_page_cache* cache) {
    if (!cache) return 1;
    return cache->failed;
}

