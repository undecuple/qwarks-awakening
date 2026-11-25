/* R&C patch 7115 host test: the SPU-thread LS window (EA 0xF0000000 + n*0x100000 + LSA = local store of SPU thread n
 * of the issuing thread's group), against the REAL runtime: every DMA goes in through spu_wrch on the MFC channels
 * (spu_channels.c -> spu_dma.h mfc_do_transfer / mfc_run_list), and the SPURS case binds through the real
 * spu_run_policy_module (spurs_policy.c). Guest memory is a 4 GB VirtualAlloc reservation with demand commit, as in
 * the port, so a transfer that falls through to the plain path reads the zero page at vm_base + 0xF0000000 (the Nexus
 * 1.01 narrow-phase symptom: zero candidate items, hit = 0).
 *
 * Cases:
 *   1. lv2 group G1: B (thread 1) GETs 16 bytes from 0xF0000000 + A's index*0x100000 + 0x7380 and sees A's LS bytes.
 *   2. PUT the other way: A PUTs 16 bytes to 0xF0100000 + 0x3000; they land in B's LS 0x3000 (and not in guest memory).
 *   3. GETL: a list in B's LS with a main-memory element and two window elements (48 bytes from A, 16 from B itself).
 *   4. isolation: C, thread 0 of another group G2, GETs 0xF0000000 + 0x7380 and sees its own LS, not A's.
 *   5. SPURS: two policy-module runs of one instance on SPUs 0 and 1 (spu_run_policy_module binds the context of each
 *      run as SPU thread spu_num); the run on SPU 1 GETs from SPU 0's LS (the ig broadphase -> narrow hand-over shape).
 *   6. an unbound context and a problem-state EA keep the plain path (no crash, a log line).
 * The bind calls are weak references: on a tree without 7115 they are absent, the window falls through to plain memory
 * and cases 1-5 FAIL (the negative control in run_spu_thread_ls_window_test.sh). */
#include "spu_helpers.h"
#include "spu_workload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include "win32_compat.h"
#endif

uint8_t* vm_base;
uint32_t ppu_vm_size;

extern void spu_wrch(spu_context*, uint32_t, u128);
extern void spu_group_ls_bind(uint32_t, uint32_t, uint32_t, spu_context*) __attribute__((weak));
extern void spu_group_ls_unbind(const spu_context*) __attribute__((weak));

static unsigned passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static spu_context *A, *B, *C, *E;

static void wr(spu_context* c, uint32_t ch, uint32_t v) { u128 x; memset(&x, 0, sizeof x); x._u32[0] = v; spu_wrch(c, ch, x); }
static void dma(spu_context* c, uint32_t cmd, uint32_t lsa, uint32_t ea, uint32_t size)
{
    wr(c, MFC_LSA, lsa); wr(c, MFC_EAH, 0); wr(c, MFC_EAL, ea); wr(c, MFC_Size, size); wr(c, MFC_TagID, 1); wr(c, MFC_Cmd, cmd);
}
static void fill(uint8_t* p, uint32_t n, uint8_t seed) { for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(seed + i * 7u); }
static void be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static int bound(void) { return spu_group_ls_bind != NULL; }
static spu_context* new_ctx(void)
{
    spu_context* c = (spu_context*)calloc(1, sizeof *c);
    if (!c) { printf("FAIL: calloc\n"); exit(2); }
    spu_context_init(c, 0);
    c->image_id = -1;
    return c;
}

/* SPURS: one policy-module "run" per SPU; the entry is host code standing in for the lifted module. */
static uint8_t s_spurs_seen[16];
static void SPU_ABI pm_entry(spu_context* c)
{
    const uint32_t spu = (uint32_t)c->ls[0x1CB];                 /* SpursKernelContext spuNum (be u32 at 0x1C8) */
    if (spu == 0) fill(c->ls + 0x7380, 16, 0xA5);               /* broadphase: candidate items in its own LS */
    else {                                                       /* narrow phase: read them through the window */
        memset(c->ls + 0x150A0, 0, 16);
        dma(c, 0x40, 0x150A0, 0xF0000000u + 0x7380u, 16);
        memcpy(s_spurs_seen, c->ls + 0x150A0, 16);
    }
    c->pc = 0x980;                                               /* exitToKernel (SPURS_PM_EXIT_TO_KERNEL_LS) */
}

int main(void)
{
    vm_base = (uint8_t*)VirtualAlloc(NULL, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS);
    if (!vm_base) { printf("FAIL: cannot reserve 4 GB guest space\n"); return 2; }
    ppu_vm_size = 0xFFFFFFFFu;
    printf("test_spu_thread_ls_window: window bind %s\n", bound() ? "present (7115)" : "ABSENT (tree without 7115)");

    A = new_ctx(); B = new_ctx(); C = new_ctx(); E = new_ctx();
    if (bound()) {
        spu_group_ls_bind(1u, 0x1000u, 0, A);
        spu_group_ls_bind(1u, 0x1000u, 1, B);
        spu_group_ls_bind(1u, 0x1001u, 0, C);
    }

    /* 1. B GETs A's candidate items */
    fill(A->ls + 0x7380, 0x40, 0x11);
    memset(B->ls + 0x150A0, 0, 16);
    dma(B, 0x40, 0x150A0, 0xF0000000u + 0 * 0x100000u + 0x7380u, 16);
    CHECK(!memcmp(B->ls + 0x150A0, A->ls + 0x7380, 16), "1. B's GET from 0xF0007380 did not return A's LS 0x7380 bytes "
          "(got %02X%02X%02X%02X, want %02X%02X%02X%02X)", B->ls[0x150A0], B->ls[0x150A1], B->ls[0x150A2], B->ls[0x150A3],
          A->ls[0x7380], A->ls[0x7381], A->ls[0x7382], A->ls[0x7383]);

    /* 2. A PUTs into B's LS */
    fill(A->ls + 0x2000, 16, 0x40);
    memset(B->ls + 0x3000, 0, 16);
    dma(A, 0x20, 0x2000, 0xF0000000u + 1 * 0x100000u + 0x3000u, 16);
    CHECK(!memcmp(B->ls + 0x3000, A->ls + 0x2000, 16), "2. A's PUT to 0xF0103000 did not land in B's LS 0x3000");
    { static const uint8_t z[16]; MEMORY_BASIC_INFORMATION mbi;
      int committed = VirtualQuery(vm_base + 0xF0103000u, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT;
      CHECK(!committed || !memcmp(vm_base + 0xF0103000u, z, 16), "2. A's PUT also wrote guest memory at 0xF0103000"); }

    /* 3. GETL: element 0 main memory, element 1 A's LS (48 bytes, the bp record size), element 2 B's own LS */
    const uint32_t main_ea = 0x00F51700u;
    if (!VirtualAlloc(vm_base + (main_ea & ~0xFFFFu), 0x10000, MEM_COMMIT, PAGE_READWRITE)) { printf("FAIL: commit\n"); return 2; }
    fill(vm_base + main_ea, 16, 0x70);
    fill(B->ls + 0x9000, 16, 0x90);
    uint8_t* L = B->ls + 0x8000;                                  /* list elements: {size, eal} big-endian */
    be32(L + 0, 16); be32(L + 4, main_ea);
    be32(L + 8, 48); be32(L + 12, 0xF0000000u + 0x7380u);
    be32(L + 16, 16); be32(L + 20, 0xF0100000u + 0x9000u);
    memset(B->ls + 0x20000, 0, 0x80);
    dma(B, 0x44, 0x20000, 0x8000, 24);                           /* GETL: EAL = list LSA, size = 8 * elements */
    CHECK(!memcmp(B->ls + 0x20000, vm_base + main_ea, 16), "3. GETL element 0 (main memory) wrong");
    CHECK(!memcmp(B->ls + 0x20010, A->ls + 0x7380, 48), "3. GETL element 1 (A's LS 0x7380, 48 bytes) wrong");
    CHECK(!memcmp(B->ls + 0x20040, B->ls + 0x9000, 16), "3. GETL element 2 (B's own LS 0x9000 via thread 1) wrong");

    /* 4. isolation: C is thread 0 of G2; the same EA names C's own LS */
    fill(C->ls + 0x7380, 16, 0xC0);
    memset(C->ls + 0x10000, 0, 16);
    dma(C, 0x40, 0x10000, 0xF0007380u, 16);
    CHECK(!memcmp(C->ls + 0x10000, C->ls + 0x7380, 16) && memcmp(C->ls + 0x10000, A->ls + 0x7380, 16),
          "4. C (thread 0 of another group) did not read its own LS through 0xF0007380");

    /* 5. SPURS instance: SPU 0's run leaves items in its LS, SPU 1's run GETs them through the window */
    { static uint8_t pm[0x100];
      const uint32_t spurs_ea = 0x00FC6380u;
      memset(s_spurs_seen, 0, sizeof s_spurs_seen);
      spu_run_policy_module(pm_entry, -1, pm, sizeof pm, 0x00FBB240u, 2, spurs_ea, 0);
      spu_run_policy_module(pm_entry, -1, pm, sizeof pm, 0x00FBB240u, 2, spurs_ea, 1);
      uint8_t want[16]; fill(want, 16, 0xA5);
      CHECK(!memcmp(s_spurs_seen, want, 16), "5. SPURS SPU 1 did not read SPU 0's LS 0x7380 through the window "
            "(got %02X%02X%02X%02X)", s_spurs_seen[0], s_spurs_seen[1], s_spurs_seen[2], s_spurs_seen[3]); }

    /* 6. no group / problem state: plain path, no crash */
    memset(E->ls + 0x100, 0xEE, 16);
    dma(E, 0x40, 0x100, 0xF0007380u, 16);
    { static const uint8_t z[16]; CHECK(!memcmp(E->ls + 0x100, z, 16), "6. unbound context's window GET did not keep the plain path"); }
    fill(B->ls + 0x500, 16, 0x33);
    dma(B, 0x20, 0x500, 0xF0000000u + 0x5400Cu - 0xCu, 16);     /* problem state of thread 0: plain path */
    if (bound()) {
        spu_group_ls_unbind(A);
        memset(B->ls + 0x150A0, 0xEE, 16);
        dma(B, 0x40, 0x150A0, 0xF0007380u, 16);                  /* thread 0 gone: plain path (zero page) */
        static const uint8_t z[16];
        CHECK(!memcmp(B->ls + 0x150A0, z, 16), "6. GET after A was unbound still reached A's LS");
    }

    printf("%s: %u checks passed, %u failed\n", fails ? "FAIL" : "PASS", passes, fails);
    return fails ? 1 : 0;
}

/* link stubs (as test_spu_machstat_channel.c): none of these is reached by the paths above */
void ppu_resv_break_store(uint64_t ea) { (void)ea; abort(); }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; abort(); }
unsigned long long ps3_ms_now(void) { abort(); }
uint32_t g_ww_lo = 0xFFFFFFFFu, g_ww_hi; int g_resv_store_active;
uint32_t g_barrier_sync_watch;
uint32_t g_ydkj_real_spurs_ea, g_ydkj_real_taskset_ea, g_ydkj_real_taskid;
void spurs_ef_set_from_spu(uint32_t ea, uint16_t bits) { (void)ea; (void)bits; abort(); }
void ydkj_wake_all_event_flags(void) { abort(); }
void spurs_pm_build_context(spu_context* c, uint32_t a, uint32_t b, uint32_t d) { (void)c; (void)a; (void)b; (void)d; abort(); }
uint32_t g_spu_image_src_ea, g_spu_image_ls_start, g_spu_image_span;
void spu_thread_publish_ctx(uint32_t tid, void* c) { (void)tid; (void)c; abort(); }
