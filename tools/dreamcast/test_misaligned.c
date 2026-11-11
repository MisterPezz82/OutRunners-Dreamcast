/* host test of ports/dreamcast/misaligned_fix.inc: each move form on misaligned addresses, and a move in
   a delayed branch's slot. cc -I../../ports/dreamcast test_misaligned.c (64-bit hosts: addresses kept below 4 GiB) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
typedef struct { uint32_t pc, pr, gbr, sr; uint32_t r[16]; } irq_context_t;
#include "misaligned_fix.inc"
static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } } while (0)
int main(void)
{
	uint8_t *mem = mmap((void *)0x10000000, 1 << 16, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (mem == MAP_FAILED) { puts("mmap failed"); return 1; }
	const uint32_t base = (uint32_t)(uintptr_t)mem;
	for (int i = 0; i < 256; i++) mem[0x1000 + i] = (uint8_t)i;
	uint16_t *code = (uint16_t *)(mem + 0x100);
	irq_context_t c;
	#define RUN(op) (memset(&c, 0, sizeof(c)), code[0] = (op), c.pc = base + 0x100)
	/* mov.w @Rm,Rn: n=1 m=2, address odd -> bytes 0x81,0x82 at 0x1081 */
	RUN(0x6121); c.r[2] = base + 0x1081; CHECK(fix_misaligned(&c) && c.r[1] == 0xffff8281u && c.pc == base + 0x102, "mov.w @Rm");
	RUN(0x6122); c.r[2] = base + 0x1001; CHECK(fix_misaligned(&c) && c.r[1] == 0x04030201u, "mov.l @Rm");
	RUN(0x6125); c.r[2] = base + 0x1003; CHECK(fix_misaligned(&c) && c.r[1] == 0x0403u && c.r[2] == base + 0x1005, "mov.w @Rm+");
	RUN(0x6126); c.r[2] = base + 0x1002; CHECK(fix_misaligned(&c) && c.r[1] == 0x05040302u && c.r[2] == base + 0x1006, "mov.l @Rm+");
	RUN(0x5123); c.r[2] = base + 0x1001; CHECK(fix_misaligned(&c) && c.r[1] == 0x100f0e0du, "mov.l @(disp,Rm)");    /* 0x1001+12 */
	RUN(0x8523); c.r[2] = base + 0x1001; CHECK(fix_misaligned(&c) && c.r[0] == 0x0807u, "mov.w @(disp,Rm),R0");     /* +6 */
	RUN(0x012d); c.r[0] = 1; c.r[2] = base + 0x1010; CHECK(fix_misaligned(&c) && c.r[1] == 0x1211u, "mov.w @(R0,Rm)");
	RUN(0x012e); c.r[0] = 3; c.r[2] = base + 0x1010; CHECK(fix_misaligned(&c) && c.r[1] == 0x16151413u, "mov.l @(R0,Rm)");
	RUN(0xc503); c.gbr = base + 0x1001; CHECK(fix_misaligned(&c) && c.r[0] == 0x0807u, "mov.w @(disp,GBR)");
	RUN(0x2121); c.r[1] = base + 0x2001; c.r[2] = 0xbeefcafe; CHECK(fix_misaligned(&c) && mem[0x2001] == 0xfe && mem[0x2002] == 0xca, "mov.w Rm,@Rn");
	RUN(0x2122); c.r[1] = base + 0x2011; c.r[2] = 0x11223344; CHECK(fix_misaligned(&c) && !memcmp(mem + 0x2011, "\x44\x33\x22\x11", 4), "mov.l Rm,@Rn");
	RUN(0x2126); c.r[1] = base + 0x2025; c.r[2] = 0xa1b2c3d4; CHECK(fix_misaligned(&c) && c.r[1] == base + 0x2021 && mem[0x2021] == 0xd4, "mov.l Rm,@-Rn");
	RUN(0x1123); c.r[1] = base + 0x2031; c.r[2] = 0x55667788; CHECK(fix_misaligned(&c) && mem[0x203d] == 0x88, "mov.l Rm,@(disp,Rn)");
	RUN(0x8113); c.r[1] = base + 0x2041; c.r[0] = 0x9988; CHECK(fix_misaligned(&c) && mem[0x2047] == 0x88 && mem[0x2048] == 0x99, "mov.w R0,@(disp,Rn)");
	RUN(0x0125); c.r[0] = 1; c.r[1] = base + 0x2050; c.r[2] = 0x7766; CHECK(fix_misaligned(&c) && mem[0x2051] == 0x66, "mov.w Rm,@(R0,Rn)");
	/* jsr @r10 with mov.w @r4+,r9 in its slot */
	RUN(0x4a0b); code[1] = 0x6945; c.r[10] = 0x8c001234; c.r[4] = base + 0x1003;
	CHECK(fix_misaligned(&c) && c.pc == 0x8c001234 && c.pr == base + 0x104 && c.r[9] == 0x0403 && c.r[4] == base + 0x1005, "jsr slot");
	/* bt/s taken and not, with a load in the slot */
	RUN(0x8d02); code[1] = 0x6121; c.sr = 1; c.r[2] = base + 0x1001; CHECK(fix_misaligned(&c) && c.pc == base + 0x100 + 4 + 4, "bt/s taken");
	RUN(0x8d02); code[1] = 0x6121; c.sr = 0; c.r[2] = base + 0x1001; CHECK(fix_misaligned(&c) && c.pc == base + 0x104, "bt/s not taken");
	/* rts with a store in the slot */
	RUN(0x000b); code[1] = 0x2122; c.pr = 0x8c005678; c.r[1] = base + 0x2061; CHECK(fix_misaligned(&c) && c.pc == 0x8c005678, "rts slot");
	/* not a move: refused, context untouched */
	RUN(0x0009); CHECK(!fix_misaligned(&c) && c.pc == base + 0x100, "nop refused");
	RUN(0x4a0b); code[1] = 0x0009; c.r[10] = 5; CHECK(!fix_misaligned(&c) && c.pc == base + 0x100 && c.pr == 0, "jsr + nop refused");
	printf("%s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
	return fails != 0;
}
