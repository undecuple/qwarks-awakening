/* Ratchet: Deadlocked HD (NPUA80646, installed PSN folder, CATEGORY HG, no update): HLE for imports the runtime leaves
 * unresolved. The same engine as the Collection games (Edge LZMA PSARC, the same WWS policy module and job modules), so the
 * Collection's handlers apply. Readiness card (2026-11-03): 10 of 287 imports have no handler in the patched tree
 * (cellL10n 0x931FF25A, cellSaveDataFixedDelete, cellVoice x4, sys_prx_load_module_by_fd, ..._on_memcontainer_by_fd,
 * cellFsAioWrite, cellKbRead); only L10nConvertStr is handled here, the rest fall back to the runtime's default return until
 * a run shows they matter (RC3 boots with the same two cellVoice notify-queue NIDs unresolved).
 * L10nConvertStr call site: func_00720D94 @0x00720E28 (bl to the import stub 0x008887D4): src_code 5 or 1 (from the language
 * word == 6), src = r4, src_len = &[sp+0x70], dst_code 0 (UTF8), dst = r7, dst_len = &[sp+0x74]; result 0 -> *r8 = written
 * length, 1/2/3 -> the game prints its own error and returns 0. Same shape as RC1's func_00551A64 (the RE notes s.24). */
#include "ppu_recomp.h"
#include <stdint.h>
#include "../rc_l10n_hle.h"   /* L10nConvertStr, shared with the Collection games */

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));

extern "C" void rc_hle_register(void)
{
    ps3_hle_register_ctx(0x931FF25Au, "L10nConvertStr", rc_l10n::hle_L10nConvertStr);
}
