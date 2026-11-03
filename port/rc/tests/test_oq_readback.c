/* Host test for R&C patch 7017 (libs/video/rsx_oq_rb.h): ZPASS report readback when the title reuses a report
 * offset for different objects. A model of KZ3 patch 2027's bookkeeping (slot per offset, readback ring drained a
 * frame later, walk-time value) with the real decision helpers. Scenario (ToD-like): two report offsets; each frame
 * the game gives them to {Ratchet (visible, 900 samples), a crate behind a wall (0 samples)} in an order that swaps
 * every frame; the game reads a report after the RSX passed its GET_REPORT (here: right after the walk, and again
 * after the late drain). Ratchet must never read 0. Base (2027) shows the wrong 0s; 7017 shows none. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rsx_oq_rb.h"

#define RING 64
typedef struct { uint32_t off, count; int guest; } ent_t;
typedef struct { int has_val; uint32_t val; uint64_t rb, guest_rb; } slot_t;
typedef struct { ent_t e[RING]; uint64_t head, tail; slot_t slot[2]; uint32_t guest_mem[2]; } model_t;

static void get_report(model_t* m, int fixed, uint32_t off, uint32_t true_count)
{
    const uint64_t h = m->head++;                       /* sink_report: queue the readback */
    m->e[h % RING].off = off; m->e[h % RING].count = true_count; m->e[h % RING].guest = 0;
    m->slot[off].rb = h + 1;
    /* walker: rsx_live_draw_report_value */
    m->e[(m->slot[off].rb - 1) % RING].guest = 1;
    m->slot[off].guest_rb = m->slot[off].rb; m->slot[off].rb = 0;
    m->guest_mem[off] = ld_rbk_walk_value(fixed, m->slot[off].has_val, m->slot[off].val, 0xFFFFu);
}
static void drain(model_t* m, int fixed, uint64_t upto)   /* entries < upto have finished on the GPU */
{
    while (m->tail < upto && m->tail < m->head) {
        ent_t* e = &m->e[m->tail % RING];
        m->slot[e->off].val = e->count; m->slot[e->off].has_val = 1;
        if (ld_rbk_drain_to_guest(fixed, e->guest, m->tail, m->slot[e->off].guest_rb)) m->guest_mem[e->off] = e->count;
        m->tail++;
    }
}

static int run(int fixed)
{
    model_t m; memset(&m, 0, sizeof m);
    int wrong = 0, reads = 0;
    uint64_t frame_end[64];
    for (int f = 0; f < 40; f++) {
        const uint32_t ratchet_off = (uint32_t)(f & 1), crate_off = ratchet_off ^ 1u;
        get_report(&m, fixed, crate_off, 0);
        get_report(&m, fixed, ratchet_off, 900);
        frame_end[f] = m.head;
        /* the game reads Ratchet's report once the RSX passed it (walked) */
        reads++; if (m.guest_mem[ratchet_off] == 0) wrong++;
        /* present flush: frame f-1 has finished on the GPU, its counts land now */
        if (f > 0) drain(&m, fixed, frame_end[f - 1]);
        reads++; if (m.guest_mem[ratchet_off] == 0) wrong++;
    }
    printf("%s: %d of %d reads of Ratchet's report gave 0\n", fixed ? "7017" : "base (2027)", wrong, reads);
    return wrong;
}

int main(void)
{
    int fail = 0;
    const int base = run(0), fixed = run(1);
    if (base == 0) { printf("FAIL the base did not show a wrong 0 (model does not reproduce)\n"); fail++; }
    if (fixed != 0) { printf("FAIL 7017 still gives Ratchet 0\n"); fail++; }
    /* a stable offset (one object) still gets its real count through the drain under 7017 */
    model_t m; memset(&m, 0, sizeof m);
    get_report(&m, 1, 0, 0); drain(&m, 1, m.head);
    if (m.guest_mem[0] != 0) { printf("FAIL a real 0 did not reach the guest\n"); fail++; }
    get_report(&m, 1, 0, 37); if (m.guest_mem[0] != 0xFFFFu) { printf("FAIL walk-time value is not 'visible'\n"); fail++; }
    drain(&m, 1, m.head); if (m.guest_mem[0] != 37) { printf("FAIL count 37 did not land\n"); fail++; }
    printf("%s (%d failures)\n", fail ? "FAIL" : "PASS", fail);
    return fail != 0;
}
