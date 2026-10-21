/* Host test (no GPU, no game run): ToD's Lua error path with the REAL lifted functions and port/rc/tods/tods_lua_ljmp.inc.
 *
 * Lifted code used (cut out of work/recomp/tods by run_lua_ljmp_test.sh at run time, never committed):
 *   luaD_rawrunprotected 0x00671CA8 (as func_00671CA8__lifted), luaD_throw 0x00671D38 (+ its tail 0x00671D9C), setjmp stub 0x0059E630 ->
 *   setjmp 0x00826A10, longjmp stub 0x0059DE10 -> longjmp 0x00826B34 (as func_00826B34__lifted).
 * Guest memory is a private mmap; the protected functions ("f" of rawrunprotected) are host bodies reached through ps3_indirect_call.
 *
 * Cases (mode "on", default):
 *   1 throw:   f pushes a string TObject and calls luaD_throw(L, 3) -> rawrunprotected returns 3; r1, lr, r14..r31 as at the call;
 *              L->errorJmp back to its old value; the code after the throw never runs; exit() is not called; the [lua] line shows the message.
 *   2 nested:  outer f runs an inner rawrunprotected whose f throws 3 (inner returns 3, outer f continues), then throws 1 -> outer returns 1.
 *   3 no throw: returns 0, nothing logged.
 *   4 deep:    the throw happens below extra host frames that bumped the dispatch-depth / vcall-sp counters; both are back after the landing.
 * Mode "off" (TODS_LUA_LJMP=0, negative control): case 1 must reproduce the port's bug: luaD_throw falls through, calls the NULL panic
 *   pointer and exit(1) (the tods-ng-7002c log: null-read 0x11/0x28, null-call near lr 0x671D94, exit-chain).
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

extern "C" {
uint8_t* vm_base;
int vm_inline_ok = 1;
uint32_t ppu_vm_size = 0;
uint32_t ppu_hle_inject_base = 0xF0000000u;
unsigned char g_ppu_vm_page[65536];
PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*) = 0;
uint8_t vm_read8(uint64_t a) { return vm_base[(uint32_t)a]; }
uint16_t vm_read16(uint64_t a) { uint16_t v; memcpy(&v, vm_base + (uint32_t)a, 2); return __builtin_bswap16(v); }
uint32_t vm_read32(uint64_t a) { uint32_t v; memcpy(&v, vm_base + (uint32_t)a, 4); return __builtin_bswap32(v); }
uint64_t vm_read64(uint64_t a) { uint64_t v; memcpy(&v, vm_base + (uint32_t)a, 8); return __builtin_bswap64(v); }
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

void func_00671CA8__lifted(ppu_context* ctx);
void func_00826B34__lifted(ppu_context* ctx);
#define PPU_OVERRIDE_00671CA8 1
#define PPU_OVERRIDE_00826B34 1
#include "../tods/tods_lua_ljmp.inc"

static int g_exit_called = 0, g_null_called = 0, g_after_throw = 0, g_tail_called = 0;
void func_00814608(ppu_context* c) { g_exit_called = 1; (void)c; }     /* libc exit(): recorded, not run */
void func_00671DA8(ppu_context* c) { g_tail_called = 1; (void)c; }     /* luaD_throw's fall-through neighbour (0x671D9C -> 0x671DA8) */

enum : uint32_t { L = 0x00100000, G = 0x00100200, STK = 0x00180000, TS = 0x00110000, OPD = 0x00120000, STACK_TOP = 0x00F00000, TOC = 0x00900000 };
enum : uint32_t { F_THROW = 0x1000, F_NESTED = 0x1100, F_NONE = 0x1200, F_DEEP = 0x1300 };
static uint32_t opd(uint32_t code) { static uint32_t n = 0; const uint32_t a = OPD + 8 * n++; vm_write32(a, code); vm_write32(a + 4, TOC); return a; }
static uint32_t g_opd_throw, g_opd_nested, g_opd_none, g_opd_deep;

static void push_string(const char* s)   /* TObject {tt = 4 (LUA_TSTRING), TString*} at L->top, TString {.., len +0xC, chars +0x10} */
{
    const uint32_t top = vm_read32(L + 8), len = (uint32_t)strlen(s);
    vm_write32(TS + 0x0C, len); memcpy(vm_base + TS + 0x10, s, len);
    vm_write32(top, 4); vm_write32(top + 4, TS); vm_write32(L + 8, top + 8);
}
static void clobber(ppu_context* ctx) { for (int r = 14; r < 32; r++) ctx->gpr[r] = 0xDEAD0000u + r; ctx->gpr[1] -= 0x100; ctx->lr = 0x12345678; }
static void throw_(ppu_context* ctx, int status) { ctx->gpr[3] = L; ctx->gpr[4] = (uint64_t)status; ctx->lr = 0x00F0F0F0; func_00671D38(ctx); DRAIN_TRAMPOLINE(ctx); }

static uint64_t rrp(ppu_context* ctx, uint32_t f)   /* call luaD_rawrunprotected(L, f, 0) the way a lifted caller does */
{
    ctx->gpr[3] = L; ctx->gpr[4] = f; ctx->gpr[5] = 0; ctx->lr = 0x00ABCDE0;
    func_00671CA8(ctx); DRAIN_TRAMPOLINE(ctx);
    return ctx->gpr[3];
}

static int g_inner_status = -1;
void ps3_indirect_call(ppu_context* ctx)
{
    switch ((uint32_t)ctx->ctr) {
    case 0: g_null_called = 1; return;                                          /* the NULL panic pointer */
    case F_THROW: push_string("[string \"x\"]:1: unexpected symbol"); clobber(ctx); throw_(ctx, 3); g_after_throw = 1; return;
    case F_NONE: ctx->gpr[3] = 0; return;
    case F_NESTED: {
        const uint64_t r1 = ctx->gpr[1];
        ctx->gpr[1] -= 0x400;                                                    /* a guest frame of the outer f */
        g_inner_status = (int)rrp(ctx, g_opd_throw);
        if (ctx->gpr[1] != r1 - 0x400) { printf("FAIL nested: inner r1 0x%llx\n", (unsigned long long)ctx->gpr[1]); exit(1); }
        push_string("outer error"); clobber(ctx); throw_(ctx, 1); g_after_throw = 1; return; }
    case F_DEEP: g_depth += 7; g_vsp += 3; ctx->ctr = g_opd_throw ? F_THROW : 0; ps3_indirect_call(ctx); return;   /* host frames in between */
    default: printf("FAIL: unexpected indirect call 0x%llx\n", (unsigned long long)ctx->ctr); exit(1);
    }
}

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void reset(ppu_context* ctx)
{
    memset(ctx, 0, sizeof *ctx);
    memset(vm_base + L, 0, 0x100); memset(vm_base + G, 0, 0x100);
    vm_write32(L + 0x08, STK); vm_write32(L + 0x10, G); vm_write32(L + 0x50, 0);   /* top, l_G, errorJmp = NULL; G->panic (+0x28) = NULL */
    ctx->gpr[1] = STACK_TOP; ctx->gpr[2] = TOC;
    for (int r = 14; r < 32; r++) ctx->gpr[r] = 0x5A000000u + r;
    g_exit_called = g_null_called = g_after_throw = g_tail_called = 0; g_depth = 2; g_vsp = 1;
}
static void check_preserved(ppu_context* ctx, const char* what)
{
    CHECK(ctx->gpr[1] == STACK_TOP, "%s: r1 0x%llx", what, (unsigned long long)ctx->gpr[1]);
    CHECK(ctx->lr == 0x00ABCDE0, "%s: lr 0x%llx", what, (unsigned long long)ctx->lr);
    for (int r = 14; r < 32; r++) CHECK(ctx->gpr[r] == 0x5A000000u + r, "%s: r%d 0x%llx", what, r, (unsigned long long)ctx->gpr[r]);
    CHECK(vm_read32(L + 0x50) == 0, "%s: errorJmp 0x%x", what, vm_read32(L + 0x50));
    CHECK(!g_exit_called && !g_null_called && !g_after_throw && !g_tail_called, "%s: exit %d null %d after %d tail %d", what,
          g_exit_called, g_null_called, g_after_throw, g_tail_called);
    CHECK(tods_lua_ljmp::s_n == 0, "%s: %d host records left", what, tods_lua_ljmp::s_n);
}

int main(int argc, char** argv)
{
    const bool off = argc > 1 && !strcmp(argv[1], "off");
    if (off) setenv("TODS_LUA_LJMP", "0", 1);
    vm_base = (uint8_t*)mmap(nullptr, 0x01000000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (vm_base == MAP_FAILED) { perror("mmap"); return 2; }
    g_opd_throw = opd(F_THROW); g_opd_nested = opd(F_NESTED); g_opd_none = opd(F_NONE); g_opd_deep = opd(F_DEEP);
    static ppu_context c; ppu_context* ctx = &c;

    if (off) {   /* negative control: the bug */
        reset(ctx);
        rrp(ctx, g_opd_throw);
        const bool bug = g_null_called && g_exit_called && g_after_throw;
        printf("off: null-call=%d exit=%d code-after-throw-ran=%d -> %s\n", g_null_called, g_exit_called, g_after_throw,
               bug ? "bug reproduced (expected)" : "NOT reproduced");
        return bug ? 0 : 1;
    }
    reset(ctx);
    uint64_t st = rrp(ctx, g_opd_throw);
    CHECK(st == 3, "throw: status %llu", (unsigned long long)st); check_preserved(ctx, "throw");

    reset(ctx);
    st = rrp(ctx, g_opd_nested);
    CHECK(g_inner_status == 3, "nested: inner status %d", g_inner_status);
    CHECK(st == 1, "nested: outer status %llu", (unsigned long long)st); check_preserved(ctx, "nested");

    reset(ctx);
    st = rrp(ctx, g_opd_none);
    CHECK(st == 0, "none: status %llu", (unsigned long long)st); check_preserved(ctx, "none");

    reset(ctx);
    st = rrp(ctx, g_opd_deep);
    CHECK(st == 3, "deep: status %llu", (unsigned long long)st); check_preserved(ctx, "deep");
    CHECK(g_depth == 2 && g_vsp == 1, "deep: depth %d vsp %d (want 2, 1)", g_depth, g_vsp);

    reset(ctx);   /* many errors in a row: no record or host stack leak */
    for (int i = 0; i < 10000; i++) { st = rrp(ctx, (i & 1) ? g_opd_throw : g_opd_nested); if (st != ((i & 1) ? 3u : 1u)) { CHECK(0, "loop %d status %llu", i, (unsigned long long)st); break; } }
    check_preserved(ctx, "loop");

    printf(fails ? "FAIL (%d)\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
