/* Host test for R&C patch 7112: the fragment-program decompiler's NRM leaves a zero vector as it is (HLSL normalize(0)
 * is NaN; RPCS3's _builtin_normalize guards with length > 0). NRM is hand-assembled as one instruction (dest r1, source
 * r0, identity swizzle) and the HLSL text is checked: the guarded select, w = 1.0 as before, no bare normalize, and the
 * destination mask / saturate still go through the common tail. Text checks only; no shader is compiled or run (the
 * repo has no host HLSL evaluator). */
#include "rsx_fp_decompiler.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); fails++; } } while (0)

static void put_word(u8* p, u32 h)
{
    const u32 be = (h << 16) | (h >> 16);
    p[0] = (u8)(be >> 24); p[1] = (u8)(be >> 16); p[2] = (u8)(be >> 8); p[3] = (u8)be;
}

#define OPC(x)      ((u32)(x) << 24)
#define OUTMASK_ALL (0xFu << 9)
#define OUTMASK_XY  ((1u << 9) | (1u << 10))
#define END         (1u << 0)
#define OUTREG(i)   ((u32)(i) << 1)
#define SAT         (1u << 31)
#define SWZ_IDENT   ((0u<<9)|(1u<<11)|(2u<<13)|(3u<<15))
#define REG(i)      ((u32)(i) << 2)
#define EXEC_ALWAYS ((7u << 18) | (0u << 21) | (1u << 23) | (2u << 25) | (3u << 27))
#define CTRL_32BIT  0x40u
#define OP_NRM      0x39u

static int build(char* hlsl, u32 hlsl_size, u32 flags)
{
    u8 prog[16];
    put_word(prog + 0, OPC(OP_NRM) | flags | OUTREG(1) | END);
    put_word(prog + 4, REG(0) | SWZ_IDENT | EXEC_ALWAYS);
    put_word(prog + 8, 0);
    put_word(prog + 12, 0);
    return rsx_fp_decompile(prog, sizeof prog, CTRL_32BIT, hlsl, hlsl_size);
}

int main(void)
{
    static char h[16384];
    const char* r0 = "(r[0]).xyzw";   /* what the identity-swizzled temp source expands to */
    char guarded[512], bare[256];
    snprintf(guarded, sizeof guarded,
             "float4(dot((%s).xyz, (%s).xyz) > 0.0 ? normalize((%s).xyz) : (%s).xyz, 1.0)", r0, r0, r0, r0);
    snprintf(bare, sizeof bare, "float4(normalize((%s).xyz), 1.0)", r0);

    const int n = build(h, sizeof h, OUTMASK_ALL);
    CHECK(n > 0, "NRM decompiles");
    CHECK(strstr(h, "TODO") == NULL, "NRM has no TODO");
    CHECK(strstr(h, "r[1].xyzw = _v.xyzw;") != NULL, "NRM writes r[1].xyzw");
    if (!strstr(h, guarded)) fprintf(stderr, "HLSL:\n%s\n", h);
    CHECK(strstr(h, guarded) != NULL, "NRM is guarded: dot(a,a) > 0 ? normalize(a.xyz) : a.xyz, w = 1.0");
    CHECK(strstr(h, bare) == NULL, "no unguarded normalize (NaN on a zero vector)");

    build(h, sizeof h, OUTMASK_XY);
    CHECK(strstr(h, "r[1].xy = _v.xy;") != NULL, "NRM honours the xy write mask");
    build(h, sizeof h, OUTMASK_ALL | SAT);
    CHECK(strstr(h, "_v = saturate(_v);") != NULL, "NRM honours saturate");

    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("fp nrm zero test: ok\n");
    return 0;
}
