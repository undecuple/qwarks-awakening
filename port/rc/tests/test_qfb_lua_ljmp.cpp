/* Host test (no GPU, no game run): Quest for Booty's Lua 5.0.2 error path with the REAL lifted functions, lifted with the
 * `setjmp 00725328 status=0x220` / `longjmp 006FF864` rows of port/rc/qfb/ppu_overrides.txt (toolkit patch 1116), and the
 * toolkit's runtime half runtime/ppu/ppu_setjmp_rt.inc. Built and run by run_qfb_lua_ljmp_test.sh. Same cases as
 * test_acit_lua_ljmp.cpp; what is new in QfB is the call shape: the setjmp and longjmp calls go through TOC stubs that reach
 * libc by an INDIRECT call (lis r11 / addi r11 / mtctr / bctr), so the setjmp row marks the stub and the longjmp is entered
 * through ps3_indirect_call.
 *
 * Lifted code used (cut out at run time, never committed): luaD_rawrunprotected 0x005601A8 (its `bl` to the setjmp stub
 * @0x005601E0 is a PPU_SJ_CALL), luaD_throw 0x00560238 (and its tail fragment 0x00560294 when the lifter splits it there; exit(1)
 * @0x00560298 through stub 0x00724FFC), the stubs 0x00725328 (setjmp), 0x00725344 (longjmp), 0x00724FFC (exit), libc setjmp
 * 0x006FF740, libc longjmp 0x006FF864 (__ljbody + host-jump wrapper). Host stand-ins: libc exit 0x006F28A8 (recorded, not run),
 * luaD_throw's fall-through neighbour 0x005602A0, the stubs' dead fall-through neighbours 0x00725018 / 0x00725360, and the
 * protected functions ("f" of rawrunprotected), all reached through ps3_indirect_call.
 * Lua 5.0 layout (read off the lifted code, the RE notes sections 3 and 21): L->l_G +0x10, L->errorJmp
 * +0x50; G->panic +0x28; struct lua_longjmp {previous +0; jmp_buf b +4; status +0x224}. Status codes 5.0: 1 ERRRUN, 2 ERRFILE,
 * 3 ERRSYNTAX, 4 ERRMEM, 5 ERRERR.
 *
 * Cases (mode "on", default):
 *   throw    f calls luaD_throw(L, 3): rawrunprotected returns 3; r1, lr, r2, r14..r31 as at the call; L->errorJmp back to its
 *            old value; the code after the throw never runs; exit() is not called; no low-address (NULL) read.
 *   nested   outer f runs an inner rawrunprotected whose f throws 3 (inner returns 3, outer f goes on), then throws 5 -> outer 5.
 *   none     returns 0.
 *   deep     the throw happens below extra host frames that bumped the dispatch-depth / vcall-sp counters: both restored.
 *   loop     10000 throws in a row: no record or host stack leak.
 * Mode "off" (KZ3_LJMP=0, negative control): ToD's tods-ng-us exit shape: luaD_throw falls through with r3 = 1 as L, reads 0x11
 *   (G(L)) and 0x28 (G->panic), then 0x0/0x4 (the NULL OPD), calls NULL, then exit(1).
 * Exit 0 = PASS. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
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

static uint32_t g_low_reads[16]; static int g_n_low = 0;   /* guest reads below 0x1000 (the runtime's [null-read] lines) */
static void note_low(uint64_t a) { if ((uint32_t)a < 0x1000 && g_n_low < 16) g_low_reads[g_n_low++] = (uint32_t)a; }

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
uint8_t vm_read8(uint64_t a) { note_low(a); return vm_base[(uint32_t)a]; }
uint16_t vm_read16(uint64_t a) { note_low(a); uint16_t v; memcpy(&v, vm_base + (uint32_t)a, 2); return __builtin_bswap16(v); }
uint32_t vm_read32(uint64_t a) { note_low(a); uint32_t v; memcpy(&v, vm_base + (uint32_t)a, 4); return __builtin_bswap32(v); }
uint64_t vm_read64(uint64_t a) { note_low(a); uint64_t v; memcpy(&v, vm_base + (uint32_t)a, 8); return __builtin_bswap64(v); }
void vm_write8(uint64_t a, uint8_t v) { vm_base[(uint32_t)a] = v; }
void vm_write16(uint64_t a, uint16_t v) { v = __builtin_bswap16(v); memcpy(vm_base + (uint32_t)a, &v, 2); }
void vm_write32(uint64_t a, uint32_t v) { v = __builtin_bswap32(v); memcpy(vm_base + (uint32_t)a, &v, 4); }
void vm_write64(uint64_t a, uint64_t v) { v = __builtin_bswap64(v); memcpy(vm_base + (uint32_t)a, &v, 8); }
int spu_coh_is_reserved(uint32_t) { return 0; }
void ppu_st_coh(uint32_t ea, const void* src, uint32_t n) { memcpy(vm_base + ea, src, n); }
void ppu_store_note(uint32_t, uint32_t) {}
void ps3_hle_call(unsigned, ppu_context*) { fprintf(stderr, "unexpected hle call\n"); exit(2); }
void ps3_indirect_call_to(ppu_context*, uint32_t) { fprintf(stderr, "unexpected indirect call\n"); exit(2); }
static int g_depth = 0, g_vsp = 0;
int* ps3_ppu_dispatch_depth_slot(void) { return &g_depth; }
int* ps3_ppu_vcall_sp_slot(void) { return &g_vsp; }
void ps3_indirect_call(ppu_context* ctx);
}
#include PPU_SJ_RT   /* the toolkit's runtime half (patch 1116), as ppu_loader.cpp includes it */

static int g_exit_called = 0, g_exit_code = -1, g_after_throw = 0, g_tail_called = 0, g_panic_path = 0;
void func_005602A0(ppu_context*) { g_tail_called = 1; }                                  /* luaD_throw's fall-through neighbour */
void func_00725018(ppu_context*) { g_tail_called = 1; }                                  /* dead fall-through of the exit stub */
void func_00725360(ppu_context*) { g_tail_called = 1; }                                  /* dead fall-through of the longjmp stub */

enum : uint32_t { L = 0x00100000, G = 0x00100200, OPD = 0x00120000, STACK_TOP = 0x00F00000, TOC = 0x0084DA0C };
enum : uint32_t { F_THROW = 0x1000, F_NESTED = 0x1100, F_NONE = 0x1200, F_DEEP = 0x1300 };
static uint32_t opd(uint32_t code) { static uint32_t n = 0; const uint32_t a = OPD + 8 * n++; vm_write32(a, code); vm_write32(a + 4, TOC); return a; }
static uint32_t g_opd_throw, g_opd_nested, g_opd_none, g_opd_deep;

static void clobber(ppu_context* ctx) { for (int r = 14; r < 32; r++) ctx->gpr[r] = 0xDEAD0000u + r; ctx->gpr[1] -= 0x100; ctx->lr = 0x12345678; }
static void throw_(ppu_context* ctx, int status) { ctx->gpr[3] = L; ctx->gpr[4] = (uint64_t)status; ctx->lr = 0x00F0F0F0; func_00560238(ctx); DRAIN_TRAMPOLINE(ctx); }

static uint64_t rrp(ppu_context* ctx, uint32_t f)   /* luaD_rawrunprotected(L, f, ud) the way a lifted caller calls it */
{
    ctx->gpr[3] = L; ctx->gpr[4] = f; ctx->gpr[5] = 0; ctx->lr = 0x00ABCDE0;
    func_005601A8(ctx); DRAIN_TRAMPOLINE(ctx);
    return ctx->gpr[3];
}

static int g_inner_status = -1;
void ps3_indirect_call(ppu_context* ctx)
{
    switch ((uint32_t)ctx->ctr) {
    case F_THROW: clobber(ctx); throw_(ctx, 3); g_after_throw = 1; return;
    case F_NONE: ctx->gpr[3] = 0; return;
    case F_NESTED: {
        const uint64_t r1 = ctx->gpr[1];
        ctx->gpr[1] -= 0x400;                                                    /* a guest frame of the outer f */
        /* inner protected call whose f throws 2 */
        ctx->gpr[3] = L; ctx->gpr[4] = g_opd_throw; ctx->gpr[5] = 0; ctx->lr = 0x00ABCDF0;
        func_005601A8(ctx); DRAIN_TRAMPOLINE(ctx);
        g_inner_status = (int)ctx->gpr[3];
        if (ctx->gpr[1] != r1 - 0x400) { printf("FAIL nested: inner r1 0x%llx\n", (unsigned long long)ctx->gpr[1]); exit(1); }
        clobber(ctx); throw_(ctx, 5); g_after_throw = 1; return; }
    case F_DEEP: g_depth += 7; g_vsp += 3; ctx->ctr = F_THROW; ps3_indirect_call(ctx); return;   /* host frames in between */
    case 0x006FF740: func_006FF740(ctx); return;                                 /* libc setjmp, from stub 0x00725328 */
    case 0x006FF864: func_006FF864(ctx); return;                                 /* libc longjmp (wrapper), from stub 0x00725344 */
    case 0x006F28A8: g_exit_called = 1; g_exit_code = (int)ctx->gpr[3]; return;  /* libc exit(): recorded, not run */
    case 0: g_panic_path = 1; return;                                            /* the NULL panic call (off mode) */
    default: printf("FAIL: unexpected indirect call 0x%llx\n", (unsigned long long)ctx->ctr); exit(1);
    }
}

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void reset(ppu_context* ctx)
{
    memset(ctx, 0, sizeof *ctx);
    memset(vm_base, 0, 0x1000);                                    /* the NULL page reads as zero, as the runtime's guard does */
    memset(vm_base + L, 0, 0x100); memset(vm_base + G, 0, 0x100);
    vm_write32(L + 0x10, G); vm_write32(L + 0x50, 0);              /* l_G, errorJmp = NULL; G->panic (+0x28) = NULL */
    ctx->gpr[1] = STACK_TOP; ctx->gpr[2] = TOC;
    for (int r = 14; r < 32; r++) ctx->gpr[r] = 0x00200000u + 0x100u * r;   /* guest addresses: the off mode's luaD_throw stores through r31 */
    g_exit_called = g_after_throw = g_tail_called = g_panic_path = 0; g_exit_code = -1; g_depth = 2; g_vsp = 1; g_n_low = 0;
}
static void check_preserved(ppu_context* ctx, const char* what)
{
    CHECK(ctx->gpr[1] == STACK_TOP, "%s: r1 0x%llx", what, (unsigned long long)ctx->gpr[1]);
    CHECK(ctx->lr == 0x00ABCDE0, "%s: lr 0x%llx", what, (unsigned long long)ctx->lr);
    for (int r = 14; r < 32; r++) CHECK(ctx->gpr[r] == 0x00200000u + 0x100u * r, "%s: r%d 0x%llx", what, r, (unsigned long long)ctx->gpr[r]);
    CHECK(ctx->gpr[2] == TOC, "%s: r2 0x%llx", what, (unsigned long long)ctx->gpr[2]);
    CHECK(vm_read32(L + 0x50) == 0, "%s: errorJmp 0x%x", what, vm_read32(L + 0x50));
    CHECK(!g_exit_called && !g_after_throw && !g_tail_called && !g_panic_path, "%s: exit %d after %d tail %d panic %d", what,
          g_exit_called, g_after_throw, g_tail_called, g_panic_path);
    CHECK(g_n_low == 0, "%s: %d NULL-page read(s), first 0x%x", what, g_n_low, g_n_low ? g_low_reads[0] : 0);
}

int main(int argc, char** argv)
{
    const bool off = argc > 1 && !strcmp(argv[1], "off");
    if (off) setenv("KZ3_LJMP", "0", 1);
    vm_base = (uint8_t*)mmap(nullptr, 0x01000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (vm_base == MAP_FAILED) { perror("mmap"); return 2; }
    g_opd_throw = opd(F_THROW); g_opd_nested = opd(F_NESTED); g_opd_none = opd(F_NONE); g_opd_deep = opd(F_DEEP);
    static ppu_context c; ppu_context* ctx = &c;

    if (off) {   /* negative control: ToD's tods-ng-us exit shape */
        reset(ctx);
        rrp(ctx, g_opd_throw);
        const bool reads = g_n_low >= 2 && g_low_reads[0] == 0x11 && g_low_reads[1] == 0x28 && g_panic_path;
        const bool bug = reads && g_exit_called && g_exit_code == 1 && g_after_throw;
        printf("off: null reads %d (0x%x, 0x%x) exit(%d) code-after-throw-ran=%d -> %s\n", g_n_low, g_n_low > 0 ? g_low_reads[0] : 0,
               g_n_low > 1 ? g_low_reads[1] : 0, g_exit_code, g_after_throw, bug ? "bug reproduced (expected)" : "NOT reproduced");
        return bug ? 0 : 1;
    }
    reset(ctx);
    uint64_t st = rrp(ctx, g_opd_throw);
    CHECK(st == 3, "throw: status %llu", (unsigned long long)st); check_preserved(ctx, "throw");

    reset(ctx);
    st = rrp(ctx, g_opd_nested);
    CHECK(g_inner_status == 3, "nested: inner status %d", g_inner_status);
    CHECK(st == 5, "nested: outer status %llu", (unsigned long long)st); check_preserved(ctx, "nested");

    reset(ctx);
    st = rrp(ctx, g_opd_none);
    CHECK(st == 0, "none: status %llu", (unsigned long long)st); check_preserved(ctx, "none");

    reset(ctx);
    st = rrp(ctx, g_opd_deep);
    CHECK(st == 3, "deep: status %llu", (unsigned long long)st); check_preserved(ctx, "deep");
    CHECK(g_depth == 2 && g_vsp == 1, "deep: depth %d vsp %d (want 2, 1)", g_depth, g_vsp);

    reset(ctx);
    for (int i = 0; i < 10000; i++) { st = rrp(ctx, (i & 1) ? g_opd_throw : g_opd_nested); if (st != ((i & 1) ? 3u : 5u)) { CHECK(0, "loop %d status %llu", i, (unsigned long long)st); break; } }
    check_preserved(ctx, "loop");
    CHECK(ppu_sj_stat(1) == 0 && ppu_sj_stat(2) == 0, "unmatched %llu, full %llu", ppu_sj_stat(1), ppu_sj_stat(2));

    if (fails) printf("FAIL (%d)\n", fails); else printf("PASS (%llu landings)\n", ppu_sj_stat(0));
    return fails ? 1 : 0;
}
