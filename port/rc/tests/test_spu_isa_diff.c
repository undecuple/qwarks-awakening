/* Every public SPU helper of runtime/spu/spu_helpers.h on the same inputs, results written raw (run_spu_isa_diff_test.sh).
 * The script builds this file once per set of SPU compile flags (port/rc/CMakeLists.txt: -O2 alone, the old build;
 * RC_SPU_SSE41 = -msse4.1 -ffp-contract=off; plus RC_SPU_FMA = -mfma) and compares the outputs lane by lane, so the
 * lifted SPU code is shown to compute the same bits whichever flags it is built with.
 * The call table is generated from the header by gen_spu_isa_diff.py.
 *   usage: test_spu_isa_diff CGM NAMES_OUT RESULTS_OUT
 * CGM = g_kz3_spu_slowcg (runtime spu_drain.c: 0 = default, the fma fast paths with the patch-1040 flush to zero;
 * 2 = the same fast paths without the flush, KZ3_SPU_FASTCG=1038; 1 = the old per-lane paths). */
#include "spu_context.h"
#include "spu_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The runtime's globals the helpers read, at the runtime defaults (spu_drain.c). */
int g_kz3_spu_xr = 1;      /* patch 1071: SPU extended-range single lanes on */
int g_kz3_spu_xrq = 1;     /* patch 1089 */
int g_kz3_spu_slowcg;      /* set from argv[1] */
int g_kz3_fma_stats;
int g_kz3_xr_stats;
void kz3_spu_xr_note(int op, unsigned m, const uint32_t* a, const uint32_t* b, const uint32_t* c, const uint32_t* r,
                     const void* ra) { (void)op; (void)m; (void)a; (void)b; (void)c; (void)r; (void)ra; }
void kz3_spu_fma_note(const double s[4], int ftz) { (void)s; (void)ftz; }

#define NV 4096
#define NI 64
#define NCALL 6000
static u128 V[NV];
static int IM[NI];
#include "spu_isa_diff.inc"

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }

int main(int argc, char** argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s CGM NAMES_OUT RESULTS_OUT\n", argv[0]); return 2; }
    g_kz3_spu_slowcg = atoi(argv[1]);
    /* Inputs: random bits, small and unit-range floats, float specials (zeros, subnormals, FLT_MIN, FLT_MAX, the
     * exponent-255 values the SPU treats as finite, NaN patterns), tiny floats whose products land in the subnormal
     * range (the flush-to-zero cases of fma/fms/fnms), and shuffle-control style bytes. */
    static const uint32_t special[] = { 0, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x00000001u, 0x807FFFFFu, 0x007FFFFFu,
                                        0x00800000u, 0x80800000u, 0x7F7FFFFFu, 0x7F800000u, 0xFF800000u, 0x7FC00000u,
                                        0x7FFFFFFFu, 0xFFFFFFFFu, 0x4F000000u, 0xCF000000u, 0x3F000000u, 0x0000FFFFu,
                                        0xFFFF0000u, 0x00010203u, 0x80C0E0F0u, 0x1F1E1D1Cu, 0x4B000000u, 0x5F000000u };
    for (int i = 0; i < NV; i++) {
        uint32_t w[4];
        for (int j = 0; j < 4; j++) {
            const uint32_t k = rnd() % 10;
            if (k < 3) w[j] = rnd() ^ (rnd() << 16);
            else if (k < 6) { float f = ((float)(int)(rnd() % 20001) - 10000.0f) / 997.0f; memcpy(&w[j], &f, 4); }
            else if (k < 7) { float f = ((float)(rnd() % 1000) + 1.0f) / 1001.0f; memcpy(&w[j], &f, 4); }
            else if (k < 8) { float f = ((float)(int)(rnd() % 2001) - 1000.0f) * 0x1p-70f; memcpy(&w[j], &f, 4); }
            else w[j] = special[rnd() % (sizeof special / sizeof special[0])];
        }
        memcpy(&V[i], w, 16);
    }
    for (int i = 0; i < NI; i++) IM[i] = (i < 40) ? i - 8 : (int)(rnd() % 1024) - 512;
    FILE* fn = fopen(argv[2], "w");
    FILE* fr = fopen(argv[3], "wb");
    if (!fn || !fr) { perror("open"); return 1; }
    const int nc = (int)(sizeof CALLS / sizeof CALLS[0]);
    fprintf(fn, "%d\n", NCALL);
    for (int c = 0; c < nc; c++) {
        fprintf(fn, "%s %d\n", CALLS[c].name, CALLS[c].dbl);
        for (int n = 0; n < NCALL; n++) {
            u128 r = CALLS[c].fn(n);
            if (fwrite(&r, 16, 1, fr) != 1) { perror("write"); return 1; }
        }
    }
    fclose(fn);
    return fclose(fr) ? 1 : 0;
}
