/* Host test for patch 7034: (1) cellSpursGetInfo hands out the instance's SPU thread ids, (2) a write_snr to one of them
 * reaches the SNR of that (instance, SPU): pending when no policy-module run is on that SPU, delivered into the run's
 * context on entry, written straight into a running context, overwrite (1 deep), unread value given back on exit;
 * (3) spurs_resolve_pm gives an unlifted policy module the interpreter entry (image id -1), which interprets from LS 0xA00;
 * with RC_SPURS_PM_INTERP=0 (argv[1] = "off") it refuses as before; (4, patch 7037) the interpreted entry turns on blocking
 * channel reads (g_spu_force_ch_block) before the first instruction, unless RC_SPURS_PM_NOBLOCK is set (argv[1] = "noblock"). Unity build of the patched cellSpurs.c and spurs_policy.c
 * over a calloc'd guest window. Run with run_spurs_pm_interp_test.sh. */
#include "libs/spurs/cellSpurs.c"
#include "runtime/spu/spurs_policy.c"
#include "rc_vm_stubs.h"
int (*g_kz3_spurs_kpoll)(uint32_t, uint32_t, uint32_t, uint32_t*);   /* spu_channels.c in the runtime */

/* stand-ins for the runtime pieces these two files call on the tested paths */
int g_spu_force_ch_block;   /* spu_channels.c in the runtime */
static int g_block_at_interp = -1;
static uint32_t g_interp_start = 0xFFFFFFFFu; static int g_interp_calls;
uint32_t spu_interp_run(spu_context* c, uint32_t lsa) { (void)c; g_interp_start = lsa; g_interp_calls++; g_block_at_interp = g_spu_force_ch_block; return 0; }
uint64_t spu_workload_fingerprint(const void* d, size_t n) { (void)d; return 0x1234500000000000ull | n; }
spu_lifted_entry_fn spu_workload_find_img(uint64_t fp, int* img) { (void)fp; if (img) *img = 0; return NULL; }
static int g_wakes; void spu_ch_wake(spu_context* c) { (void)c; g_wakes++; }

#define EA_IG  0x00FC6380u
#define INFO   0x00010000u
#define PM_EA  0x00020000u

int main(int argc, char** argv)
{
    vm_base = calloc(1, 0x200000); assert(vm_base);
    const int off = argc > 1 && !strcmp(argv[1], "off");
    const int noblock = argc > 1 && !strcmp(argv[1], "noblock");

    /* an instance "ig" of 5 SPUs in slot 1, an empty slot 0 (as ACiT: "" first, then "ig") */
    s_inst[0].ea = 0x00FDF980; s_inst[0].nspus = 1; strcpy(s_inst[0].prefix, "");
    s_inst[1].ea = EA_IG; s_inst[1].nspus = 5; strcpy(s_inst[1].prefix, "ig");

    if (!off) {
        /* (1) GetInfo: nSpus, spuThreads[0..4] = 0x7E000100 + i, rows 5..7 zero */
        memset(vm_base + INFO, 0xEE, 0x80);
        assert(cellSpursGetInfo(EA_IG, INFO) == CELL_OK);
        assert(vm_read32(INFO) == 5);
        for (u32 i = 0; i < 8; i++) assert(vm_read32(INFO + 0x28 + 4 * i) == (i < 5 ? 0x7E000100u + i : 0u));

        /* (2) SNR: unknown ids are refused */
        assert(rc_spurs_snr_hook(0x2000, 0, 1) != 0);              /* an lv2 SPU thread id */
        assert(rc_spurs_snr_hook(0x7E000105, 0, 1) != 0);          /* SPU 5 of a 5-SPU instance */
        assert(rc_spurs_snr_hook(0x7E000300, 0, 1) != 0);          /* instance slot 3 is free */
        /* the game's loop: SNR1 = i to SPU i, before any run */
        for (u32 i = 0; i < 5; i++) assert(rc_spurs_snr_hook(vm_read32(INFO + 0x28 + 4 * i), 0, i) == 0);
        assert(g_wakes == 0);
        spu_context* c = calloc(1, sizeof(spu_context)); assert(c);
        c->ch_sig_notify[0].count = 1; c->ch_sig_notify[0].value = 0x99;   /* stale state of a reused context: replaced */
        spurs_snr_enter(EA_IG, 3, c);
        assert(c->ch_sig_notify[0].count == 1 && spu_channel_read(&c->ch_sig_notify[0]) == 3);
        assert(c->ch_sig_notify[0].count == 0 && c->ch_sig_notify[1].count == 0);
        /* a write while the run is on SPU 3 goes into the context and wakes it; overwrite mode, 1 deep */
        assert(rc_spurs_snr_hook(0x7E000103, 0, 0x10) == 0 && g_wakes == 1);
        assert(rc_spurs_snr_hook(0x7E000103, 0, 0x20) == 0 && g_wakes == 2);
        assert(c->ch_sig_notify[0].count == 1 && c->ch_sig_notify[0].value == 0x20);
        /* SNR2 too */
        assert(rc_spurs_snr_hook(0x7E000103, 1, 0x5) == 0 && c->ch_sig_notify[1].count == 1);
        /* the run ends without reading: the values stay with SPU 3, the next run there gets them */
        spurs_snr_leave(EA_IG, 3, c);
        assert(c->ch_sig_notify[0].count == 0 && c->ch_sig_notify[1].count == 0);
        spu_context* d = calloc(1, sizeof(spu_context)); assert(d);
        spurs_snr_enter(EA_IG, 3, d);
        assert(spu_channel_read(&d->ch_sig_notify[0]) == 0x20 && spu_channel_read(&d->ch_sig_notify[1]) == 0x5);
        spurs_snr_leave(EA_IG, 3, d);
        spurs_snr_enter(EA_IG, 3, c);                                /* everything was read: nothing pending */
        assert(c->ch_sig_notify[0].count == 0 && c->ch_sig_notify[1].count == 0);
        spurs_snr_leave(EA_IG, 3, c);
        /* the other SPUs still hold their own values; another instance's SPU 0 is separate */
        spurs_snr_enter(EA_IG, 0, d); assert(spu_channel_read(&d->ch_sig_notify[0]) == 0); assert(d->ch_sig_notify[0].count == 0);
        spurs_snr_leave(EA_IG, 0, d);
        spurs_snr_enter(EA_IG, 4, d); assert(d->ch_sig_notify[0].count == 1 && d->ch_sig_notify[0].value == 4); spurs_snr_leave(EA_IG, 4, d);
        spurs_snr_enter(0x00FDF980, 0, c); assert(c->ch_sig_notify[0].count == 0); spurs_snr_leave(0x00FDF980, 0, c);
        /* pending writes overwrite each other (no queue) */
        assert(rc_spurs_snr_hook(0x7E000101, 0, 7) == 0 && rc_spurs_snr_hook(0x7E000101, 0, 8) == 0);
        spurs_snr_enter(EA_IG, 1, c);
        assert(c->ch_sig_notify[0].count == 1 && spu_channel_read(&c->ch_sig_notify[0]) == 8 && c->ch_sig_notify[0].count == 0);
        spurs_snr_leave(EA_IG, 1, c);
        printf("PASS getinfo ids + snr routing\n");
    }

    /* (3) an unlifted policy module */
    for (u32 i = 0; i < 0x230; i += 4) vm_write32(PM_EA + i, 0x40800000u + i);
    s_workloads[2].in_use = 1; s_workloads[2].pm = (const void*)(uintptr_t)PM_EA; s_workloads[2].sizePm = 0x230;
    WklPm* r = spurs_resolve_pm(2);
    if (off) {
        assert(r == NULL);
        printf("PASS off: unlifted policy module refused\n");
    } else {
        assert(r && r->fn == spurs_pm_interp_entry && r->image_id == -1 && r->resolved == 1);
        assert(spurs_resolve_pm(2) == r);                             /* cached */
        spu_context* c = calloc(1, sizeof(spu_context)); assert(c);
        assert(g_spu_force_ch_block == 0);
        r->fn(c);
        assert(g_interp_calls == 1 && g_interp_start == 0xA00);
        printf("PASS unlifted policy module -> interpreter from LS 0xA00, image -1\n");
        if (noblock) { assert(g_block_at_interp == 0); printf("PASS noblock: channel reads left non-blocking\n"); }
        else { assert(g_block_at_interp == 1); printf("PASS 7037: blocking channel reads on before the first instruction\n"); }
    }
    return 0;
}
