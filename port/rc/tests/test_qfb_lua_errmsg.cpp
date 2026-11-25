/* Host test (no GPU, no game run, no game data in the repo): Quest for Booty's RC_LUA_ERRMSG diagnostic
 * (port/rc/qfb/qfb_lua_errmsg.inc) through the landing hook of toolkit patch 7113, with the REAL lifted luaD_rawrunprotected
 * 0x005601A8 and luaD_throw 0x00560238 (and the setjmp/longjmp stubs and libc setjmp/longjmp) cut out of the QfB lift at run time,
 * as run_qfb_lua_ljmp_test.sh does, against the toolkit's runtime half runtime/ppu/ppu_setjmp_rt.inc (patch 1116 + 7113). Built
 * and run by run_qfb_lua_errmsg_test.sh. The Lua 5.0.2 twin of test_acit_lua_errmsg.cpp; unlike that test, the frame the hook
 * reads (L at r1+0x2A0, f at +0x2B0, ud at +0x2B4, the saved LR at +0x2D0) is written by the lifted code itself, not by a stand-in.
 *
 * Synthetic guest data, laid out as read off the lift (see the .inc): lua_State L with top at +0x08, errorJmp +0x50 (luaD_throw
 * reads it); Lua 5.0 TObjects {tt +0, value +4}; a TString with len +0x0C and the text at +0x10; an SParser {z +0}; a ZIO
 * {n +0, p +4, reader +8, data +0xC, name +0x10}. The protected function f (reached through ps3_indirect_call from the OPD the
 * lifted rawrunprotected reads) clobbers r3..r31, opens a frame, pushes the message and calls the lifted luaD_throw.
 *
 * Cases: load   lua_load (f = f_parser's OPD 0x0082CBA0) with status 3 through the `execute` reader (OPD 0x0082C1E8) and a
 *               guest back chain: message, chunk name (ZIO.name), `src=`, `ra=` with rawrunprotected's own saved LR first.
 *        regs   r31/r24/r25 hold other L/f/name values at the call: the line uses the frame's.
 *        call   another f with status 1 (ERRRUN in 5.0): message, `call f=0x...`, no src field.
 *        mem    status 4, err status 5: the line, no message (Lua sets it after the landing).
 *        notstr status 3 with a number at top-1: the `<top-1 not a string` note.
 *        site   the hook called for another site (ACiT's 0x0050AF94): no line.
 *        long   a 260-byte statement: the first 200 bytes and `...`.
 *        gets   lauxlib getS (OPD 0x0082C890) after the read (size 0, the ZIO 5 bytes in with 3 left): the 8 chunk bytes;
 *        gets0  the same before the read (size 8): the 8 bytes.
 *        reader another reader: `src='<reader 0x... data 0x...>'`.
 *        cap    RC_LUA_ERRMSG=2 and 5 landings: 2 lines.
 *        off    (negative control) RC_LUA_ERRMSG unset: the hook is not installed, the `[ljmp] caught` lines still print,
 *               no `[lua-err]` line.
 * Exit 0 = PASS. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <sys/mman.h>
#define PPU_VMI_DISABLE 1
#include LUA_LIFTED_SUBSET
#undef vm_read8
#undef vm_read16
#undef vm_read32
#undef vm_read64
#undef vm_write8
#undef vm_write16
#undef vm_write32
#undef vm_write64

extern "C" {
uint8_t* vm_base;
int vm_inline_ok = 1;
int vm_inline_st_ok = 1;
uint32_t g_null_sweep_hi = 0;
uint32_t g_barrier_sync_watch = 0;
const unsigned char* g_spu_coh_bits = 0;
uint32_t ppu_vm_size = 0;
uint32_t ppu_hle_inject_base = 0xF0000000u;
unsigned char g_ppu_vm_page[65536];
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*) = 0;
static void bounds(uint64_t a) { if ((uint32_t)a >= 0x01000000u) { printf("FAIL: guest access 0x%08X outside the test memory\n", (uint32_t)a); exit(1); } }
uint8_t vm_read8(uint64_t a) { bounds(a); return vm_base[(uint32_t)a]; }
uint16_t vm_read16(uint64_t a) { bounds(a); uint16_t v; memcpy(&v, vm_base + (uint32_t)a, 2); return __builtin_bswap16(v); }
uint32_t vm_read32(uint64_t a) { bounds(a); uint32_t v; memcpy(&v, vm_base + (uint32_t)a, 4); return __builtin_bswap32(v); }
uint64_t vm_read64(uint64_t a) { bounds(a); uint64_t v; memcpy(&v, vm_base + (uint32_t)a, 8); return __builtin_bswap64(v); }
void vm_write8(uint64_t a, uint8_t v) { bounds(a); vm_base[(uint32_t)a] = v; }
void vm_write16(uint64_t a, uint16_t v) { bounds(a); v = __builtin_bswap16(v); memcpy(vm_base + (uint32_t)a, &v, 2); }
void vm_write32(uint64_t a, uint32_t v) { bounds(a); v = __builtin_bswap32(v); memcpy(vm_base + (uint32_t)a, &v, 4); }
void vm_write64(uint64_t a, uint64_t v) { bounds(a); v = __builtin_bswap64(v); memcpy(vm_base + (uint32_t)a, &v, 8); }
int spu_coh_is_reserved(uint32_t) { return 0; }
void ppu_st_coh(uint32_t ea, const void* src, uint32_t n) { memcpy(vm_base + ea, src, n); }
void ppu_store_note(uint32_t, uint32_t) {}
void ps3_hle_call(unsigned, ppu_context*) { fprintf(stdout, "FAIL: unexpected hle call\n"); exit(2); }
void ps3_indirect_call_to(ppu_context*, uint32_t) { fprintf(stdout, "FAIL: unexpected indirect call\n"); exit(2); }
static int g_depth = 0, g_vsp = 0;
int* ps3_ppu_dispatch_depth_slot(void) { return &g_depth; }
int* ps3_ppu_vcall_sp_slot(void) { return &g_vsp; }
void ps3_indirect_call(ppu_context* ctx);
}
#include PPU_SJ_RT                              /* the toolkit's runtime half (patch 1116 + 7113) */
#include QFB_LUA_ERRMSG_INC                     /* port/rc/qfb/qfb_lua_errmsg.inc */

static int g_unexpected = 0;
void func_005602A0(ppu_context*) { g_unexpected = 1; }                                   /* luaD_throw's fall-through neighbour */
void func_00725018(ppu_context*) { g_unexpected = 1; }                                   /* dead fall-through of the exit stub */
void func_00725360(ppu_context*) { g_unexpected = 1; }                                   /* dead fall-through of the longjmp stub */

/* Guest addresses (all inside the 16 MB test memory; the OPD addresses are QfB's own) */
static const uint32_t TOC = 0x0084DA0Cu, F_ERR = 0x00001000u;
static const uint32_t L = 0x00100000u, G = 0x00100200u, STACK = 0x00110000u, TS = 0x00130000u, SP = 0x00140000u;
static const uint32_t NAME = 0x00150000u, ZIO = 0x00160000u, DATA = 0x00170000u, SRC = 0x00180000u;
static const uint32_t kOtherOpd = 0x00120000u, L_REG = 0x00111000u;
static const uint32_t F1 = 0x00E00000u, F2 = F1 + 0x100u, F3 = F1 + 0x200u;   /* the caller frames above rawrunprotected */

static void put64(uint32_t a, uint32_t lo) { vm_write32(a, 0); vm_write32(a + 4, lo); }

struct Case { uint32_t func; int32_t status; int str_on_top; const char* text; };
static Case g_case;

/* The ZIO lua_load built (reader 0 = SParser.z = NULL), its data and the guest back chain F1 -> F2 -> F3 -> 0 (chain) or F1 -> 0. */
static void set_source(uint32_t reader, const char* name, const char* text, uint32_t size, uint32_t zp_off, uint32_t zn, bool chain)
{
    vm_write32(SP + 0x00, reader ? ZIO : 0);
    memset(vm_base + NAME, 0, 0x100); if (name) memcpy(vm_base + NAME, name, strlen(name) + 1);
    vm_write32(ZIO + 0x00, zn); vm_write32(ZIO + 0x04, zp_off ? SRC + zp_off : 0);
    vm_write32(ZIO + 0x08, reader); vm_write32(ZIO + 0x0C, DATA); vm_write32(ZIO + 0x10, name ? NAME : 0);
    vm_write32(DATA + 0x00, SRC); vm_write32(DATA + 0x04, size);
    memset(vm_base + SRC, 0, 0x400);
    if (text) memcpy(vm_base + SRC, text, strlen(text) + 1);
    put64(F1, chain ? F2 : 0); put64(F2, F3); put64(F3, 0);
    put64(F2 + 0x10, 0x00557414u);                 /* lua_load's LR: return into lua_load after luaD_protectedparser */
    put64(F3 + 0x10, 0x0054D460u);                 /* protectedparser's caller's LR: return into the `execute` loader */
}

static void clobber(ppu_context* ctx) { for (int r = 3; r < 32; r++) if (r != 1 && r != 2) ctx->gpr[r] = 0xDEAD0000u + (uint32_t)r; }

void ps3_indirect_call(ppu_context* ctx)
{
    switch ((uint32_t)ctx->ctr) {
    case F_ERR: {                                                                    /* f: push the message and throw */
        clobber(ctx);
        vm_write64(ctx->gpr[1] - 0x100, ctx->gpr[1]); ctx->gpr[1] -= 0x100;           /* f's own frame */
        const uint32_t len = (uint32_t)strlen(g_case.text);
        vm_write32(TS + 0x0C, len);
        memcpy(vm_base + TS + 0x10, g_case.text, len + 1);
        vm_write32(STACK + 0, g_case.str_on_top ? 4u : 3u);                          /* L->top - 1: tt */
        vm_write32(STACK + 4, g_case.str_on_top ? TS : 0x3F800000u);                 /*               value */
        vm_write32(L + 0x08, STACK + 8);                                             /* L->top */
        ctx->gpr[3] = L; ctx->gpr[4] = (uint64_t)(uint32_t)g_case.status; ctx->lr = 0x00F0F0F0;
        func_00560238(ctx); DRAIN_TRAMPOLINE(ctx);                                   /* the lifted luaD_throw */
        fprintf(stdout, "FAIL: luaD_throw returned (no landing)\n"); exit(1);
    }
    case 0x006FF740: func_006FF740(ctx); return;                                 /* libc setjmp, from stub 0x00725328 */
    case 0x006FF864: func_006FF864(ctx); return;                                 /* libc longjmp (wrapper), from stub 0x00725344 */
    default: fprintf(stdout, "FAIL: unexpected indirect call 0x%llx\n", (unsigned long long)ctx->ctr); exit(1);
    }
}

static int32_t rrp(ppu_context* ctx, bool decoys)   /* luaD_rawrunprotected(L, f, &SParser) the way luaD_protectedparser calls it */
{
    memset(ctx, 0, sizeof *ctx);
    vm_write32(L + 0x10, G); vm_write32(L + 0x50, 0);                 /* l_G, errorJmp = NULL */
    ctx->gpr[1] = F1; ctx->gpr[2] = TOC;
    if (decoys) { ctx->gpr[31] = L_REG; ctx->gpr[24] = qfb_lua_errmsg::kFParserOpd; ctx->gpr[25] = NAME; }
    ctx->gpr[3] = L; ctx->gpr[4] = g_case.func; ctx->gpr[5] = SP; ctx->lr = 0x005602F4u;
    func_005601A8(ctx); DRAIN_TRAMPOLINE(ctx);
    if (ctx->gpr[1] != F1) { fprintf(stdout, "FAIL: r1 0x%llx after rawrunprotected\n", (unsigned long long)ctx->gpr[1]); exit(1); }
    if (vm_read32(L + 0x50) != 0) { fprintf(stdout, "FAIL: errorJmp not restored\n"); exit(1); }
    if (g_unexpected) { fprintf(stdout, "FAIL: a fall-through neighbour ran\n"); exit(1); }
    return (int32_t)ctx->gpr[3];
}

static std::string run(const Case& c, int times = 1, bool decoys = false)
{
    g_case = c;
    fflush(stderr);
    FILE* t = tmpfile();
    if (!t) { perror("tmpfile"); exit(2); }
    FILE* keep = fdopen(dup(fileno(stderr)), "w");
    fflush(stderr); dup2(fileno(t), fileno(stderr));
    for (int i = 0; i < times; i++) {
        static ppu_context ctx;
        if (rrp(&ctx, decoys) != c.status) { dup2(fileno(keep), fileno(stderr)); fprintf(stdout, "FAIL: rawrunprotected did not return status %d\n", c.status); exit(1); }
    }
    fflush(stderr); dup2(fileno(keep), fileno(stderr)); fclose(keep);
    std::string out; char buf[4096]; size_t n;
    rewind(t);
    while ((n = fread(buf, 1, sizeof buf, t)) > 0) out.append(buf, n);
    fclose(t);
    return out;
}

static int g_fail = 0;
static void expect(const char* name, const std::string& out, const char* needle, bool present)
{
    const bool has = out.find(needle) != std::string::npos;
    if (has != present) { g_fail++; fprintf(stdout, "FAIL %s: %s `%s`\n--- stderr ---\n%s--------------\n", name, present ? "missing" : "unexpected", needle, out.c_str()); }
}
static int count(const std::string& out, const char* needle)
{
    int n = 0; for (size_t p = out.find(needle); p != std::string::npos; p = out.find(needle, p + 1)) n++; return n;
}

int main(int argc, char** argv)
{
    const bool off = argc > 1 && !strcmp(argv[1], "off");
    namespace Q = qfb_lua_errmsg;
    vm_base = (uint8_t*)mmap(nullptr, 0x01000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (vm_base == MAP_FAILED) { perror("mmap"); return 2; }
    vm_write32(Q::kFParserOpd, F_ERR); vm_write32(Q::kFParserOpd + 4, TOC);   /* f_parser's OPD -> the test's f */
    vm_write32(kOtherOpd, F_ERR); vm_write32(kOtherOpd + 4, TOC);
    const char* syn = "[string \"execute\"]:1: unexpected symbol near `]'";
    if (off) {
        unsetenv("RC_LUA_ERRMSG");
        if (Q::install()) { fprintf(stdout, "FAIL off: installed without RC_LUA_ERRMSG\n"); return 1; }
        set_source(Q::kExecReader, "execute", "levelvars[] = 1", 0, 0, 0, true);
        const std::string o = run({Q::kFParserOpd, 3, 1, syn});
        expect("off", o, "[ljmp] caught status=3", true);
        expect("off", o, "site 0x005601E0", true);
        expect("off", o, "[lua-err]", false);
        fprintf(stdout, g_fail ? "off: FAIL\n" : "off: PASS (hook not installed: only the [ljmp] line)\n");
        return g_fail ? 1 : 0;
    }
    setenv("RC_LUA_ERRMSG", "50", 1);
    if (!Q::install()) { fprintf(stdout, "FAIL: RC_LUA_ERRMSG=50 did not install\n"); return 1; }

    set_source(Q::kExecReader, "execute", "levelvars[] = 1", 0, 0, 0, true);
    std::string o = run({Q::kFParserOpd, 3, 1, syn}, 1, true);
    expect("load", o, "[lua-err] #1 status=3 (ERRSYNTAX) L=0x00100000 lua_load msg='[string \"execute\"]:1: unexpected symbol near `]\\x27' chunk='execute' src='levelvars[] = 1' ra=0x005602F4,0x00557414,0x0054D460\n", true);
    expect("regs", o, "L=0x00111000", false);

    set_source(0, nullptr, nullptr, 0, 0, 0, false);
    o = run({kOtherOpd, 1, 1, "scripts/x.lua:3: attempt to index a nil value"});
    expect("call", o, "status=1 (ERRRUN) L=0x00100000 call f=0x00120000 msg='scripts/x.lua:3: attempt to index a nil value' chunk='' ra=0x005602F4\n", true);
    expect("call", o, " src=", false);
    o = run({kOtherOpd, 4, 1, "x"});
    expect("mem", o, "status=4 (ERRMEM) L=0x00100000 call f=0x00120000 msg='' chunk=''", true);
    o = run({kOtherOpd, 5, 1, "x"});
    expect("err", o, "status=5 (ERRERR) L=0x00100000 call f=0x00120000 msg='' chunk=''", true);
    o = run({Q::kFParserOpd, 3, 0, syn});
    expect("notstr", o, "msg='<top-1 not a string: top=0x00110008 tt=3>'", true);
    {
        fflush(stderr);
        FILE* t = tmpfile(); FILE* keep = fdopen(dup(fileno(stderr)), "w");
        dup2(fileno(t), fileno(stderr));
        static ppu_context c; memset(&c, 0, sizeof c); c.gpr[1] = F1 - 0x2C0;
        Q::on_landed(&c, 0x0050AF94u, 3);
        fflush(stderr); dup2(fileno(keep), fileno(stderr)); fclose(keep);
        const long sz = ftell(t); fclose(t);
        if (sz != 0) { g_fail++; fprintf(stdout, "FAIL site: %ld bytes printed for another site\n", sz); }
    }

    std::string big(260, 'x'); big[0] = 'a'; big[199] = 'B'; big[200] = 'C';
    set_source(Q::kExecReader, "execute", big.c_str(), 0, 0, 0, false);
    o = run({Q::kFParserOpd, 3, 1, syn});
    expect("long", o, ("src='" + big.substr(0, 200) + "...' ra=0x005602F4\n").c_str(), true);
    expect("long", o, "BC", false);
    set_source(Q::kGetSReader, "=levels/prologue/init", "t[\"k\"]()REST", 0, 5, 3, false);
    o = run({Q::kFParserOpd, 3, 1, syn});
    expect("gets", o, "chunk='=levels/prologue/init' src='t[\"k\"]()' ra=", true);
    set_source(Q::kGetSReader, "=levels/prologue/init", "t[\"k\"]()", 0, 8, 0xFFFFFFFFu, false);
    o = run({Q::kFParserOpd, 3, 1, syn});
    expect("gets-eof", o, "src='t[\"k\"]()' ra=", true);   /* end of input: n = -1, the chunk ends at zp (no byte lost) */
    set_source(Q::kGetSReader, "=levels/prologue/init", "t[\"k\"]()REST", 8, 0, 0, false);
    o = run({Q::kFParserOpd, 3, 1, syn});
    expect("gets0", o, "src='t[\"k\"]()' ra=", true);
    set_source(kOtherOpd, "?", "x", 0, 0, 0, false);
    o = run({Q::kFParserOpd, 3, 1, syn});
    expect("reader", o, "chunk='?' src='<reader 0x00120000 data 0x00170000>'", true);
    set_source(0, nullptr, nullptr, 0, 0, 0, false);

    setenv("RC_LUA_ERRMSG", "2", 1); Q::s_n = 0; Q::install();
    o = run({Q::kFParserOpd, 3, 1, syn}, 5);
    if (count(o, "[lua-err] #") != 2) { g_fail++; fprintf(stdout, "FAIL cap: %d lines, want 2\n%s", count(o, "[lua-err] #"), o.c_str()); }
    if (count(o, "[ljmp] caught") != 5) { g_fail++; fprintf(stdout, "FAIL cap: %d [ljmp] lines, want 5\n", count(o, "[ljmp] caught")); }
    fprintf(stdout, g_fail ? "on: FAIL (%d)\n" : "on: PASS (load, regs, call, mem, err, notstr, site, long, gets, gets-eof, gets0, reader, cap)\n", g_fail);
    return g_fail ? 1 : 0;
}
