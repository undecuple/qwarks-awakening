/* rc_mouse.h -- R&C mouse look, pure helpers (host test port/rc/tests/test_rc_mouse.c). ENHANCEMENT, default off
 * (keybinds.ini [mouse] look = off; RC_MOUSE_LOOK=1 or look = on turns it on). The counts come from Project Helghast's
 * pad_mouse (Killzone 3 PC port, same author, MIT): Raw Input, pad_mouse_take once per camera update.
 *
 * ToD (the RE notes s.18): the follow camera's update func_00070D28 turns the right stick's x into a yaw rate with
 * func_0006D4D8 (rad/s; stored at camera+0x118) and turns it this frame by rate * dt (func_000125D0 = rate * 1.0 * clock[+4],
 * then the half-angle 0.5 into sincos). The mouse adds its yaw as a rate on top of the stick's: dyaw / dt, so the camera
 * turns by exactly counts * radians-per-count that frame, never through the stick's dead zone, response curve, the
 * game's camera speed option or its invert-x option. */
#ifndef RC_MOUSE_H
#define RC_MOUSE_H

#include <math.h>

#define RC_MOUSE_PLAY_MS 300u   /* KZ2 6046's rule: a camera update took counts within this long = in play */
#define RC_MOUSE_DT_MIN  0.0005f /* frame times outside [0.5 ms, 0.5 s] are not a running camera: no mouse rate */
#define RC_MOUSE_DT_MAX  0.5f

/* In play (pointer hidden and held) while a camera update ran within the last RC_MOUSE_PLAY_MS. last_ms 0 = never.
 * now earlier than last (two threads' clock reads) counts as in play. */
static inline int rc_mouse_in_play(unsigned long long now_ms, unsigned long long last_ms)
{
    if (!last_ms) return 0;
    if (now_ms <= last_ms) return 1;
    return now_ms - last_ms <= RC_MOUSE_PLAY_MS;
}

/* Extra yaw rate (rad/s) for dx counts this frame: dx * rad_per_count / dt. ASSUMPTION (hand test): a positive rate
 * turns the camera the way the stick pushed right does (func_0006D4D8 with the invert-x option off returns
 * +(speed + 0.1) * pi * camera+0x144 * x), so mouse right = +. Returns 0 for a dt outside the range above or a
 * non-finite input. */
static inline float rc_mouse_yaw_rate(int dx, float rad_per_count, float dt)
{
    if (!dx || !(dt >= RC_MOUSE_DT_MIN && dt <= RC_MOUSE_DT_MAX) || !isfinite(rad_per_count)) return 0.0f;
    return (float)dx * rad_per_count / dt;
}

/* Pitch (2026-11-04): the port keeps its own pitch offset, added to the follow camera's pitch target each update.
 * ToD's func_0006D548(y, camera, params) returns the target elevation (radians):
 *     base = params+0x7B ? params+0x5C : camera+0x138 * k        (k = TOC r2-0x5328, deg -> rad)
 *     s    = c * (option+0x114B0 + K) * -y                        (c = r2-0x5338, or r2-0x5334 with the invert-y option)
 *     target = base + |s| * (limit - base), limit = table[1] * k for s >= 0, table[0] * k for s < 0   (table = [r2-0x5324])
 * and func_00070D28 springs the camera's elevation towards it (func_00012A10, stiffness params+0x60, dt = [r2-0x530C]+0xA4).
 * So table[0] and table[1] are the game's own pitch limits; the port's offset is kept so that base + offset stays between
 * them (counts past a limit are not banked), and the final target (stick lerp + offset) is clamped to them as well.
 *
 * Mouse forward (dy < 0) = look up, as Project Helghast's pad_look_latch_delta (p = p0 - dy * k * (invert ? -1 : 1)).
 * RC_MOUSE_PITCH_SIGN maps "look up" onto the target's elevation: -1, because a larger elevation is the camera higher above
 * Ratchet looking down (seen in runs tods-pitch-up / tods-pitch-updown, 2026-11-04; the RE notes s.18). */
#ifndef RC_MOUSE_PITCH_SIGN
#define RC_MOUSE_PITCH_SIGN (-1.0f)
#endif
#define RC_MOUSE_PITCH_IDLE_MS 1000u /* no camera update this long (cutscene, load, respawn, ship, pause): the offset resets */

/* Elevation change (radians, on the target) for dy counts: -dy * k (look up for mouse forward), flipped by invert,
 * mapped by RC_MOUSE_PITCH_SIGN. 0 for no counts or a non-finite factor. */
static inline float rc_mouse_pitch_delta(int dy, float rad_per_count, int invert)
{
    if (!dy || !isfinite(rad_per_count)) return 0.0f;
    const float up = -(float)dy * rad_per_count * (invert ? -1.0f : 1.0f);
    return RC_MOUSE_PITCH_SIGN * up;
}

/* The two limits in either order -> lo <= hi (the table's order is the game's, not checked here). */
static inline void rc_mouse_pitch_limits(float a, float b, float* lo, float* hi)
{
    if (a <= b) { *lo = a; *hi = b; } else { *lo = b; *hi = a; }
}

/* New offset: old + delta, kept so that base + offset is within [lo, hi] (widened to include base when the game's own
 * base is outside them, so the offset never pulls the camera by itself). A non-finite input leaves 0. */
static inline float rc_mouse_pitch_offset(float offset, float delta, float base, float lo, float hi)
{
    float o = offset + delta;
    if (!isfinite(o) || !isfinite(base) || !isfinite(lo) || !isfinite(hi) || lo > hi) return 0.0f;
    if (base < lo) lo = base;
    if (base > hi) hi = base;
    if (base + o > hi) o = hi - base;
    if (base + o < lo) o = lo - base;
    return o;
}

/* Target handed back to the camera: the game's target plus the offset, clamped to [lo, hi] (widened to include base, as
 * above). With offset 0 the game's target is returned untouched (bit for bit), so look = on without vertical counts
 * changes nothing. */
static inline float rc_mouse_pitch_target(float game_target, float offset, float base, float lo, float hi)
{
    if (offset == 0.0f || !isfinite(offset) || !isfinite(game_target)) return game_target;
    if (base < lo) lo = base;
    if (base > hi) hi = base;
    float t = game_target + offset;
    if (t > hi) t = hi;
    if (t < lo) t = lo;
    return t;
}

/* The offset resets when the camera update did not run for RC_MOUSE_PITCH_IDLE_MS (last_ms 0 = first update). */
static inline int rc_mouse_pitch_reset(unsigned long long now_ms, unsigned long long last_ms)
{
    if (!last_ms) return 1;
    if (now_ms <= last_ms) return 0;
    return now_ms - last_ms > RC_MOUSE_PITCH_IDLE_MS;
}

#endif
