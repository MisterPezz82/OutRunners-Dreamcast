/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Experimental debug tool: Captures first fault context and freezes.
 * DO NOT USE IN PRODUCTION. Intended for Flycast/Lua memory capture.
 */

#include <arch/irq.h>
#include <stdint.h>
#include <stddef.h>
#include "diag_screen.h"


/* Volatile globals for fault capture */
volatile uint32_t dc_fault_code = 0;
volatile uint32_t dc_fault_pc = 0;
volatile uint32_t dc_fault_pr = 0;
volatile uint32_t dc_fault_sp = 0;
volatile uint32_t dc_fault_regs[16] = {0};
volatile uint32_t dc_exit_status = 0;
volatile uint32_t dc_exit_caller = 0;

#include "misaligned_fix.inc"

/* Handler signature: void fault_handler(irq_t code, irq_context_t *ctx, void *data) */
static void fault_handler(irq_t code, irq_context_t *ctx, void *data) {
    (void)data;
    if ((code == EXC_DATA_ADDRESS_READ || code == EXC_DATA_ADDRESS_WRITE) && fix_misaligned(ctx)) {
        dc_unaligned_count++;
        return;
    }
    dc_fault_code = (uint32_t)code;
    dc_fault_pc = ctx->pc;
    dc_fault_pr = ctx->pr;
    dc_fault_sp = ctx->r[15]; /* SP is r15 on SH4 */
    for (int i = 0; i < 16; i++) {
        dc_fault_regs[i] = ctx->r[i];
    }
    dc_diag_screen("CRASHED: CPU exception", (uint32_t)code, ctx->pc, ctx->pr, ctx->r[15]);
    /* Freeze forever with NOPs */
    for (;;) {
        asm volatile("nop");
    }
}

int dc_fault_install(void) {
    if (irq_set_handler(EXC_DATA_ADDRESS_READ, fault_handler, NULL) != 0) return -1;
    if (irq_set_handler(EXC_DATA_ADDRESS_WRITE, fault_handler, NULL) != 0) return -1;
    if (irq_set_handler(EXC_ILLEGAL_INSTR, fault_handler, NULL) != 0) return -1;
    if (irq_set_handler(EXC_SLOT_ILLEGAL_INSTR, fault_handler, NULL) != 0) return -1;
    if (irq_set_handler(EXC_FPU, fault_handler, NULL) != 0) return -1;
    /* Note: EXC_GENERAL_FPU and EXC_SLOT_FPU are NOT overridden to preserve KOS lazy FPU */
    return 0;
}

__attribute__((noreturn)) void __wrap_exit(int status) {
    dc_exit_status = (uint32_t)status;
    dc_exit_caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    dc_fault_code = 0xeeee;
    dc_diag_screen_hold("STOPPED: exit()", (uint32_t)status, dc_exit_caller, 0, 0);
    for (;;) {
        asm volatile("nop");
    }
}

__attribute__((noreturn)) void __wrap_abort(void) {
    dc_exit_status = 0xabab;
    dc_exit_caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
    dc_fault_code = 0xaaaa;
    dc_diag_screen_hold("STOPPED: abort()", 0, dc_exit_caller, 0, 0);
    for (;;) {
        asm volatile("nop");
    }
}
