/* test_rc_mouse.c -- host test of port/rc/rc_mouse.h (ToD mouse look arithmetic, the stage 3 in-play rule) together with
 * pad_mouse.h's radians-per-count (Project Helghast). Run: port/rc/tests/run_rc_mouse_test.sh */
#include "pad_mouse.h"
#include "rc_mouse.h"

#include <math.h>
#include <stdio.h>

static int fails, passes;
#define CHECK(c, ...) do { if (c) { passes++; printf("[PASS] " __VA_ARGS__); printf("\n"); } \
                           else { fails++; printf("[FAIL] " __VA_ARGS__); printf("\n"); } } while (0)

/* What func_00070D28 turns this frame: rate * 1.0 * dt (func_000125D0), then sincos(angle * 0.5) of a unit quaternion = angle. */
static float frame_turn(float stick_rate, float mouse_rate, float dt) { return (stick_rate + mouse_rate) * dt; }

int main(void)
{
    int bad = 0;
    const float k = pad_mouse_rad_per_count(PAD_MOUSE_USER_SENS_DEF, PAD_MOUSE_DEG_PER_COUNT, &bad);
    CHECK(!bad && fabsf(k - 2.5f * 0.022f * 0.017453292519943295f) < 1e-9f, "default radians per count %.8f", k);

    /* the frame turns by exactly counts * k, at any frame time */
    const float dts[] = { 1.0f / 60, 1.0f / 30, 1.0f / 20, 0.1f };
    for (unsigned i = 0; i < sizeof dts / sizeof dts[0]; i++) {
        const float dt = dts[i];
        const float r = rc_mouse_yaw_rate(1000, k, dt);
        const float turn = frame_turn(0.0f, r, dt);
        CHECK(fabsf(turn - 1000 * k) < 1e-5f, "1000 counts at dt %.4f turn %.6f rad (want %.6f)", dt, turn, 1000 * k);
    }
    /* adds on top of the stick, sign follows dx (mouse right = the stick's right, + rate) */
    CHECK(fabsf(frame_turn(2.0f, rc_mouse_yaw_rate(-200, k, 1.0f / 30), 1.0f / 30) - (2.0f / 30 - 200 * k)) < 1e-5f,
          "stick 2 rad/s plus -200 counts");
    CHECK(rc_mouse_yaw_rate(1, k, 1.0f / 30) > 0.0f && rc_mouse_yaw_rate(-1, k, 1.0f / 30) < 0.0f, "sign of dx kept");
    /* one revolution: 360 / (2.5 * 0.022) = 6545.45 counts at the default sensitivity */
    CHECK(fabsf(frame_turn(0.0f, rc_mouse_yaw_rate(6545, k, 1.0f / 30), 1.0f / 30) - 6.2831853f) < 0.002f,
          "6545 counts = one revolution at sensitivity 2.5");
    /* no rate without counts, with a frame time that is not a running camera, or a bad factor */
    CHECK(rc_mouse_yaw_rate(0, k, 1.0f / 30) == 0.0f, "no counts, no rate");
    CHECK(rc_mouse_yaw_rate(100, k, 0.0f) == 0.0f, "dt 0: no rate");
    CHECK(rc_mouse_yaw_rate(100, k, -0.016f) == 0.0f, "negative dt: no rate");
    CHECK(rc_mouse_yaw_rate(100, k, 1.0f) == 0.0f, "dt 1 s (a stall or a paused clock): no rate");
    CHECK(rc_mouse_yaw_rate(100, k, NAN) == 0.0f, "dt NaN: no rate");
    CHECK(rc_mouse_yaw_rate(100, NAN, 1.0f / 30) == 0.0f, "factor NaN: no rate");
    CHECK(rc_mouse_yaw_rate(100, k, RC_MOUSE_DT_MIN) != 0.0f && rc_mouse_yaw_rate(100, k, RC_MOUSE_DT_MAX) != 0.0f,
          "dt range ends included");

    /* stage 3: in play while a camera update ran in the last 300 ms */
    CHECK(!rc_mouse_in_play(5000, 0), "never updated: not in play");
    CHECK(rc_mouse_in_play(5000, 5000), "updated now: in play");
    CHECK(rc_mouse_in_play(5300, 5000), "300 ms ago: in play");
    CHECK(!rc_mouse_in_play(5301, 5000), "301 ms ago: menu");
    CHECK(rc_mouse_in_play(4990, 5000), "stamp a little ahead of the reader's clock: in play");

    /* pitch: mouse forward (dy < 0) = look up; RC_MOUSE_PITCH_SIGN maps it onto the target's elevation */
    const float S = RC_MOUSE_PITCH_SIGN;
    CHECK(fabsf(rc_mouse_pitch_delta(-100, k, 0) - S * 100 * k) < 1e-9f, "100 counts forward: elevation %+.6f", rc_mouse_pitch_delta(-100, k, 0));
    CHECK(rc_mouse_pitch_delta(-100, k, 1) == -rc_mouse_pitch_delta(-100, k, 0), "invert flips the pitch");
    CHECK(rc_mouse_pitch_delta(50, k, 0) == -rc_mouse_pitch_delta(-50, k, 0), "back = the other way");
    CHECK(rc_mouse_pitch_delta(0, k, 0) == 0.0f && rc_mouse_pitch_delta(10, NAN, 0) == 0.0f, "no counts or NaN factor: no pitch");
    float lo, hi;
    rc_mouse_pitch_limits(1.0f, -0.5f, &lo, &hi);
    CHECK(lo == -0.5f && hi == 1.0f, "limits in either order");
    /* the offset accumulates, and stops at the limit without banking counts */
    float o = 0.0f;
    for (int i = 0; i < 10; i++) o = rc_mouse_pitch_offset(o, 0.05f, 0.2f, lo, hi);
    CHECK(fabsf(o - 0.5f) < 1e-6f, "10 x 0.05 rad from base 0.2: offset %.4f", o);
    for (int i = 0; i < 100; i++) o = rc_mouse_pitch_offset(o, 0.05f, 0.2f, lo, hi);
    CHECK(fabsf(0.2f + o - hi) < 1e-6f, "held at the upper limit: base + offset %.4f", 0.2f + o);
    o = rc_mouse_pitch_offset(o, -0.05f, 0.2f, lo, hi);
    CHECK(fabsf(0.2f + o - (hi - 0.05f)) < 1e-6f, "one step back leaves the limit at once (nothing banked)");
    for (int i = 0; i < 1000; i++) o = rc_mouse_pitch_offset(o, -0.05f, 0.2f, lo, hi);
    CHECK(fabsf(0.2f + o - lo) < 1e-6f, "held at the lower limit: base + offset %.4f", 0.2f + o);
    CHECK(rc_mouse_pitch_offset(0.0f, 0.1f, 2.0f, lo, hi) == 0.0f, "base above the limits: the offset never pulls it further");
    CHECK(fabsf(rc_mouse_pitch_offset(0.0f, -0.1f, 2.0f, lo, hi) + 0.1f) < 1e-6f, "base above the limits: back towards them is free");
    CHECK(rc_mouse_pitch_offset(0.3f, NAN, 0.2f, lo, hi) == 0.0f, "NaN delta: offset 0");
    /* the target: game's own plus the offset, within the limits; offset 0 = the game's target bit for bit */
    CHECK(rc_mouse_pitch_target(0.123456f, 0.0f, 0.2f, lo, hi) == 0.123456f, "offset 0: the game's target untouched");
    CHECK(fabsf(rc_mouse_pitch_target(0.2f, 0.3f, 0.2f, lo, hi) - 0.5f) < 1e-6f, "stick at rest: target = base + offset");
    CHECK(rc_mouse_pitch_target(0.9f, 0.3f, 0.2f, lo, hi) == hi, "stick towards the limit plus the offset: clamped to the limit");
    CHECK(rc_mouse_pitch_target(-0.4f, -0.3f, 0.2f, lo, hi) == lo, "the other way: clamped to the lower limit");
    /* reset: the first update, or no camera update for over a second */
    CHECK(rc_mouse_pitch_reset(5000, 0), "first update: reset");
    CHECK(!rc_mouse_pitch_reset(6000, 5000), "1000 ms gap: kept");
    CHECK(rc_mouse_pitch_reset(6001, 5000), "1001 ms gap: reset");
    CHECK(!rc_mouse_pitch_reset(4990, 5000), "clock a little behind: kept");

    printf("%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
