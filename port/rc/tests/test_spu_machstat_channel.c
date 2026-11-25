/* R&C patch 7105 host test against the REAL runtime: spu_rdch(ctx, SPU_RdMachStat) in spu_channels.c, the SPU
 * interpreter's rdch and bi-family E/D bits (spu_interp.c), and interrupt entry (spu_drain.c spu_take_interrupt).
 * Built like upstream's runtime/spu/tests/test_spu_machine_status.c (same link set; that fixture does not build on the
 * patched tree since KZ3 patch 1017 turned g_spu_trampoline_fn into a ctx-> field, so this one keeps `ctx` a pointer).
 *
 * Cases (CBEA v1.02 s.9.8: bit 31 = interrupt enable, set/cleared only by a taken bi-family E/D branch and cleared by
 * interrupt entry):
 *   1. channel: every run state x IE 0/1 reads IE (not the run state); the read changes nothing.
 *   2. interpreter, Insomniac WWS job-API shape (A4O pm 0xDAE900 0x17F4: `rdch $2,MachStat; andi $2,$2,1;
 *      brnz $2,0x1714`, 0x1714 = `stop 0`): entered with interrupts on through `bisld` (D), it must not take the trap.
 *   3. interpreter, KZ3 AddJob save/restore shape (`rdch; andi 1; bid; ...; re-enable only if it was set`): entered with
 *      IE 0 it leaves IE 0, entered with IE 1 it leaves IE 1.
 *   4. interrupt entry: a pending enabled event with `bra` at LS 0 clears IE; the handler reads 0 (A4O pm 0x25D4 asserts
 *      that, else `stop` at 0x1790); its `irete` sets IE again.
 * KZ3_SPU_MACHSTAT_IE=0 (the old "1 while running" rule) must FAIL cases 1, 2, 3 and 4: the negative control
 * (port/rc/tests/run_spu_machstat_test.sh). */
#include "spu_helpers.h"
#include "spu_interp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t* vm_base;
uint32_t ppu_vm_size;
extern u128 spu_rdch(spu_context*, uint32_t);
extern uint32_t spu_rchcnt(spu_context*, uint32_t);

static spu_context g_ctx;
static spu_context* const ctx = &g_ctx;
static unsigned passes, fails;
#define CHECK(c, ...) do { if (c) passes++; else { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void word(uint32_t pc, uint32_t op)
{
    ctx->ls[pc] = op >> 24; ctx->ls[pc + 1] = op >> 16; ctx->ls[pc + 2] = op >> 8; ctx->ls[pc + 3] = op;
}
static uint32_t run(uint32_t start)
{
    ctx->image_id = -1; ctx->status = SPU_STATUS_RUNNING;   /* no lifted image: the interpreter runs it all */
    return spu_interp_run(ctx, start);
}

/* encodings (SPU ISA 1.2) */
#define RDCH_MACHSTAT(rt)  (0x01A00680u | (rt))                          /* rdch rt, ch13 */
#define ANDI1(rt, ra)      (0x14000000u | (1u << 14) | ((ra) << 7) | (rt))
#define BRNZ(rt, from, to) (0x21000000u | ((((uint32_t)((to) - (from)) >> 2) & 0xFFFFu) << 7) | (rt))
#define BRZ(rt, from, to)  (0x20000000u | ((((uint32_t)((to) - (from)) >> 2) & 0xFFFFu) << 7) | (rt))
#define BI(ra)             (0x35000000u | ((ra) << 7))
#define BISL(rt, ra)       (0x35200000u | ((ra) << 7) | (rt))
#define E_BIT              0x40000u
#define D_BIT              0x80000u
#define STOP(c)            (0x00000000u | (c))
#define BRA(to)            (0x30000000u | ((((to) >> 2) & 0xFFFFu) << 7))

int main(void)
{
    spu_context_init(ctx, 0);
    const char* e = getenv("KZ3_SPU_MACHSTAT_IE");
    printf("test_spu_machstat_channel: KZ3_SPU_MACHSTAT_IE=%s\n", e ? e : "(unset: tracked IE, the 7105 default)");

    /* 1. the channel itself */
    const unsigned states[] = { SPU_STATUS_RUNNING, SPU_STATUS_STOPPED, SPU_STATUS_STOPPED_BY_STOP,
                                SPU_STATUS_STOPPED_BY_HALT, SPU_STATUS_WAITING_CHANNEL };
    for (unsigned s = 0; s < sizeof states / sizeof states[0]; s++)
        for (unsigned ie = 0; ie < 2; ie++) {
            ctx->status = states[s]; ctx->int_enable = ie;
            const uint32_t v = spu_rdch(ctx, SPU_RdMachStat)._u32[0];
            CHECK(v == ie, "channel: status=0x%X IE=%u read 0x%X (want %u)", states[s], ie, v, ie);
            CHECK(ctx->int_enable == ie, "channel: the read changed IE");
            CHECK(spu_rchcnt(ctx, SPU_RdMachStat) == 1, "channel: rchcnt != 1");
        }

    /* 2. WWS job-API assert, entered by `bisld $0,$5` from a job running with interrupts on */
    word(0x4000, BISL(0, 5) | D_BIT);
    word(0x4100, RDCH_MACHSTAT(2));
    word(0x4104, ANDI1(2, 2));
    word(0x4108, BRNZ(2, 0x4108, 0x4200));
    word(0x410C, STOP(0x2001));          /* the job API returns normally */
    word(0x4200, STOP(0x0000));          /* the PM's stop-0 trap (A4O 0x1714) */
    for (int job = 0; job < 4; job++) {
        ctx->int_enable = 1;             /* the PM's `bisle` into the job (A4O pm 0x35EC) */
        ctx->gpr[5] = spu_splat_u32(0x4100);
        const uint32_t code = run(0x4000);
        CHECK(code == 0x2001, "WWS job-API assert: job %d stopped with code 0x%X at the trap (want 0x2001)", job, code);
        CHECK(ctx->int_enable == 0, "WWS job-API assert: bisld left IE %u", ctx->int_enable);
    }

    /* 3. AddJob save/restore: save IE, bid, re-enable with bie only if it was set */
    word(0x4400, RDCH_MACHSTAT(3));
    word(0x4404, ANDI1(3, 3));
    word(0x4408, BI(6) | D_BIT);         /* bid -> 0x440C */
    word(0x440C, BRZ(3, 0x440C, 0x4414));
    word(0x4410, BI(7) | E_BIT);         /* bie -> 0x4414 */
    word(0x4414, STOP(0x2002));
    for (unsigned ie = 0; ie < 2; ie++) {
        ctx->int_enable = ie;
        ctx->gpr[6] = spu_splat_u32(0x440C);
        ctx->gpr[7] = spu_splat_u32(0x4414);
        const uint32_t code = run(0x4400);
        CHECK(code == 0x2002, "AddJob shape: stop code 0x%X", code);
        CHECK(ctx->int_enable == ie, "AddJob shape: entered with IE %u, left IE %u", ie, ctx->int_enable);
    }

    /* 4. interrupt entry clears IE; the handler's assert reads 0; irete sets it */
    memset(ctx->ls, 0, 16);
    word(0x0000, BRA(0x4800));           /* the PM's vector `bra 0xA2C` (A4O entry 0x2ECC) */
    ctx->int_enable = 1; ctx->event_mask = 0x2; ctx->event_status = 0x2; ctx->pc = 0x4900; ctx->status = SPU_STATUS_RUNNING;
    spu_take_interrupt(ctx, spu_indirect_branch);
    CHECK(ctx->int_enable == 0, "interrupt entry left IE %u", ctx->int_enable);
    CHECK((ctx->pc & SPU_LS_MASK) == 0x4800 && (ctx->srr0 & SPU_LS_MASK) == 0x4900,
          "interrupt entry: pc 0x%X srr0 0x%X", ctx->pc, ctx->srr0);
    ctx->irq_saved = 0; ctx->event_status = 0; ctx->event_mask = 0;
    word(0x4800, RDCH_MACHSTAT(2));
    word(0x4804, ANDI1(2, 2));
    word(0x4808, BRNZ(2, 0x4808, 0x4880));
    word(0x480C, STOP(0x2003));          /* handler body ran */
    word(0x4880, STOP(0x0001));          /* the handler's own trap (A4O 0x1790) */
    const uint32_t hcode = run(0x4800);
    CHECK(hcode == 0x2003, "interrupt handler assert: stop code 0x%X (want 0x2003)", hcode);
    word(0x4A00, 0x35400000u | E_BIT);   /* irete */
    word(0x4B00, STOP(0x2004));
    ctx->srr0 = 0x4B00;
    const uint32_t icode = run(0x4A00);
    CHECK(icode == 0x2004 && ctx->int_enable == 1, "irete: stop 0x%X IE %u", icode, ctx->int_enable);
    ctx->status = SPU_STATUS_RUNNING;
    CHECK(spu_rdch(ctx, SPU_RdMachStat)._u32[0] == 1, "after irete the channel reads 0");

    printf("test_spu_machstat_channel: %u passed, %u failed\n", passes, fails);
    return fails ? 1 : 0;
}

/* link stubs (as upstream's test_spu_machine_status.c): none of these is reached by the paths above */
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
