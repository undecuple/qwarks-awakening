/* Quest for Booty: the text of each Lua error caught at luaD_rawrunprotected (diagnostic, default off).
 * Included by port/rc/qfb/hle_qfb.cpp; port/rc/tests/test_qfb_lua_errmsg.cpp includes it too.
 * The Lua 5.0.2 twin of port/rc/ffa/ffa_lua_errmsg.inc and port/rc/acit120/acit120_lua_errmsg.inc (same output line, same
 * switch); the frame reads match ACiT's, the data layout is Lua 5.0's (the RE notes section 21).
 *
 * Why: run qfb-s1 (work/proton/qfb-s1/app/app_stdout.txt l.1991-2001 and after l.2360, measured) has 22 `[ljmp] caught` landings
 * at site 0x005601E0, 2 with status 3 and 20 with status 1, one 3 + ten 1 at each level load, with no text.
 * RC_LUA_ERRMSG=N prints the first N of them (then every 1000th) as
 *   [lua-err] #n status=S (NAME) L=0x... lua_load|call f=0x... msg='<message>' chunk='<chunk name>' [src='...'] ra=...
 * through the landing hook of toolkit patch 7113 (ppu_sj_landed_fn). Unset or 0: the hook is not installed.
 *
 * Layout, read off the lifted code (work/recomp/qfb, ppu_recomp_045.cpp / _046.cpp) and the ELF's data words, 2026-11-23:
 *   func_005601A8 = luaD_rawrunprotected(L r3, f r4, ud r5): frame 0x2C0 (stdu); L at r1+0x2A0 (stw; also std at +0x2A8), f at
 *     r1+0x2B0, ud at r1+0x2B4 (the body reloads them from there after the setjmp, @0x005601E8..0x005601F4); lj.previous = L->errorJmp
 *     (L+0x50) at r1+0x70, lj.b = r1+0x74 (setjmp's r3), lj.status = r1+0x294 = jmp_buf + 0x220; L->errorJmp = &lj. The setjmp
 *     call is the `bl 0x00725328` (TOC stub to libc setjmp 0x006FF740) @0x005601E0, the site PPU_SJ_CALL passes; setjmp saves
 *     r1 and the lifted longjmp 0x006FF864 restores it, so at the landing ctx->gpr[1] is this frame. Its own LR is stored at
 *     r1+0x2D0 = caller_sp+0x10. f is a function descriptor (OPD) pointer: ctr = [f+0], r2 = [f+4], bctrl @0x0056020C.
 *   Lua 5.0 TObject is {int tt +0, Value value +4}, 8 bytes, and the status codes are 5.0's: luaD_protectedparser 0x005602A0
 *     (seterrorobj inlined) copies L->top - 1 (top = L+0x08, L->stack = L+0x1C) to oldtop for status 1 and 3 (words +0 and +4),
 *     makes luaS_newlstr(L, "not enough memory" 0x007C1B80, 0x11) for status 4 and ("error in error handling" 0x007C1B98, 0x17)
 *     for status 5 with tt 4 at +0 and the TString at +4 (strings through TOC 0x0084DA0C + 0x70D8 / + 0x70DC), then
 *     L->top = oldtop + 8. So at the landing the message is the TString at vm_read32(vm_read32(L+8) - 4) when
 *     vm_read32(top - 8) == 4 (LUA_TSTRING).
 *   func_0056D3F8 = luaS_newlstr: allocates len + 0x11, tt 4 at +4, marked +5, reserved +6, hash +8, len +0x0C, chars copied to
 *     +0x10, NUL at +0x10 + len; a hit compares the length at +0x0C and memcmp's from +0x10 (stub 0x00725398).
 *   luaD_protectedparser 0x005602A0: SParser on its stack at r1+0x70 {z +0, buff.buffer +4 (= 0), buff.size +8 (= 0),
 *     bin +0xC}, passed as ud straight to luaD_rawrunprotected(L, f_parser, &p) (Lua 5.0 has no SParser.name). f_parser's OPD
 *     is [TOC 0x0084DA0C + 0x70D4] = [0x00854AE0] = 0x0082CBA0 (ELF data word; code 0x00560850, OPD TOC 0x0084DA0C).
 *   func_00579D98 = luaZ_init(z, reader, data, name): ZIO {n +0, p +4, reader +8 (an OPD), data +0xC, name +0x10}; lua_load
 *     0x005573A0 inits it on its stack (r1+0x70) with its r4/r5/r6 (name "?" 0x007C1398 when r6 = 0, [TOC + 0x6F00]), calls
 *     luaZ_lookahead 0x00579E30 (which fills: p = the reader's buffer, n = its size) and luaD_protectedparser @0x00557410.
 *     So the chunk name is z->name, not an SParser field as in 5.1.
 *   Readers (OPDs read from the ELF; all three functions run on TOC 0x0084DA0C):
 *     the `execute` loader 0x0054D408 (the one ToD's RC_LUA_SRC hooks, matched by xgame-1123) passes reader [TOC + 0x6A9C] =
 *       0x0082C1E8 (OPD of 0x0054D5F8: data {char* s +0, u8 done +4}; strlen through stub 0x00722730, s NUL-terminated and
 *       left in place) and chunk name [TOC + 0x6AA0] = 0x007C02D0 "execute", then lua_pcall 0x00557CE8 if the load succeeded;
 *     luaL_loadbuffer 0x0055A100 and its load + pcall twin 0x0055A078 pass [TOC + 0x6F24] = 0x0082C890 (OPD of getS
 *       0x00559F70: LoadS {char* s +0, size_t size +4}; it returns s once and zeroes size, so at a landing size is 0 and the
 *       chunk length is (z->p - s) + z->n, the buffer the ZIO is still walking).
 *   ra = saved LR words up the guest back chain from rawrunprotected's frame (64-bit frames: back chain at sp+0, low word +4;
 *     the callee's LR at caller_sp+0x10, low word +0x14). Return sites: 0x005602F4 (luaD_protectedparser; the other
 *     rawrunprotected callers return to 0x00560460 (luaD_pcall 0x00560400), 0x00560B44, 0x0056CF3C, 0x0056D014), 0x00557414
 *     (lua_load), then 0x0054D460 (the `execute` loader), 0x0055A120 (luaL_loadbuffer) or 0x0055A0A4 (0x0055A078).
 * All addresses and offsets above are read in code or from the ELF's data words. ASSUMPTION: the status names 1 ERRRUN,
 * 2 ERRFILE, 3 ERRSYNTAX are Lua 5.0.2's lua.h names; the binary ties only 4 and 5 to their messages and 1/3 to the copy. */
#include "ppu_setjmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace qfb_lua_errmsg {
static const uint32_t kRawrunSite = 0x005601E0u;   /* luaD_rawrunprotected's setjmp-stub call (func_005601A8) */
static const uint32_t kFrameL     = 0x2A0u;        /* rawrunprotected frame: L (stw) */
static const uint32_t kFrameF     = 0x2B0u;        /*                        f (OPD pointer) */
static const uint32_t kFrameUd    = 0x2B4u;        /*                        ud */
static const uint32_t kFParserOpd = 0x0082CBA0u;   /* f_parser's OPD (code 0x00560850), passed by luaD_protectedparser 0x005602A0 */
static const uint32_t kLuaTString = 4u;            /* LUA_TSTRING */
static const uint32_t kTopTt      = 8u;            /* Lua 5.0 TObject {tt +0, value +4}: top-1 tt at top - 8 */
static const uint32_t kTopValue   = 4u;            /*                                       value at top - 4 */
static const uint32_t kZioName    = 0x10u;         /* ZIO.name (luaZ_init 0x00579D98) */
static const uint32_t kExecReader = 0x0082C1E8u;   /* OPD of func_0054D5F8, the `execute` loader's string reader */
static const uint32_t kGetSReader = 0x0082C890u;   /* OPD of lauxlib getS func_00559F70 (luaL_loadbuffer 0x0055A100) */
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
    static const char* const names[] = { "OK", "ERRRUN", "ERRFILE", "ERRSYNTAX", "ERRMEM", "ERRERR" };   /* Lua 5.0 */
    const uint32_t sp0 = (uint32_t)ctx->gpr[1];                     /* rawrunprotected's frame (setjmp saved r1) */
    const int fr = plausible(sp0);
    const uint32_t L = fr ? vm_read32(sp0 + kFrameL) : 0;
    const uint32_t f = fr ? vm_read32(sp0 + kFrameF) : 0;
    const uint32_t ud = fr ? vm_read32(sp0 + kFrameUd) : 0;
    char msg[4 * 240 + 8] = "", chunk[4 * 120 + 8] = "";
    if (status == 1 || status == 3) {                           /* the message is at L->top - 1 (seterrorobj copies it) */
        const uint32_t top = plausible(L) ? vm_read32(L + 0x08) : 0;
        if (plausible(top) && vm_read32(top - kTopTt) == kLuaTString) {
            const uint32_t ts = vm_read32(top - kTopValue);
            if (plausible(ts)) text(msg, sizeof msg, ts + 0x10, (long)vm_read32(ts + 0x0C), 240);
        } else {
            snprintf(msg, sizeof msg, "<top-1 not a string: top=0x%08X tt=%u>", top, plausible(top) ? vm_read32(top - kTopTt) : 0u);
        }
    }
    const int parser = f == kFParserOpd;
    char src[4 * kSrcMax + 48] = "";
    if (parser) {
        const uint32_t z = plausible(ud) ? vm_read32(ud + 0x00) : 0;     /* SParser.z -> ZIO */
        if (plausible(z)) {
            const uint32_t name = vm_read32(z + kZioName);
            if (plausible(name)) text(chunk, sizeof chunk, name, -1, 120);
            const uint32_t zn = vm_read32(z + 0x00), zp = vm_read32(z + 0x04);
            const uint32_t reader = vm_read32(z + 0x08), data = vm_read32(z + 0x0C);
            const uint32_t s = plausible(data) ? vm_read32(data + 0x00) : 0;
            if (reader == kExecReader && plausible(s)) text(src, sizeof src, s, -1, kSrcMax);
            else if (reader == kGetSReader && plausible(s)) {
                uint32_t len = vm_read32(data + 0x04);                     /* still unread: the whole buffer */
                /* read: getS zeroed size, the ZIO walks s. At end of input the lexer's zgetc (@0x005AC894) stores
                 * n - 1 before luaZ_fill (0x00579CF8) returns EOZ without resetting it, so n is 0xFFFFFFFF there and
                 * the chunk ends at zp. */
                if (!len && zp >= s) len = (int32_t)zn < 0 ? (zp - s) : (zp - s) + zn;
                text(src, sizeof src, s, (long)len, kSrcMax);
            }
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
} // namespace qfb_lua_errmsg
