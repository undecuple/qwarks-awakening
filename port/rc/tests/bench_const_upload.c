/* Host benchmark (R&C pass 15): live draw's per-draw constant upload. Each draw copies the whole vertex
 * constant file (RSX_DSP_NUM_CONSTANTS = 512 vec4 = 8 KB) plus the 32-byte viewport transform into the next
 * CB_BLOCK_ALIGNED (8448 B) block of the upload ring (rsx_live_draw.c, `memcpy(cbdst, g.rsx.constants, ...)`).
 * Frame of tods-cyan-c2 frame 6986: 6,964 draws. Measures (a) the full copy per draw, (b) copying only a changed range
 * of N vec4 per draw (N = 4, 16, 64) into a block that keeps the rest from the previous draw's block, (c) reusing the
 * previous block for draws whose constants did not change (3,124 of 6,964 in that frame). The ring here is ordinary
 * cached memory; the real one is an UPLOAD (write-combined) heap, so (a) is a lower bound there.
 * Build: gcc -O2 port/rc/tests/bench_const_upload.c -o bench_const_upload */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
enum { NCONST = 512, BLOCK = 8448, DRAWS = 6964, RING_BLOCKS = 4096, SAME = 3124, FRAMES = 50 };
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(void)
{
    static float consts[NCONST * 4]; for (int i = 0; i < NCONST * 4; i++) consts[i] = (float)i;
    unsigned char* ring = aligned_alloc(256, (size_t)BLOCK * RING_BLOCKS); memset(ring, 0, (size_t)BLOCK * RING_BLOCKS);
    volatile unsigned sink = 0; unsigned used = 0;
    double t0 = now();
    for (int f = 0; f < FRAMES; f++)
        for (int d = 0; d < DRAWS; d++) {
            unsigned char* dst = ring + (size_t)(used++ % RING_BLOCKS) * BLOCK;
            consts[(d * 7) % (NCONST * 4)] += 1.0f;
            memcpy(dst, consts, NCONST * 16); memcpy(dst + NCONST * 16, consts, 32); sink += dst[5];
        }
    const double full = (now() - t0) / FRAMES;
    printf("(a) full 8 KB copy per draw : %6.2f ms/frame (%.0f ns/draw, %.1f MB/frame)\n", full * 1e3, full * 1e9 / DRAWS,
           (double)DRAWS * (NCONST * 16 + 32) / 1048576.0);
    const int ranges[3] = { 4, 16, 64 };
    for (int k = 0; k < 3; k++) {
        const int n = ranges[k];
        t0 = now();
        for (int f = 0; f < FRAMES; f++)
            for (int d = 0; d < DRAWS; d++) {
                /* a dirty-range scheme needs a full block per ring slot anyway unless blocks are shared: model the
                 * cheapest form, a shared constant buffer per frame with per-draw offsets into append-only ranges */
                unsigned char* dst = ring + (size_t)(used++ % (RING_BLOCKS * 32)) % ((size_t)BLOCK * RING_BLOCKS / 64 - 1) * 64;
                memcpy(dst, consts + (d % 400) * 4, (size_t)n * 16); sink += dst[1];
            }
        const double t = (now() - t0) / FRAMES;
        printf("(b) %2d vec4 changed range    : %6.2f ms/frame (%.0f ns/draw, %.2f MB/frame)\n", n, t * 1e3, t * 1e9 / DRAWS,
               (double)DRAWS * n * 16 / 1048576.0);
    }
    printf("(c) reuse the previous block for unchanged draws: %d of %d copies skipped -> (a) x %.2f = %.2f ms/frame\n",
           SAME, DRAWS, (double)(DRAWS - SAME) / DRAWS, full * 1e3 * (DRAWS - SAME) / DRAWS);
    return (int)(sink & 0);
}
