/* Host test for patches 7030 (_cellSpursLFQueuePushBody + the _cellSpursLFQueueInitialize layout) and 7031 (the waiter wake
 * rule: id = (wid << 8) | task, IWL owner spurs|1 resolved through wklInfo1[wid].arg; a taskset's workload id): unity build of the patched
 * libs/spurs/cellSpurs.c over a calloc'd guest window. A model consumer pops by the rules of the SPU consumer lift
 * (work/reports/lfqueue-layout-acit.md section 2): available when (push-completed +0x08 - pop-in-flight +0x06) mod 2*depth > 0,
 * advance +0x06, read the element at buffer + size*(idx>=depth ? idx-depth : idx), then advance pop-completed +0x00.
 * Run with run_lfqueue_test.sh. */
#include "libs/spurs/cellSpurs.c"
#ifndef RC_LFQ_NO_7031
#include "libs/spurs/spurs_pm.c"        /* spurs_pm_build_context: LS 0x1DC */
#include "libs/spurs/spurs_taskset.c"   /* spurs_taskset_init: taskset +0x74 */
#endif
#include <pthread.h>
#include <time.h>
/* a real mutex for the line lock (rc_vm_stubs.h would define empty ones: rename those) */
#define spu_lockline_lock rc_stub_lock_unused
#define spu_lockline_unlock rc_stub_unlock_unused
#include "rc_vm_stubs.h"
#undef spu_lockline_lock
#undef spu_lockline_unlock
static pthread_mutex_t g_line = PTHREAD_MUTEX_INITIALIZER;
void spu_lockline_lock(void) { pthread_mutex_lock(&g_line); }
void spu_lockline_unlock(void) { pthread_mutex_unlock(&g_line); }
int (*g_kz3_spurs_switch_wanted)(uint32_t, uint32_t, uint64_t);
int (*g_kz3_spurs_kpoll)(uint32_t, uint32_t, uint32_t, uint32_t*);
static struct { u32 ea, id; int n; } g_sig;
void spu_taskset_signal_task(uint32_t ea, uint32_t id) { g_sig.ea = ea; g_sig.id = id; g_sig.n++; }

#define Q    0x1000u          /* queue line; buffer 0x1080 (12 x 0x20) */
#define BUF  0x1080u
#define ELEM 0x3000u          /* source element, 16-aligned */
#define D    12u

static void put_elem(u32 tag) { for (u32 i = 0; i < 8; i++) vm_write32(ELEM + 4 * i, tag + i); }
static void init_q(u32 dir) { assert(_cellSpursLFQueueInitialize(0x2000 | 1, Q, BUF, 0x20, D, dir) == CELL_OK); }
static s32 push(u32 tag, u32 blocking) { put_elem(tag); return _cellSpursLFQueuePushBody(Q, ELEM, blocking); }

/* the model SPU consumer: GetPopPointer (+0x06) then CompletePopPointer (+0x00), whole line under the line lock */
static int pop(u32* tag)
{
    int got = 0;
    spu_lockline_lock();
    u32 h1 = vm_read16(Q + 0), h4 = vm_read16(Q + 6), h5 = vm_read16(Q + 8), depth = vm_read32(Q + 0x14), mod = 2 * depth;
    if (((h5 + mod - h4) % mod) > 0) {
        u32 slot = h4 >= depth ? h4 - depth : h4;
        *tag = vm_read32((u32)(vm_read64(Q + 0x18) & ~1ull) + vm_read32(Q + 0x10) * slot);
        u16 r = ps3_bswap16((u16)((h4 + 1) % mod)); memcpy(vm_ptr8(Q + 6), &r, 2);
        r = ps3_bswap16((u16)((h1 + 1) % mod)); memcpy(vm_ptr8(Q + 0), &r, 2);
        got = 1;
    }
    spu_lockline_unlock();
    return got;
}

static int stop_now(void) { return 1; }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static void* popper(void* arg) { struct timespec ts = { 0, 30 * 1000000 }; nanosleep(&ts, NULL); u32 t; (void)arg; assert(pop(&t) && t == 0x100); return NULL; }
static void* producer(void* arg) { u32 b = (u32)(uintptr_t)arg; u32 e = ELEM + 0x40 * (b ? 1 : 0) + 0x80;
    for (u32 i = 0; i < 6; i++) { for (u32 k = 0; k < 8; k++) vm_write32(e + 4 * k, 0x5000 + b * 0x100 + i);
        assert(_cellSpursLFQueuePushBody(Q, e, 0) == 0); } return NULL; }

int main(void)
{
    vm_base = calloc(1, 0x200000); assert(vm_base);

    /* ---- Initialize layouts: direction 2 (ToD) and 3 (ACiT) ---- */
    for (u32 dir = 2; dir <= 3; dir++) {
        memset(vm_base + Q, 0xEE, 128);
        init_q(dir);
        for (u32 o = 0; o < 0x10; o += 4) assert(vm_read32(Q + o) == 0);                  /* +0x0C is NOT the depth */
        assert(vm_read32(Q + 0x10) == 0x20 && vm_read32(Q + 0x14) == D);
        assert(vm_read64(Q + 0x18) == (BUF | (dir == 3 ? 1u : 0u)));                      /* low bit tags ANY2ANY */
        assert(vm_read32(Q + 0x24) == dir && vm_read32(Q + 0x2C) == 1);
        assert(vm_read64(Q + 0x70) == (0x2000 | 1));
        for (u32 o = 0x30; o < 0x70; o += 4) assert(vm_read32(Q + o) == 0);
        for (u32 o = 0x78; o < 0x80; o += 4) assert(vm_read32(Q + o) == 0);
        for (u32 o = 0x20; o < 0x24; o += 4) assert(vm_read32(Q + o) == 0);
        assert(vm_read32(Q + 0x28) == 0);
    }

    for (u32 dir = 2; dir <= 3; dir++) {
        init_q(dir);
        /* ---- 12 pushes fill the slots in order; the 13th non-blocking one is AGAIN ---- */
        for (u32 i = 0; i < D; i++) {
            assert(push(0x100 * (i + 1), 0) == 0);
            for (u32 k = 0; k < 8; k++) assert(vm_read32(BUF + 0x20 * i + 4 * k) == 0x100 * (i + 1) + k);
            assert(vm_read16(Q + 0x08) == i + 1 && vm_read16(Q + 0x0E) == i + 1);
            assert(vm_read16(Q + 0x0A) == 0 && vm_read16(Q + 0x0C) == 0 && vm_read16(Q + 0x00) == 0);
        }
        assert(push(0xDEAD00, 0) == (s32)0x80410901);
        assert(vm_read16(Q + 0x08) == D && vm_read16(Q + 0x0E) == D);                   /* nothing reserved, nothing published */
        assert(vm_read32(BUF) == 0x100);                                                  /* slot 0 not overwritten */

        /* ---- the model consumer frees a slot; the next push goes to slot 0 (index 12 wraps) ---- */
        u32 t; assert(pop(&t) && t == 0x100);
        assert(vm_read16(Q + 0x00) == 1 && vm_read16(Q + 0x06) == 1);
        assert(push(0x1300, 0) == 0);
        assert(vm_read32(BUF) == 0x1300 && vm_read16(Q + 0x08) == D + 1 && vm_read16(Q + 0x0E) == D + 1);
        assert(push(0xDEAD00, 0) == (s32)0x80410901);                                    /* full again */

        /* ---- FIFO through several wraps of the 2*depth index ---- */
        for (u32 i = 2; i <= D; i++) assert(pop(&t) && t == 0x100 * i);
        assert(pop(&t) && t == 0x1300 && !pop(&t));
        for (u32 n = 0; n < 100; n++) {
            assert(push(0x9000 + n, 0) == 0);
            assert(pop(&t) && t == 0x9000 + n);
        }
        assert(vm_read16(Q + 0x00) == vm_read16(Q + 0x08) && vm_read16(Q + 0x00) == vm_read16(Q + 0x0E));
        assert(vm_read16(Q + 0x08) < 2 * D);
    }

    /* ---- blocking push on a full queue completes after a pop on another thread ---- */
    init_q(3);
    for (u32 i = 0; i < D; i++) assert(push(0x100 * (i + 1), 1) == 0);
    pthread_t th; pthread_create(&th, NULL, popper, NULL);
    double t0 = now_ms();
    assert(push(0xB00000, 1) == 0);
    double dt = now_ms() - t0;
    pthread_join(th, NULL);
    assert(dt >= 20.0 && dt < 2000.0);
    assert(vm_read32(BUF) == 0xB00000 && vm_read16(Q + 0x08) == D + 1);
    /* a stop request ends a blocking wait with AGAIN, never a silent drop */
    {
        u32 t; while (pop(&t)) {}
        for (u32 i = 0; i < D; i++) assert(push(0xC00 + i, 0) == 0);
        g_rc_lfq_stop_requested = stop_now;
        assert(push(0xDEAD00, 1) == (s32)0x80410901);
        g_rc_lfq_stop_requested = NULL;
    }

    /* ---- argument errors ---- */
    init_q(2);
    assert(_cellSpursLFQueuePushBody(0, ELEM, 0) == (s32)0x80410111);
    assert(_cellSpursLFQueuePushBody(Q, 0, 0) == (s32)0x80410111);
    assert(_cellSpursLFQueuePushBody(Q + 0x40, ELEM, 0) == (s32)0x80410110);
    assert(_cellSpursLFQueuePushBody(Q, ELEM + 8, 0) == (s32)0x80410110);
    for (u32 dir = 0; dir <= 1; dir++) { init_q(dir); assert(push(1, 0) == (s32)0x80410109); assert(vm_read16(Q + 0x0E) == 0); }
    assert(vm_read16(Q + 0x08) == 0);

    /* ---- wake of a waiting consumer (patch 7031 rule): pack at +0x30 (direction 2) / +0x04 (direction 3), ids at +0x32 ----
     * Guest SPURS instance at 0x2000 (wklInfo1 at +0xB00, 32 bytes each, arg at +8): wids 0 and 1 are job workloads with
     * other arguments (ACiT: Wws_Job SharedList0/1, data 0xF6ED00/0xF6F180), wid 2 is the taskset at 0x8000. A waiting task
     * stores (wid << 8) | taskId (SPU id callback @0x97F8). Build with -DRC_LFQ_NO_7031 to run case (a) against 7030 alone. */
#define SPURS 0x2000u
#define TS    0x8000u
    memset(vm_base + SPURS, 0, 0x1000);
    vm_write64(SPURS + 0xB00 + 0 * 32 + 8, 0xF6ED00); vm_write64(SPURS + 0xB00 + 1 * 32 + 8, 0xF6F180);
#ifndef RC_LFQ_NO_7031
    /* the taskset's workload id: the port's CreateTaskset path (wids 0, 1 taken as in ACiT) */
    s_workloads[0].in_use = s_workloads[1].in_use = 1; s_workloads[0].spurs_ea = s_workloads[1].spurs_ea = SPURS;
    u32 wid = rc_spurs_taskset_wid_take(SPURS, TS);
    assert(wid == 2 && rc_spurs_taskset_wid_take(SPURS, TS) == 2);                         /* same taskset, same slot */
    assert(vm_read64(SPURS + 0xB00 + 2 * 32 + 8) == TS && s_workloads[2].taskset_ea == TS);
    assert(vm_read32(SPURS + SPURS_WKL_ENABLED) == 0 && vm_base[SPURS + SPURS_WKL_STATE1 + 2] == 0);   /* never run as a PM */
    assert(s_wkl_pm[2].resolved == -1 && !(kz3_kern_mine(SPURS) & (0x8000u >> 2)));
    spurs_taskset_init(TS, SPURS, 0, wid, 0x1900, 0, 0);
    assert(vm_read32(TS + 0x74) == 2);
    /* the task context the port builds: LS 0x1DC wklCurrentId = 2, and the SPU's own id rule gives 0x0200 for task 0 */
    {
        static uint8_t ls[0x40000];
        spurs_pm_build_context(ls, TS, 0, 0, 0);
#define LSW(o) ((u32)ls[o] << 24 | (u32)ls[(o) + 1] << 16 | (u32)ls[(o) + 2] << 8 | ls[(o) + 3])
        assert(LSW(0x1DC) == 2 && LSW(0x2FB8) == 0x2700);
        assert(((LSW(0x1DC) << 8) | LSW(LSW(0x2FB8) + 0xD4)) == 0x0200);
#undef LSW
    }
#else
    vm_write64(SPURS + 0xB00 + 2 * 32 + 8, TS);
#endif
    /* (a) ACiT acit-boot2: owner = spurs | 1 (IWL), direction 3, one task (task 0 of the taskset in wid 2) waiting */
    {
        assert(_cellSpursLFQueueInitialize(SPURS | 1, Q, BUF, 0x20, D, 3) == CELL_OK);
        int before = g_sig.n;
        vm_write16(Q + 0x32, 0x0200); vm_write16(Q + 0x04, 1);                            /* tail 1, head 0 */
        assert(push(0x700, 0) == 0);
        assert(g_sig.n == before + 1);
        assert(g_sig.ea == TS && g_sig.id == 0);   /* the taskset of wid 2, NOT the SPURS instance (7030 alone: 0x2000) */
        assert(((vm_read16(Q + 0x04) >> 10) & 0x1F) == 1);
        /* two waiters, task 5 of wid 2 then task 3 of wid 0: the wid byte alone picks the wklInfo entry */
        vm_write16(Q + 0x34, 0x0205); vm_write16(Q + 0x36, 0x0003); vm_write16(Q + 0x04, (1 << 10) | 3);
        assert(push(0x701, 0) == 0 && g_sig.n == before + 2 && g_sig.ea == TS && g_sig.id == 5);
        assert(push(0x702, 0) == 0 && g_sig.n == before + 3 && g_sig.ea == 0xF6ED00 && g_sig.id == 3);
        before++;
        /* an empty wklInfo argument is refused like the SPU's send (@0x9828: taskset 0 -> 0x80410911) */
        vm_write16(Q + 0x38, 0x0301); vm_write16(Q + 0x04, (3 << 10) | 4);
        assert(push(0x705, 0) == 0 && g_sig.n == before + 2);
        /* wid > 15 is refused (fpSendSignal @0x99F8: 0x80410902) */
        vm_write16(Q + 0x3A, 0x1001); vm_write16(Q + 0x04, (4 << 10) | 5);
        assert(push(0x703, 0) == 0 && g_sig.n == before + 2);
        assert(push(0x704, 0) == 0 && g_sig.n == before + 2);                               /* ring empty again */
    }
    /* (b) owner = the taskset itself (ToD), directions 2 and 3: signal task (id & 0xFF) of eaSignal, the wid byte ignored */
    for (u32 dir = 2; dir <= 3; dir++) {
        u32 pa = Q + (dir == 3 ? 0x04 : 0x30);
        assert(_cellSpursLFQueueInitialize(TS, Q, BUF, 0x20, D, dir) == CELL_OK);
        int before = g_sig.n;
        assert(push(0x710, 0) == 0 && g_sig.n == before);                                  /* nobody waiting: no signal */
        assert(pop(&(u32){0}));
        vm_write16(Q + 0x32, 0x0209); vm_write16(Q + 0x34, 0x0207);                        /* two waiters: tasks 9, 7 */
        vm_write16(pa, 2);                                                                 /* tail 2, head 0 */
        assert(push(0x711, 0) == 0);
        assert(g_sig.n == before + 1 && g_sig.ea == TS && g_sig.id == 9);
        assert(((vm_read16(pa) >> 10) & 0x1F) == 1 && (vm_read16(pa) & 0x1F) == 2);
        assert(push(0x712, 0) == 0 && g_sig.n == before + 2 && g_sig.id == 7);
        assert(push(0x713, 0) == 0 && g_sig.n == before + 2);                              /* ring empty again */
    }
#ifndef RC_LFQ_NO_7031
    /* JoinTaskset frees the workload id; a later AddWorkload in the slot resolves its own PM */
    rc_spurs_taskset_wid_free(TS);
    assert(!s_workloads[2].in_use && s_wkl_pm[2].resolved == 0 && vm_read64(SPURS + 0xB00 + 2 * 32 + 8) == 0);
#endif

    /* ---- out-of-order publish (two producers): h5 moves only when the oldest slot completes ---- */
    init_q(3);
    u32 s0, s1;
    assert(rc_lfq_reserve(Q, D, &s0) && rc_lfq_reserve(Q, D, &s1) && s0 == 0 && s1 == 1);
    rc_lfq_publish(Q, D, s1, 3);
    assert(vm_read16(Q + 0x08) == 0 && vm_read16(Q + 0x0A) == 1);                          /* h6 bit 0 = slot h5+1 done ahead */
    rc_lfq_publish(Q, D, s0, 3);
    assert(vm_read16(Q + 0x08) == 2 && vm_read16(Q + 0x0A) == 0 && vm_read16(Q + 0x0E) == 2);

    /* ---- two producer threads, no consumer: all 12 land, h5 = h8 = 12 ---- */
    init_q(3);
    pthread_t a, b;
    pthread_create(&a, NULL, producer, (void*)0); pthread_create(&b, NULL, producer, (void*)1);
    pthread_join(a, NULL); pthread_join(b, NULL);
    assert(vm_read16(Q + 0x08) == D && vm_read16(Q + 0x0E) == D && vm_read16(Q + 0x0A) == 0);
    u32 seen[2] = { 0, 0 };
    for (u32 i = 0; i < D; i++) { u32 v = vm_read32(BUF + 0x20 * i); assert(v >= 0x5000 && v < 0x5200); seen[(v >> 8) & 1] |= 1u << (v & 0xFF); }
    assert(seen[0] == 0x3F && seen[1] == 0x3F);

    puts("LFQueue Initialize layout + PushBody checks passed");
    return 0;
}
