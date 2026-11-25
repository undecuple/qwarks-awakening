/* A Crack in Time 1.20: the text of each Lua error caught at luaD_rawrunprotected (diagnostic, default off).
 * Included by port/rc/acit120/hle_acit120.cpp; port/rc/tests/test_acit_lua_errmsg.cpp includes it too.
 * The ACiT twin of port/rc/ffa/ffa_lua_errmsg.inc (same output line, same switch); only the frame reads differ.
 *
 * Why: run ls-acit-newgame-1119-093907 has 60 `[ljmp] caught status=3` (LUA_ERRSYNTAX) landings, all at the setjmp call
 * 0x0050AF94, from 471 s in great_clock_a in pairs every ~10 s (the period of the fade + autosave loop), with no text.
 * RC_LUA_ERRMSG=N prints the first N of them (then every 1000th) as
 *   [lua-err] #n status=S (NAME) L=0x... lua_load|call f=0x... msg='<message>' chunk='<chunk name>' [src='...'] ra=...
 * through the landing hook of toolkit patch 7113 (ppu_sj_landed_fn). Unset or 0: the hook is not installed.
 *
 * Layout, read off the lifted code (work/recomp/acit120, ppu_recomp_023.cpp) and the decomp (work/decomp/acit120), Lua 5.1
 * with 4-byte pointers, 2026-11-23:
 *   func_0050AF5C = luaD_rawrunprotected(L r3, f r4, ud r5), NOT inlined into luaD_pcall 0x0050B8A8 (unlike FFA): frame
 *     0x2D0 (stdu); it keeps its arguments on the stack, not in callee-saved registers: L at r1+0x2A0 (stw; also std at
 *     +0x2A8), f at r1+0x2B0, ud at r1+0x2B4; lj.previous = L->errorJmp (L+0x5C) at r1+0x70, lj.b = r1+0x74 (setjmp's
 *     r3), lj.status = r1+0x294 = jmp_buf + 0x220 (the overrides row's status=0x220); L->errorJmp = &lj. The setjmp `bl`
 *     is @0x0050AF94; setjmp saves r1 and the lifted longjmp restores it, so at the landing ctx->gpr[1] is this frame.
 *     f is a function descriptor (OPD) pointer: the body does ctr = [f+0], r2 = [f+4] @0x0050AFB0..
 *   func_0050A780 = luaD_seterrorobj(L, status, oldtop), called by luaD_pcall after the landing: status 2/3 copy
 *     L->top - 1 (top = L+0x08) to oldtop as two words {value +0, tt +4} (TValue 8 bytes), then L->top = oldtop + 8;
 *     status 4/5 make luaS_newlstr(L, "not enough memory", 0x11) / ("error in error handling", 0x17) with tt 4
 *     (both strings read at the TOC-relative table the code loads: 0x00ABFC60 / 0x00ABFC78). So at the landing the
 *     message is the TString at vm_read32(vm_read32(L+8) - 8) when vm_read32(top - 4) == 4 (LUA_TSTRING).
 *   func_005137E4 = luaS_newlstr: allocates len + 0x11, tt 4 at +4, hash +8, len +0x0C, chars copied to +0x10, NUL at
 *     +0x10 + len; a hit compares the length at +0x0C and memcmp's from +0x10.
 *   func_0050B9D0 = luaD_protectedparser(L, z, name): SParser on its stack at r1+0x70 {z +0, buff.buffer +4 (= 0),
 *     buff.buffsize +0xC (= 0), name +0x10}, passed as u to luaD_pcall(L, f_parser, &p, savestack(top), L->errfunc)
 *     (savestack = L+0x08 - L+0x20 stack, errfunc L+0x60); luaD_pcall passes r4/r5 on unchanged to rawrunprotected.
 *     f_parser's OPD is loaded through the TOC: [[TOC-0x5344] - 0x7FD4] = [0x00CA021C] = 0x00D0CB38 (ELF data word,
 *     TOC 0x00D347B0, [0x00D2F46C] = 0x00CA81F0), its code 0x0050AA7C: reads z = [p+0] (lookahead, 0x1B = binary chunk
 *     test) and the name at p+0x10.
 *   func_0051B6EC = luaZ_init(L, z, reader, data): ZIO {n +0, p +4, reader +8, data +0xC, L +0x10} (as FFA).
 *   Readers: the `execute` loaders func_00500C84 (lua_load then lua_pcall(L, 0, 1, 0) and a number read) and
 *     func_00500D5C (lua_load then lua_pcall(L, 0, 0, 0)) both pass reader [[TOC-0x5358] - 0x7FBC] = 0x00D0C498 (OPD of
 *     func_004FFC48: {char* s +0, u8 done +4}, s NUL-terminated, strlen 0x00903910) and chunk name [.. - 0x7FB8] =
 *     0x00ABF458 "execute"; luaL_loadbuffer func_00505698 passes [[TOC-0x5350] - 0x7FF0] = 0x00D0C7E8 (OPD of getS
 *     func_00505574: LoadS {char* s +0, size_t size +4}).
 *   ra = saved LR words up the guest back chain from rawrunprotected's frame (64-bit frames, back chain sp+0 low word +4,
 *     the callee's LR at caller_sp+0x10 low word +0x14; rawrunprotected's own at r1+0x2E0 = caller_sp+0x10). Return
 *     sites: 0x0050B900 (luaD_pcall; the other rawrunprotected callers return to 0x0050B084, 0x005130A0, 0x0051325C),
 *     0x0050BA2C (protectedparser), 0x00501A0C (lua_load), then 0x00500CC0 / 0x00500D94 (the two `execute` loaders) or
 *     0x005056C0 (luaL_loadbuffer) and the other lua_load callers.
 * All addresses and offsets above are read in code or from the ELF's data words; none is a guess. */
#include "ppu_setjmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace acit120_lua_errmsg {
static const uint32_t kRawrunSite = 0x0050AF94u;   /* luaD_rawrunprotected's setjmp call (func_0050AF5C) */
static const uint32_t kFrameL     = 0x2A0u;        /* rawrunprotected frame: L (stw) */
static const uint32_t kFrameF     = 0x2B0u;        /*                        f (OPD pointer) */
static const uint32_t kFrameUd    = 0x2B4u;        /*                        ud */
static const uint32_t kFParserOpd = 0x00D0CB38u;   /* f_parser's OPD (code 0x0050AA7C), passed by luaD_protectedparser 0x0050B9D0 */
static const uint32_t kLuaTString = 4u;            /* LUA_TSTRING */
static const uint32_t kExecReader = 0x00D0C498u;   /* OPD of func_004FFC48, the `execute` loaders' string reader */
static const uint32_t kGetSReader = 0x00D0C7E8u;   /* OPD of lauxlib getS func_00505574 (luaL_loadbuffer 0x00505698) */
static const uint32_t kSrcMax     = 200u;          /* source bytes printed */
static unsigned long long s_cap = 0;
static unsigned long long s_n = 0;

static int plausible(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

/* Guest bytes [src, src+len) as printable text (non-printables as \xNN), cut at `max` bytes; len < 0 = NUL-terminated. */
static void text(char* out, size_t cap, uint32_t src, long len, uint32_t max)
{
    size_t o = 0; uint32_t i = 0;
    for (; i < max && (len < 0 || (long)i < len) && o + 5 < cap; i++) {
        const uint8_t c = vm_read8(src + i);
        if (len < 0 && !c) break;
        if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c >= 0x20 && c < 0x7F && c != '\\' && c != '\'') out[o++] = (char)c;
        else o += (size_t)snprintf(out + o, cap - o, "\\x%02X", c);
    }
    out[o] = 0;
    if (i == max && (len < 0 ? vm_read8(src + i) != 0 : (long)i < len) && o + 4 < cap) { memcpy(out + o, "...", 4); }
}

static void on_landed(struct ppu_context* c, uint32_t site, int32_t status)
{
    if (site != kRawrunSite) return;
    ppu_context* ctx = (ppu_context*)c;
    const unsigned long long n = __atomic_add_fetch(&s_n, 1, __ATOMIC_RELAXED);
    if (n > s_cap && n % 1000) return;
    static const char* const names[] = { "OK", "YIELD", "ERRRUN", "ERRSYNTAX", "ERRMEM", "ERRERR" };
    const uint32_t sp0 = (uint32_t)ctx->gpr[1];                     /* rawrunprotected's frame (setjmp saved r1) */
    const int fr = plausible(sp0);
    const uint32_t L = fr ? vm_read32(sp0 + kFrameL) : 0;
    const uint32_t f = fr ? vm_read32(sp0 + kFrameF) : 0;
    const uint32_t ud = fr ? vm_read32(sp0 + kFrameUd) : 0;
    char msg[4 * 240 + 8] = "", chunk[4 * 120 + 8] = "";
    if (status == 2 || status == 3) {                           /* the message is at L->top - 1 (luaD_seterrorobj copies it) */
        const uint32_t top = plausible(L) ? vm_read32(L + 0x08) : 0;
        if (plausible(top) && vm_read32(top - 4) == kLuaTString) {
            const uint32_t ts = vm_read32(top - 8);
            if (plausible(ts)) text(msg, sizeof msg, ts + 0x10, (long)vm_read32(ts + 0x0C), 240);
        } else {
            snprintf(msg, sizeof msg, "<top-1 not a string: top=0x%08X tt=%u>", top, plausible(top) ? vm_read32(top - 4) : 0u);
        }
    }
    const int parser = f == kFParserOpd;
    char src[4 * kSrcMax + 48] = "";
    if (parser) {
        const uint32_t name = plausible(ud) ? vm_read32(ud + 0x10) : 0;  /* SParser.name */
        if (plausible(name)) text(chunk, sizeof chunk, name, -1, 120);
        const uint32_t z = plausible(ud) ? vm_read32(ud + 0x00) : 0;     /* SParser.z -> ZIO */
        if (plausible(z)) {
            const uint32_t reader = vm_read32(z + 0x08), data = vm_read32(z + 0x0C);
            const uint32_t s = plausible(data) ? vm_read32(data + 0x00) : 0;
            if (reader == kExecReader && plausible(s))      text(src, sizeof src, s, -1, kSrcMax);
            else if (reader == kGetSReader && plausible(s)) text(src, sizeof src, s, (long)vm_read32(data + 0x04), kSrcMax);
            else snprintf(src, sizeof src, "<reader 0x%08X data 0x%08X>", reader, data);
        }
    }
    char ra[8 * 11 + 8] = "";
    {
        size_t o = 0;
        uint32_t sp = sp0;
        for (int i = 0; i < 8 && plausible(sp); i++) {
            const uint32_t back = vm_read32(sp + 0x04);                     /* 64-bit back chain, low word */
            if (!plausible(back) || back <= sp) break;
            const uint32_t lr = vm_read32(back + 0x14);                     /* saved LR at caller_sp + 0x10, low word */
            o += (size_t)snprintf(ra + o, sizeof ra - o, "%s0x%08X", i ? "," : "", lr);
            sp = back;
        }
    }
    char kind[32];
    if (parser) snprintf(kind, sizeof kind, "lua_load");
    else snprintf(kind, sizeof kind, "call f=0x%08X", f);
    fprintf(stderr, "[lua-err] #%llu status=%d (%s) L=0x%08X %s msg='%s' chunk='%s'%s%s%s ra=%s\n", n, status,
            (status >= 0 && status <= 5) ? names[status] : "?", L, kind, msg, chunk,
            parser ? " src='" : "", src, parser ? "'" : "", ra[0] ? ra : "-");
}

/* RC_LUA_ERRMSG=N (N > 0): install the hook. Returns 1 when installed. */
static int install(void)
{
    const char* e = getenv("RC_LUA_ERRMSG");
    const long long v = e ? atoll(e) : 0;
    if (v <= 0) return 0;
    s_cap = (unsigned long long)v;
    ppu_sj_landed_fn = on_landed;
    fprintf(stderr, "[lua-err] RC_LUA_ERRMSG=%lld: Lua error text at luaD_rawrunprotected 0x%08X (first %lld, then every 1000th)\n",
            v, kRawrunSite, v);
    return 1;
}
} // namespace acit120_lua_errmsg
