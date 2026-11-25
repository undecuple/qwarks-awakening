/* Host test for patch 7111 (cellSpursEventFlagWait: one PPU waiter per event flag). Unity build of the patched
 * libs/spurs/cellSpurs.c with real threads; run with port/rc/tests/run_spurs_eventflag_busy_test.sh.
 *
 * Hardware rule (RPCS3 _cellSpursEventFlagWait, read for behaviour): while a PPU thread is blocked on a flag
 * (ctrl.ppuWaitMask set), another PPU wait on it returns CELL_SPURS_TASK_ERROR_BUSY at once, whether or not its bits are
 * there. A wait whose bits are already set never blocks, so it leaves no waiter behind.
 *
 * Cases: (1) a satisfied wait returns at once and leaves no waiter; (2) thread A blocks, thread B's wait on the same flag
 * returns BUSY within 500 ms (without the patch B blocks: FAIL); (3) a wait on another flag is not affected; (4) A wakes
 * on the bit with AUTO clear; (5) after A left, a new wait is accepted again. */
#include "libs/spurs/cellSpurs.c"
#include "rc_vm_stubs.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
int (*g_kz3_spurs_switch_wanted)(uint32_t, uint32_t, uint64_t);
int (*g_kz3_spurs_kpoll)(uint32_t, uint32_t, uint32_t, uint32_t*);

#define FLAG_A  0x1000u   /* 128-byte guest flags */
#define FLAG_B  0x1080u
#define BITS_A  0x2000u   /* guest u16 in/out words */
#define BITS_B  0x2010u
#define BITS_C  0x2020u

typedef struct { uint32_t flag, bits; u16 pattern; atomic_int started, done; s32 rc; } waiter;

static void* wait_main(void* arg)
{
    waiter* w = arg;
    vm_write16(w->bits, w->pattern);
    atomic_store(&w->started, 1);
    w->rc = cellSpursEventFlagWait((CellSpursEventFlag*)(uintptr_t)w->flag, (u16*)(uintptr_t)w->bits,
                                   CELL_SPURS_EVENT_FLAG_AND);
    atomic_store(&w->done, 1);
    return NULL;
}

static int done_within(waiter* w, unsigned ms)
{
    for (unsigned i = 0; i < ms * 10 && !atomic_load(&w->done); i++) usleep(100);
    return atomic_load(&w->done);
}

static void set_events(uint32_t flag, u16 bits)
{
    EventFlagSync* s = ef_sync_find(flag);
    assert(s);
    ef_lock(s);
    ef_line_begin(flag);
    ef_w16(flag + EF_EVENTS, (u16)(ef_r16(flag + EF_EVENTS) | bits));
    ef_line_end(flag);
    ef_broadcast(s);
    ef_unlock(s);
}

static void fail(const char* what)
{
    fprintf(stderr, "FAIL: %s\n", what);
    fflush(stderr);
    _exit(1);   /* a blocked waiter thread must not keep the process alive */
}

int main(void)
{
    ppu_vm_size = 0x10000;
    vm_base = calloc(1, ppu_vm_size);
    assert(vm_base);
    vm_write8(FLAG_A + EF_CLEAR_MODE, CELL_SPURS_EVENT_FLAG_CLEAR_AUTO);
    vm_write8(FLAG_B + EF_CLEAR_MODE, CELL_SPURS_EVENT_FLAG_CLEAR_AUTO);

    /* (1) bits already there: the wait returns at once, consumes them and leaves no waiter. */
    vm_write16(FLAG_A + EF_EVENTS, 0x0004);
    vm_write16(BITS_C, 0x0004);
    assert(cellSpursEventFlagWait((CellSpursEventFlag*)(uintptr_t)FLAG_A, (u16*)(uintptr_t)BITS_C,
                                  CELL_SPURS_EVENT_FLAG_AND) == CELL_OK);
    assert(vm_read16(FLAG_A + EF_EVENTS) == 0);
    puts("ok 1: a satisfied wait returns at once");

    /* (2) A blocks on bit 0x0001. */
    waiter a = { FLAG_A, BITS_A, 0x0001 };
    pthread_t ta;
    assert(!pthread_create(&ta, NULL, wait_main, &a));
    while (!atomic_load(&a.started)) usleep(100);
    for (int i = 0; i < 5000 && !s_ef_ppu_waiters; i++) usleep(100);
    usleep(50000);   /* A is past its first check and asleep in the poll */
    if (atomic_load(&a.done)) fail("waiter A did not block");

    /* B: a second PPU wait on the same flag must be refused at once, not block. */
    waiter b = { FLAG_A, BITS_B, 0x0001 };
    pthread_t tb;
    assert(!pthread_create(&tb, NULL, wait_main, &b));
    if (!done_within(&b, 500)) fail("second PPU waiter blocked on a flag that already has one (expected BUSY)");
    if (b.rc != CELL_SPURS_TASK_ERROR_BUSY) fail("second PPU waiter did not get CELL_SPURS_TASK_ERROR_BUSY");
    assert(!pthread_join(tb, NULL));
    if (atomic_load(&a.done)) fail("the refused wait disturbed waiter A");
    puts("ok 2: second PPU waiter gets CELL_SPURS_TASK_ERROR_BUSY");

    /* (3) another flag is independent: its satisfied wait still works while A waits on FLAG_A. */
    vm_write16(FLAG_B + EF_EVENTS, 0x0002);
    vm_write16(BITS_C, 0x0002);
    assert(cellSpursEventFlagWait((CellSpursEventFlag*)(uintptr_t)FLAG_B, (u16*)(uintptr_t)BITS_C,
                                  CELL_SPURS_EVENT_FLAG_AND) == CELL_OK);
    puts("ok 3: a wait on another flag is not refused");

    /* (4) A wakes on its bit; AUTO clear consumes it. */
    set_events(FLAG_A, 0x0001);
    if (!done_within(&a, 500)) fail("waiter A did not wake on its bit");
    assert(!pthread_join(ta, NULL));
    assert(a.rc == CELL_OK && vm_read16(BITS_A) == 0x0001 && vm_read16(FLAG_A + EF_EVENTS) == 0);
    puts("ok 4: waiter A woke and consumed its bit");

    /* (5) the flag has no PPU waiter again: a new blocking wait is accepted and woken. */
    waiter c = { FLAG_A, BITS_B, 0x0008 };
    pthread_t tc;
    assert(!pthread_create(&tc, NULL, wait_main, &c));
    usleep(50000);
    if (atomic_load(&c.done)) fail("a wait after the first waiter left did not block (marker not cleared?)");
    set_events(FLAG_A, 0x0008);
    if (!done_within(&c, 500)) fail("the new waiter did not wake");
    assert(!pthread_join(tc, NULL));
    assert(c.rc == CELL_OK);
    puts("ok 5: the flag accepts a PPU waiter again after the first one left");

    puts("PASS test_spurs_eventflag_busy");
    return 0;
}
