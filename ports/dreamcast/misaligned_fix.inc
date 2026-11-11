/* SPDX-License-Identifier: BSD-3-Clause */
/* included by fault_record.c (and the host test tools/dreamcast/test_misaligned.c): needs irq_context_t with
   pc, pr, gbr, sr and r[16] */
/* ---- misaligned reads and writes ----
   The game's data tables are the arcade's, whose V60 reads 16 and 32-bit values at any address (the
   flashing scenery's palette cycles sit at odd ones, 0x21041...), and the C reads them where they are.
   The SH4 stops on such an access with an address error; Flycast and Redream do not, which is why only
   the console crashed. The handler does the access a byte at a time (little-endian, as the SH4 runs),
   updates the registers as the instruction would, and steps over it; one in a delayed branch's slot is
   done, then the branch. Anything else is a real fault, and the crash screen shows it. */
volatile uint32_t dc_unaligned_count = 0;

static uint32_t load(uint32_t addr, int size) {
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)addr;
    uint32_t v = 0;
    for (int i = 0; i < size; i++) v |= (uint32_t)p[i] << (8 * i);
    return size == 2 ? (uint32_t)(int32_t)(int16_t)v : v;    /* (mov.w sign-extends) */
}

static void store(uint32_t addr, int size, uint32_t v) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)addr;
    for (int i = 0; i < size; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* one data move instruction, done by hand: 1, or 0 when it is not one this handles */
static int emulate_move(irq_context_t *ctx, uint32_t pc, uint16_t op) {
    uint32_t *r = ctx->r;
    const int n = (op >> 8) & 15, m = (op >> 4) & 15, d4 = op & 15, d8 = op & 255;
    switch (op >> 12) {
    case 0x0:
        switch (op & 15) {
        case 0xd: r[n] = load(r[0] + r[m], 2); return 1;                 /* mov.w @(R0,Rm),Rn */
        case 0xe: r[n] = load(r[0] + r[m], 4); return 1;                 /* mov.l @(R0,Rm),Rn */
        case 0x5: store(r[0] + r[n], 2, r[m]); return 1;                 /* mov.w Rm,@(R0,Rn) */
        case 0x6: store(r[0] + r[n], 4, r[m]); return 1;                 /* mov.l Rm,@(R0,Rn) */
        }
        return 0;
    case 0x1: store(r[n] + d4 * 4, 4, r[m]); return 1;                   /* mov.l Rm,@(disp,Rn) */
    case 0x2:
        switch (op & 15) {
        case 0x1: store(r[n], 2, r[m]); return 1;                        /* mov.w Rm,@Rn */
        case 0x2: store(r[n], 4, r[m]); return 1;                        /* mov.l Rm,@Rn */
        case 0x5: { const uint32_t v = r[m]; r[n] -= 2; store(r[n], 2, v); return 1; }    /* mov.w Rm,@-Rn */
        case 0x6: { const uint32_t v = r[m]; r[n] -= 4; store(r[n], 4, v); return 1; }    /* mov.l Rm,@-Rn */
        }
        return 0;
    case 0x5: r[n] = load(r[m] + d4 * 4, 4); return 1;                   /* mov.l @(disp,Rm),Rn */
    case 0x6:
        switch (op & 15) {
        case 0x1: r[n] = load(r[m], 2); return 1;                        /* mov.w @Rm,Rn */
        case 0x2: r[n] = load(r[m], 4); return 1;                        /* mov.l @Rm,Rn */
        case 0x5: { const uint32_t a = r[m]; if (n != m) r[m] += 2; r[n] = load(a, 2); return 1; }    /* mov.w @Rm+,Rn */
        case 0x6: { const uint32_t a = r[m]; if (n != m) r[m] += 4; r[n] = load(a, 4); return 1; }    /* mov.l @Rm+,Rn */
        }
        return 0;
    case 0x8:
        if ((op >> 8) == 0x81) { store(r[m] + d4 * 2, 2, r[0]); return 1; }    /* mov.w R0,@(disp,Rn) */
        if ((op >> 8) == 0x85) { r[0] = load(r[m] + d4 * 2, 2); return 1; }    /* mov.w @(disp,Rm),R0 */
        return 0;
    case 0xc:
        switch (op >> 8) {
        case 0xc1: store(ctx->gbr + d8 * 2, 2, r[0]); return 1;          /* mov.w R0,@(disp,GBR) */
        case 0xc2: store(ctx->gbr + d8 * 4, 4, r[0]); return 1;          /* mov.l R0,@(disp,GBR) */
        case 0xc5: r[0] = load(ctx->gbr + d8 * 2, 2); return 1;          /* mov.w @(disp,GBR),R0 */
        case 0xc6: r[0] = load(ctx->gbr + d8 * 4, 4); return 1;          /* mov.l @(disp,GBR),R0 */
        }
        return 0;
    }
    (void)pc;
    return 0;
}

/* a delayed branch: its target (and PR set) as the branch does, before its slot runs; 0 if not one */
static int delayed_branch(irq_context_t *ctx, uint32_t pc, uint16_t op, uint32_t *target) {
    const uint32_t *r = ctx->r;
    const int m = (op >> 8) & 15;
    const int32_t d12 = (int32_t)((op & 0xfff) ^ 0x800) - 0x800, d8 = (int32_t)(int8_t)(op & 0xff);
    const int t = ctx->sr & 1;
    if ((op & 0xf000) == 0xa000) { *target = pc + 4 + d12 * 2; return 1; }                          /* bra */
    if ((op & 0xf000) == 0xb000) { *target = pc + 4 + d12 * 2; ctx->pr = pc + 4; return 1; }        /* bsr */
    if ((op & 0xf0ff) == 0x0023) { *target = pc + 4 + r[m]; return 1; }                              /* braf */
    if ((op & 0xf0ff) == 0x0003) { *target = pc + 4 + r[m]; ctx->pr = pc + 4; return 1; }           /* bsrf */
    if ((op & 0xf0ff) == 0x402b) { *target = r[m]; return 1; }                                       /* jmp */
    if ((op & 0xf0ff) == 0x400b) { *target = r[m]; ctx->pr = pc + 4; return 1; }                    /* jsr */
    if (op == 0x000b) { *target = ctx->pr; return 1; }                                               /* rts */
    if ((op & 0xff00) == 0x8d00) { *target = t ? pc + 4 + d8 * 2 : pc + 4; return 1; }              /* bt/s */
    if ((op & 0xff00) == 0x8f00) { *target = !t ? pc + 4 + d8 * 2 : pc + 4; return 1; }             /* bf/s */
    return 0;
}

static int fix_misaligned(irq_context_t *ctx) {
    const uint32_t pc = ctx->pc;
    const uint16_t op = *(const volatile uint16_t *)(uintptr_t)pc;
    if (emulate_move(ctx, pc, op)) {
        ctx->pc = pc + 2;
        return 1;
    }
    uint32_t target;
    irq_context_t saved = *ctx;
    if (delayed_branch(ctx, pc, op, &target)) {
        const uint16_t slot = *(const volatile uint16_t *)(uintptr_t)(pc + 2);
        if (emulate_move(ctx, pc + 2, slot)) {
            ctx->pc = target;
            return 1;
        }
        *ctx = saved;
    }
    return 0;
}

