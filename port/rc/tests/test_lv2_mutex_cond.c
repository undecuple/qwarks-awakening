/* Host test (POSIX threads; no game data, no GPU): lv2 mutex/cond semantics of the runtime's sys_mutex.c / sys_cond.c
 * (work/ps3recomp-patched, built by run_lv2_sync_test.sh against the tree given to it).
 *
 * What lv2 does (RPCS3 sys_mutex.cpp / sys_cond.cpp, read 2026-10-21, no code copied):
 *  - sys_cond_signal hands the bound mutex to the woken waiter at the signal (try_own; if the mutex is held, the
 *    waiter is queued on it and gets it at the unlock); the waiter does not re-lock it itself.
 *  - sys_mutex_destroy: ESRCH, then EBUSY while owned, then EPERM while a cond is bound.
 *  - sys_cond_wait releases the whole recursion count and restores it; EPERM for a non-owner.
 *  - sys_cond_signal_to wakes only that thread; EPERM when it does not wait on the cond.
 *
 * Cases (exit 0 = all PASS):
 *  handoff   ToD's TIER loader pattern (0x0060AA30 / 0x0060A8B8 / 0x0060A988): thread 2 waits on cond C with mutex M;
 *            thread 1 locks M, unlocks, signals C, then trylocks M at once -> must be EBUSY (M already belongs to
 *            thread 2). The old runtime left M free until thread 2 re-locked it, so the trylock succeeded: with the
 *            game's next step that is the level-load deadlock. 200 rounds, count of steals must be 0.
 *  destroy   destroy while held -> EBUSY; with a cond bound -> EPERM; after cond destroy + unlock -> OK; ESRCH after;
 *            a new mutex then works from another thread.
 *  waiter    destroy while another thread blocks in lock -> EBUSY, and that thread gets the mutex at the unlock.
 *  depth     recursive mutex locked twice, cond_wait, signal from another thread: owner and count 2 restored.
 *  timeout   cond_wait 20 ms without a signal -> ETIMEDOUT and the mutex is owned again; lock with a 20 ms timeout on
 *            a held mutex -> ETIMEDOUT.
 *  signal_to wrong thread -> EPERM (waiter stays); right thread -> wakes.
 *  stress    8 threads x 20000 lock/unlock increments, plus a cond ping-pong: no lost update, no hang.
 *  create    (7007 on KZ3 1122) null id/attr pointer -> EFAULT; bad protocol / recursive word -> EINVAL; a destroyed id
 *            is not handed out again by the next create (next-fit); a recursive lock/trylock at count 0x7FFFFFFF ->
 *            EKRESOURCE, the mutex unchanged.
 *  diag      (7006 on KZ3 1122) lv2_sync_describe_thread: a thread blocked in sys_mutex_lock is described as queued with
 *            the owner; a cond waiter as waiting for a signal, after the signal (mutex still held) as waiting for the
 *            hand-off; nothing once it returned. */
#define _GNU_SOURCE
#include "runtime/syscalls/sys_cond.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint8_t* vm_base;
int64_t lv2_usec_deadline(uint64_t usec)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000ll + t.tv_nsec + (int64_t)usec * 1000ll;
}
int lv2_deadline_passed(int64_t d)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000ll + t.tv_nsec >= d;
}
void ppu_thread_note_block(uint64_t tid) { (void)tid; }   /* sys_ppu_thread.c (sys_cond_wait calls it) */
uint32_t vm_read32(uint64_t a) { const uint8_t* p = vm_base + (uint32_t)a; return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failures++; } } while (0)
static int32_t call(int64_t (*f)(ppu_context*), uint64_t tid, uint64_t a, uint64_t b)
{
    ppu_context c; memset(&c, 0, sizeof c); c.thread_id = tid; c.gpr[3] = a; c.gpr[4] = b;
    return (int32_t)f(&c);
}
static uint32_t be(unsigned off) { return vm_read32(off); }
static void put_be(unsigned off, uint32_t v) { vm_base[off] = v >> 24; vm_base[off + 1] = v >> 16; vm_base[off + 2] = v >> 8; vm_base[off + 3] = v; }
static unsigned s_attr_next = 0x1000;
static uint32_t mk_mutex(int recursive)
{
    const unsigned a = s_attr_next; s_attr_next += 0x40;
    put_be(a + 0, SYS_SYNC_PRIORITY); put_be(a + 4, recursive ? SYS_SYNC_RECURSIVE : SYS_SYNC_NOT_RECURSIVE);
    memcpy(vm_base + a + 24, "testmtx", 8);
    if (call(sys_mutex_create, 1, a + 0x30, a) != CELL_OK) return 0;
    return be(a + 0x30);
}
static uint32_t mk_cond(uint32_t mid)
{
    const unsigned a = s_attr_next; s_attr_next += 0x40;
    if (call(sys_cond_create, 1, a + 0x30, mid) != CELL_OK) return 0;   /* r5 (attr) = 0 */
    return be(a + 0x30);
}
static void msleep(int ms) { struct timespec t = { 0, ms * 1000000L }; nanosleep(&t, NULL); }

/* ---- handoff ---- */
static uint32_t h_mid, h_cid; static volatile int h_waiting, h_woke, h_go;
static void* h_loader(void* p)
{
    (void)p;
    CHECK(call(sys_mutex_lock, 2, h_mid, 0) == CELL_OK);
    h_waiting = 1;
    CHECK(call(sys_cond_wait, 2, h_cid, 0) == CELL_OK);
    h_woke = 1;
    while (!h_go) msleep(1);                       /* keeps M, like the loader that goes on to wait on O+0x20 */
    CHECK(call(sys_mutex_unlock, 2, h_mid, 0) == CELL_OK);
    return NULL;
}
static void t_handoff(void)
{
    int steals = 0;
    for (int r = 0; r < 200; r++) {
        h_mid = mk_mutex(0); h_cid = mk_cond(h_mid); h_waiting = h_woke = h_go = 0;
        pthread_t th; pthread_create(&th, NULL, h_loader, NULL);
        while (!h_waiting) msleep(1);
        CHECK(call(sys_mutex_lock, 1, h_mid, 0) == CELL_OK);   /* returns once the loader sleeps in cond_wait */
        CHECK(call(sys_mutex_unlock, 1, h_mid, 0) == CELL_OK);
        CHECK(call(sys_cond_signal, 1, h_cid, 0) == CELL_OK);
        const int32_t t = call(sys_mutex_trylock, 1, h_mid, 0);
        if (t == CELL_OK) { steals++; call(sys_mutex_unlock, 1, h_mid, 0); }
        else CHECK(t == (int32_t)CELL_EBUSY);
        h_go = 1;
        pthread_join(th, NULL);
        CHECK(h_woke);
        CHECK(call(sys_cond_destroy, 1, h_cid, 0) == CELL_OK);
        CHECK(call(sys_mutex_destroy, 1, h_mid, 0) == CELL_OK);
    }
    printf("handoff: %d of 200 trylocks took the mutex between the signal and the waiter (lv2: 0)\n", steals);
    CHECK(steals == 0);
}

/* ---- destroy ---- */
static uint32_t d_mid; static volatile int d_ok;
static void* d_other(void* p) { (void)p; d_ok = call(sys_mutex_lock, 3, d_mid, 0) == CELL_OK && call(sys_mutex_unlock, 3, d_mid, 0) == CELL_OK; return NULL; }
static void t_destroy(void)
{
    uint32_t m = mk_mutex(0), c = mk_cond(m);
    CHECK(call(sys_mutex_lock, 1, m, 0) == CELL_OK);
    CHECK(call(sys_mutex_destroy, 1, m, 0) == (int32_t)CELL_EBUSY);
    CHECK(call(sys_mutex_unlock, 1, m, 0) == CELL_OK);
    CHECK(call(sys_mutex_destroy, 1, m, 0) == (int32_t)CELL_EPERM);
    CHECK(call(sys_cond_destroy, 1, c, 0) == CELL_OK);
    CHECK(call(sys_mutex_destroy, 1, m, 0) == CELL_OK);
    CHECK(call(sys_mutex_lock, 1, m, 0) == (int32_t)CELL_ESRCH);
    d_mid = mk_mutex(0); d_ok = 0;
    CHECK(d_mid != 0);
    pthread_t th; pthread_create(&th, NULL, d_other, NULL); pthread_join(th, NULL);
    CHECK(d_ok);
    CHECK(call(sys_mutex_destroy, 1, d_mid, 0) == CELL_OK);
}

/* ---- waiter ---- */
static uint32_t w_mid; static volatile int w_in, w_got;
static void* w_locker(void* p) { (void)p; w_in = 1; if (call(sys_mutex_lock, 4, w_mid, 0) == CELL_OK) { w_got = 1; call(sys_mutex_unlock, 4, w_mid, 0); } return NULL; }
static void t_waiter(void)
{
    w_mid = mk_mutex(0); w_in = w_got = 0;
    CHECK(call(sys_mutex_lock, 1, w_mid, 0) == CELL_OK);
    pthread_t th; pthread_create(&th, NULL, w_locker, NULL);
    while (!w_in) msleep(1);
    msleep(20);
    CHECK(call(sys_mutex_destroy, 1, w_mid, 0) == (int32_t)CELL_EBUSY);
    CHECK(!w_got);
    CHECK(call(sys_mutex_unlock, 1, w_mid, 0) == CELL_OK);
    pthread_join(th, NULL);
    CHECK(w_got);
    CHECK(call(sys_mutex_destroy, 1, w_mid, 0) == CELL_OK);
}

/* ---- depth / timeout / signal_to ---- */
static uint32_t s_mid, s_cid; static volatile int s_rc = -1, s_in;
static void* s_waiter(void* p)
{
    const uint64_t tid = (uint64_t)(uintptr_t)p;
    call(sys_mutex_lock, tid, s_mid, 0);
    s_in = 1;
    s_rc = call(sys_cond_wait, tid, s_cid, 0);
    call(sys_mutex_unlock, tid, s_mid, 0);
    return NULL;
}
static void t_depth_timeout_to(void)
{
    uint32_t m = mk_mutex(1), c = mk_cond(m);
    CHECK(call(sys_mutex_lock, 1, m, 0) == CELL_OK);
    CHECK(call(sys_mutex_lock, 1, m, 0) == CELL_OK);
    CHECK(call(sys_cond_wait, 1, c, 20000) == (int32_t)CELL_ETIMEDOUT);
    CHECK(g_sys_mutexes[m - 1].owner_tid == 1 && g_sys_mutexes[m - 1].lock_count == 2);
    CHECK(call(sys_mutex_unlock, 1, m, 0) == CELL_OK);
    CHECK(call(sys_mutex_unlock, 1, m, 0) == CELL_OK);
    CHECK(call(sys_cond_wait, 1, c, 0) == (int32_t)CELL_EPERM);
    /* lock timeout */
    s_mid = m; s_cid = c; s_in = 0; s_rc = -1;
    pthread_t th; pthread_create(&th, NULL, s_waiter, (void*)(uintptr_t)5);
    while (!s_in) msleep(1);
    msleep(5);
    CHECK(call(sys_mutex_lock, 1, m, 20000) == CELL_OK);          /* the waiter released it in cond_wait */
    CHECK(call(sys_mutex_lock, 1, m, 0) == CELL_OK);              /* recursive */
    CHECK(call(sys_cond_signal_to, 1, c, 6) == (int32_t)CELL_EPERM);
    CHECK(call(sys_cond_signal_to, 1, c, 5) == CELL_OK);
    CHECK(call(sys_mutex_unlock, 1, m, 0) == CELL_OK);
    CHECK(call(sys_mutex_unlock, 1, m, 0) == CELL_OK);            /* now handed to thread 5 */
    pthread_join(th, NULL);
    CHECK(s_rc == CELL_OK);
    /* lock timeout on a held mutex */
    CHECK(call(sys_mutex_lock, 7, m, 0) == CELL_OK);
    CHECK(call(sys_mutex_lock, 1, m, 20000) == (int32_t)CELL_ETIMEDOUT);
    CHECK(call(sys_mutex_unlock, 7, m, 0) == CELL_OK);
    CHECK(call(sys_cond_destroy, 1, c, 0) == CELL_OK);
    CHECK(call(sys_mutex_destroy, 1, m, 0) == CELL_OK);
}

/* ---- stress ---- */
static uint32_t x_mid, x_cid; static volatile long x_count; static volatile int x_turn;
static void* x_worker(void* p)
{
    const uint64_t tid = 10 + (uint64_t)(uintptr_t)p;
    for (int i = 0; i < 20000; i++) {
        call(sys_mutex_lock, tid, x_mid, 0);
        long v = x_count; x_count = v + 1;
        call(sys_mutex_unlock, tid, x_mid, 0);
    }
    return NULL;
}
static void* x_pong(void* p)
{
    const int me = (int)(uintptr_t)p; const uint64_t tid = 30 + me;
    for (int i = 0; i < 2000; i++) {
        call(sys_mutex_lock, tid, x_mid, 0);
        while (x_turn != me) call(sys_cond_wait, tid, x_cid, 0);
        x_turn = !me;
        call(sys_cond_signal_all, tid, x_cid, 0);
        call(sys_mutex_unlock, tid, x_mid, 0);
    }
    return NULL;
}
static void t_stress(void)
{
    x_mid = mk_mutex(0); x_cid = mk_cond(x_mid); x_count = 0; x_turn = 0;
    pthread_t th[10];
    for (int i = 0; i < 8; i++) pthread_create(&th[i], NULL, x_worker, (void*)(uintptr_t)i);
    pthread_create(&th[8], NULL, x_pong, (void*)(uintptr_t)0);
    pthread_create(&th[9], NULL, x_pong, (void*)(uintptr_t)1);
    for (int i = 0; i < 10; i++) pthread_join(th[i], NULL);
    CHECK(x_count == 8 * 20000);
}

/* ---- create (7007) ---- */
static void t_create(void)
{
    const unsigned a = s_attr_next; s_attr_next += 0x40;
    put_be(a + 0, SYS_SYNC_FIFO); put_be(a + 4, SYS_SYNC_NOT_RECURSIVE);
    CHECK(call(sys_mutex_create, 1, 0, a) == (int32_t)CELL_EFAULT);
    CHECK(call(sys_mutex_create, 1, a + 0x30, 0) == (int32_t)CELL_EFAULT);
    put_be(a + 0, 0x4);
    CHECK(call(sys_mutex_create, 1, a + 0x30, a) == (int32_t)CELL_EINVAL);
    put_be(a + 0, SYS_SYNC_PRIORITY_INHERIT); put_be(a + 4, 0x30);
    CHECK(call(sys_mutex_create, 1, a + 0x30, a) == (int32_t)CELL_EINVAL);
    put_be(a + 4, SYS_SYNC_NOT_RECURSIVE);
    CHECK(call(sys_mutex_create, 1, a + 0x30, a) == CELL_OK);
    const uint32_t m1 = be(a + 0x30);
    CHECK(call(sys_mutex_destroy, 1, m1, 0) == CELL_OK);
    const uint32_t m2 = mk_mutex(0);
    CHECK(m2 != 0 && m2 != m1);                                    /* next-fit: not the slot just freed */
    CHECK(call(sys_mutex_destroy, 1, m2, 0) == CELL_OK);
    const uint32_t r = mk_mutex(1);
    CHECK(call(sys_mutex_lock, 1, r, 0) == CELL_OK);
    g_sys_mutexes[r - 1].core.count = 0x7FFFFFFF;                  /* as if locked 0x7FFFFFFF times */
    CHECK(call(sys_mutex_lock, 1, r, 0) == (int32_t)CELL_EKRESOURCE);
    CHECK(call(sys_mutex_trylock, 1, r, 0) == (int32_t)CELL_EKRESOURCE);
    CHECK(g_sys_mutexes[r - 1].core.count == 0x7FFFFFFF && g_sys_mutexes[r - 1].core.owner == 1);
    g_sys_mutexes[r - 1].core.count = 1;
    CHECK(call(sys_mutex_unlock, 1, r, 0) == CELL_OK);
    CHECK(call(sys_mutex_destroy, 1, r, 0) == CELL_OK);
}

/* ---- diag (7006) ---- */
static uint32_t g_mid, g_cid; static volatile int g_in, g_done;
static void* g_locker(void* p) { (void)p; g_in = 1; call(sys_mutex_lock, 8, g_mid, 0); call(sys_mutex_unlock, 8, g_mid, 0); g_done = 1; return NULL; }
static void* g_waiter(void* p) { (void)p; call(sys_mutex_lock, 9, g_mid, 0); g_in = 1; call(sys_cond_wait, 9, g_cid, 0); call(sys_mutex_unlock, 9, g_mid, 0); g_done = 1; return NULL; }
static void t_diag(void)
{
    char b[384];
    g_mid = mk_mutex(0); g_cid = mk_cond(g_mid);
    CHECK(lv2_sync_describe_thread(8, b, sizeof b) == 0);
    CHECK(call(sys_mutex_lock, 1, g_mid, 0) == CELL_OK);
    g_in = g_done = 0;
    pthread_t th; pthread_create(&th, NULL, g_locker, NULL);
    while (!g_in) msleep(1);
    msleep(20);
    CHECK(lv2_sync_describe_thread(8, b, sizeof b) == 1);
    printf("diag: %s\n", b);
    CHECK(strstr(b, "lock wait (queued)") && strstr(b, "owner=1 ") && strstr(b, "queued=1 "));
    CHECK(call(sys_mutex_unlock, 1, g_mid, 0) == CELL_OK);
    pthread_join(th, NULL);
    CHECK(lv2_sync_describe_thread(8, b, sizeof b) == 0);
    g_in = g_done = 0;
    pthread_create(&th, NULL, g_waiter, NULL);
    while (!g_in) msleep(1);
    CHECK(call(sys_mutex_lock, 1, g_mid, 0) == CELL_OK);          /* returns once thread 9 sleeps in cond_wait */
    CHECK(lv2_sync_describe_thread(9, b, sizeof b) == 1);
    printf("diag: %s\n", b);
    CHECK(strstr(b, "waiting for a signal") && strstr(b, "cond_waiters=1 "));
    CHECK(call(sys_cond_signal, 1, g_cid, 0) == CELL_OK);
    CHECK(lv2_sync_describe_thread(9, b, sizeof b) == 1);
    printf("diag: %s\n", b);
    CHECK(strstr(b, "waiting for the mutex hand-off"));
    CHECK(call(sys_mutex_unlock, 1, g_mid, 0) == CELL_OK);
    pthread_join(th, NULL);
    CHECK(g_done && lv2_sync_describe_thread(9, b, sizeof b) == 0);
    CHECK(call(sys_cond_destroy, 1, g_cid, 0) == CELL_OK);
    CHECK(call(sys_mutex_destroy, 1, g_mid, 0) == CELL_OK);
}

int main(int argc, char** argv)
{
    vm_base = calloc(1, 1 << 20);
    const char* only = argc > 1 ? argv[1] : "all";
    alarm(60);
    if (!strcmp(only, "all") || !strcmp(only, "handoff")) t_handoff();
    if (!strcmp(only, "all") || !strcmp(only, "destroy")) t_destroy();
    if (!strcmp(only, "all") || !strcmp(only, "waiter")) t_waiter();
    if (!strcmp(only, "all") || !strcmp(only, "depth")) t_depth_timeout_to();
    if (!strcmp(only, "all") || !strcmp(only, "stress")) t_stress();
    if (!strcmp(only, "all") || !strcmp(only, "create")) t_create();
    if (!strcmp(only, "all") || !strcmp(only, "diag")) t_diag();
    printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
