/* Host test for R&C patch 7116 (libs/video/cellGcmSys.c: gcm_note_ppu_put / gcm_put_snap_fetch).
 *
 * Builds the real walker (cellGcmSys.c included, as the upstream libs/video/tests/test_gcm_*.c do) and plays the frame
 * end of Full Frontal Assault 0x006F6138 (variant B, read in the lifted code; addresses below are the test's own):
 *   1. the walker is parked at G (get == put == G);
 *   2. the title writes a block at B whose last word, at slot S, is the JUMP-back 0x20000000 | G, writes JUMP B at G
 *      and moves put G -> S;
 *   3. the walker runs the block and parks at S; the title sees get == put == S;
 *   4. the title moves put S -> G, restores the word at G and reuses the scratch at S (a probe method there, then a
 *      JUMP to G, so the old behaviour is visible as one extra probe);
 *   5. the walker's next pass starts at S.
 * The guest's put store goes through guest_put(): the REAL hook line of the tree's out-of-line vm_write32
 * (runtime/ppu/ppu_loader.cpp), extracted verbatim by port/rc/tests/put_hook_lines.py into RC_PUT_HOOK_LINES, then the
 * store. The extracted vm_write64 lines are run the same way (a std whose high or low word is put).
 *
 * argv[1] = on | log | off | trace (the GCM_PUT_SNAP the runner set; trace = GCM_PUT_SNAP=0 GCM_PUT_TRACE=on, patch
 * 7118): on takes the kept JUMP-back (no extra probe); log, off and trace walk memory (one extra probe); on and log
 * count the same keeps; off is not armed; trace is armed (the hook runs) but keeps nothing, and the runner checks its
 * two [put-trace] lines. Mode on also checks the behind-walker case (nothing kept), cellGcm_syscall_set_fifo dropping
 * the kept word, a resync dropping it, a stale keep dropped at a pass start, a put move by a std whose low word is put,
 * and both ring recycle paths dropping a pending keep: the walker's own recycle of the title's ring (the guest's put
 * store lands mid-pass, after the pass read put) and cellGcm_fifo_recycle (the buffer-full callback, observed from a
 * walker thread while the callback waits). Every mode checks the extracted hook lines' dispatch (which store calls the
 * hook with which word). */
#include "cellGcmSys.c"
#ifndef RC_PUT_HOOK_LINES
#error "build with -DRC_PUT_HOOK_LINES=<generated header> (port/rc/tests/put_hook_lines.py; the runner does it)"
#endif
#include RC_PUT_HOOK_LINES
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
uint8_t* vm_base;
uint32_t ppu_vm_size;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t ea) { (void)ea; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_coh_notify_write(uint32_t a) { (void)a; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
ps3_guest_caller_fn g_ps3_guest_caller;
PPU_THREAD_LOCAL ppu_context* g_active_ctx;
u32 g_rsx_last_reference;
void (*g_spu_put_hook)(uint32_t ea, uint32_t size);
void ppu_dump_bctrl_ring(uint32_t n, const char* why) { (void)n; (void)why; }
void ppu_dump_guest_stack(ppu_context* c, const char* why) { (void)c; (void)why; }
void rsx_state_init(rsx_state* state) { (void)state; }
#define PROBE_METHOD 0x1EE0u
static u32 seen_n;
int  rsx_process_method(rsx_state* state, u32 method, u32 data)
{
    (void)state; (void)data;
    if (method == PROBE_METHOD) seen_n++;
    return 0;
}
int  rsx_draw_engine_enabled(void) { return 0; }
int  rsx_process_vp_run(rsx_state* state, u32 method, const u8* src, u32 count)
{ (void)state; (void)method; (void)src; (void)count; return 0; }
rsx_backend* rsx_get_backend(void) { return NULL; }
void rsx_draw_engine_method(u32 method, u32 arg) { (void)method; (void)arg; }
void rsx_draw_engine_note_flip(u32 buffer_id) { (void)buffer_id; }
void rsx_draw_engine_guest_read(u32 l, u32 o, u32 p, u32 w, u32 h) { (void)l; (void)o; (void)p; (void)w; (void)h; }
void rsx_draw_engine_guest_wrote(u32 l, u32 o, u32 p, u32 w, u32 h) { (void)l; (void)o; (void)p; (void)w; (void)h; }
/* The walker calls rsx_live_draw_enabled() right before its ring-recycle block (KZ3 patch 2029's submit). The recycle
 * case arms s_midpass_put so that the guest's put store lands there, after the pass read put, as a guest thread's store
 * racing the walker would. */
static u32 s_midpass_put;
static void guest_put(u32 v);
int  rsx_live_draw_enabled(void)
{
    if (s_midpass_put) { const u32 v = s_midpass_put; s_midpass_put = 0; guest_put(v); }
    return 0;
}
void rsx_live_draw_method(u32 method, u32 arg) { (void)method; (void)arg; }
int  rsx_live_draw_blit(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 w, u32 h)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)w; (void)h; return 0; }
void rsx_live_draw_note_resolve(u32 a, u32 b, u32 c, u32 d) { (void)a; (void)b; (void)c; (void)d; }
int  rsx_live_draw_resolve_blit(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g, u32 h,
                                u32 i, u32 j, u32 k, u32 l)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; (void)j; (void)k; (void)l; return 0; }
void rsx_raise_user_cmd(uint32_t arg) { (void)arg; }
/* The patched tree's other walker consumers (KZ3 wait accounting, live-draw bookkeeping): not reached here. */
int g_kz3_wacct;
void (*g_kz3_wtrace)(const char*, int, uint32_t, uint64_t, uint64_t);
volatile u32 g_rsx_game_frame_marks;
void kz3_wacct_flip(void) {}
void kz3_wacct_note(int k, uint32_t a, uint64_t ns) { (void)k; (void)a; (void)ns; }
uint64_t kz3_wacct_now(void) { return 0; }
void kz3_wacct_trace(int k, uint32_t a, uint64_t b, uint64_t c) { (void)k; (void)a; (void)b; (void)c; }
u32  rsx_live_draw_before_guest_sync(void) { return 0; }
void rsx_live_draw_debug_cond_render_stats(u64 out[6]) { memset(out, 0, 6 * sizeof(u64)); }
void rsx_live_draw_debug_report_readback_stats(u64 out[6]) { memset(out, 0, 6 * sizeof(u64)); }
u32  rsx_live_draw_note_guest_write(u32 l, u32 o, u32 s) { (void)l; (void)o; (void)s; return 0; }
void rsx_live_draw_note_guest_write_pages(u32 l, u32 o, u32 s) { (void)l; (void)o; (void)s; }
void rsx_live_draw_note_spu_read(u32 l, u32 o, u32 s) { (void)l; (void)o; (void)s; }
u32  rsx_live_draw_report_value(u32 off, u32 dflt) { (void)off; return dflt; }
void rsx_live_draw_set_write_source(u32 src) { (void)src; }
void rsx_live_draw_submit_pending(void) {}
void rsx_live_draw_watch_copy(u32 l, u32 o, u32 s) { (void)l; (void)o; (void)s; }
u32  rsx_live_draw_writeback_range(u32 l, u32 o, u32 s) { (void)l; (void)o; (void)s; return 0; }

#define IO_ADDR   0x00100000u
#define IO_SIZE   0x00100000u
#define CMD_SIZE  0x00010000u
#define BLOCK_B   0x00008000u

static uint32_t g_heap = 0x00400000u;
static u32 host_alloc(u32 size, u32 align)
{
    if (!align) align = 4;
    g_heap = (g_heap + align - 1u) & ~(align - 1u);
    u32 r = g_heap;
    g_heap += size;
    return r;
}
static void gw32(uint32_t ea, uint32_t v)
{
    uint8_t* p = vm_base + ea;
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static u32 gr32(uint32_t ea)
{
    const uint8_t* p = vm_base + ea;
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static void host_w32(u32 ea, u32 v) { gw32(ea, v); }

static int fails;
static void check(int ok, const char* what)
{
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    fails += !ok;
}
static u32 put_probe(u32 io, u32 arg)
{
    gw32(IO_ADDR + io, (1u << 18) | PROBE_METHOD);
    gw32(IO_ADDR + io + 4, arg);
    return io + 8;
}
static uint32_t ctrl;
/* The lifted stw to put: vm_write32's real hook line (extracted), then the store. */
static void guest_put(u32 v)
{
    loader_put_hook_w32(ctrl + 0, v);
    gw32(ctrl + 0, v);
}
/* A lifted std at EA a: vm_write64's real hook lines (extracted), then the 8-byte big-endian store. */
static void guest_std(u32 a, uint64_t v)
{
    loader_put_hook_w64(a, v);
    gw32(a, (u32)(v >> 32));
    gw32(a + 4, (u32)v);
}
/* Recorder for the hook-line dispatch case. */
static int rec_n;
static u32 rec_v;
static void rec_hook(uint32_t v) { rec_n++; rec_v = v; }
static int rec32(u32 a, u32 v) { rec_n = 0; rec_v = 0; loader_put_hook_w32(a, v); return rec_n; }
static int rec64(u32 a, uint64_t v) { rec_n = 0; rec_v = 0; loader_put_hook_w64(a, v); return rec_n; }
/* cellGcm_fifo_recycle waits on the guest thread for the walker; this thread is the walker. It reads the slot once the
 * recycle has moved put and marked itself waiting (both after its gcm_put_snap_forget), then runs passes until the
 * walker takes the JUMP to begin. */
static volatile uint64_t s_obs_slot = ~0ull;
static volatile int s_obs_seen;
static void* recycle_walker(void* arg)
{
    (void)arg;
    const u32 bj0 = s_fifo_back_jumps;
    for (int i = 0; i < 5000 && !s_fifo_recycle_waiting; i++) {
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
    }
    s_obs_seen = s_fifo_recycle_waiting;
    s_obs_slot = __atomic_load_n(&s_put_snap, __ATOMIC_ACQUIRE);
    for (int i = 0; i < 100 && s_fifo_back_jumps == bj0; i++) cellGcm_rsx_process_fifo();
    return NULL;
}
static long C(volatile long* c) { return __atomic_load_n(c, __ATOMIC_RELAXED); }

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* mode = argc > 1 ? argv[1] : "on";
    const int on = !strcmp(mode, "on"), off = !strcmp(mode, "off"), trace = !strcmp(mode, "trace");
    const uint32_t vm_size = ppu_hle_inject_base + 0x10000u;
    vm_base = (uint8_t*)calloc(1, vm_size);
    if (!vm_base) { puts("FAIL: guest VM alloc"); return 1; }
    ppu_vm_size = vm_size;

    u32 ctx_out = host_alloc(4, 4);
    (void)cellGcmSetupContext(ctx_out, CMD_SIZE, IO_SIZE, IO_ADDR, host_alloc, host_w32);
    ctrl = (uint32_t)(uintptr_t)cellGcmGetControlRegister();
    printf("[gcm_put_snap] mode %s\n", mode);
    check(off ? (g_gcm_put_hook == NULL && g_gcm_put_ea == 0)
              : (g_gcm_put_hook == gcm_note_ppu_put && g_gcm_put_ea == ctrl),
          off ? "GCM_PUT_SNAP=off: the hook is not armed" : "cellGcmInit arms the hook on the control register's put");

    /* The extracted vm_write32 / vm_write64 hook lines: which store calls the hook, with which word. Armed on the
     * recorder for this case only (every mode), then restored. */
    { const uint32_t ea0 = g_gcm_put_ea;
      void (*const h0)(uint32_t) = g_gcm_put_hook;
      g_gcm_put_ea = ctrl; g_gcm_put_hook = rec_hook;
      check(rec32(ctrl, 0x1234) == 1 && rec_v == 0x1234, "vm_write32 at put calls the hook with the new put");
      check(rec32(ctrl + 4, 1) == 0 && rec32(ctrl - 4, 1) == 0 && rec32(ctrl + 2, 1) == 0,
            "vm_write32 at put+4 / put-4 / put+2: no hook");
      check(rec64(ctrl, 0x00005678AAAABBBBull) == 1 && rec_v == 0x5678, "std at put: the hook gets the high word");
      check(rec64(ctrl - 4, 0xAAAABBBB00009ABCull) == 1 && rec_v == 0x9ABC,
            "std at put-4 (a + 4 == put): the hook gets the low word");
      check(rec64(ctrl + 4, 1) == 0 && rec64(ctrl - 8, 1) == 0, "std at put+4 / put-8: no hook");
      g_gcm_put_hook = NULL;
      check(rec32(ctrl, 1) == 0 && rec64(ctrl, 1) == 0 && rec64(ctrl - 4, 1) == 0, "hook NULL: no call");
      g_gcm_put_hook = rec_hook; g_gcm_put_ea = 0;
      check(rec64(0xFFFFFFFCu, 1) == 0 && rec32(0xFFFFFFFCu, 1) == 0,
            "put EA 0 (not armed): a store whose low word wraps to EA 0 does not call the hook");
      g_gcm_put_ea = ea0; g_gcm_put_hook = h0; }

    /* 1. Walker parked at G. */
    const u32 G = put_probe(0, 0);
    s_fifo_getoff = 0;
    gw32(ctrl + 4, 0);
    gw32(ctrl + 0, G);
    cellGcm_rsx_process_fifo();
    check(seen_n == 1 && s_fifo_getoff == G && gr32(ctrl + 4) == G, "walker parked at G, get published");

    /* 2. Block B, JUMP-back at S; JUMP B at G; put G -> S. */
    const u32 S = put_probe(BLOCK_B, 1);
    gw32(IO_ADDR + S, 0x20000000u | G);
    const u32 W0 = gr32(IO_ADDR + G);
    gw32(IO_ADDR + G, 0x20000000u | BLOCK_B);
    guest_put(S);
    /* 3. */
    cellGcm_rsx_process_fifo();
    check(seen_n == 2 && s_fifo_getoff == S && gr32(ctrl + 4) == S, "the walker runs the injected block and parks at S");

    /* 4. put S -> G (no wait for the JUMP-back), *G = W0, scratch at S reused. */
    guest_put(G);
    gw32(IO_ADDR + G, W0);
    put_probe(S, 2);
    gw32(IO_ADDR + S + 8, 0x20000000u | G);
    /* 5. */
    cellGcm_rsx_process_fifo();
    check(s_fifo_getoff == G, "the next pass ends at G");
    if (on) check(seen_n == 2, "on: the walker took the kept JUMP-back, not the reused scratch");
    else    check(seen_n == 3, "log/off: the walker walked the reused scratch (the old behaviour)");
    if (off)
        check(C(&s_put_snap_hook) == 0 && C(&s_put_snap_taken) == 0, "off: nothing hooked, nothing kept");
    else if (trace)
        check(C(&s_put_snap_hook) == 2 && C(&s_put_snap_taken) == 0 && s_put_snap == 0,
              "trace (GCM_PUT_SNAP=0): both put stores hooked, nothing kept");
    else
        check(C(&s_put_snap_hook) == 2 && C(&s_put_snap_taken) == 2 && C(&s_put_snap_used) == 2 &&
              C(&s_put_snap_diff) == 1 && C(&s_put_snap_drop) == 0 && s_put_snap == 0,
              "counters: hook=2 kept=2 used=2 changed=1 dropped=0, slot empty");

    if (on) {
        /* Behind walker: two put moves with no pass between. The first is kept (parked at G), the second is not
         * (published get G is not the old put). */
        const u32 p1 = put_probe(G, 3);
        guest_put(p1);
        const u32 p2 = put_probe(p1, 4);
        guest_put(p2);
        check(C(&s_put_snap_taken) == 3 && C(&s_put_snap_hook) == 4, "behind walker: only the parked put move keeps a word");
        cellGcm_rsx_process_fifo();
        check(s_fifo_getoff == p2 && seen_n == 4 && C(&s_put_snap_used) == 3 && C(&s_put_snap_diff) == 1,
              "...and the walker reads the unchanged words as before");

        /* cellGcm_syscall_set_fifo moves the walker without the hook: the kept word is dropped. */
        const u32 p3 = put_probe(p2, 5);
        guest_put(p3);
        check(s_put_snap != 0 && C(&s_put_snap_taken) == 4, "a keep pending at p2");
        cellGcm_syscall_set_fifo(p2, p2);
        check(s_put_snap == 0 && C(&s_put_snap_drop) == 1, "set_fifo drops the kept word");

        /* A resync drops it too. */
        guest_put(p3);   /* walker parked at p2 (set_fifo published get = p2): kept #5 */
        check(s_put_snap != 0 && C(&s_put_snap_taken) == 5, "a keep pending at p2 again");
        { u32 g = p2; gcm_fifo_resync_why("test", &g, p3); }
        check(s_put_snap == 0 && C(&s_put_snap_drop) == 2, "a resync drops the kept word");

        /* A pass that starts at another offset drops a stale keep. */
        s_fifo_getoff = p2;
        gw32(ctrl + 4, p2);
        gw32(ctrl + 0, p2);
        const u32 p4 = put_probe(p2, 6);
        guest_put(p4);   /* kept #6 for p2 */
        s_fifo_getoff = p4;   /* the walker is elsewhere (as after a runtime move) */
        cellGcm_rsx_process_fifo();
        check(s_put_snap == 0 && C(&s_put_snap_drop) == 3 && C(&s_put_snap_used) == 3,
              "a pass starting at another offset drops the stale keep unused");

        /* A put move by a std whose LOW word is put (EA put - 4; vm_write64's second hook line): kept like a stw. The
         * high word goes to put - 4 unchanged. */
        check(s_fifo_getoff == p4 && gr32(ctrl + 0) == p4 && gr32(ctrl + 4) == p4, "walker parked at p4");
        const u32 p5 = put_probe(p4, 7);
        guest_std(ctrl - 4, ((uint64_t)gr32(ctrl - 4) << 32) | p5);
        check(gr32(ctrl + 0) == p5 && C(&s_put_snap_hook) == 8 && C(&s_put_snap_taken) == 7 && s_put_snap != 0,
              "std at put-4 moves put and keeps the word at the old put");
        cellGcm_rsx_process_fifo();
        check(s_fifo_getoff == p5 && seen_n == 5 && C(&s_put_snap_used) == 4 && s_put_snap == 0,
              "...and the walker takes it");

        /* Ring recycle 1: the walker's own recycle of the title's ring (the title overran: current >= end). The guest
         * moves put after the pass read it (s_midpass_put), so a keep is pending when the recycle moves put to begin
         * without the hook. It must be dropped there, not left for a later pass. */
        const u32 cx = gr32(ctx_out), c_begin = gr32(cx + 0x0), c_end = gr32(cx + 0x4);
        const u32 io_begin = c_begin - IO_ADDR;
        gw32(cx + 0x8, c_end);
        const u32 p6 = put_probe(p5, 8);
        const long kept0 = C(&s_put_snap_taken), drop0 = C(&s_put_snap_drop), used0 = C(&s_put_snap_used);
        s_midpass_put = p6;
        cellGcm_rsx_process_fifo();
        check(s_midpass_put == 0 && C(&s_put_snap_taken) == kept0 + 1, "recycle 1: the mid-pass put store kept a word");
        check(gr32(ctrl + 0) == io_begin && s_fifo_getoff == io_begin && gr32(cx + 0x8) == c_begin &&
              gr32(c_end - 4) == (0x20000000u | io_begin),
              "recycle 1: the walker recycled the title's ring (JUMP begin at end-4, put = get = begin)");
        check(s_put_snap == 0 && C(&s_put_snap_drop) == drop0 + 1 && C(&s_put_snap_used) == used0,
              "recycle 1: the pending keep is dropped at the recycle, unused");

        /* Ring recycle 2: cellGcm_fifo_recycle (the title's buffer-full callback, guest thread). Walker parked at q1,
         * the guest wrote up to q2 and moved put (keep pending for q1), then the callback appends JUMP begin at the
         * write head and moves put to begin without the hook. The walker thread reads the slot once the callback
         * waits (after its put store), then walks q1 -> the JUMP -> begin. */
        const u32 q1 = put_probe(io_begin, 9);
        guest_put(q1);
        cellGcm_rsx_process_fifo();
        check(s_fifo_getoff == q1 && gr32(ctrl + 4) == q1 && s_put_snap == 0, "walker parked at q1");
        const u32 q2 = put_probe(q1, 10);
        guest_put(q2);
        check(s_put_snap != 0, "recycle 2: a keep pending for q1");
        gw32(cx + 0x8, IO_ADDR + q2);
        const long drop1 = C(&s_put_snap_drop), used1 = C(&s_put_snap_used);
        const u32 seen1 = seen_n;
        pthread_t th;
        if (pthread_create(&th, NULL, recycle_walker, NULL) != 0) { puts("FAIL: pthread_create"); return 1; }
        cellGcm_fifo_recycle(cx);
        pthread_join(th, NULL);
        check(s_obs_seen && s_obs_slot == 0, "recycle 2: no keep left once cellGcm_fifo_recycle has moved put");
        check(C(&s_put_snap_drop) == drop1 + 1 && C(&s_put_snap_used) == used1 && seen_n == seen1 + 1,
              "recycle 2: dropped unused; the walker read q1 from memory");
        check(gr32(ctrl + 0) == io_begin && s_fifo_getoff == io_begin && gr32(cx + 0x8) == c_begin,
              "recycle 2: the walker took the JUMP to begin, current = begin");

        /* Offset match (review 2026-11-24): a word kept for X is left alone by a fetch at another offset Y (the
         * walker re-reads put inside a pass, patch 1087), and is used only by a fetch at X. */
        { const long used0 = C(&s_put_snap_used), diff0 = C(&s_put_snap_diff);
          const u32 X = 0x100u, Y = 0x200u;
          const uint64_t keep = ((uint64_t)(X | 1u) << 32) | 0xDEADBEEFu;
          __atomic_store_n(&s_put_snap, keep, __ATOMIC_SEQ_CST);
          u32 w = 0x11111111u;
          gcm_put_snap_fetch(Y, &w);
          check(w == 0x11111111u && s_put_snap == keep && C(&s_put_snap_used) == used0,
                "fetch at another offset: word and slot unchanged, nothing used");
          gcm_put_snap_fetch(X, &w);
          check(w == 0xDEADBEEFu && s_put_snap == 0 && C(&s_put_snap_used) == used0 + 1 &&
                C(&s_put_snap_diff) == diff0 + 1, "fetch at the kept offset: kept word used, slot empty"); }
    }
    gcm_put_snap_summary("test");
    printf("[gcm_put_snap] %s %s (%d failed)\n", mode, fails ? "FAIL" : "PASS", fails);
    free(vm_base);
    return fails ? 1 : 0;
}
