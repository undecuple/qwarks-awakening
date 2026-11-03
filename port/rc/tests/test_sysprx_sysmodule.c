/* Host test for patches 7018 (sys_prx_load_module, ..._on_memcontainer, sys_prx_register_library) and 7019
 * (cellSysmoduleInitialize / Finalize state): unity build of the patched sources. Run with tests/run_imports_test.sh. */
#include "libs/system/sysPrxForUser.c"
#include "libs/system/cellSysmodule.c"
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
ps3_guest_caller_fn g_ps3_guest_caller;

static void put(u32 ea, const char* s) { strcpy((char*)yz_g2h((void*)(uintptr_t)ea), s); }

int main(void)
{
    vm_base = calloc(1, 0x10000); assert(vm_base);
    /* sysmodule: the init state follows Initialize/Finalize; both always succeed. */
    assert(!ps3_sysmodule_is_initialized());
    assert(cellSysmoduleInitialize() == CELL_OK && ps3_sysmodule_is_initialized());
    assert(cellSysmoduleLoadModule(CELL_SYSMODULE_FS) == CELL_OK);
    assert(cellSysmoduleFinalize() == CELL_OK && !ps3_sysmodule_is_initialized());
    assert(cellSysmoduleFinalize() == CELL_OK);
    assert(cellSysmoduleIsLoaded(CELL_SYSMODULE_FS) == CELL_OK);   /* the table survives Finalize (ASSUMPTION in the patch) */

    /* sys_prx_load_module: firmware modules get stable ids, anything else fails like a missing file. */
    put(0x1000, "/dev_flash/sys/external/libsysutil_avc2.sprx");
    put(0x1100, "/dev_flash/sys/external/libfs.sprx");
    put(0x1200, "/dev_hdd0/game/NPUA80000/USRDIR/extra.sprx");
    put(0x1300, "/dev_flash/sys/external/libfs.sprx.bak");
    s32 a = sys_prx_load_module((void*)0x1000, 0, NULL);
    s32 b = sys_prx_load_module((void*)0x1100, 0, NULL);
    assert(a > 0 && b > 0 && a != b && (u32)a >> 24 == 0x23);
    assert(sys_prx_load_module((void*)0x1000, 0, NULL) == a);                       /* same path, same id */
    assert(sys_prx_load_module_on_memcontainer((void*)0x1100, 0x55, 0, NULL) == b);
    assert(sys_prx_load_module((void*)0x1200, 0, NULL) == (s32)0x80010006);          /* not firmware: ENOENT */
    assert(sys_prx_load_module((void*)0x1300, 0, NULL) == (s32)0x80010002);          /* not a .sprx: EINVAL */
    assert(sys_prx_load_module((void*)0x1000, 1, NULL) == (s32)0x80010002);          /* flags must be 0 */
    assert(sys_prx_load_module(NULL, 0, NULL) == (s32)0x8001000D);                   /* EFAULT */
    assert(sys_prx_load_module_on_memcontainer(NULL, 0, 0, NULL) == (s32)0x8001000D);

    /* sys_prx_register_library: records the entry; repeat is fine; NULL is EFAULT. */
    assert(sys_prx_register_library((void*)0x2000) == CELL_OK);
    assert(sys_prx_register_library((void*)0x2000) == CELL_OK && s_prx_nlibs == 1);
    assert(sys_prx_register_library((void*)0x2100) == CELL_OK && s_prx_nlibs == 2);
    assert(sys_prx_register_library(NULL) == (s32)0x8001000D);
    puts("sys_prx_* and cellSysmodule state checks passed");
    return 0;
}
