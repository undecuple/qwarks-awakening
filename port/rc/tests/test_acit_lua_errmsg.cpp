/* Host test (no GPU, no game run, no game data): ACiT 1.20's RC_LUA_ERRMSG diagnostic (port/rc/acit120/acit120_lua_errmsg.inc)
 * through the landing hook of toolkit patch 7113, against the toolkit's runtime half runtime/ppu/ppu_setjmp_rt.inc (patch
 * 1116 + 7113). Built and run by run_acit_lua_errmsg_test.sh. The twin of test_ffa_lua_errmsg.cpp.
 *
 * A synthetic guest memory holds what ACiT's luaD_rawrunprotected (func_0050AF5C) has at a landing, laid out as read off the
 * lifted code (see the .inc): its 0x2D0 frame at r1 with L at +0x2A0, f (an OPD pointer) at +0x2B0, ud at +0x2B4, lj.status
 * at +0x294 (jmp_buf r1+0x74 + 0x220); lua_State L with top at +0x08; TValues of 8 bytes {value, tt}; a TString with len
 * +0x0C and the text at +0x10; an SParser with the ZIO at +0 and the chunk name at +0x10. A stand-in for the lifted
 * rawrunprotected writes the frame words, keeps r1 = its frame, takes the setjmp through PPU_SJ_CALL at site 0x0050AF94
 * with status offset 0x220, and its protected body clobbers r1/r3..r31, writes lj.status, pushes the message and longjmps
 * (guest registers restored as the lifted longjmp does, then ppu_sj_longjmp), so the hook runs on a real host landing and
 * must read L/f/ud from the frame, not from registers.
 *
 * Cases: load   lua_load (f = f_parser's OPD 0x00D0CB38) failing with status 3: one `[lua-err]` line, message, chunk name.
 *        call   another f failing with status 2: the message, `call f=0x...`, no chunk, no src field.
 *        mem    status 4: the line, no message (Lua sets it after the landing).
 *        notstr status 3 with a number at top-1: the `<top-1 not a string` note.
 *        site   the same landing at another setjmp site (FFA's 0x0080DE98): no line.
 *        regs   r31/r24/r25 hold FFA-style L/f/ud values that differ from the frame's: the line uses the frame's.
 *        cap    RC_LUA_ERRMSG=2 and 5 landings: 2 lines.
 *        exec   lua_load through the `execute` loaders' reader (OPD 0x00D0C498, data {s, done}) with a guest back chain of
 *               four frames: `src='<the statement>'` and `ra=` with the four saved LR words.
 *        long   the same with a 260-byte statement: the first 200 bytes and `...`.
 *        gets   lauxlib getS (0x00D0C7E8, data {s, size}): only `size` bytes.
 *        reader another reader: `src='<reader 0x... data 0x...>'`.
 *        off    (negative control) RC_LUA_ERRMSG unset: the hook is not installed, the `[ljmp] caught` lines still print,
 *               no `[lua-err]` line.
 * Exit 0 = PASS. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <unistd.h>
#include "ppu_context.h"

static uint8_t g_mem[0x200000];                 /* guest 0x00100000..0x002FFFFF */
static const uint32_t kBase = 0x00100000u;
static uint8_t* at(uint64_t a)
{
    const uint32_t o = (uint32_t)a - kBase;
    if ((uint32_t)a < kBase || o + 8 > sizeof g_mem) { fprintf(stdout, "FAIL: guest access 0x%08X outside the test memory\n", (uint32_t)a); exit(1); }
    return g_mem + o;
}
extern "C" {
uint8_t vm_read8(uint64_t a) { return *at(a); }
uint32_t vm_read32(uint64_t a) { uint32_t v; memcpy(&v, at(a), 4); return __builtin_bswap32(v); }
void vm_write32(uint64_t a, uint32_t v) { v = __builtin_bswap32(v); memcpy(at(a), &v, 4); }
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*) = 0;
static int g_depth = 0, g_vsp = 0;
int* ps3_ppu_dispatch_depth_slot(void) { return &g_depth; }
int* ps3_ppu_vcall_sp_slot(void) { return &g_vsp; }
}
#include PPU_SJ_RT                              /* the toolkit's runtime half (patch 1116 + 7113) */
#include ACIT_LUA_ERRMSG_INC                    /* port/rc/acit120/acit120_lua_errmsg.inc */

/* Guest addresses */
static const uint32_t L = 0x00110000u, STACK = 0x00120000u, TS = 0x00130000u, SP = 0x00140000u, NAME = 0x00150000u;
static const uint32_t R1 = 0x001F0000u, JB_RAW = R1 + 0x74u;         /* rawrunprotected's frame (stdu -0x2D0) */
static const uint32_t kOtherOpd = 0x00D0F000u;
static const uint32_t ZIO = 0x00160000u, DATA = 0x00170000u, SRC = 0x00180000u;
static const uint32_t L_REG = 0x00111000u;                            /* a wrong L left in r31 (FFA's register) */

static void put64(uint32_t a, uint32_t lo) { vm_write32(a, 0); vm_write32(a + 4, lo); }
/* The ZIO/data the parser was reading from (reader 0 = none) and a back chain R1 -> F1 -> F2 -> F3 -> F4 -> 0 (or none);
 * F1 = R1 + 0x2D0 (the caller's frame right above rawrunprotected's), so its LR slot is r1+0x2E0 as in the lifted code. */
static void set_source(uint32_t reader, const char* text, uint32_t size, bool chain)
{
    vm_write32(SP + 0x00, reader ? ZIO : 0);
    vm_write32(ZIO + 0x08, reader); vm_write32(ZIO + 0x0C, DATA);
    vm_write32(DATA + 0x00, SRC); vm_write32(DATA + 0x04, size);
    memset(at(SRC), 0, 0x400);
    if (text) memcpy(at(SRC), text, strlen(text) + 1);
    static const uint32_t F[4] = { R1 + 0x2D0, R1 + 0x3A0, R1 + 0x450, R1 + 0x500 };
    static const uint32_t LR[4] = { 0x0050B900u, 0x0050BA2Cu, 0x00501A0Cu, 0x00500D94u };
    put64(R1, chain ? F[0] : 0);
    for (int i = 0; i < 4; i++) { put64(F[i], i < 3 ? F[i + 1] : 0); put64(F[i] + 0x10, LR[i]); }
}

struct Case { uint32_t site; uint32_t func; int32_t status; int str_on_top; const char* text; };
static Case g_case;
static uint64_t g_saved[32];

static void fake_setjmp(ppu_context* ctx) { memcpy(g_saved, ctx->gpr, sizeof g_saved); ctx->gpr[3] = 0; }

static void __attribute__((noinline)) protected_body(ppu_context* ctx)
{
    for (int r = 1; r < 32; r++) if (r != 2) ctx->gpr[r] = 0xDEAD0000u + (uint32_t)r;          /* deeper code clobbers them */
    vm_write32(JB_RAW + 0x220u, (uint32_t)g_case.status);                                  /* lj.status (luaD_throw) */
    const uint32_t len = (uint32_t)strlen(g_case.text);
    vm_write32(TS + 0x0C, len);
    memcpy(at(TS + 0x10), g_case.text, len);
    vm_write32(STACK + 0, g_case.str_on_top ? TS : 0x3F800000u);                          /* L->top - 1: value */
    vm_write32(STACK + 4, g_case.str_on_top ? 4u : 3u);                                    /*               tt  */
    vm_write32(L + 0x08, STACK + 8);                                                       /* L->top */
    vm_write32(L_REG + 0x08, 0);                                                           /* the wrong L has no top */
    memcpy(ctx->gpr, g_saved, sizeof g_saved);                                             /* the lifted longjmp body */
    ctx->gpr[3] = 1;
    ppu_sj_longjmp(ctx, JB_RAW);
    fprintf(stdout, "FAIL: ppu_sj_longjmp returned (no landing)\n");
    exit(1);
}

static int __attribute__((noinline)) rawrun_like(ppu_context* ctx)   /* stands in for the lifted func_0050AF5C */
{
    ctx->gpr[1] = R1;
    vm_write32(R1 + 0x2A0, L); put64(R1 + 0x2A8, L);                  /* stw r3,0x2A0 / std r3,0x2A8 */
    vm_write32(R1 + 0x2B0, g_case.func);                              /* stw r4,0x2B0 (f) */
    vm_write32(R1 + 0x2B4, SP);                                       /* stw r5,0x2B4 (ud) */
    vm_write32(R1 + 0x294, 0);                                        /* lj.status = 0 */
    ctx->gpr[31] = L_REG; ctx->gpr[24] = acit120_lua_errmsg::kFParserOpd; ctx->gpr[25] = NAME;   /* decoys (FFA's regs) */
    ctx->gpr[3] = JB_RAW;
    PPU_SJ_CALL(ctx, fake_setjmp, g_case.site, 0x220);
    if ((int32_t)ctx->gpr[3] == 0) protected_body(ctx);
    return (int32_t)vm_read32(R1 + 0x294u);
}

static std::string run(const Case& c, int times = 1)
{
    g_case = c;
    fflush(stderr);
    FILE* t = tmpfile();
    if (!t) { perror("tmpfile"); exit(2); }
    FILE* keep = fdopen(dup(fileno(stderr)), "w");
    fflush(stderr); dup2(fileno(t), fileno(stderr));
    for (int i = 0; i < times; i++) {
        ppu_context ctx; memset(&ctx, 0, sizeof ctx);
        if (rawrun_like(&ctx) != c.status) { fprintf(stdout, "FAIL: rawrun_like did not return the status\n"); exit(1); }
        if ((uint32_t)ctx.gpr[1] != R1) { fprintf(stdout, "FAIL: r1 not restored at the landing\n"); exit(1); }
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
    namespace A = acit120_lua_errmsg;
    const uint32_t site = 0x0050AF94u;
    const char* nm = "=levels/great_clock_a/init";
    memcpy(at(NAME), nm, strlen(nm) + 1);
    vm_write32(SP + 0x10, NAME);
    const char* syn = "[string \"levels/great_clock_a/init\"]:12: unexpected symbol near `<eof>`";
    if (off) {
        unsetenv("RC_LUA_ERRMSG");
        if (A::install()) { fprintf(stdout, "FAIL off: installed without RC_LUA_ERRMSG\n"); return 1; }
        const std::string o = run({site, A::kFParserOpd, 3, 1, syn});
        expect("off", o, "[ljmp] caught status=3", true);
        expect("off", o, "[lua-err]", false);
        fprintf(stdout, g_fail ? "off: FAIL\n" : "off: PASS (hook not installed: only the [ljmp] line)\n");
        return g_fail ? 1 : 0;
    }
    setenv("RC_LUA_ERRMSG", "50", 1);
    if (!A::install()) { fprintf(stdout, "FAIL: RC_LUA_ERRMSG=50 did not install\n"); return 1; }
    std::string o = run({site, A::kFParserOpd, 3, 1, syn});
    expect("load", o, "[lua-err] #1 status=3 (ERRSYNTAX) L=0x00110000 lua_load msg='[string \"levels/great_clock_a/init\"]:12: unexpected symbol near `<eof>`' chunk='=levels/great_clock_a/init' src='' ra=-", true);
    expect("regs", o, "L=0x00111000", false);
    o = run({site, kOtherOpd, 2, 1, "scripts/x.lua:3: attempt to index a nil value"});
    expect("call", o, "status=2 (ERRRUN) L=0x00110000 call f=0x00D0F000 msg='scripts/x.lua:3: attempt to index a nil value' chunk='' ra=-", true);
    expect("call", o, " src=", false);
    o = run({site, kOtherOpd, 4, 1, "x"});
    expect("mem", o, "status=4 (ERRMEM) L=0x00110000 call f=0x00D0F000 msg='' chunk=''", true);
    o = run({site, A::kFParserOpd, 3, 0, syn});
    expect("notstr", o, "msg='<top-1 not a string: top=0x00120008 tt=3>'", true);
    o = run({0x0080DE98u, A::kFParserOpd, 3, 1, syn});
    expect("site", o, "[ljmp] caught", true);
    expect("site", o, "[lua-err]", false);

    const char* exe = "[string \"execute\"]:1: unexpected symbol near `]'";
    set_source(A::kExecReader, "levelvars[] = 1", 0, true);
    o = run({site, A::kFParserOpd, 3, 1, exe});
    expect("exec", o, "chunk='=levels/great_clock_a/init' src='levelvars[] = 1' ra=0x0050B900,0x0050BA2C,0x00501A0C,0x00500D94\n", true);
    std::string big(260, 'x'); big[0] = 'a'; big[199] = 'B'; big[200] = 'C';
    set_source(A::kExecReader, big.c_str(), 0, false);
    o = run({site, A::kFParserOpd, 3, 1, exe});
    expect("long", o, ("src='" + big.substr(0, 200) + "...' ra=-").c_str(), true);
    expect("long", o, "BC", false);
    set_source(A::kGetSReader, "t[\"k\"]()REST", 8, false);
    o = run({site, A::kFParserOpd, 3, 1, exe});
    expect("gets", o, "src='t[\"k\"]()' ra=-", true);
    set_source(kOtherOpd, "x", 0, false);
    o = run({site, A::kFParserOpd, 3, 1, exe});
    expect("reader", o, "src='<reader 0x00D0F000 data 0x00170000>'", true);
    set_source(0, nullptr, 0, false);

    setenv("RC_LUA_ERRMSG", "2", 1); A::s_n = 0; A::install();
    o = run({site, A::kFParserOpd, 3, 1, syn}, 5);
    if (count(o, "[lua-err] #") != 2) { g_fail++; fprintf(stdout, "FAIL cap: %d lines, want 2\n%s", count(o, "[lua-err] #"), o.c_str()); }
    fprintf(stdout, g_fail ? "on: FAIL (%d)\n" : "on: PASS (load, regs, call, mem, notstr, site, exec, long, gets, reader, cap)\n", g_fail);
    return g_fail ? 1 : 0;
}
