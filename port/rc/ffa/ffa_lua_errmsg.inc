/* Full Frontal Assault 1.05: the text of each Lua error caught at luaD_pcall (diagnostic, default off).
 * Included by port/rc/ffa/hle_ffa.cpp; port/rc/tests/test_ffa_lua_errmsg.cpp includes it too.
 *
 * Why: run ls-ffa-newgame-1119-203122 caught 1000-1999 Lua errors at 238 s, when phoenix_ph1 opened, all at the setjmp
 * call 0x0080DE98 with status 3 (LUA_ERRSYNTAX: a lua_load failed), and the patch 1116 line `[ljmp] caught` has no text.
 * RC_LUA_ERRMSG=N prints the first N of them (then every 1000th) as
 *   [lua-err] #n status=S (NAME) L=0x... msg='<the parser's message>' chunk='<chunk name>'
 * through the landing hook of toolkit patch 7113 (ppu_sj_landed_fn). Unset or 0: the hook is not installed.
 *
 * Layout, read off the lifted code (work/recomp/ffa, 2026-11-19), Lua 5.1 with a 4-byte lua_Number:
 *   func_0080DE24 = luaD_pcall(L r3, func r4, u r5, old_top r6, ef r7) with luaD_rawrunprotected inlined:
 *     r31 = L, r24 = func (an OPD pointer), r25 = u, all set before the setjmp `bl` @0x0080DE98, so the registers
 *     the landing gets back (setjmp's save) hold them; lj.status = r1+0x294 = jmp_buf arg (r1+0x74) + 0x220.
 *     Status 2/3 copy L->top - 1 to old_top (0x0080DF00..0x0080DF10): top = L+0x08, TValue 8 bytes, {value +0, tt +4};
 *     status 4 / 5 set luaS_newlstr("not enough memory", 17) / ("error in error handling", 23) with tt = 4 = string.
 *   func_00816D68 = luaS_newlstr: TString len +0x0C (compared with the new length), chars +0x10 (memcmp from ts+0x10).
 *   func_0080E07C = luaD_protectedparser(L, z, name): struct SParser on its stack at r1+0x70 {z +0, buff.buffer +4,
 *     buff.n +8, buff.buffsize +0xC, name +0x10}, passed as u; func = 0x00E5E650 (lis 0xE6; addi -6576) = f_parser's OPD.
 *   So at a landing: message = TString at vm_read32(vm_read32(L+8) - 8) when vm_read32(top - 4) == 4, text at +0x10,
 *   length at +0x0C; chunk name = vm_read32(u + 0x10) when func == f_parser (a lua_load), else none (a lua_pcall).
 * Each syntax message already starts with the chunk id ([string "..."]:line:), so `chunk=` mostly confirms it.
 *
 * The source text (2026-11-20, run ls-ffa-newgame-luamsg-1120-001755: every phoenix_ph1 failure is
 * `[string "execute"]:1: unexpected symbol near `]'` with chunk 'execute'). For a lua_load the line also carries
 *   src='<first 200 bytes of the chunk>' ra=<return addresses up the guest stack>
 *   SParser.z (u + 0) is the ZIO, laid out by luaZ_init func_0081D9D8: {n +0, p +4, reader +8, data +0xC, L +0x10}.
 *   Reader 0x00E5E608 (OPD of func_00805BD0) is the game's string reader used by its `execute` helper func_00805C34
 *     (lua_load(L, reader, &{s, done}, "execute") then lua_pcall; chunk name "execute" = 0x00C3F3CC): data = {char* s +0,
 *     u8 done +4}, s NUL-terminated (strlen 0x00ABEC58). func_00805C34 is called by the script-VM method func_00804218
 *     (vtable slot, `this+4` = L; also via the Lua global `execute` = func_0004DC40 -> func_0002D0F0 on the VM at
 *     0x00F85CA0, registered at 0x00035FC4) and by 0x0066329C (the constant rand_select definition at 0x00C76464).
 *   Reader 0x00E5E680 is lauxlib's getS (luaL_loadbuffer func_0081F8F0): data = LoadS {char* s +0, size_t size +4}.
 *   Any other reader: its OPD and data pointer are printed.
 *   ra = the saved LR words up the back chain from luaD_pcall's frame (r1 at setjmp): 64-bit frames, back chain at
 *     sp + 0 (low word +4), the callee's LR at caller_sp + 0x10 (low word +0x14): 0x0080E0D4 (in protectedparser),
 *     0x0080A8E8 (lua_load), 0x00805C70 (execute helper), 0x00804234 (VM method), then the method's caller:
 *     0x0002D10C + 0x0004DC5C = the Lua global `execute`, anything else = C++ game code calling the VM slot. */
#include "ppu_setjmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ffa_lua_errmsg {
static const uint32_t kPcallSite   = 0x0080DE98u;   /* luaD_pcall's setjmp call (func_0080DE24) */
static const uint32_t kFParserOpd  = 0x00E5E650u;   /* f_parser's OPD, passed by luaD_protectedparser 0x0080E07C */
static const uint32_t kLuaTString  = 4u;            /* LUA_TSTRING */
static const uint32_t kExecReader  = 0x00E5E608u;   /* OPD of func_00805BD0, the `execute` helper's string reader */
static const uint32_t kGetSReader  = 0x00E5E680u;   /* OPD of lauxlib getS (luaL_loadbuffer) */
static const uint32_t kSrcMax      = 200u;          /* source bytes printed */
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
    if (site != kPcallSite) return;
    ppu_context* ctx = (ppu_context*)c;
    const unsigned long long n = __atomic_add_fetch(&s_n, 1, __ATOMIC_RELAXED);
    if (n > s_cap && n % 1000) return;
    static const char* const names[] = { "OK", "YIELD", "ERRRUN", "ERRSYNTAX", "ERRMEM", "ERRERR" };
    const uint32_t L = (uint32_t)ctx->gpr[31];
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
    const int parser = (uint32_t)ctx->gpr[24] == kFParserOpd;
    char src[4 * kSrcMax + 48] = "";
    if (parser) {
        const uint32_t sp = (uint32_t)ctx->gpr[25];
        const uint32_t name = plausible(sp) ? vm_read32(sp + 0x10) : 0;
        if (plausible(name)) text(chunk, sizeof chunk, name, -1, 120);
        const uint32_t z = plausible(sp) ? vm_read32(sp + 0x00) : 0;        /* SParser.z -> ZIO */
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
        uint32_t sp = (uint32_t)ctx->gpr[1];
        for (int i = 0; i < 8 && plausible(sp); i++) {
            const uint32_t back = vm_read32(sp + 0x04);                     /* 64-bit back chain, low word */
            if (!plausible(back) || back <= sp) break;
            const uint32_t lr = vm_read32(back + 0x14);                     /* saved LR at caller_sp + 0x10, low word */
            o += (size_t)snprintf(ra + o, sizeof ra - o, "%s0x%08X", i ? "," : "", lr);
            sp = back;
        }
    }
    fprintf(stderr, "[lua-err] #%llu status=%d (%s) L=0x%08X %s msg='%s' chunk='%s'%s%s%s ra=%s\n", n, status,
            (status >= 0 && status <= 5) ? names[status] : "?", L, parser ? "lua_load" : "lua_pcall", msg, chunk,
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
    fprintf(stderr, "[lua-err] RC_LUA_ERRMSG=%lld: Lua error text at luaD_pcall 0x%08X (first %lld, then every 1000th)\n",
            v, kPcallSite, v);
    return 1;
}
} // namespace ffa_lua_errmsg
