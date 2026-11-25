/* Host test for R&C patch 7108: a fragment program that reads WPOS gets .w = 1/w (the RSX value, like GL's
 * gl_FragCoord.w), not SV_POSITION.w, which FXC (D3D12) gives as the clip w.
 *
 * Two hand-assembled programs read WPOS: MOV r0, WPOS and MOV r0, WPOS.wwww. Each is decompiled buffered (the draw
 * engine's path, window convention through fp_alpha) and unbuffered. Text checks: the read goes through _rsx_wpos, the
 * _rsx_inv_w helper is declared, and the _rsx_wpos macro takes its w from _rsx_inv_w. Value check: the helper lines
 * (the RSX_FRAGCOORD_W_IS_RCP block and the _rsx_wpos define) are written to <outdir>/wpos_{buf,unbuf}.h, and
 * test_fp_wpos_w_eval.c evaluates them as C (run_fp_wpos_w_test.sh builds it with and without
 * RSX_FRAGCOORD_W_IS_RCP). Usage: test_fp_wpos_w <outdir>. */
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
#define END         (1u << 0)
#define INSRC(i)    ((u32)(i) << 13)
#define T_INPUT     1u
#define SWZ(x,y,z,w) (((u32)(x) << 9) | ((u32)(y) << 11) | ((u32)(z) << 13) | ((u32)(w) << 15))
#define EXEC_ALWAYS ((7u << 18) | (0u << 21) | (1u << 23) | (2u << 25) | (3u << 27))
#define CTRL_32BIT  0x40u

/* MOV r0, WPOS.<swz> (END). */
static void wpos_prog(u8 prog[16], u32 swz)
{
    put_word(prog + 0, OPC(0x01) | OUTMASK_ALL | INSRC(0) | END);
    put_word(prog + 4, T_INPUT | swz | EXEC_ALWAYS);
    put_word(prog + 8, 0);
    put_word(prog + 12, 0);
}

/* Copy one line starting at `from` (through its newline) to f; returns the next line or NULL. */
static const char* put_line(FILE* f, const char* from)
{
    const char* nl = strchr(from, '\n');
    if (!nl) return NULL;
    fwrite(from, 1, (size_t)(nl - from + 1), f);
    return nl + 1;
}

/* The helper block the body's macros need: the RSX_FRAGCOORD_W_IS_RCP block (if any) and the _rsx_wpos define. */
static int dump_helpers(const char* hlsl, const char* path)
{
    FILE* f = fopen(path, "w");
    if (!f) { perror(path); return -1; }
    const char* at = strstr(hlsl, "#ifdef RSX_FRAGCOORD_W_IS_RCP\n");
    if (at) {
        const char* end = strstr(at, "#endif\n");
        if (end) fwrite(at, 1, (size_t)(end - at) + 7, f);
    }
    at = strstr(hlsl, "#define _rsx_wpos(p)");
    if (at) put_line(f, at);
    fclose(f);
    return 0;
}

static char h[32768];

static void case_one(const char* name, int buffered, u32 swz, const char* read, const char* dump)
{
    u8 prog[16]; char msg[200]; u32 nk = 0;
    wpos_prog(prog, swz);
    const int n = buffered ? rsx_fp_decompile_buffered_ex(prog, sizeof prog, CTRL_32BIT, 0, h, sizeof h, &nk)
                           : rsx_fp_decompile(prog, sizeof prog, CTRL_32BIT, h, sizeof h);
    snprintf(msg, sizeof msg, "%s decompiles", name);                     CHECK(n > 0, msg);
    snprintf(msg, sizeof msg, "%s reads %s", name, read);                 CHECK(strstr(h, read) != NULL, msg);
    snprintf(msg, sizeof msg, "%s declares _rsx_inv_w", name);            CHECK(strstr(h, "#define _rsx_inv_w(p)") != NULL, msg);
    const char* def = strstr(h, "#define _rsx_wpos(p)");
    snprintf(msg, sizeof msg, "%s declares _rsx_wpos", name);             CHECK(def != NULL, msg);
    if (def) {
        const char* nl = strchr(def, '\n');
        char line[400] = {0};
        if (nl && (size_t)(nl - def) < sizeof line) memcpy(line, def, (size_t)(nl - def));
        snprintf(msg, sizeof msg, "%s: _rsx_wpos w is _rsx_inv_w(p) [%s]", name, line);
        CHECK(strstr(line, ", _rsx_inv_w(p))") != NULL, msg);
        snprintf(msg, sizeof msg, "%s: _rsx_wpos does not pass (p).w [%s]", name, line);
        CHECK(strstr(line, "(p).w)") == NULL && strcmp(line, "#define _rsx_wpos(p) (p)") != 0, msg);
    }
    if (dump) dump_helpers(h, dump);
}

int main(int argc, char** argv)
{
    char buf_h[512], unbuf_h[512];
    const char* dir = argc > 1 ? argv[1] : NULL;
    if (dir) {
        snprintf(buf_h, sizeof buf_h, "%s/wpos_buf.h", dir);
        snprintf(unbuf_h, sizeof unbuf_h, "%s/wpos_unbuf.h", dir);
    }
    const u32 ident = SWZ(0, 1, 2, 3), wwww = SWZ(3, 3, 3, 3);
    case_one("buffered WPOS",        1, ident, "_rsx_wpos(input.position)).xyzw", dir ? buf_h : NULL);
    case_one("unbuffered WPOS",      0, ident, "_rsx_wpos(input.position)).xyzw", dir ? unbuf_h : NULL);
    case_one("buffered WPOS.wwww",   1, wwww,  "_rsx_wpos(input.position)).wwww", NULL);
    case_one("unbuffered WPOS.wwww", 0, wwww,  "_rsx_wpos(input.position)).wwww", NULL);

    /* A program without a WPOS read keeps neither helper (byte-identical to before). */
    {
        u8 prog[16]; u32 nk = 0;
        wpos_prog(prog, ident);
        put_word(prog + 0, OPC(0x01) | OUTMASK_ALL | INSRC(1) | END); /* MOV r0, COL0 */
        rsx_fp_decompile_buffered_ex(prog, sizeof prog, CTRL_32BIT, 0, h, sizeof h, &nk);
        CHECK(strstr(h, "_rsx_wpos") == NULL, "COL0 program has no _rsx_wpos");
        CHECK(strstr(h, "_rsx_inv_w") == NULL, "COL0 program has no _rsx_inv_w");
    }

    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("fp wpos.w text test: ok\n");
    return 0;
}
