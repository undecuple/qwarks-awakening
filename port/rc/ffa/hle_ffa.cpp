/* Ratchet & Clank: Full Frontal Assault 1.05 (BCUS98380): HLE for imports the runtime leaves unresolved.
 * None registered yet (first build setup, 2026-11-03). Add a handler here only after reading its call site.
 * Also the home of the title's runtime diagnostics that need no lifted-code change. */
#include "ppu_recomp.h"
#include <stdint.h>
#include "ffa_lua_errmsg.inc"   /* RC_LUA_ERRMSG=N: Lua error text at luaD_pcall 0x0080DE98 (diagnostic, default off; patch 7113) */

extern "C" void rc_hle_register(void)
{
    ffa_lua_errmsg::install();
}
