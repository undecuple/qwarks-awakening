/* Host test for R&C patch 7049: rsx_fp_apply_gamma takes 4 bits per unit (R, G, B, A) and wraps each sample of a
 * gamma unit in rsx_srgb(..., float4(r, g, b, a)); the helper decodes alpha where the mask says so. */
#include <stdio.h>
#include <string.h>
#include "rsx_fp_decompiler.h"

static int fails;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); fails++; } } while (0)

int main(void)
{
    char h[4096];
    /* Unit 7 with field 0xF (ToD's environment cube) and unit 1 with 0x7 (R/G/B only). */
    strcpy(h, "float4 main() { float4 a = rsx_tex[7].Sample(s, uv); float4 b = rsx_tex1.Sample(s, uv); return a + b; }\n");
    const unsigned long long mask = (0xFull << (7 * 4)) | (0x7ull << (1 * 4));
    const int n = rsx_fp_apply_gamma(h, sizeof h, mask);
    CHECK(n == 2, "two sample sites wrapped");
    CHECK(strstr(h, "rsx_srgb(rsx_tex[7].Sample(s, uv), float4(1, 1, 1, 1))") != NULL, "unit 7 decodes R, G, B and A");
    CHECK(strstr(h, "rsx_srgb(rsx_tex1.Sample(s, uv), float4(1, 1, 1, 0))") != NULL, "unit 1 keeps alpha");
    CHECK(strstr(h, "float4 rsx_srgb(float4 c, float4 m)") != NULL, "helper takes a float4 mask");
    CHECK(strstr(h, "rsx_TEX") == NULL, "done markers restored");

    /* Alpha-only field (0x8) on unit 15: the top nibble of the u64. */
    strcpy(h, "float4 main() { return rsx_tex[15].Sample(s, uv); }\n");
    CHECK(rsx_fp_apply_gamma(h, sizeof h, 0x8ull << 60) == 1, "unit 15 alpha-only wrapped");
    CHECK(strstr(h, "float4(0, 0, 0, 1))") != NULL, "unit 15 decodes alpha only");

    /* No neighbour bleed: unit 2's bits must not wrap unit 3. */
    strcpy(h, "float4 main() { return rsx_tex[3].Sample(s, uv); }\n");
    CHECK(rsx_fp_apply_gamma(h, sizeof h, 0xFull << 8) == 0, "unit 2 bits leave unit 3 alone");

    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("gamma alpha test: ok\n");
    return 0;
}
