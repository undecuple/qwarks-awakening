/* Host test for patch 7021 (cellSpursSetGlobalExceptionEventHandler): unity build of the patched libs/spurs/cellSpurs.c.
 * Argument checks (NULL spurs/handler, 128-byte alignment), the repeat-registration BUSY rule, the eight-instance table limit
 * and the read-back accessor. Run with tests/run_imports_test.sh. RC_LINK_LENIENT */
#include "libs/spurs/cellSpurs.c"
#include "rc_vm_stubs.h"
/* cellSpurs.c's two constructors store into these hooks; everything else it references is never called here. */
int (*g_kz3_spurs_switch_wanted)(uint32_t, uint32_t, uint64_t);
int (*g_kz3_spurs_kpoll)(uint32_t, uint32_t, uint32_t, uint32_t*);

int main(void)
{
    u32 arg = 0xDEAD;
    assert(ps3_spurs_global_exception_handler(0x10000, &arg) == 0 && arg == 0xDEAD);      /* nothing registered */

    assert(cellSpursSetGlobalExceptionEventHandler(0, 0x2000, 0x3000) == CELL_SPURS_CORE_ERROR_NULL_POINTER);
    assert(cellSpursSetGlobalExceptionEventHandler(0x10000, 0, 0x3000) == CELL_SPURS_CORE_ERROR_NULL_POINTER);
    assert(cellSpursSetGlobalExceptionEventHandler(0x10040, 0x2000, 0x3000) == CELL_SPURS_CORE_ERROR_ALIGN);
    assert(ps3_spurs_global_exception_handler(0x10000, &arg) == 0);                        /* errors recorded nothing */

    assert(cellSpursSetGlobalExceptionEventHandler(0x10000, 0x2000, 0x3000) == CELL_OK);
    assert(ps3_spurs_global_exception_handler(0x10000, &arg) == 0x2000 && arg == 0x3000);
    assert(ps3_spurs_global_exception_handler(0x10080, &arg) == 0);                        /* another instance: none */
    assert(ps3_spurs_global_exception_handler(0x10000, NULL) == 0x2000);                   /* arg out-pointer optional */
    assert(cellSpursSetGlobalExceptionEventHandler(0x10000, 0x2100, 0x3100) == CELL_SPURS_CORE_ERROR_BUSY);
    assert(ps3_spurs_global_exception_handler(0x10000, &arg) == 0x2000 && arg == 0x3000);  /* BUSY kept the first one */

    /* Instances 2..8 fill the table (8 entries); the ninth distinct instance is BUSY. */
    for (u32 i = 1; i < 8; i++)
        assert(cellSpursSetGlobalExceptionEventHandler(0x10000 + i * 0x1000, 0x2000 + i, i) == CELL_OK);
    assert(ps3_spurs_global_exception_handler(0x10000 + 7 * 0x1000, &arg) == 0x2007 && arg == 7);
    assert(cellSpursSetGlobalExceptionEventHandler(0x20000, 0x2000, 0) == CELL_SPURS_CORE_ERROR_BUSY);

    puts("cellSpursSetGlobalExceptionEventHandler checks passed");
    return 0;
}
