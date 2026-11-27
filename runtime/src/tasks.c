/**
 *    Name:        tasks.c
 *    Description: native stacks for the game's tasks (host/tasks.h): fibers on Windows, the registers on the
 *                 task's stack on the Switch, Android and iOS (aarch64), ucontext elsewhere
 *    Version:     0.1.0
 *    Created:     20-10-2026
 *    Modified:    25-11-2026
 *    Author:      Sebastian Toyos
 *
 *    license:BSD-3-Clause
 */
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600    /* (macOS keeps the ucontext routines behind it, and marks them deprecated) */
#endif
#ifdef __APPLE__
#include <TargetConditionals.h>    /* (TARGET_OS_IPHONE: iOS and its Simulator) */
#endif
#include "host/tasks.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the C of a task nests as deep as the game's calls do, with the locals of the generated code */
#define TASK_STACK_SIZE (16u << 20)

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct host_task
{
	void *fiber;
	void (*start)(void);
};

static host_task main_task;
static host_task *current;

host_task *host_task_main(void)
{
	if (!main_task.fiber)
		main_task.fiber = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(NULL);
	if (!main_task.fiber)
	{
		fprintf(stderr, "The main task could not be set up\n");
		exit(3);
	}
	if (!current)
		current = &main_task;
	return &main_task;
}

static VOID CALLBACK run_task(LPVOID parameter)
{
	((host_task *)parameter)->start();
	fprintf(stderr, "A task came to its end\n");
	exit(3);
}

host_task *host_task_create(void (*start)(void))
{
	host_task *task = calloc(1, sizeof(*task));

	if (task)
	{
		task->start = start;
		task->fiber = CreateFiberEx(64u << 10, TASK_STACK_SIZE, 0, run_task, task);
	}
	if (!task || !task->fiber)
	{
		fprintf(stderr, "A task could not be created\n");
		exit(3);
	}
	return task;
}

void host_task_switch(host_task *to)
{
	if (to == current)
		return;
	current = to;
	SwitchToFiber(to->fiber);
}

#elif defined(__aarch64__) && (defined(__SWITCH__) || defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE))

/* The Switch's C library and Android's have no ucontext, and iOS's has it no longer (Apple's headers: "No longer
   supported"; outrunners-port's pull request 14, yoshisuga's, tried on an iPhone): a task's registers are kept on
   its own stack. What a call keeps for its caller (AAPCS64: x19-x29, the return address x30, d8-d15) is pushed
   where the task stops and popped where it goes on; the stack pointer is all the task keeps beside it */
#define SAVED_BYTES 160    /* x19-x30 and d8-d15, 8 bytes each */

void host_switch_stacks(void **from_stack, void *to_stack);

#ifdef __APPLE__
/* (Mach-O's names: a C function's with an underscore before it, and no symbol types nor sizes) */
#define SWITCH_STACKS_START ".global _host_switch_stacks\n" "_host_switch_stacks:\n"
#define SWITCH_STACKS_END ""
#else
#define SWITCH_STACKS_START ".global host_switch_stacks\n" ".type host_switch_stacks, %function\n" "host_switch_stacks:\n"
#define SWITCH_STACKS_END ".size host_switch_stacks, .-host_switch_stacks\n"
#endif

__asm__(
	".text\n"
	SWITCH_STACKS_START
	"	sub sp, sp, #160\n"
	"	stp x19, x20, [sp, #0]\n"
	"	stp x21, x22, [sp, #16]\n"
	"	stp x23, x24, [sp, #32]\n"
	"	stp x25, x26, [sp, #48]\n"
	"	stp x27, x28, [sp, #64]\n"
	"	stp x29, x30, [sp, #80]\n"
	"	stp d8, d9, [sp, #96]\n"
	"	stp d10, d11, [sp, #112]\n"
	"	stp d12, d13, [sp, #128]\n"
	"	stp d14, d15, [sp, #144]\n"
	"	mov x2, sp\n"
	"	str x2, [x0]\n"
	"	mov sp, x1\n"
	"	ldp x19, x20, [sp, #0]\n"
	"	ldp x21, x22, [sp, #16]\n"
	"	ldp x23, x24, [sp, #32]\n"
	"	ldp x25, x26, [sp, #48]\n"
	"	ldp x27, x28, [sp, #64]\n"
	"	ldp x29, x30, [sp, #80]\n"
	"	ldp d8, d9, [sp, #96]\n"
	"	ldp d10, d11, [sp, #112]\n"
	"	ldp d12, d13, [sp, #128]\n"
	"	ldp d14, d15, [sp, #144]\n"
	"	add sp, sp, #160\n"
	"	ret\n"
	SWITCH_STACKS_END);

struct host_task
{
	void *stack_pointer;    /* where it stopped: its registers on top */
	void *stack;
	void (*start)(void);
};

static host_task main_task;
static host_task *current;

host_task *host_task_main(void)
{
	if (!current)
		current = &main_task;
	return &main_task;
}

static void run_task(void)
{
	current->start();
	fprintf(stderr, "A task came to its end\n");
	exit(3);
}

host_task *host_task_create(void (*start)(void))
{
	host_task *task = calloc(1, sizeof(*task));
	uint64_t *registers;

	if (task)
		task->stack = malloc(TASK_STACK_SIZE);
	if (!task || !task->stack)
	{
		fprintf(stderr, "A task could not be created\n");
		exit(3);
	}
	task->start = start;
	/* its first switch pops registers that return into run_task, at the top of its stack (16-byte aligned) */
	registers = (uint64_t *)(((uintptr_t)task->stack + TASK_STACK_SIZE - SAVED_BYTES) & ~(uintptr_t)15);
	memset(registers, 0, SAVED_BYTES);
	registers[11] = (uint64_t)(uintptr_t)run_task;    /* (x30: where the first ret goes) */
	task->stack_pointer = registers;
	return task;
}

void host_task_switch(host_task *to)
{
	host_task *from = current;

	if (to == from)
		return;
	current = to;
	host_switch_stacks(&from->stack_pointer, to->stack_pointer);
}

#else

#include <ucontext.h>

#ifdef __APPLE__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

struct host_task
{
	ucontext_t context;
	void *stack;
	void (*start)(void);
};

static host_task main_task;
static host_task *current;

host_task *host_task_main(void)
{
	if (!current)
		current = &main_task;
	return &main_task;
}

static void run_task(void)
{
	current->start();
	fprintf(stderr, "A task came to its end\n");
	exit(3);
}

host_task *host_task_create(void (*start)(void))
{
	host_task *task = calloc(1, sizeof(*task));

	if (task)
		task->stack = malloc(TASK_STACK_SIZE);
	if (!task || !task->stack || getcontext(&task->context) != 0)
	{
		fprintf(stderr, "A task could not be created\n");
		exit(3);
	}
	task->start = start;
	task->context.uc_stack.ss_sp = task->stack;
	task->context.uc_stack.ss_size = TASK_STACK_SIZE;
	task->context.uc_link = NULL;
	makecontext(&task->context, run_task, 0);
	return task;
}

void host_task_switch(host_task *to)
{
	host_task *from = current;

	if (to == from)
		return;
	current = to;
	swapcontext(&from->context, &to->context);
}

#endif

host_task *host_task_current(void)
{
	return current;
}
