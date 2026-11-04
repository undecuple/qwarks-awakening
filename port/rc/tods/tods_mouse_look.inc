/* ENHANCEMENT: native mouse look for ToD's follow camera (port/rc/tods/ppu_overrides.txt line 0006D4D8), default off: keybinds.ini
 * [mouse] look = off; `look = on` or RC_MOUSE_LOOK=1 turns it on (rc_settings.cpp puts it into KZ3_MOUSE_LOOK for pad_mouse).
 * Included at the end of tods_ppu_overrides.cpp. Design and arithmetic: port/rc/rc_mouse.h (host test port/rc/tests/test_rc_mouse.c).
 *
 * Hook point (the RE notes s.18, tracer run tods-kbm-trace5): the follow camera update func_00070D28 reads its input context's
 * right-stick axes through func_00662DF8 and calls func_0006D4D8(x, camera) @0x70E84 for the yaw rate (rad/s), stores it at
 * camera+0x118 and turns the camera by rate * dt (func_000125D0, dt = clock[+4] with clock = TOC word r2-0x52D8 of TOC 0x88FF38).
 * func_0006D4D8 (@0x6D4D8, a leaf: 0x70 bytes) is called from nowhere else. The override runs the lifted body and adds the mouse's
 * yaw as a rate, so the frame's turn is exactly counts * radians-per-count.
 * Pitch (override row 0006D548): the follow camera's pitch is a target the stick's y pushes towards a limit (func_0006D548:
 * lerp between a base pitch and a table limit by y, called only by func_00070D28 @0x70E9C right after the yaw) and the camera's
 * elevation springs towards it (func_00012A10). The port holds its own offset, moved by the vertical counts of the same take,
 * added to the returned target and kept within the game's own limits (table [r2-0x5324] +0 / +4, degrees); the stick still
 * applies on top. The offset resets when no camera update ran for RC_MOUSE_PITCH_IDLE_MS (cutscene, load, respawn, ship,
 * pause) or the camera object changes. Mouse forward = look up (seen in the scripted runs, rc_mouse.h). ASSUMPTION until
 * the hand test: mouse right turns the camera right. */
#include "../rc_mouse.h"
#include "pad_mouse.h"
#include <windows.h>

extern "C" volatile unsigned long long g_rc_mouse_take_ms;   /* main.cpp: last camera update with mouse look on (stage 3 cursor rule) */

static int s_tods_pitch_dy;   /* vertical counts of this update's take (func_0006D4D8 -> func_0006D548) */

static inline uint32_t tods_rd32(uint32_t ea) { uint32_t v; memcpy(&v, vm_base + ea, 4); return __builtin_bswap32(v); }
static inline float tods_rdf(uint32_t ea) { uint32_t v = tods_rd32(ea); float f; memcpy(&f, &v, 4); return f; }

#ifdef PPU_OVERRIDE_0006D4D8
static float tods_mouse_dt(void)
{
    uint32_t clk, v;
    memcpy(&clk, vm_base + (0x88FF38u - 0x52D8u), 4);   /* TOC word r2-0x52D8: the camera clock (0x10731DA0 in the disc's ELF) */
    clk = __builtin_bswap32(clk);
    if (!clk) return 0.0f;
    memcpy(&v, vm_base + clk + 4, 4);                     /* func_0069B620: lfs f0, 4(r3) */
    v = __builtin_bswap32(v);
    float dt; memcpy(&dt, &v, 4);
    return dt;
}

/* @0x0006d4d8  follow camera: yaw rate from the right stick's x (f1 = x, r3 = camera) */
void func_0006D4D8(ppu_context* ctx)
{
    func_0006D4D8__lifted(ctx);
    static int on = -1;
    if (on < 0) on = pad_mouse_enabled() && vm_base;
    if (!on) return;
    LARGE_INTEGER qf, qc;
    QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qc);
    const double now = (double)qc.QuadPart / (double)qf.QuadPart;
    g_rc_mouse_take_ms = (unsigned long long)(now * 1000.0);
    int dx = 0, dy = 0;
    const int moved = pad_mouse_take(now, &dx, &dy);
    const float dt = tods_mouse_dt();
    s_tods_pitch_dy += dy;   /* taken by func_0006D548 right after this, in the same camera update */
    const float add = moved ? rc_mouse_yaw_rate(dx, pad_mouse_cfg()->sens, dt) : 0.0f;
    if (add != 0.0f) ctx->fpr[1] = (double)(float)((float)ctx->fpr[1] + add);
    static double s_last_line = -1e9;
    static long long s_sum_dx = 0, s_sum_dy = 0, s_updates = 0;
    s_sum_dx += dx; s_sum_dy += dy; s_updates++;
    if (moved && (pad_mouse_log_each() || now - s_last_line >= 1.0)) {
        s_last_line = now;
        fprintf(stderr, "[mouse] tods yaw: d=%d,%d dt=%.4f rate+%.3f rad/s (total %lld,%lld counts over %lld updates)\n",
                dx, dy, dt, add, s_sum_dx, s_sum_dy, s_updates);
    }
}
#endif

#ifdef PPU_OVERRIDE_0006D548
/* @0x0006d548  follow camera: pitch target from the right stick's y (f1 = y, r3 = camera, r4 = camera params; returns f1, rad) */
void func_0006D548(ppu_context* ctx)
{
    static int on = -1;
    if (on < 0) on = pad_mouse_enabled() && vm_base;
    if (!on) { func_0006D548__lifted(ctx); return; }
    const uint32_t toc = (uint32_t)ctx->gpr[2], cam = (uint32_t)ctx->gpr[3], prm = (uint32_t)ctx->gpr[4];
    func_0006D548__lifted(ctx);
    const int dy = s_tods_pitch_dy; s_tods_pitch_dy = 0;
    if (!toc || !cam || !prm) return;
    /* the lifted body's own inputs (rc_mouse.h): k = deg -> rad, the limit table, the base pitch */
    const float k = tods_rdf(toc - 0x5328);
    const uint32_t tab = tods_rd32(toc - 0x5324);
    if (!tab) return;
    const float base = (vm_base[prm + 0x7B] != 0) ? tods_rdf(prm + 0x5C) : tods_rdf(cam + 0x138) * k;   /* @0x6D57C bne cr7 -> 0x6D5E0 */
    float lo, hi;
    rc_mouse_pitch_limits(tods_rdf(tab) * k, tods_rdf(tab + 4) * k, &lo, &hi);

    LARGE_INTEGER qf, qc;
    QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&qc);
    const double now = (double)qc.QuadPart / (double)qf.QuadPart;
    const unsigned long long now_ms = (unsigned long long)(now * 1000.0);
    static unsigned long long s_last_ms = 0;
    static uint32_t s_cam = 0;
    static float s_offset = 0.0f;
    static long long s_resets = 0, s_sum_dy = 0;
    if (rc_mouse_pitch_reset(now_ms, s_last_ms) || cam != s_cam) {
        if (s_offset != 0.0f || !s_last_ms)
            fprintf(stderr, "[mouse] tods pitch: reset (%s; offset %.4f rad dropped) camera 0x%08X limits [%.4f, %.4f] rad (table %.2f, %.2f deg)"
                    " base %.4f rad\n", !s_last_ms ? "first update" : cam != s_cam ? "camera changed" : "no camera update for 1 s",
                    s_offset, cam, lo, hi, tods_rdf(tab), tods_rdf(tab + 4), base);
        s_offset = 0.0f; s_resets++;
    }
    s_last_ms = now_ms; s_cam = cam;
    const PadMouseCfg* c = pad_mouse_cfg();
    const float game = (float)ctx->fpr[1];
    s_offset = rc_mouse_pitch_offset(s_offset, rc_mouse_pitch_delta(dy, c->sens, c->invert), base, lo, hi);
    const float t = rc_mouse_pitch_target(game, s_offset, base, lo, hi);
    if (t != game) ctx->fpr[1] = (double)t;
    s_sum_dy += dy;
    static double s_last_line = -1e9;
    if (dy && (pad_mouse_log_each() || now - s_last_line >= 1.0)) {
        s_last_line = now;
        fprintf(stderr, "[mouse] tods pitch: dy=%d offset %.4f rad base %.4f game %.4f -> target %.4f limits [%.4f, %.4f] (total dy %lld, %lld resets)\n",
                dy, s_offset, base, game, t, lo, hi, s_sum_dy, s_resets);
    }
}
#endif
