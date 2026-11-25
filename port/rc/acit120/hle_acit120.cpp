/* Ratchet & Clank: A Crack in Time 1.20 (BCUS98124): HLE for imports the runtime leaves unresolved.
 * None registered yet (first build, 2026-11-03). Patches 7018-7027 added the system functions the 1.20 import survey
 * (work/reports/imports-updates-2026-11-03/acit120_imports.tsv) found missing; _cellSpursLFQueuePushBody is patch 6003's
 * and _cellGcmFunc15 is still open (listed in docs/in-progress/porting-status.md). Add a handler here only after reading
 * its call site. Also the home of the title's runtime diagnostics that need no lifted-code change. */
#include "ppu_recomp.h"
#include <stdint.h>
#include "acit120_lua_errmsg.inc"   /* RC_LUA_ERRMSG=N: Lua error text at luaD_rawrunprotected 0x0050AF94 (diagnostic, default off; patch 7113) */

extern "C" void rc_hle_register(void)
{
    acit120_lua_errmsg::install();
}
