/* Ratchet & Clank Collection, Ratchet & Clank 1 (RC1.ppu.elf, disc BCUS98282, no update): HLE for imports the runtime leaves
 * unresolved. Built and run directly on RC1.ppu.elf (the launcher EBOOT's sys_game_process_exitspawn2 start path is not handled).
 * The readiness card (2026-11-03) lists as unhandled after patches 7032/7033: cellL10n L10nConvertStr (NID 0x931FF25A, call site
 * func_00551A64), cellFsAioWrite, sys_prx_load_module_by_fd / _on_memcontainer_by_fd (func_00577DEC). Only L10nConvertStr is
 * handled here (2026-11-09: the first boot logged it unresolved at the NEW GAME save, t=75 s); the others still fall back to
 * the runtime's default return. Add a handler here only after reading its call site and the first boot's log. */
#include "ppu_recomp.h"
#include <stdint.h>
#include "../rc_l10n_hle.h"   /* L10nConvertStr, shared with the other Collection games */

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));

extern "C" void rc_hle_register(void)
{
    ps3_hle_register_ctx(0x931FF25Au, "L10nConvertStr", rc_l10n::hle_L10nConvertStr);
}
