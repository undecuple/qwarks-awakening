/* Host test (no GPU): KZ3 patch 2076's GPU vertex fetch (rsx_vtx_gpu.h) against the CPU fetch (rsx_vertex_fetch_many),
 * for random layouts over every RSX vertex type, size, stride, offset and alignment, and for ToD's layouts (stride 20:
 * SINT16x4 @0, HALF2 @8, CMP32 @12; plus UNORM8x4 / FLOAT). The GPU side is the shared decode compiled as C
 * (RSX_VG_C_REFERENCE) reading the block rsx_vg_plan + rsx_vg_write_block build, with the element index the walker
 * uploads (rsx_vg_element). Any float that differs bit-for-bit is reported with its draw, attribute and component.
 * Exit 0 = PASS. Build: run_vtx_gpu_test.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rsx_dispatch.h"
#include "rsx_vertex_compact.h"
#include "rsx_vertex_formats.h"
static const unsigned char* g_blk; static const float* g_lut;
static unsigned vg_raw_load(unsigned a) { unsigned w; memcpy(&w, g_blk + a, 4); return w; }   /* little-endian host */
#define VG_RAW(a) vg_raw_load(a)
#define VG_LUT(i) g_lut[i]
#define RSX_VG_C_REFERENCE 1
#include "rsx_vtx_gpu.h"

static unsigned char* mem;
static const u8* gp(void* u, u32 loc, u32 off, u32 n) { (void)u; (void)n; return mem + (loc ? 0x800000u : 0u) + off; }
static unsigned rs = 12345u;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static int fails, draws_checked, gpu_draws;

static void one_draw(int nattr, const u32 types[], const u32 sizes[], const u32 strides[], const u32 offs[], const u32 locs[],
                     u32 base_off, u32 nref, u32 first, u32 base_index, const char* tag)
{
    static rsx_dispatch r; memset(&r, 0, sizeof r);
    u32 mask = 0;
    for (int a = 0; a < nattr; a++) {
        r.regs[(0x1740 >> 2) + a] = types[a] | (sizes[a] << 4) | (strides[a] << 8);
        r.regs[(0x1680 >> 2) + a] = offs[a] | (locs[a] << 31);
        mask |= 1u << a;
    }
    r.regs[0x1738 >> 2] = base_off;   /* VERTEX_DATA_BASE_OFFSET */
    rsx_vertex_layout_plan layout; rsx_vertex_layout_plan_init(&layout, mask);
    static rsx_vertex_fetch_plan plan;
    rsx_vertex_fetch_plan_init(&plan, &r, &layout, gp, NULL);
    rsx_vertex_ref refs[512];
    for (u32 i = 0; i < nref; i++) { refs[i].vertex_id = first + (rnd() % (nref * 2)); refs[i].base_index = base_index; }
    rsx_vertex_fetch_plan_prepare(&plan, refs, nref);
    static u8 cpu[512 * 16 * 16];
    /* ps3recomp v0.18.0: the output stride is the layout's (vertex i at cpu + i * layout.stride, slot s 16 bytes in) */
    if (!rsx_vertex_fetch_many(&plan, refs, nref, cpu)) return;
    static rsx_vg_draw d;
    const int why = rsx_vg_plan(&plan, 8u << 20, &d);
    draws_checked++;
    if (why != RSX_VG_OK) return;
    gpu_draws++;
    static unsigned char blk[9u << 20];
    rsx_vg_write_block(&d, blk); g_blk = blk;
    for (u32 i = 0; i < nref; i++) {
        const u32 e = rsx_vg_element(&refs[i]);
        for (u32 s = 0; s < layout.count; s++) {
            const u32 a = layout.attrs[s];
            const rsx_vg_attr* t = &d.table[a];
            for (u32 c = 0; c < 4; c++) {
                const float g = vg_fetch(t->base, t->stride, t->fmt, t->lim, t->dflt[c], e, c);
                float h; memcpy(&h, cpu + i * layout.stride + s * 16 + c * 4, 4);
                if (memcmp(&g, &h, 4)) {
                    if (fails < 20) printf("MISMATCH %s ref %u (vid %u base %u) attr %u type %u size %u stride %u off %u comp %u: gpu %g cpu %g\n",
                                           tag, i, refs[i].vertex_id, refs[i].base_index, a, types[a], sizes[a], strides[a], offs[a], c, g, h);
                    fails++;
                }
            }
        }
    }
}

int main(void)
{
    mem = calloc(1, 0x1000000u);
    for (u32 i = 0; i < 0x1000000u; i++) mem[i] = (unsigned char)rnd();
    for (u32 i = 0; i < 0x1000000u; i += 4) { if ((mem[i] & 0x7C) == 0x7C) mem[i] &= 0x83; }   /* fewer HALF NaN/Inf */
    static float lut[RSX_VG_LUT_WORDS]; rsx_vg_lut_build(lut); g_lut = lut;
    /* ToD's scene layout and variants */
    { const u32 t[] = {5,3,6}, s[] = {4,2,1}, st[] = {20,20,20}, o[] = {0x10000,0x10008,0x1000C}, l[] = {0,0,0};
      for (int k = 0; k < 200; k++) one_draw(3, t, s, st, o, l, 0, 1 + rnd() % 400, rnd() % 5000, rnd() % 3 ? 0 : rnd() % 100, "tod"); }
    { const u32 t[] = {2,4,3,6,4}, s[] = {3,4,2,1,4}, st[] = {32,32,32,32,32}, o[] = {0x20001,0x2000D,0x20011,0x20015,0x20019}, l[] = {1,1,1,1,1};
      for (int k = 0; k < 200; k++) one_draw(5, t, s, st, o, l, 0x40, 1 + rnd() % 300, rnd() % 3000, 0, "mix"); }
    for (int k = 0; k < 3000; k++) {   /* random */
        const int n = 1 + rnd() % 6; u32 t[16], s[16], st[16], o[16], l[16];
        for (int a = 0; a < n; a++) {
            t[a] = 1 + rnd() % 7; s[a] = t[a] == 6 ? 1 : 1 + rnd() % 4;
            const u32 es = rsx_vertex_attrib_size(t[a], s[a]);
            st[a] = es + rnd() % 24; if (st[a] > 255) st[a] = 255;
            o[a] = (rnd() % 0x40000) ; l[a] = rnd() & 1;
        }
        one_draw(n, t, s, st, o, l, rnd() % 64, 1 + rnd() % 500, rnd() % 20000, rnd() % 4 ? 0 : rnd() % 1000, "rand");
    }
    printf("draws %d, on the GPU path %d, mismatching floats %d -> %s\n", draws_checked, gpu_draws, fails, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
