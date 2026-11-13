/* Host test for patch 7053 (a List/Fixed savedata call never returns faster than the console's lower bound): unity
 * build of the patched libs/system/cellSaveData.c. A Fixed-style call (savedata_execute_dialog) whose funcStat ends the
 * sequence (OK_LAST, nothing written) must take >= 100 ms; with something written (the modified flag) >= 500 ms; the
 * plain savedata_execute (Auto entries) is not delayed. Run with tests/run_imports_test.sh test_savedata_dialog_bound.c. */
#include "libs/system/cellSaveData.c"
#include <assert.h>
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
void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*)) { (void)nid; (void)name; (void)fn; }
ps3_guest_caller_fn g_ps3_guest_caller;

static int stat_calls;
static void guest_callback(u32 opd, u64 cb, u64 get, u64 set, u64 a, u64 b, u64 c, u64 d, u64 e)
{
    (void)opd; (void)get; (void)set; (void)a; (void)b; (void)c; (void)d; (void)e;
    stat_calls++;
    vm_write32(cb, 1);                  /* CELL_SAVEDATA_CBRESULT_OK_LAST: nothing to write */
}

static double ms_since(uint64_t t0) { return (double)(savedata_now_us() - t0) / 1000.0; }

int main(void)
{
    char root[] = "/tmp/rc-savedata-bound-XXXXXX";
    assert(mkdtemp(root)); strcpy(s_save_root, root);
    vm_base = calloc(1, 0x200000); assert(vm_base);
    assert(cellSaveData_set_scratch_region(0x100000, 0x20000) == CELL_OK);
    vm_write32(0x200, 1); vm_write32(0x204, 4);   /* setBuf: dirListMax, fileListMax */
    g_ps3_guest_caller = guest_callback;

    /* 1. Auto path: no bound. */
    uint64_t t0 = savedata_now_us();
    s32 r = savedata_execute("TEST00", 1, (CellSaveDataSetBuf*)(uintptr_t)0x200, (CellSaveDataStatCallback)(uintptr_t)0x800,
                             (CellSaveDataFileCallback)(uintptr_t)0x900, (void*)(uintptr_t)0x10);
    double ms = ms_since(t0);
    printf("auto: r=%d %.1f ms\n", r, ms);
    assert(r == CELL_OK && stat_calls == 1 && ms < 90.0);

    /* 2. Dialog path, nothing written: >= 100 ms, well under the 500 ms of a write. */
    t0 = savedata_now_us();
    r = savedata_execute_dialog("TEST00", 1, (CellSaveDataSetBuf*)(uintptr_t)0x200, (CellSaveDataStatCallback)(uintptr_t)0x800,
                                (CellSaveDataFileCallback)(uintptr_t)0x900, (void*)(uintptr_t)0x10);
    ms = ms_since(t0);
    printf("dialog, unmodified: r=%d %.1f ms\n", r, ms);
    assert(r == CELL_OK && stat_calls == 2 && ms >= 100.0 && ms < 450.0);

    /* 3. The bound for a written save: 500 ms. */
    s_savedata_modified = 1;
    t0 = savedata_now_us();
    savedata_console_bound(t0 + 500000ull);
    ms = ms_since(t0);
    printf("bound 500: %.1f ms\n", ms);
    assert(ms >= 500.0 && ms < 900.0);
    printf("PASS\n");
    return 0;
}
