/* Host test for R&C patch 7052 (libs/video/rsx_pso_prio.h): demand-first PSO build queue.
 * 1. Order: lower FIFO distance first, equal distances in submission order.
 * 2. Simulated level load: 3 workers busy, 100 lookahead builds queued, then the walker waits on a build. With the
 *    demand-first queue it starts as soon as the first running build ends (it waits only for builds already running).
 *    Negative control: the same load through a FIFO (2048's old queue) makes it wait behind the queued builds.
 * 3. Shared compiles: a demanded build that needs a blob owned by a far lookahead build takes the ownership; with one
 *    worker the simulation completes. Negative control: without the claim the one worker would wait for a compile that
 *    sits behind it in the queue (deadlock), which the simulation detects.
 * 4. Fuzz: random submissions, demands and pops over a small blob pool; after every step no queued job is ahead of the
 *    queued owner of a compile it needs, and every taken job's compiles were owned by itself or a job taken earlier.
 * 5. Worker count from the logical processor count.
 * 6. Template prewarm jobs (R&C patch 7061), the lowest class: queued before builds, they still come after every
 *    lookahead build and every demand; a template job is never promoted; at most tmpl_cap run at once, a build is
 *    still taken at the cap, and a finished template lets the next one run. Fuzz with a semaphore model: no template
 *    job is ever taken while a build is queued, the running count never exceeds the cap, and every job is taken in the
 *    end (no template is stranded by a pop declined at the cap). One worker: cap 0, no template job is taken.
 * No game data. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsx_pso_async.h"
#include "rsx_pso_prio.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct job {
    rsx_apq_node qn;   /* first member, as in ld_apso_job */
    int id;
    double dur, start, end;
    rsx_apso_blob* e;  /* one shared compile (NULL = none) */
    uint8_t own;
    int taken, done;
} job;

static rsx_apso_blobtab tab;
static rsx_apq q;
static uint64_t fifo_seq;

static void submit(job* j, uint64_t prio, int prio_on, const char* blob_src, int claim)
{
    j->e = NULL; j->own = 0;
    if (blob_src) {
        int own = 0;
        j->e = rsx_apso_blob_acquire(&tab, 'V', blob_src, (uint32_t)strlen(blob_src), (uint64_t)blob_src[0], &own);
        j->own = (uint8_t)own;
    }
    rsx_apq_insert(&q, &j->qn, prio_on ? prio : ++fifo_seq);
    if (j->e && j->own) { j->e->owner = &j->qn; j->e->owner_flag = &j->own; }
    if (claim && j->e && !j->own) rsx_apq_claim(&q, &j->qn, &j->own, &j->e->owner, &j->e->owner_flag);
}

static job* take(void)
{
    job* j = (job*)rsx_apq_pop(&q);
    if (j && j->e && j->own) { j->e->owner = NULL; j->e->owner_flag = NULL; }
    if (j) j->taken = 1;
    return j;
}

static void reset(void)
{
    rsx_apso_blob* e = tab.head;
    while (e) { rsx_apso_blob* n = e->next; free(e->src); free(e); e = n; }
    memset(&tab, 0, sizeof tab); memset(&q, 0, sizeof q); fifo_seq = 0;
}

/* Event simulation of K workers over the queue. A worker that takes a job needing a compile it does not own waits
 * until the owner job ends. Returns 0, or -1 when every worker waits on a compile no worker will run (deadlock). */
#define MAXJ 256
static job* all[MAXJ]; static int n_all;
static int simulate(int k, double t0, job** running, int n_running)
{
    job* w[8] = {0};
    double free_at[8];
    for (int i = 0; i < k; i++) { free_at[i] = t0; if (i < n_running) { w[i] = running[i]; free_at[i] = running[i]->end; } }
    for (int guard = 0; guard < 100000; guard++) {
        /* the worker that frees first takes the next job */
        int wi = 0;
        for (int i = 1; i < k; i++) if (free_at[i] < free_at[wi]) wi = i;
        if (w[wi]) { w[wi]->done = 1; w[wi] = NULL; }
        job* j = take();
        if (!j) {
            int busy = 0; for (int i = 0; i < k; i++) if (w[i]) busy = 1;
            if (!busy) return 0;
            free_at[wi] = 1e300; continue;
        }
        double start = free_at[wi];
        if (j->e && !j->own) {   /* wait for the owner's compile */
            job* o = NULL;
            for (int i = 0; i < n_all; i++) if (all[i]->e == j->e && all[i]->own && all[i] != j) o = all[i];
            if (!o || !o->taken) return -1;   /* the owner sits behind it in the queue: nobody will compile it */
            if (o->end > start) start = o->end;
        }
        j->start = start; j->end = start + j->dur;
        w[wi] = j; free_at[wi] = j->end;
    }
    return 0;
}

int main(void)
{
    /* 1. order */
    {
        reset();
        job a = {0}, b = {0}, c = {0}, d = {0};
        a.id = 1; b.id = 2; c.id = 3; d.id = 4;
        submit(&a, 50, 1, NULL, 1); submit(&b, 10, 1, NULL, 1); submit(&c, 30, 1, NULL, 1); submit(&d, 10, 1, NULL, 1);
        int order[4]; for (int i = 0; i < 4; i++) order[i] = ((job*)rsx_apq_pop(&q))->id;
        CHECK(order[0] == 2 && order[1] == 4 && order[2] == 3 && order[3] == 1, "pop order %d %d %d %d (want 2 4 3 1)",
              order[0], order[1], order[2], order[3]);
        CHECK(q.n_queued == 0 && !rsx_apq_pop(&q), "queue not empty after 4 pops");
    }

    /* 2. demand during a lookahead backlog: 3 running (end 0.4, 0.7, 1.0 s), 100 queued (0.3 s each) */
    for (int prio_on = 1; prio_on >= 0; prio_on--) {
        reset(); n_all = 0;
        static job run[3], la[100], dem;
        job* running[3];
        for (int i = 0; i < 3; i++) { memset(&run[i], 0, sizeof run[i]); run[i].end = 0.4 + 0.3 * i; run[i].taken = 1; running[i] = &run[i]; }
        for (int i = 0; i < 100; i++) { memset(&la[i], 0, sizeof la[i]); la[i].id = 100 + i; la[i].dur = 0.3;
                                         submit(&la[i], 1000 + (uint64_t)(100 - i), prio_on, NULL, 1); all[n_all++] = &la[i]; }
        memset(&dem, 0, sizeof dem); dem.id = 1; dem.dur = 0.3;
        submit(&dem, RSX_APQ_DEMAND, prio_on, NULL, 1); all[n_all++] = &dem;
        CHECK(simulate(3, 0.0, running, 3) == 0, "simulation deadlocked");
        const double first_free = 0.4;
        if (prio_on) {
            CHECK(dem.start <= first_free + 1e-9, "demand-first: the demanded build started at %.2f s, a worker was free at %.2f s",
                  dem.start, first_free);
            /* nearest first: la[99] has the lowest distance */
            CHECK(la[99].start <= la[0].start, "lookahead not nearest first (%.2f vs %.2f)", la[99].start, la[0].start);
            printf("demand-first: demanded build starts at %.2f s (first running build ends at %.2f s)\n", dem.start, first_free);
        } else {
            CHECK(dem.start > first_free + 1.0, "negative control: FIFO let the demand start at %.2f s, the test cannot see the queue",
                  dem.start);
            printf("negative control (FIFO): demanded build starts at %.2f s, behind 100 queued builds\n", dem.start);
        }
    }

    /* 2b. promotion: the walker reaches a draw whose lookahead build is still queued far back */
    {
        reset(); n_all = 0;
        static job la[20];
        for (int i = 0; i < 20; i++) { memset(&la[i], 0, sizeof la[i]); la[i].id = i; submit(&la[i], 10 + (uint64_t)i, 1, NULL, 1); }
        CHECK(rsx_apq_promote(&q, &la[17].qn) == 1, "promote of a queued build failed");
        CHECK(q.head == &la[17].qn && la[17].qn.prio == RSX_APQ_DEMAND, "promoted build is not at the front");
        job* t = take();
        CHECK(t == &la[17], "next build taken is %d, not the promoted 17", t ? t->id : -1);
        CHECK(rsx_apq_promote(&q, &la[17].qn) == 0, "promote of a taken build moved something");
    }

    /* 3. shared compile: far lookahead build owns the blob the demanded build needs; one worker */
    for (int claim = 1; claim >= 0; claim--) {
        reset(); n_all = 0;
        static job far, mid, dem;
        memset(&far, 0, sizeof far); memset(&mid, 0, sizeof mid); memset(&dem, 0, sizeof dem);
        far.id = 3; far.dur = 0.5; mid.id = 2; mid.dur = 0.5; dem.id = 1; dem.dur = 0.5;
        submit(&far, 900, 1, "X shared vertex shader", claim); all[n_all++] = &far;
        submit(&mid, 500, 1, NULL, claim); all[n_all++] = &mid;
        submit(&dem, RSX_APQ_DEMAND, 1, "X shared vertex shader", claim); all[n_all++] = &dem;
        const int r = simulate(1, 0.0, NULL, 0);
        if (claim) {
            CHECK(r == 0, "with the claim the one-worker simulation deadlocked");
            CHECK(dem.own == 1 && far.own == 0, "ownership did not move to the demanded build (dem %u far %u)", dem.own, far.own);
            CHECK(dem.start == 0.0 && far.start >= dem.end, "demanded build at %.2f, far at %.2f", dem.start, far.start);
            printf("shared compile: the demanded build took the compile; one worker completes\n");
        } else {
            CHECK(r == -1, "negative control: without the claim the simulation should deadlock (got %d)", r);
            printf("negative control (no claim): the demanded build waits on a compile queued behind it: deadlock\n");
        }
    }

    /* 4. fuzz: 300 rounds of up to 64 jobs */
    {
        static job js[MAXJ];
        const char* pool[] = { "A blob", "B blob", "C blob", "D blob", "E blob" };
        unsigned rng = 12345u;
        int bad = 0, steps = 0, tot_sub = 0, tot_taken = 0;
        unsigned long long tot_prom = 0, tot_claims = 0;
        for (int round = 0; round < 300 && !bad; round++) {
        reset(); n_all = 0;
        int n_sub = 0, taken_order[MAXJ], n_taken = 0;
        for (int it = 0; it < 200 && !bad; it++) {
            rng = rng * 1103515245u + 12345u;
            const unsigned op = (rng >> 16) % 10u;
            if (op < 5 && n_sub < 64) {
                job* j = &js[n_sub]; memset(j, 0, sizeof *j); j->id = n_sub;
                const unsigned r2 = (rng >> 8) % 7u;
                submit(j, (uint64_t)((rng >> 4) % 64u), 1, r2 < 5 ? pool[r2] : NULL, 1);
                all[n_all++] = j; n_sub++;
            } else if (op < 7 && n_sub) {
                job* j = &js[(rng >> 8) % (unsigned)n_sub];
                if (rsx_apq_promote(&q, &j->qn) && j->e && !j->own)
                    rsx_apq_claim(&q, &j->qn, &j->own, &j->e->owner, &j->e->owner_flag);
            } else {
                job* j = take();
                if (j) {
                    taken_order[n_taken++] = j->id;
                    if (j->e && !j->own) {   /* its compile's owner must have been taken already */
                        int ok = 0;
                        for (int i = 0; i < n_taken - 1; i++) if (js[taken_order[i]].e == j->e && js[taken_order[i]].own) ok = 1;
                        if (!ok) { bad = 1; printf("FAIL fuzz step %d: job %d taken before the owner of its compile\n", it, j->id); }
                    }
                }
            }
            /* invariant: no queued job is ahead of the queued owner of its compile */
            for (rsx_apq_node* n = q.head; n && !bad; n = n->next) {
                job* j = (job*)n;
                if (j->e && !j->own && j->e->owner && rsx_apq_before(&q, &j->qn, (rsx_apq_node*)j->e->owner)) {
                    bad = 1; printf("FAIL fuzz step %d: job %d queued ahead of its compile's owner\n", it, j->id);
                }
            }
            steps++;
        }
        tot_sub += n_sub; tot_taken += n_taken; tot_prom += q.promoted; tot_claims += q.claims;
        }
        if (bad) fails++;
        printf("fuzz: %d steps, %d submissions, %d taken, %llu promotions, %llu ownership moves\n", steps, tot_sub, tot_taken,
               tot_prom, tot_claims);
        CHECK(tot_claims > 100 && tot_prom > 100, "fuzz exercised too few claims (%llu) or promotions (%llu)", tot_claims, tot_prom);
    }

    /* 6. template prewarm jobs (R&C patch 7061) */
    {
        reset();
        job t[5], la40, la20, dem;
        memset(t, 0, sizeof t); memset(&la40, 0, sizeof la40); memset(&la20, 0, sizeof la20); memset(&dem, 0, sizeof dem);
        for (int i = 0; i < 5; i++) { t[i].id = 100 + i; rsx_apq_insert_template(&q, &t[i].qn); }
        la40.id = 40; la20.id = 20; dem.id = 1;
        submit(&la40, 40, 1, NULL, 1); submit(&la20, 20, 1, NULL, 1);
        submit(&dem, 900, 1, NULL, 1);   /* queued far, then the walker waits on it */
        CHECK(rsx_apq_promote(&q, &t[4].qn) == 0, "a template job was promoted");
        CHECK(rsx_apq_promote(&q, &dem.qn) == 1, "demand not promoted");
        CHECK(q.n_queued == 3 && q.n_tmpl == 5, "counts %u builds / %u templates (want 3 / 5)", q.n_queued, q.n_tmpl);
        q.tmpl_cap = 8;
        int order[8];
        for (int i = 0; i < 8; i++) {
            const uint32_t builds_before = q.n_queued;
            job* j = (job*)rsx_apq_pop(&q);
            order[i] = j ? j->id : -1;
            if (j && j->qn.cls == RSX_APQ_CLS_TEMPLATE)
                CHECK(builds_before == 0, "template %d taken while %u builds were queued", j->id, builds_before);
        }
        CHECK(order[0] == 1 && order[1] == 20 && order[2] == 40 && order[3] == 100 && order[7] == 104,
              "pop order %d %d %d %d .. %d (want 1 20 40 100 .. 104)", order[0], order[1], order[2], order[3], order[7]);
        CHECK(q.tmpl_running == 5 && !q.head, "running %u", q.tmpl_running);
        /* cap */
        reset();
        q.tmpl_cap = 2;
        for (int i = 0; i < 4; i++) { t[i].id = 100 + i; t[i].qn.cls = 0; rsx_apq_insert_template(&q, &t[i].qn); }
        job* a = (job*)rsx_apq_pop(&q); job* b = (job*)rsx_apq_pop(&q); job* c = (job*)rsx_apq_pop(&q);
        CHECK(a && b && a->id == 100 && b->id == 101 && !c, "cap 2: took %d %d %d", a ? a->id : -1, b ? b->id : -1, c ? c->id : -1);
        job lb = {0}; lb.id = 7; submit(&lb, 5, 1, NULL, 1);
        c = (job*)rsx_apq_pop(&q);
        CHECK(c && c->id == 7, "a build was not taken at the template cap");
        CHECK(rsx_apq_template_done(&q) == 1, "template done with two queued did not ask for a release");
        c = (job*)rsx_apq_pop(&q);
        CHECK(c && c->id == 102, "after a template finished the next one did not run");
        CHECK(rsx_apq_template_cap(0, 8) == 3 && rsx_apq_template_cap(0, 2) == 1 && rsx_apq_template_cap(6, 4) == 3 &&
              rsx_apq_template_cap(1, 1) == 0 && rsx_apq_template_cap(0, 1) == 0, "template cap");
        /* one worker: cap 0, no template job is ever taken (the review of rc-int-1600: cap 1 let one hold the only
         * worker the next demand needs); a build still is */
        reset();
        q.tmpl_cap = rsx_apq_template_cap(3, 1);
        t[0].id = 100; t[0].qn.cls = 0; rsx_apq_insert_template(&q, &t[0].qn);
        c = (job*)rsx_apq_pop(&q);
        CHECK(!c && q.tmpl_running == 0, "cap 0: a template job was taken");
        job lb1 = {0}; lb1.id = 8; submit(&lb1, 5, 1, NULL, 1);
        c = (job*)rsx_apq_pop(&q);
        CHECK(c && c->id == 8, "cap 0: the build was not taken");
    }
    {   /* 6b. fuzz with a semaphore model */
        static job pool[4000];
        job* run_t[64]; int n_run = 0;
        int bad = 0, n_tmpl_taken = 0, n_build_taken = 0, n_declined = 0;
        unsigned seed = 7061u;
        for (int round = 0; round < 50 && !bad; round++) {
            reset();
            q.tmpl_cap = 1u + (unsigned)(round % 4);
            int np = 0, tokens = 0, taken = 0; n_run = 0;
            for (int step = 0; step < 400 && !bad; step++) {
                seed = seed * 1103515245u + 12345u;
                const unsigned r = (seed >> 16) % 10u;
                if (r < 3 && np < 4000) {           /* template record found by the scout */
                    job* j = &pool[np++]; memset(j, 0, sizeof *j); j->id = np;
                    rsx_apq_insert_template(&q, &j->qn); tokens++;
                } else if (r < 5 && np < 4000) {    /* lookahead build or a demand */
                    job* j = &pool[np++]; memset(j, 0, sizeof *j); j->id = np;
                    rsx_apq_insert(&q, &j->qn, (r == 3) ? 0 : (uint64_t)((seed >> 4) % 1000u)); tokens++;
                    if (r == 3) rsx_apq_promote(&q, &j->qn);
                } else if (r < 8 && tokens) {       /* a worker wakes */
                    tokens--;
                    const uint32_t builds_before = q.n_queued;
                    job* j = (job*)rsx_apq_pop(&q);
                    if (!j) { n_declined++; if (builds_before) bad = 1; continue; }
                    j->taken = 1; taken++;
                    if (j->qn.cls == RSX_APQ_CLS_TEMPLATE) {
                        n_tmpl_taken++;
                        if (builds_before) { bad = 1; printf("FAIL fuzz: template taken with %u builds queued\n", builds_before); }
                        if (q.tmpl_running > q.tmpl_cap) { bad = 1; printf("FAIL fuzz: %u templates run, cap %u\n", q.tmpl_running, q.tmpl_cap); }
                        run_t[n_run++ & 63] = j;
                    } else n_build_taken++;
                } else if (q.tmpl_running) {        /* a template job ends */
                    if (rsx_apq_template_done(&q)) tokens++;
                }
            }
            /* drain: workers wake while tokens remain, running templates end */
            for (int guard = 0; guard < 100000 && (q.head || q.tmpl_running); guard++) {
                if (tokens) {
                    tokens--;
                    job* j = (job*)rsx_apq_pop(&q);
                    if (j) { j->taken = 1; taken++; if (j->qn.cls == RSX_APQ_CLS_TEMPLATE) n_tmpl_taken++; else n_build_taken++; }
                } else if (q.tmpl_running) {
                    if (rsx_apq_template_done(&q)) tokens++;
                } else break;
            }
            if (taken != np) { bad = 1; printf("FAIL fuzz round %d: %d of %d jobs taken (stranded)\n", round, taken, np); }
        }
        (void)run_t;
        if (bad) fails++;
        printf("template fuzz: %d templates and %d builds taken, %d pops declined at the cap\n", n_tmpl_taken, n_build_taken, n_declined);
        CHECK(n_declined > 50 && n_tmpl_taken > 1000, "template fuzz exercised too little (%d declined, %d templates)", n_declined, n_tmpl_taken);
    }

    /* 5. worker count */
    CHECK(rsx_apq_workers(20) == 8, "20 logical -> %d", rsx_apq_workers(20));
    CHECK(rsx_apq_workers(12) == 4, "12 logical -> %d", rsx_apq_workers(12));
    CHECK(rsx_apq_workers(8) == 2, "8 logical -> %d", rsx_apq_workers(8));
    CHECK(rsx_apq_workers(4) == 2, "4 logical -> %d", rsx_apq_workers(4));
    CHECK(rsx_apq_workers(64) == 8, "64 logical -> %d", rsx_apq_workers(64));

    reset();
    if (fails) { printf("%d FAILED\n", fails); return 1; }
    printf("test_pso_prio: all checks pass\n");
    return 0;
}
