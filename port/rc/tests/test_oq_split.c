/* Host test for R&C patch 2173 (libs/video/rsx_oq_split.h): chunk submits split an open ZPASS query into host
 * segments. A model of rsx_live_draw.c's walker (draw counter and LD_SUBMIT_CHUNK gate, ld_oq_before_draw's query
 * open, flushes, sink_report) drives the real helpers, with a per-query sample count, and checks:
 *  1. 9000 draws under one ZPASS span, chunk 128: mode 1 submits 69 times (each a host split), one game segment, and
 *     the report sums all 70 host counts; mode 0 refuses 8872 times and submits never (today's loss).
 *  2. a report after 5 host splits: PENDING, first = the BEGIN's serial, k = 5, sum = the 5 counts; saturation.
 *  3. a flush inside the game segment: ALWAYS (segs 2) in mode 0 and mode 1 alike; mode 2 keeps it PENDING.
 *  4. resolve / readback runs with the query-heap and readback-ring wraps; a full ring drops the report.
 *  5. the index-reuse guard tests the first host segment, not the last.
 *  6. random traces (ZPASS on/off, clears, draws, flushes, reports): mode 1 gives every report the same state and the
 *     same count as mode 0. Mode 2 keeps every mode-1 PENDING report and its count.
 * Built with -DOQS_MUTANT_REFUSE (the gate refuses under an open query) case 1 fails; with -DOQS_MUTANT_LAST (the
 * guard tests the last host segment) case 5 fails. run_oq_split_test.sh requires both failures. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rsx_oq_split.h"

#define QN 8192u    /* LD_OQ_N */
#define RN 4096u    /* LD_RB_N */
#define CHUNK 128u  /* LD_SUBMIT_CHUNK default */
#define MAXQ 200000u

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

typedef struct {
    oqs_seg_t s;
    int mode, zpass;
    uint32_t unsub;
    uint64_t cnt[MAXQ];   /* samples per query serial */
    uint64_t submits, refused, splits, game_opens;
} sim_t;

static void sim_init(sim_t* m, int mode) { memset(m, 0, sizeof *m); m->mode = mode; }

/* sink_end_impl: the chunk gate (before the draw records), then ld_oq_before_draw's open, then the draw counts. */
static void sim_draw(sim_t* m, uint64_t samples)
{
    if (++m->unsub > CHUNK) {
        const int g = oqs_gate(1, m->unsub, m->s.open, m->mode);
        if (g == OQS_REFUSE_OPEN) m->refused++;
        else if (g == OQS_SUBMIT || g == OQS_SUBMIT_REOPEN) {
            if (g == OQS_SUBMIT_REOPEN) { oqs_close(&m->s); oqs_host_reopen(&m->s); m->splits++; }
            m->unsub = 0; m->submits++;
        }
    }
    if (m->zpass && !m->s.open) { int game; oqs_open_for_draw(&m->s, m->mode, &game); m->game_opens += game; }
    if (m->s.open && m->s.open_serial < MAXQ) m->cnt[m->s.open_serial] += samples;
}
static void sim_flush(sim_t* m) { oqs_flush_close(&m->s, m->mode); m->unsub = 0; }
static void sim_zpass(sim_t* m, int on) { m->zpass = on; if (!on) oqs_zpass_off(&m->s); }
static void sim_clear(sim_t* m) { oqs_clear(&m->s); }
/* sink_report: close, decide; *val = the count the readback group would sum (UINT32_MAX-saturated). */
static oqs_report_t sim_report(sim_t* m, uint32_t type, uint32_t* val)
{
    oqs_close(&m->s);
    const oqs_report_t r = oqs_report(&m->s, type, QN);
    *val = 0;
    if (r.state == OQS_R_PENDING) {
        uint64_t sum = 0;
        for (uint32_t j = 0; j < r.k; j++) sum += m->cnt[r.first + j];
        *val = sum > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)sum;
    }
    return r;
}

static uint32_t rng = 12345u;
static uint32_t rnd(uint32_t n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }

static sim_t A, B, C;

static void case1(void)
{
    uint64_t total = 0;
    sim_init(&A, OQS_MODE_SPLIT); sim_init(&B, OQS_MODE_OFF);
    sim_clear(&A); sim_clear(&B); sim_zpass(&A, 1); sim_zpass(&B, 1);
    for (uint32_t d = 0; d < 9000u; d++) { const uint64_t c = d % 7u; total += c; sim_draw(&A, c); sim_draw(&B, c); }
    uint32_t va, vb;
    const oqs_report_t ra = sim_report(&A, 1, &va), rb = sim_report(&B, 1, &vb);
    printf("case 1: mode 1 submits %llu (host splits %llu, refused %llu), game segments %u, report k %u count %u;"
           " mode 0 submits %llu refused %llu, report k %u count %u (true %llu)\n",
           (unsigned long long)A.submits, (unsigned long long)A.splits, (unsigned long long)A.refused, A.s.segs, ra.k, va,
           (unsigned long long)B.submits, (unsigned long long)B.refused, rb.k, vb, (unsigned long long)total);
    CHECK(A.submits == 69 && A.splits == 69 && A.refused == 0, "case 1: mode 1 should submit 69 times with a host split each");
    CHECK(A.s.segs == 1 && A.game_opens == 1, "case 1: mode 1 should keep one game segment");
    CHECK(ra.state == OQS_R_PENDING && ra.k == 70 && va == total, "case 1: mode 1 report should be PENDING, k 70, the true count");
    CHECK(B.submits == 0 && B.refused == 8872, "case 1: mode 0 should refuse 8872 times");
    CHECK(rb.state == OQS_R_PENDING && rb.k == 1 && vb == total, "case 1: mode 0 report should be PENDING, k 1, the true count");
}

static void case2(void)
{
    sim_init(&A, OQS_MODE_SPLIT);
    sim_clear(&A); sim_zpass(&A, 1);
    for (uint32_t d = 0; d < 4u * (CHUNK + 1u) + 10u; d++) sim_draw(&A, 1);   /* 4 submits inside -> 5 host segments */
    const uint64_t begin = 0;   /* the first query of the run */
    uint32_t v;
    const oqs_report_t r = sim_report(&A, 1, &v);
    uint64_t sum = 0;
    for (uint32_t j = 0; j < 5; j++) sum += A.cnt[begin + j];
    printf("case 2: state %d first %llu k %u count %u (5 host counts sum %llu)\n", r.state,
           (unsigned long long)r.first, r.k, v, (unsigned long long)sum);
    CHECK(r.state == OQS_R_PENDING && r.first == begin && r.k == 5 && v == sum && sum == 4u * (CHUNK + 1u) + 10u,
          "case 2: report after 5 host splits");
    /* the readback group sum (ld_rb_drain), ring wrap and saturation */
    uint64_t map[RN]; memset(map, 0, sizeof map);
    for (uint32_t j = 0; j < 5; j++) map[(RN - 2u + j) % RN] = A.cnt[begin + j];
    CHECK(oqs_sum_sat(map, RN, RN - 2u, 5) == v, "case 2: the group sum across the ring wrap differs");
    map[0] = 0xFFFFFFF0ull; map[1] = 0x20ull;
    CHECK(oqs_sum_sat(map, RN, 0, 2) == 0xFFFFFFFFu, "case 2: the group sum does not saturate");
    map[0] = 0x100000000ull;
    CHECK(oqs_sum_sat(map, RN, 0, 1) == 0xFFFFFFFFu, "case 2: a single count does not saturate (2027 rule)");
    map[0] = 37;
    CHECK(oqs_sum_sat(map, RN, 0, 1) == 37u, "case 2: a single count changed (k 1 must be 2027's value)");
}

static void case3(void)
{
    for (int mode = 0; mode <= 2; mode++) {
        sim_init(&A, mode);
        sim_clear(&A); sim_zpass(&A, 1);
        for (uint32_t d = 0; d < 300u; d++) sim_draw(&A, 2);
        sim_flush(&A);
        for (uint32_t d = 0; d < 300u; d++) sim_draw(&A, 3);
        uint32_t v;
        const oqs_report_t r = sim_report(&A, 1, &v);
        printf("case 3: mode %d: flush inside the game segment -> state %d segs %u k %u count %u\n", mode, r.state,
               A.s.segs, r.k, v);
        if (mode < 2) CHECK(r.state == OQS_R_ALWAYS && A.s.segs == 2, "case 3: mode %d should give ALWAYS (segs 2)", mode);
        else CHECK(r.state == OQS_R_PENDING && v == 300u * 2u + 300u * 3u, "case 3: mode 2 should sum across the flush");
    }
}

static void case4(void)
{
    oqs_piece_t p[3];
    int n = oqs_pieces(8190, 5, QN, RN - 2u, RN, p);
    CHECK(n == 2 && p[0].q == 8190 && p[0].r == RN - 2u && p[0].n == 2 && p[1].q == 0 && p[1].r == 0 && p[1].n == 3,
          "case 4: query wrap with ring wrap at the same step");
    n = oqs_pieces(8190, 5, QN, RN - 1u, RN, p);
    CHECK(n == 3 && p[0].n == 1 && p[1].q == 8191 && p[1].r == 0 && p[1].n == 1 && p[2].q == 0 && p[2].r == 1 && p[2].n == 3,
          "case 4: query and ring wrapping at different steps");
    printf("case 4: runs (8190,k5,h%u) = %d: (%u,%u,%u) (%u,%u,%u) (%u,%u,%u)\n", RN - 1u, n, p[0].q, p[0].r, p[0].n,
           p[1].q, p[1].r, p[1].n, p[2].q, p[2].r, p[2].n);
    n = oqs_pieces(12, 1, QN, 77, RN, p);
    CHECK(n == 1 && p[0].q == 12 && p[0].r == 77 && p[0].n == 1, "case 4: k 1 must be 2027's single resolve");
    const uint64_t hd = 10000u;
    CHECK(oqs_rb_room(hd, hd - RN + 1u, RN, 1), "case 4: one free entry should take a k 1 report (2027 rule)");
    CHECK(!oqs_rb_room(hd, hd - RN, RN, 1), "case 4: a full ring should drop (2027 rule)");
    CHECK(!oqs_rb_room(hd, hd - RN + 4u, RN, 5), "case 4: 4 free entries should not take k 5");
    CHECK(oqs_rb_room(hd, hd - RN + 5u, RN, 5), "case 4: 5 free entries should take k 5");
}

static void case5(void)
{
    const uint64_t first = 1000, last = 1050, serial = first + QN + 10u;   /* first's index reused, last's not */
    const int live = oqs_range_live(first, last, serial, QN);
    printf("case 5: range [%llu, %llu] at serial %llu: live %d\n", (unsigned long long)first, (unsigned long long)last,
           (unsigned long long)serial, live);
    CHECK(!live, "case 5: the guard must see the first host segment's index reused");
    CHECK(oqs_range_live(first, first, first + QN - 1u, QN), "case 5: k 1, not yet reused");
    CHECK(!oqs_range_live(first, first, first + QN, QN), "case 5: k 1, reused (2025 rule)");
}

static void case6(void)
{
    sim_init(&A, OQS_MODE_OFF); sim_init(&B, OQS_MODE_SPLIT); sim_init(&C, OQS_MODE_SPLIT_FLUSH);
    sim_t* m[3] = { &A, &B, &C };
    uint64_t reports = 0, pending = 0, split_pending = 0, diff = 0, m2_kept = 0, m2_bad = 0, m2_more = 0;
    for (uint32_t ev = 0; ev < 1500000u; ev++) {
        const uint32_t x = rnd(1000);
        if (x < 970) { const uint64_t c = rnd(4) == 0 ? rnd(50) : 0; for (int i = 0; i < 3; i++) sim_draw(m[i], c); }
        else if (x < 973) { for (int i = 0; i < 3; i++) sim_flush(m[i]); }
        else if (x < 980) { const int on = (int)rnd(2); for (int i = 0; i < 3; i++) sim_zpass(m[i], on); }
        else if (x < 985) { for (int i = 0; i < 3; i++) sim_clear(m[i]); }
        else {
            const uint32_t type = rnd(8) ? 1u : 2u;
            uint32_t v[3]; oqs_report_t r[3];
            for (int i = 0; i < 3; i++) r[i] = sim_report(m[i], type, &v[i]);
            reports++;
            if (r[0].state != r[1].state || (r[0].state == OQS_R_PENDING && v[0] != v[1])) diff++;
            if (r[1].state == OQS_R_PENDING) {
                pending++; if (r[1].k > 1) split_pending++;
                if (r[2].state == OQS_R_PENDING && v[2] == v[1]) m2_kept++; else m2_bad++;
            } else if (r[2].state == OQS_R_PENDING) m2_more++;
        }
        if (A.s.serial >= MAXQ - 10u || C.s.serial >= MAXQ - 10u) break;
    }
    printf("case 6: %llu reports, %llu PENDING (%llu of them split), mode 0 vs 1 differences %llu;"
           " mode 2 keeps %llu, loses %llu, adds %llu PENDING; mode 1 submits %llu vs mode 0 %llu\n",
           (unsigned long long)reports, (unsigned long long)pending, (unsigned long long)split_pending,
           (unsigned long long)diff, (unsigned long long)m2_kept, (unsigned long long)m2_bad, (unsigned long long)m2_more,
           (unsigned long long)B.submits, (unsigned long long)A.submits);
    CHECK(diff == 0, "case 6: mode 1 changed %llu report outcomes or counts", (unsigned long long)diff);
    CHECK(split_pending > 100, "case 6: the trace has too few split reports to test anything");
    CHECK(m2_bad == 0, "case 6: mode 2 lost a mode-1 PENDING report");
    CHECK(B.submits > A.submits, "case 6: mode 1 should submit more often than mode 0");
}

/* 7. ZPASS off closes the query (review 2026-11-24): on, draws, off, on, draws -> two game segments in one report
 *    span, so the report is ALWAYS (a ZPASS-off that left the query open would give one segment and PENDING). */
static void case7(void)
{
    sim_init(&A, OQS_MODE_SPLIT);
    sim_clear(&A); sim_zpass(&A, 1);
    for (uint32_t d = 0; d < 10u; d++) sim_draw(&A, 1);
    sim_zpass(&A, 0);
    CHECK(!A.s.open, "case 7: ZPASS off left the query open");
    sim_zpass(&A, 1);
    for (uint32_t d = 0; d < 10u; d++) sim_draw(&A, 1);
    uint32_t v;
    const oqs_report_t r = sim_report(&A, 1, &v);
    printf("case 7: ZPASS off/on inside one report span -> state %d segs %u\n", r.state, (unsigned)A.s.segs);
    CHECK(r.state == OQS_R_ALWAYS, "case 7: ZPASS off/on inside one span must report ALWAYS");
}

int main(void)
{
    case1(); case2(); case3(); case4(); case5(); case6(); case7();
    printf("%s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
