/* Host test for patch 7029 (_cellGcmFunc15 TEMP argument logger): the header libs/video/gcm_func15_log.h on its own. One log
 * per distinct argument set (registers and the words behind pointer-looking ones), repeats silent, bounded table, no read
 * outside the main-memory window. Run with tests/run_imports_test.sh. */
#include "rc_vm_stubs.h"
#include "libs/video/gcm_func15_log.h"
#include <stdlib.h>

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    vm_base = calloc(1, 0x20000); assert(vm_base);
    ppu_vm_size = 0x20000;
    vm_write32(0x11000, 0x11111111u); vm_write32(0x11004, 0x22222222u);

    uint64_t r[8] = { 0x11000, 0x20, 0, 0xC0000000u, 0xD0001000u, 0, 0, 0x1ull << 40 };   /* stack/RSX/large values: no deref */
    assert(!rc_f15_is_main_ptr(0xC0000000u) && !rc_f15_is_main_ptr(0xD0001000u) && !rc_f15_is_main_ptr(0x1ull << 40));
    assert(!rc_f15_is_main_ptr(0x11002) && !rc_f15_is_main_ptr(0x20) && !rc_f15_is_main_ptr(0x2FFFFFF0u));
    assert(!rc_f15_is_main_ptr(0x1000));               /* below the first user page */
    assert(rc_f15_is_main_ptr(0x11000) && rc_f15_is_main_ptr(0x2FFFFFE0u));
    assert(rc_gcm_func15_log(r) == 1);                 /* new set: logged */
    assert(rc_gcm_func15_log(r) == 0);                 /* same registers, same memory: silent */
    vm_write32(0x11004, 0x33333333u);
    assert(rc_gcm_func15_log(r) == 1);                 /* pointed memory changed: a new set */
    r[1] = 0x21;
    assert(rc_gcm_func15_log(r) == 1);                 /* a register changed: a new set */
    assert(s_f15_n == 3 && s_f15_calls == 4);

    /* Bounded: after RC_F15_SEEN_MAX distinct sets, one "table full" line and -1 for each further new one. */
    int logged = 3;
    for (uint64_t i = 0; logged < RC_F15_SEEN_MAX; i++) { r[2] = 0x100 + i; if (rc_gcm_func15_log(r) == 1) logged++; }
    assert(s_f15_n == RC_F15_SEEN_MAX);
    r[2] = 0xABull << 40; assert(rc_gcm_func15_log(r) == -1);   /* (not pointer-looking: the test memory is 0x20000 bytes) */
    r[2] = 0xACull << 40; assert(rc_gcm_func15_log(r) == -1);
    r[2] = 0x100;    assert(rc_gcm_func15_log(r) == 0);   /* an old set is still recognised */

    puts("_cellGcmFunc15 argument logger checks passed");
    return 0;
}
