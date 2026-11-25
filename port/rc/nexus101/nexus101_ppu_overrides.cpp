/* Into the Nexus 1.01 (BCUS99245): host bodies for port/rc/nexus101/ppu_overrides.txt.
 *
 * 00591B88 and 007660E0: the collision-query probe (DIAGNOSTIC, default off, RC_COLLPROBE; nexus101_collprobe.inc).
 * Both call the lifted body unchanged; with RC_COLLPROBE unset they do nothing else. 2026-11-19, prison_transport fall
 * (work/coord/nexus-fall-1119/static-1119.md).
 */
#include "ppu_recomp.h"
#include "ppu_overrides.h"   /* PPU_OVERRIDE_<addr> and the <name>__lifted declarations */
#include <chrono>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
extern uint8_t* vm_base;
}

#include "nexus101_collprobe.inc"

#if defined(PPU_OVERRIDE_00591B88) || defined(PPU_OVERRIDE_007660E0)
static const std::chrono::steady_clock::time_point g_cp_t0 = std::chrono::steady_clock::now();   /* ~process start */
static double cp_now() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_cp_t0).count(); }

static cp_state g_cp;
static cp_state* cp_get()
{
    static int init = 0;
    if (!init) {
        init = 1;
        cp_init(&g_cp, getenv("RC_COLLPROBE"), getenv("RC_COLLPROBE_AFTER"));
        if (g_cp.on) fprintf(stderr, "[collprobe] armed: mode %d, %ld detail lines after %.1f s\n", g_cp.on, g_cp.detail_left, g_cp.after_s);
    }
    return g_cp.on ? &g_cp : nullptr;
}
#endif

#ifdef PPU_OVERRIDE_00591B88
/* @0x00591b88  sync collision query finaliser (waits for the record's done byte, clears 0x180 in [0xF516B0], returns [0xF51718]) */
void func_00591B88(ppu_context* ctx)
{
    cp_state* s = cp_get();
    if (!s) { func_00591B88__lifted(ctx); return; }
    cp_finish_pre(s, vm_base, (uint32_t)ctx->lr);
    func_00591B88__lifted(ctx);
    cp_finish_post(s, vm_base, stderr, cp_now(), (uint32_t)ctx->gpr[3]);
}
#endif

#ifdef PPU_OVERRIDE_007660E0
/* @0x007660e0  player ground probe (r3 point, f1, r5 query desc: +0 result buffer, +0x10 object); returns 1 on a hit */
/* ctx->lr on entry is the guest return address: the lifted caller sets it before the call (ctx->lr = 0x00028010 in
 * func_00027B88, 0x0002C730 in func_0002C608), so it is the real caller, not a host or trampoline value. */
void func_007660E0(ppu_context* ctx)
{
    cp_state* s = cp_get();
    if (!s || s->on < 2) { func_007660E0__lifted(ctx); return; }
    cp_ground_pre(s, vm_base, (uint32_t)ctx->gpr[3], ctx->fpr[1], (uint32_t)ctx->gpr[5], (uint32_t)ctx->lr);
    func_007660E0__lifted(ctx);
    cp_ground_post(s, vm_base, stderr, cp_now(), (uint32_t)ctx->gpr[3]);
}
#endif
