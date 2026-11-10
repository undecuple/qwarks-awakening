/* Ratchet & Clank Collection, Ratchet & Clank 3: Up Your Arsenal (RC3.ppu.elf, disc BCUS98282, no update): HLE for imports the
 * runtime leaves unresolved. Built and run directly on RC3.ppu.elf (the launcher EBOOT is skipped, as for RC1/RC2).
 * The readiness card (2026-11-03) lists RC2's unhandled imports plus
 * sceNpClans announcement/challenge calls and sceNpTus multi-slot/-user variable calls (online play, offline here).
 * L10nConvertStr is RC1's shared handler (port/rc/rc_l10n_hle.h); the rest fall back to the runtime's default return until a
 * run shows they matter. */
#include "ppu_recomp.h"
#include <stdint.h>
#include "../rc_l10n_hle.h"   /* L10nConvertStr, shared with the other Collection games */

extern "C" void ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));

extern "C" void rc_hle_register(void)
{
    ps3_hle_register_ctx(0x931FF25Au, "L10nConvertStr", rc_l10n::hle_L10nConvertStr);
}
