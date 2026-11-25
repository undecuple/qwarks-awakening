/* Host test for R&C patch 7082: a PSO miss waits for its own cache slot (rsx_pso_slot.h, the code that ships).
 *
 * A small model of live draw's PSO table: get_pso appends the draw's pending entry and calls rsx_pso_demand_tail with
 * the slot it filled; the fake scan appends the lookahead's pending entries (the nested get_pso calls of 7050); the
 * fake await records which slot it was asked for and returns that slot's pipeline.
 *
 * -DRC_PSO_TAIL_PRE7082 builds the negative control instead: a copy of the tail before 7082 (await n - 1 after the
 * scan). The wrapper requires that build to FAIL, which shows these cases catch the bug. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rsx_pso_slot.h"

#define MAX 64
static struct { uint64_t key; void* pso; uint8_t pending, ahead; } t[MAX];
static uint32_t n;
static char obj[MAX];          /* the sentinel pipeline of slot i is &obj[i] */
static int ahead_active, scan_calls, scan_adds, await_calls;
static uint32_t awaited[8];
static uint64_t scan_key0;
static uint64_t fail_key;      /* an async compile of this key fails: its entry ends with a NULL pipeline */

static uint32_t insert(uint64_t key, int pending)
{
    const uint32_t s = n;
    t[s].key = key; t[s].pso = &obj[s]; t[s].pending = (uint8_t)pending; t[s].ahead = (uint8_t)(ahead_active != 0);
    n++;
    return s;
}
static void* get_pso(uint64_t key, int pending);
static void fake_scan(void)
{
    scan_calls++;
    ahead_active = 1;
    for (int i = 0; i < scan_adds; i++)   /* upcoming draws that miss: each goes through the same get_pso */
        if (get_pso(scan_key0 + (uint64_t)i, 1) != NULL) fprintf(stderr, "FAIL: a lookahead miss returned a pipeline\n");
    ahead_active = 0;
}
static void* fake_await(uint32_t idx)
{
    if (await_calls < 8) awaited[await_calls] = idx;
    await_calls++;
    t[idx].pending = 0;   /* the build finished (the workers' merge matches by key) */
    if (fail_key && t[idx].key == fail_key) t[idx].pso = NULL;   /* ... or failed: NULL, pending cleared */
    return t[idx].pso;
}
#ifdef RC_PSO_TAIL_PRE7082
/* Negative control: the tail as it was before 7082 (rsx_live_draw.c get_pso, 7050/7054). */
static void* tail_pre7082(uint32_t slot, int active, void (*scan)(void), void* (*await_slot)(uint32_t))
{
    (void)slot;
    if (active) return NULL;
    scan();
    return await_slot(n - 1u);
}
#define TAIL tail_pre7082
#else
#define TAIL rsx_pso_demand_tail
#endif
static void* get_pso(uint64_t key, int pending)
{
    for (uint32_t i = 0; i < n; i++)
        if (t[i].key == key) return t[i].pending ? (ahead_active ? NULL : fake_await(i)) : t[i].pso;
    const uint32_t slot = insert(key, pending);
    if (pending) return TAIL(slot, ahead_active, fake_scan, fake_await);
    return t[slot].pso;
}

static int fails;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); fails++; } } while (0)
static void reset(uint32_t prefix)
{
    memset(t, 0, sizeof t); n = 0; ahead_active = 0; scan_calls = 0; scan_adds = 0; await_calls = 0; fail_key = 0;
    for (uint32_t i = 0; i < prefix; i++) insert(0x9000u + i, 0);   /* pipelines already built */
}

int main(void)
{
    /* 1. A pending miss whose scan appends 3 entries (FFA frame 42/43: the scan submitted builds), after cache
     *    prefixes of 0, 3 and 14 finished pipelines. */
    const uint32_t prefixes[3] = { 0, 3, 14 };
    for (int p = 0; p < 3; p++) {
        reset(prefixes[p]); scan_adds = 3; scan_key0 = 0xA1;
        const uint32_t own = n;
        void* r = get_pso(0x10, 1);
        CHECK(r == &obj[own], "case 1: the miss gets its own pipeline, not the last lookahead entry");
        CHECK(await_calls == 1 && awaited[0] == own, "case 1: the await is for the draw's own slot");
        CHECK(n == own + 4u && scan_calls == 1, "case 1: one scan, three lookahead entries appended");
        CHECK(t[own + 1].ahead && t[own + 3].ahead && t[own + 3].pending, "case 1: lookahead entries stay pending");
    }
    /* 2. A pending miss whose scan appends nothing: its own slot (also right before 7082). */
    reset(5); scan_adds = 0;
    CHECK(get_pso(0x20, 1) == &obj[5] && awaited[0] == 5 && n == 6, "case 2: empty scan, own slot");
    /* 3. A miss whose build is not pending (synchronous build): its pipeline, no scan. */
    reset(2); scan_adds = 3; scan_key0 = 0xB1;
    CHECK(get_pso(0x30, 0) == &obj[2] && scan_calls == 0 && await_calls == 0, "case 3: not pending, no scan");
    /* 4. Inside a scan (ahead_active): NULL, no nested scan, no await. */
    reset(0); scan_adds = 3; ahead_active = 1;
    CHECK(rsx_pso_demand_tail(0, 1, fake_scan, fake_await) == NULL && scan_calls == 0 && await_calls == 0,
          "case 4: a lookahead miss only submits");
    ahead_active = 0;
    /* 5. Two misses in a row, each scan appending builds: each gets its own pipeline. */
    reset(1); scan_adds = 2; scan_key0 = 0xC1;
    void* r1 = get_pso(0x40, 1);   /* slot 1, scan adds 2..3 */
    scan_key0 = 0xD1;
    void* r2 = get_pso(0x41, 1);   /* slot 4, scan adds 5..6 */
    CHECK(r1 == &obj[1] && r2 == &obj[4], "case 5: two misses, each its own pipeline");
    CHECK(await_calls == 2 && awaited[0] == 1 && awaited[1] == 4, "case 5: awaits for slots 1 then 4");
    /* 6. A later real draw that takes a lookahead entry waits for that entry (unchanged path, matched by key). */
    CHECK(get_pso(0xC2, 1) == &obj[3] && awaited[2] == 3, "case 6: a lookahead entry is found by its key");
    /* 7. Failed async builds (the merge stores NULL and clears pending). A failed demand build is NULL, never a
     *    lookahead entry's pipeline; a failed lookahead entry does not suppress the demand draw's pipeline. */
    reset(2); scan_adds = 2; scan_key0 = 0xE1; fail_key = 0x50;
    CHECK(get_pso(0x50, 1) == NULL && awaited[0] == 2, "case 7: a failed demand build is NULL, not a borrowed pipeline");
    reset(2); scan_adds = 2; scan_key0 = 0xE1; fail_key = 0xE2;   /* the last lookahead entry fails */
    CHECK(get_pso(0x51, 1) == &obj[2] && awaited[0] == 2, "case 7: a failed lookahead entry does not hide the demand pipeline");
    /* 8. The same key asked again (a repeat of the miss, or the lookahead meeting the demand key) finds the demand
     *    entry; a key repeated inside the scan neither appends a second entry nor waits. */
    reset(0); scan_adds = 3; scan_key0 = 0x60;
    void* a = get_pso(0x60, 1);   /* demand key == the scan's first key: found pending inside the scan, NULL, no entry */
    CHECK(a == &obj[0] && n == 3u, "case 8: a lookahead key equal to the demand key adds no second entry");
    CHECK(get_pso(0x60, 1) == &obj[0] && t[0].pending == 0, "case 8: the demand key is a plain hit afterwards");
    /* 9. The cache nearly full: the demand entry still fits (the caller checks n < MAX before the store), the slot
     *    stays valid whatever the scan appends. */
    reset(MAX - 6); scan_adds = 4; scan_key0 = 0x70;
    { const uint32_t own = n; CHECK(get_pso(0x7F, 1) == &obj[own] && awaited[0] == own, "case 9: slot valid at a nearly full table"); }

    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("pso demand slot test: ok\n");
    return 0;
}
