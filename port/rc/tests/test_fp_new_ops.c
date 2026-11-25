/* Host test for R&C patch 7067: the fragment-program decompiler emits STR, SFL, DST, LIT, DP2A and RFL. Each op is
 * hand-assembled as one instruction (dest r1, sources r0/r2/r3, identity swizzles) and the HLSL text is checked for a
 * destination write, the expected expression, and no "TODO". Also: destination mask, saturate and a predicated form
 * go through the common tail. Text checks only; no shader is compiled or run (see the report). */
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

/* One instruction: op, dest r1 with `flags`, src0 = r0, src1 = r2, src2 = r3. */
static int build(char* hlsl, u32 hlsl_size, u32 op, u32 flags)
{
    u8 prog[16];
    put_word(prog + 0, OPC(op) | flags | OUTREG(1) | END);
    put_word(prog + 4, REG(0) | SWZ_IDENT | EXEC_ALWAYS);
    put_word(prog + 8, REG(2) | SWZ_IDENT);
    put_word(prog + 12, REG(3) | SWZ_IDENT);
    return rsx_fp_decompile(prog, sizeof prog, CTRL_32BIT, hlsl, hlsl_size);
}

static void one(const char* name, u32 op, const char* expr)
{
    static char h[16384]; char msg[160];
    const int n = build(h, sizeof h, op, OUTMASK_ALL);
    snprintf(msg, sizeof msg, "%s decompiles", name);              CHECK(n > 0, msg);
    snprintf(msg, sizeof msg, "%s has no TODO", name);             CHECK(strstr(h, "TODO") == NULL, msg);
    snprintf(msg, sizeof msg, "%s writes r[1].xyzw", name);        CHECK(strstr(h, "r[1].xyzw = _v.xyzw;") != NULL, msg);
    snprintf(msg, sizeof msg, "%s expression %s", name, expr);
    if (!strstr(h, expr)) fprintf(stderr, "HLSL:\n%s\n", h);
    CHECK(strstr(h, expr) != NULL, msg);

    /* Destination mask, result scale and saturate are the common tail's: same text as every other op. */
    build(h, sizeof h, op, OUTMASK_XY);
    snprintf(msg, sizeof msg, "%s honours the xy write mask", name);   CHECK(strstr(h, "r[1].xy = _v.xy;") != NULL, msg);
    build(h, sizeof h, op, OUTMASK_ALL | SAT);
    snprintf(msg, sizeof msg, "%s honours saturate", name);            CHECK(strstr(h, "_v = saturate(_v);") != NULL, msg);
}

int main(void)
{
    const char* r0 = "(r[0]).xyzw";   /* what the identity-swizzled temp source expands to */
    char e[512];

    one("STR", 0x20, "float4(1.0, 1.0, 1.0, 1.0)");
    one("SFL", 0x21, "float4(0.0, 0.0, 0.0, 0.0)");
    snprintf(e, sizeof e, "float4(1.0, (%s).y * ((r[2]).xyzw).y, (%s).z, ((r[2]).xyzw).w)", r0, r0);
    one("DST", 0x07, e);
    snprintf(e, sizeof e, "((%s).x > 0.0) ? exp2(clamp((%s).w, -128.0, 128.0) * log2(max((%s).y, 1e-30))) : 0.0, 1.0)", r0, r0, r0);
    one("LIT", 0x1E, e);
    snprintf(e, sizeof e, "(dot((%s).xy, ((r[2]).xyzw).xy) + ((r[3]).xyzw).x)", r0);
    one("DP2A", 0x2E, e);
    snprintf(e, sizeof e, "float4(2.0 * dot((%s).xyz, ((r[2]).xyzw).xyz) / dot((%s).xyz, (%s).xyz) * (%s).xyz - ((r[2]).xyzw).xyz, 1.0)",
          r0, r0, r0, r0);
    one("RFL", 0x36, e);

    /* A predicated STR (exec_if lt only, not unconditional) still writes through the select. */
    {
        static char h[16384]; u8 prog[16];
        put_word(prog + 0, OPC(0x20) | OUTMASK_ALL | OUTREG(1) | END);
        put_word(prog + 4, REG(0) | SWZ_IDENT | (1u << 18) | (0u << 21) | (1u << 23) | (2u << 25) | (3u << 27));
        put_word(prog + 8, 0); put_word(prog + 12, 0);
        rsx_fp_decompile(prog, sizeof prog, CTRL_32BIT, h, sizeof h);
        CHECK(strstr(h, "TODO") == NULL, "predicated STR has no TODO");
        CHECK(strstr(h, "r[1].xyzw = (_p.xyzw != 0.0) ? _v.xyzw : r[1].xyzw;") != NULL, "predicated STR writes through the select");
    }

    /* An opcode still outside the table keeps the TODO (0x3F is not named). */
    {
        static char h[16384];
        build(h, sizeof h, 0x3F, OUTMASK_ALL);
        CHECK(strstr(h, "TODO: unhandled FP opcode 0x3F") != NULL, "an unnamed opcode still reports TODO");
    }

    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("fp new ops test: ok\n");
    return 0;
}
