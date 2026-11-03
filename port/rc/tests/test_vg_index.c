/* Host test + micro-benchmark (no GPU): R&C patch 7013's direct index path for GPU-fetch draws (rsx_vg_index.h) against
 * the old refs/remap path, through the real libs/video code (rsx_vertex_compact.c, rsx_vtx_gpu.h, rsx_dispatch.c).
 * Per index occurrence k of a draw:
 *   old path: refs {index, base} -> rsx_vertex_remap_build -> the shader reads payload[remap(k)] = rsx_vg_element(ref),
 *             decoded through the block planned over all refs;
 *   direct  : the D3D index buffer holds rsx_vg_index_convert's value v_k, the vertex fetched is identity[v_k + base]
 *             (BaseVertexLocation = base index), decoded through the block planned over {min, max}.
 * Both must give the same element and the same decoded float4 for every attribute (vg_fetch as C). Cases: ToD's layout
 * (stride 20: SINT16x4, HALF2, CMP32) and random layouts; 16- and 32-bit indices; base index 0 / random; index buffer at
 * odd offsets; a restart value in the data (direct path must decline); a wrap past 20 bits (must decline).
 * Mode "bench": ToD's per-frame counts (3.6M index references, 1.3M unique vertices over ~6,964 draws): time of refs+remap
 * (old) vs index conversion (direct). Exit 0 = PASS. Build: run_vg_index_test.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rsx_dispatch.h"
#include "rsx_vertex_compact.h"
#include "rsx_vertex_formats.h"
static const unsigned char* g_blk; static const float* g_lut;
static unsigned vg_raw_load(unsigned a) { unsigned w; memcpy(&w, g_blk + a, 4); return w; }
#define VG_RAW(a) vg_raw_load(a)
#define VG_LUT(i) g_lut[i]
#define RSX_VG_C_REFERENCE 1
#include "rsx_vtx_gpu.h"
#include "rsx_vg_index.h"

static unsigned char* mem;
static const u8* gp(void* u, u32 loc, u32 off, u32 n) { (void)u; (void)n; return mem + (loc ? 0x800000u : 0u) + off; }
static unsigned rs = 777u;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static int fails, checked, declined_ok;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void setup(rsx_dispatch* r, int nattr, const u32* t, const u32* s, const u32* st, const u32* o, u32* mask)
{
    memset(r, 0, sizeof *r); *mask = 0;
    for (int a = 0; a < nattr; a++) {
        r->regs[(0x1740 >> 2) + a] = t[a] | (s[a] << 4) | (st[a] << 8);
        r->regs[(0x1680 >> 2) + a] = o[a];
        *mask |= 1u << a;
    }
}
/* one draw: n indices (multiple of 3) drawn from [lo_v, lo_v + span), stored big-endian at ib_off */
static void one_draw(int nattr, const u32* t, const u32* s, const u32* st, const u32* o, u32 n, int is_u32, u32 base,
                     u32 lo_v, u32 span, u32 ib_off, int restart_at, u32 restart_value, const char* tag)
{
    static rsx_dispatch r; u32 mask;
    setup(&r, nattr, t, s, st, o, &mask);
    rsx_vertex_layout_plan layout; rsx_vertex_layout_plan_init(&layout, mask);
    u8* ib = mem + 0x700000u + ib_off;
    static u32 idx[60000];
    for (u32 k = 0; k < n; k++) {
        u32 v = lo_v + rnd() % span;
        if ((int)k == restart_at) v = restart_value;
        idx[k] = v;
        if (is_u32) { ib[k*4] = v >> 24; ib[k*4+1] = v >> 16; ib[k*4+2] = v >> 8; ib[k*4+3] = v; }
        else        { ib[k*2] = (u8)(v >> 8); ib[k*2+1] = (u8)v; }
    }
    /* direct path (what ld_vg_try_direct does) */
    static u32 dir[60000]; u32 lo, hi, e_lo, e_hi;
    const int conv = rsx_vg_index_convert(ib, n, is_u32, restart_at >= 0, restart_value, dir, &lo, &hi);
    const int range = conv && rsx_vg_index_range(lo, hi, base, &e_lo, &e_hi);
    if (restart_at >= 0 || (u64)lo_v + span - 1 + base >= RSX_VG_ELEMENT_LIMIT) {   /* must decline */
        if (conv && range) { printf("FAIL %s: direct path taken with a restart value / a 20-bit wrap\n", tag); fails++; }
        else declined_ok++;
        return;
    }
    if (!conv || !range) { printf("FAIL %s: direct path declined a plain draw\n", tag); fails++; return; }
    static rsx_vertex_fetch_plan pd; static rsx_vg_draw dd;
    rsx_vertex_ref span2[2] = { { lo, base }, { hi, base } };
    rsx_vertex_fetch_plan_init(&pd, &r, &layout, gp, NULL);
    rsx_vertex_fetch_plan_prepare(&pd, span2, 2);
    if (rsx_vg_plan(&pd, 8u << 20, &dd) != RSX_VG_OK) return;
    static unsigned char bd[9u << 20]; rsx_vg_write_block(&dd, bd);
    /* old path */
    static rsx_vertex_ref refs[60000]; static rsx_vertex_remap remap; u32 uniq = n;
    for (u32 k = 0; k < n; k++) { refs[k].vertex_id = idx[k]; refs[k].base_index = base; }
    if (!rsx_vertex_remap_build(&remap, refs, n, &uniq)) { printf("remap failed\n"); fails++; return; }
    static rsx_vertex_fetch_plan po; static rsx_vg_draw dold;
    rsx_vertex_fetch_plan_init(&po, &r, &layout, gp, NULL);
    rsx_vertex_fetch_plan_prepare(&po, refs, uniq);
    if (rsx_vg_plan(&po, 8u << 20, &dold) != RSX_VG_OK) { printf("FAIL %s: old plan refused\n", tag); fails++; return; }
    static unsigned char bo[9u << 20]; rsx_vg_write_block(&dold, bo);
    checked++;
    for (u32 k = 0; k < n; k++) {
        const u32 e_old = rsx_vg_element(&refs[rsx_vertex_remap_index(&remap, k)]);
        const u32 e_dir = dir[k] + base;   /* identity[index + BaseVertexLocation] */
        if (e_old != e_dir) { if (fails < 10) printf("FAIL %s k=%u element old %u direct %u\n", tag, k, e_old, e_dir); fails++; continue; }
        for (u32 sl = 0; sl < layout.count; sl++) {
            const u32 a = layout.attrs[sl];
            for (u32 c = 0; c < 4; c++) {
                g_blk = bo; const rsx_vg_attr* to = &dold.table[a];
                const float fo = vg_fetch(to->base, to->stride, to->fmt, to->lim, to->dflt[c], e_old, c);
                g_blk = bd; const rsx_vg_attr* td = &dd.table[a];
                const float fd = vg_fetch(td->base, td->stride, td->fmt, td->lim, td->dflt[c], e_dir, c);
                if (memcmp(&fo, &fd, 4)) { if (fails < 10) printf("FAIL %s k=%u attr %u c %u old %g direct %g\n", tag, k, a, c, fo, fd); fails++; }
            }
        }
    }
}

static void bench(void)
{
    /* ~6,964 draws/frame, 3.6M references, 1.3M unique vertices: ~517 references and ~187 unique per draw */
    const u32 draws = 6964, per = 516, uniq = 187;
    static u8 ib[516 * 2]; static u32 dir[516]; static rsx_vertex_ref refs[516]; static rsx_vertex_remap remap;
    double t_old = 0, t_dir = 0; volatile u32 sink = 0;
    for (int rep = 0; rep < 3; rep++) {
        double t0 = now();
        for (u32 d = 0; d < draws; d++) {
            const u32 base = (d * 37u) % 40000u;
            for (u32 k = 0; k < per; k++) { const u32 v = base + (k * 7919u) % uniq; ib[k*2] = (u8)(v >> 8); ib[k*2+1] = (u8)v; }
            u32 n = 0;   /* old: refs (as fetch_batches_hoisted) + remap */
            for (u32 k = 0; k < per; k++) { refs[n].vertex_id = ((u32)ib[k*2] << 8) | ib[k*2+1]; refs[n].base_index = 0; n++; }
            u32 u = n; rsx_vertex_remap_build(&remap, refs, n, &u); sink += u;
        }
        t_old += now() - t0;
        t0 = now();
        for (u32 d = 0; d < draws; d++) {
            const u32 base = (d * 37u) % 40000u;
            for (u32 k = 0; k < per; k++) { const u32 v = base + (k * 7919u) % uniq; ib[k*2] = (u8)(v >> 8); ib[k*2+1] = (u8)v; }
            u32 lo, hi; rsx_vg_index_convert(ib, per, 0, 0, 0, dir, &lo, &hi); sink += hi - lo;
        }
        t_dir += now() - t0;
    }
    /* the ib fill is in both loops; time it alone and subtract */
    double t0 = now();
    for (int rep = 0; rep < 3; rep++)
        for (u32 d = 0; d < draws; d++) { const u32 base = (d * 37u) % 40000u;
            for (u32 k = 0; k < per; k++) { const u32 v = base + (k * 7919u) % uniq; ib[k*2] = (u8)(v >> 8); ib[k*2+1] = (u8)v; } sink += ib[3]; }
    const double t_fill = now() - t0;
    printf("bench per frame (%u draws, %u refs, %u unique): old refs+remap %.2f ms, direct index conversion %.2f ms"
           " (also no 4-byte element payload per unique vertex)\n", draws, draws * per, draws * uniq,
           (t_old - t_fill) / 3 * 1e3, (t_dir - t_fill) / 3 * 1e3);
}

int main(int argc, char** argv)
{
    mem = calloc(1, 0x1000000u);
    for (u32 i = 0; i < 0x700000u; i++) mem[i] = (unsigned char)rnd();
    static float lut[RSX_VG_LUT_WORDS]; rsx_vg_lut_build(lut); g_lut = lut;
    if (argc > 1 && !strcmp(argv[1], "bench")) { bench(); return 0; }
    { const u32 t[] = {5,3,6}, s[] = {4,2,1}, st[] = {20,20,20}, o[] = {0x10000,0x10008,0x1000C};
      for (int k = 0; k < 300; k++) {
          const int u32i = rnd() & 1; const u32 base = rnd() % 3 ? 0 : rnd() % 2000;
          one_draw(3, t, s, st, o, 3 * (1 + rnd() % 400), u32i, base, rnd() % 20000, 1 + rnd() % 3000, (rnd() % 64) * 2, -1, 0, "tod"); } }
    for (int k = 0; k < 2000; k++) {
        const int n = 1 + rnd() % 5; u32 t[8], s[8], st[8], o[8];
        for (int a = 0; a < n; a++) { t[a] = 1 + rnd() % 7; s[a] = t[a] == 6 ? 1 : 1 + rnd() % 4;
            st[a] = rsx_vertex_attrib_size(t[a], s[a]) + rnd() % 16; o[a] = rnd() % 0x20000; }
        const int u32i = rnd() & 1;
        one_draw(n, t, s, st, o, 3 * (1 + rnd() % 300), u32i, rnd() % 4 ? 0 : rnd() % 5000, rnd() % (u32i ? 100000 : 60000),
                 1 + rnd() % 2000, (rnd() % 32) * 2, -1, 0, "rand");
    }
    { const u32 t[] = {5}, s[] = {4}, st[] = {8}, o[] = {0x100};   /* must decline: restart value present; 20-bit wrap */
      one_draw(1, t, s, st, o, 30, 0, 0, 0, 100, 0, 7, 0xFFFF, "restart16");
      one_draw(1, t, s, st, o, 30, 1, 0, 0, 100, 0, 11, 0xFFFFFFFF, "restart32");
      one_draw(1, t, s, st, o, 30, 1, 0x80000, 0x7FF00, 0x200, 0, -1, 0, "wrap"); }
    printf("draws compared %d, correctly declined %d, mismatches %d -> %s\n", checked, declined_ok, fails, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
