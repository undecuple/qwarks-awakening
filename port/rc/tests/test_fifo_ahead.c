/* Host test for R&C patch 7050: rsx_fifo_ahead decodes the FIFO ahead of the walker like the walker does. */
#include <stdio.h>
#include <string.h>
#include "rsx_fifo_ahead.h"

static uint32_t mem[1024];   /* IO offsets 0x0000..0x0FFC */
static int rd(void* u, uint32_t off, uint32_t* w)
{
    (void)u;
    if (off >= sizeof mem) return 0;
    *w = mem[off / 4u];
    return 1;
}
static uint32_t got_m[64], got_a[64], n_got, stop_after;
static int fn(void* u, uint32_t m, uint32_t a)
{
    (void)u;
    if (n_got < 64) { got_m[n_got] = m; got_a[n_got] = a; }
    n_got++;
    return stop_after && n_got >= stop_after;
}
static uint32_t hdr(uint32_t subch, uint32_t method, uint32_t count, int ni)
{
    return (ni ? 0x40000000u : 0u) | (count << 18) | (subch << 13) | method;
}
static int fails;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); fails++; } } while (0)

int main(void)
{
    /* 0x000: NOP; incrementing 2 x 0x1808..; old JUMP to 0x100 */
    memset(mem, 0, sizeof mem);
    mem[0] = 0;
    mem[1] = hdr(0, 0x1808, 2, 0); mem[2] = 5; mem[3] = 6;
    mem[4] = 0x20000000u | 0x100u;
    /* 0x100: CALL 0x200; then NI 3 x 0x1D94 on subchannel 1; then a 2D method (subchannel 3) */
    mem[0x100 / 4] = 0x200u | 2u;
    mem[0x104 / 4] = hdr(1, 0x1D94, 3, 1); mem[0x108 / 4] = 7; mem[0x10C / 4] = 8; mem[0x110 / 4] = 9;
    mem[0x114 / 4] = hdr(3, 0x0300, 1, 0); mem[0x118 / 4] = 0xAA;
    mem[0x11C / 4] = 0x300u | 1u;   /* new-style JUMP to 0x300 */
    /* 0x200: one method then RET */
    mem[0x200 / 4] = hdr(0, 0x0A00, 1, 0); mem[0x204 / 4] = 0x42;
    mem[0x208 / 4] = 0x00020000u;
    /* 0x300: one method, then the flip word */
    mem[0x300 / 4] = hdr(0, 0x1FEC, 1, 0); mem[0x304 / 4] = 1;
    mem[0x308 / 4] = 0xFEAD0001u;
    mem[0x30C / 4] = hdr(0, 0x0B00, 1, 0); mem[0x310 / 4] = 2;   /* after the flip: never read */

    n_got = 0; stop_after = 0;
    int why = rsx_fifo_ahead(0, 0x400, 0, 1000, rd, NULL, fn, NULL);
    CHECK(why == RSX_AHEAD_FLIP, "stops at the flip word");
    CHECK(n_got == 7, "seven 3D methods");
    CHECK(got_m[0] == 0x1808 && got_a[0] == 5 && got_m[1] == 0x180C && got_a[1] == 6, "incrementing methods");
    CHECK(got_m[2] == 0x0A00 && got_a[2] == 0x42, "CALL target before the caller's next command");
    CHECK(got_m[3] == ((1u << 13) | 0x1D94) && got_m[5] == ((1u << 13) | 0x1D94) && got_a[5] == 9,
          "non-incrementing on subchannel 1 keeps its method");
    CHECK(got_m[6] == 0x1FEC, "new-style JUMP followed, 2D subchannel skipped");

    /* put reached; the limit; a park; an acquire; a stop from the callback */
    n_got = 0;
    CHECK(rsx_fifo_ahead(0, 0x10, 0, 1000, rd, NULL, fn, NULL) == RSX_AHEAD_PUT && n_got == 2, "stops at put");
    n_got = 0;
    CHECK(rsx_fifo_ahead(0, 0x400, 0, 3, rd, NULL, fn, NULL) == RSX_AHEAD_LIMIT, "word limit");
    n_got = 0; stop_after = 3;
    CHECK(rsx_fifo_ahead(0, 0x400, 0, 1000, rd, NULL, fn, NULL) == RSX_AHEAD_STOPPED && n_got == 3, "callback stops");
    stop_after = 0;
    mem[0x400 / 4] = 0x20000000u | 0x400u;   /* park: JUMP to itself */
    CHECK(rsx_fifo_ahead(0x400, 0x800, 0, 1000, rd, NULL, fn, NULL) == RSX_AHEAD_PARK, "park");
    mem[0x500 / 4] = hdr(0, 0x0068, 1, 0); mem[0x504 / 4] = 1;   /* semaphore acquire */
    n_got = 0;
    CHECK(rsx_fifo_ahead(0x500, 0x800, 0, 1000, rd, NULL, fn, NULL) == RSX_AHEAD_ACQUIRE && n_got == 0, "acquire");
    mem[0x600 / 4] = 0x00020000u;   /* RET without CALL */
    CHECK(rsx_fifo_ahead(0x600, 0x800, 0, 1000, rd, NULL, fn, NULL) == RSX_AHEAD_RET, "RET without CALL");
    mem[0x700 / 4] = 0x20000000u | 0x10000u;   /* JUMP out of the mapped range */
    CHECK(rsx_fifo_ahead(0x700, 0x800, 0, 1000, rd, NULL, fn, NULL) == RSX_AHEAD_UNMAPPED, "unmapped");
    /* A pending CALL return from the walker is taken on RET. */
    n_got = 0;
    CHECK(rsx_fifo_ahead(0x208, 0x2000, 0x300, 1000, rd, NULL, fn, NULL) == RSX_AHEAD_FLIP && n_got == 1 &&
          got_m[0] == 0x1FEC, "walker's CALL return");
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("fifo ahead test: ok\n");
    return 0;
}
