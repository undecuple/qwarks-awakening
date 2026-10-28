/* Host test for R&C patch 7016 (libs/video/rsx_dyn_bias.h): the PSO key's view of the polygon offset.
 * ToD's Fastoon shadow passes change POLY_OFFSET_BIAS / SCALE every frame (tods-play-1140); the key held the
 * raw float bits, so each frame built new PSOs. Checks: with dynamic depth bias the key is the same for the
 * same state with different bias values (and the base, static path, differs: the churn); offset on/off stay
 * distinct; RSSetDepthBias gets the values the PSO path baked (rounded units, slope as is). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rsx_dyn_bias.h"

typedef struct { uint32_t depth_test, depth_func, poly_scale_bits, poly_bias_bits, color_mask; } st_t;
static uint64_t fnv(const void* p, size_t n, uint64_t h)
{ const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }
static uint32_t fb(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint64_t key(st_t s, int dyn)
{ ld_dynb_key_bits(&s.poly_scale_bits, &s.poly_bias_bits, dyn); return fnv(&s, sizeof s, 1469598103934665603ull); }

int main(void)
{
    int fail = 0;
    /* one shadow-pass state, five frames' bias values */
    const float scales[5] = { 1.25f, 1.31f, 2.0f, 1.0625f, 4.5f }, biases[5] = { 2.0f, 3.7f, 18.0f, 1.0f, 250.4f };
    st_t s = { 1, 0x0203, 0, 0, 0 };
    uint64_t kd0 = 0, ks[5];
    int distinct_static = 0;
    for (int i = 0; i < 5; i++) {
        s.poly_scale_bits = fb(scales[i]); s.poly_bias_bits = fb(biases[i]);
        const uint64_t kd = key(s, 1); ks[i] = key(s, 0);
        if (i == 0) kd0 = kd; else if (kd != kd0) { printf("FAIL dynamic key differs at frame %d\n", i); fail++; }
        int seen = 0; for (int j = 0; j < i; j++) seen |= ks[j] == ks[i];
        distinct_static += !seen;
        /* the same state built twice gives the same key */
        if (key(s, 1) != kd || key(s, 0) != ks[i]) { printf("FAIL key not stable at frame %d\n", i); fail++; }
    }
    printf("static (base) path: %d distinct keys for 5 frames of one state; dynamic: 1\n", distinct_static);
    if (distinct_static != 5) { printf("FAIL base did not reproduce the churn\n"); fail++; }
    st_t off = s; off.poly_scale_bits = off.poly_bias_bits = 0;
    if (key(off, 1) == kd0) { printf("FAIL offset off and on share a key\n"); fail++; }
    if (key(off, 1) != key(off, 0)) { printf("FAIL offset-off key changed by 7016\n"); fail++; }
    st_t only_bias = s; only_bias.poly_scale_bits = 0; only_bias.poly_bias_bits = fb(-3.0f);
    if (key(only_bias, 1) != kd0) { printf("FAIL bias-only offset not keyed as on\n"); fail++; }
    /* values, as patch 2006's PSO path bakes them */
    float db, ss;
    ld_dynb_values(fb(1.25f), fb(2.4f), &db, &ss);   if (db != 2.0f || ss != 1.25f) { printf("FAIL values 2.4 -> %g %g\n", db, ss); fail++; }
    ld_dynb_values(fb(0.0f), fb(-2.5f), &db, &ss);   if (db != -3.0f || ss != 0.0f) { printf("FAIL values -2.5 -> %g\n", db); fail++; }
    ld_dynb_values(0x7FC00000u, 0x7FC00000u, &db, &ss); if (db != 0.0f || ss != 0.0f) { printf("FAIL NaN\n"); fail++; }
    ld_dynb_values(fb(1.0f), fb(3.0e9f), &db, &ss);  if (db != 0.0f) { printf("FAIL out of range\n"); fail++; }
    printf("%s (%d failures)\n", fail ? "FAIL" : "PASS", fail);
    return fail != 0;
}
