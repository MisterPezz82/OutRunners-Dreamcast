/*
 *    Name:        tasks_kos.c
 *    Description: KOS backend for host/tasks.h using KOS threads and semaphores.
 *    Version:     0.1.0
 *
 *    license:BSD-3-Clause
 *    This is an experimental backend. It relies on the KOS scheduler to handle
 *    SH4 register and FPU state. The stack size (DC_TASK_STACK_SIZE) is a default
 *    and may need measurement for specific game tasks. There is no cleanup API;
 *    task lifetime is tied to the process.
 */

#include <host/tasks.h>
#include <kos/thread.h>
#include <kos/sem.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <malloc.h>

/* Debug evidence for the Flycast capture: each game task's stack is pre-filled with a
   pattern so its high-water mark and an overflow of the bottom guard can be observed. */
#define DC_STACK_FILL 0xA5A5A5A5u
#define DC_STACK_GUARD_WORDS 64u
#define DC_MAX_TASKS 4u
volatile uint32_t dc_task_count = 0;
volatile uint32_t dc_task_stack_base[DC_MAX_TASKS];
volatile uint32_t dc_task_stack_min_free[DC_MAX_TASKS];
volatile uint32_t dc_stack_fault = 0;

#ifndef DC_TASK_STACK_SIZE
/* (measured over whole scripted races: at most about 8.7 KiB used; 128 KiB leaves 15 times that, and
   the guard words below still catch an overflow) */
#define DC_TASK_STACK_SIZE (128u * 1024u)
#endif

struct host_task {
    void (*start)(void);
    kthread_t *thread;
    semaphore_t gate;
};

static host_task main_task;
static bool main_initialized = false;
static host_task *g_current = NULL;

static void fatal(const char *msg) {
    fprintf(stderr, "tasks_kos: %s\n", msg);
    exit(3);
}

static void *task_wrapper(void *arg) {
    host_task *task = (host_task *)arg;
    
    /* Wait for the gate to be signaled before starting */
    if (sem_wait(&task->gate) != 0) {
        fatal("sem_wait failed in task_wrapper");
    }

    if (task->start) {
        task->start();
    }

    /* If start returns, it is fatal as per spec */
    fatal("task start returned unexpectedly");
    return NULL;
}

host_task *host_task_main(void) {
    if (!main_initialized) {
        main_task.start = NULL;
        main_task.thread = NULL;
        if (sem_init(&main_task.gate, 0) != 0) {
            fatal("sem_init failed for main task");
        }
        main_initialized = true;
    }
    return &main_task;
}

host_task *host_task_create(void (*start)(void)) {
    if (start == NULL) {
        fatal("host_task_create: start is NULL");
    }

    /* Ensure main task is initialized */
    host_task_main();

    host_task *task = (host_task *)calloc(1, sizeof(host_task));
    if (task == NULL) {
        fatal("calloc failed for new task");
    }

    task->start = start;
    task->thread = NULL;

    if (sem_init(&task->gate, 0) != 0) {
        free(task);
        fatal("sem_init failed for new task");
    }

    uint32_t *stack = (uint32_t *)memalign(32, DC_TASK_STACK_SIZE);
    if (stack == NULL) {
        free(task);
        fatal("stack allocation failed");
    }
    for (uint32_t i = 0; i < DC_TASK_STACK_SIZE / 4u; i++)
        stack[i] = DC_STACK_FILL;
    if (dc_task_count < DC_MAX_TASKS) {
        dc_task_stack_base[dc_task_count] = (uint32_t)(uintptr_t)stack;
        dc_task_stack_min_free[dc_task_count] = DC_TASK_STACK_SIZE;
        dc_task_count++;
    }

    kthread_attr_t attr = {0};
    attr.stack_size = DC_TASK_STACK_SIZE;
    attr.stack_ptr = stack;
    attr.create_detached = true;
    attr.label = "outrunners-task";

    kthread_t *thd = thd_create_ex(&attr, task_wrapper, task);
    if (thd == NULL) {
        free(task);
        fatal("thd_create_ex failed");
    }

    task->thread = thd;

    /* DO NOT signal the gate here. Child must stay blocked until first explicit host_task_switch. */

    return task;
}

void host_task_switch(host_task *to) {
    if (to == NULL) {
        fatal("host_task_switch: to is NULL");
    }

    /* Initialize main if needed */
    host_task_main();

    host_task *from = g_current;
    if (from == NULL) {
        g_current = &main_task;
        from = &main_task;
    }

    if (from == to) {
        return; /* No-op */
    }

    g_current = to;

    /* Signal the target task to run */
    if (sem_signal(&to->gate) != 0) {
        fatal("sem_signal failed in switch");
    }

    /* Wait for the source task to be signaled back */
    if (sem_wait(&from->gate) != 0) {
        fatal("sem_wait failed in switch");
    }
}

host_task *host_task_current(void) {
    host_task_main();
    if (g_current == NULL) {
        g_current = &main_task;
    }
    return g_current;
}

/* Returns nonzero if a task's bottom guard has been overwritten. A full high-water scan is
   done when 'full' is set (it reads the whole stack, so call it occasionally). */
int dc_task_stack_check(int full) {
    for (uint32_t t = 0; t < dc_task_count; t++) {
        const uint32_t *stack = (const uint32_t *)(uintptr_t)dc_task_stack_base[t];
        for (uint32_t i = 0; i < DC_STACK_GUARD_WORDS; i++) {
            if (stack[i] != DC_STACK_FILL) {
                dc_stack_fault = 0x57AC0000u | t;
                return 1;
            }
        }
    }
    /* the unused depth, measured a slice a call (a whole stack at once took milliseconds, a frame's
       worth every 64 frames): the scan runs up from the guard to the first word in use */
    static uint32_t scan_task, scan_word = DC_STACK_GUARD_WORDS;
    if (full && dc_task_count) {
        if (scan_task >= dc_task_count) { scan_task = 0; scan_word = DC_STACK_GUARD_WORDS; }
        const uint32_t *stack = (const uint32_t *)(uintptr_t)dc_task_stack_base[scan_task];
        const uint32_t end = DC_TASK_STACK_SIZE / 4u;
        uint32_t i = scan_word, limit = scan_word + 1024u < end ? scan_word + 1024u : end;
        while (i < limit && stack[i] == DC_STACK_FILL)
            i++;
        if (i < limit || i == end) {
            dc_task_stack_min_free[scan_task] = i * 4u;
            scan_task++;
            scan_word = DC_STACK_GUARD_WORDS;
        } else {
            scan_word = i;
        }
    }
    return 0;
}
