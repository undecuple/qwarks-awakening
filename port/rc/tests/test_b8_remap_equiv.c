/* Host test (no GPU): patch 2036's R8 direct-upload SRV mapping (ld_r8_mapping) must read exactly what the texel
 * decode (decode_texel B8 case + remap_comp) gave, for EVERY 16-bit remap word and EVERY byte value. The two
 * functions below are copied from third_party/ps3recomp libs/video/rsx_live_draw.c (+ patch 2036) so the test
 * builds without D3D12; the D3D12 side is modelled (R8_UNORM samples as R=v G=0 B=0 A=1, component mapping
 * values 0..3 = R,G,B,A, 4 = 0, 5 = 1). Rules out the B8 remap path as the cause of the green patch (R&C).
 * gcc -O1 -Wall -o t port/rc/tests/test_b8_remap_equiv.c && ./t */
#include <stdio.h>
typedef unsigned char u8; typedef unsigned int u32;
static u8 remap_comp(const u8 s[4], u32 remap, u32 comp)
{
    const u32 op = (remap >> (8 + comp * 2)) & 3, sel = (remap >> (comp * 2)) & 3;
    if (op == 0) return 0;
    if (op == 1) return 255;
    return s[sel];
}
static void decode_b8(u8 p, u32 remap, u8 d[4])
{
    u8 s[4] = { 255, p, p, p };
    d[0] = remap_comp(s, remap, 1); d[1] = remap_comp(s, remap, 2);
    d[2] = remap_comp(s, remap, 3); d[3] = remap_comp(s, remap, 0);
}
static u32 ld_r8_mapping(u32 remap)
{
    static const u32 out2comp[4] = { 1, 2, 3, 0 };
    u32 m = 1u << 12;
    for (u32 out = 0; out < 4; out++) {
        const u32 comp = out2comp[out];
        const u32 op = (remap >> (8 + comp * 2)) & 3;
        const u32 sel = (remap >> (comp * 2)) & 3;
        const u32 v = op == 0 ? 4u : op == 1 ? 5u : sel == 0 ? 5u : 0u;
        m |= v << (3 * out);
    }
    return m;
}
static void sample_r8(u8 v, u32 mapping, u8 d[4])
{
    const u8 res[6] = { v, 0, 0, 255, 0, 255 };
    for (int o = 0; o < 4; o++) d[o] = res[(mapping >> (3 * o)) & 7];
}
int main(void)
{
    int bad = 0;
    for (u32 remap = 0; remap < 0x10000; remap++)
        for (u32 v = 0; v < 256; v += 51) {
            u8 a[4], b[4];
            decode_b8((u8)v, remap, a);
            sample_r8((u8)v, ld_r8_mapping(remap), b);
            for (int i = 0; i < 4; i++) if (a[i] != b[i]) { if (bad++ < 5) printf("mismatch remap=%04X v=%u ch=%d %u vs %u\n", remap, v, i, a[i], b[i]); }
        }
    printf("b8 remap equivalence: %s (%d mismatches)\n", bad ? "FAIL" : "PASS", bad);
    return bad != 0;
}
