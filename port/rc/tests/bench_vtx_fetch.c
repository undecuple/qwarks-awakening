/* Build (one compiler, light): gcc -std=gnu11 -O2 -w -I<tree>/libs/video -I<tree>/include -I<tree> port/rc/tests/bench_vtx_fetch.c \
 *   <tree>/libs/video/rsx_vertex_compact.c <tree>/libs/video/rsx_dispatch.c -lm   (tree = work/ps3recomp-patched). R&C pass 14. */
/* Host benchmark: rsx_vertex_fetch_many on a ToD-like vertex layout (stride 20: SINT16x4 pos, HALF2 uv, CMP32 normal,
 * UNORM8x4 colour, HALF2 uv2), 126-vertex draws (the median of tods-cyan-c2 frame 6986), 3.2M vertices per "frame". */
#include "rsx_vertex_compact.h"
#include "rsx_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned char* mem;
static const u8* gp(void* u, u32 loc, u32 off, u32 n) { (void)u; (void)loc; (void)n; return mem + off; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(void)
{
    const u32 nverts = 1u << 20, stride = 20;
    mem = malloc((size_t)nverts * stride + 4096);
    for (size_t i = 0; i < (size_t)nverts * stride; i++) mem[i] = (unsigned char)(i * 131 + 7);
    rsx_dispatch* r = calloc(1, sizeof *r);
    /* VTXFMT: type | size<<4 | stride<<8 ; VTXBUF_OFFSET */
    const u32 fmts[5][3] = { {5,4,0}, {3,2,8}, {6,1,12}, {4,4,16}, {3,2,8} };   /* SINT16x4@0 HALF2@8 CMP@12 UNORM8x4@16 HALF2@8 */
    for (int a = 0; a < 5; a++) { r->regs[(0x1740 >> 2) + a] = fmts[a][0] | (fmts[a][1] << 4) | (stride << 8); r->regs[(0x1680 >> 2) + a] = fmts[a][2]; }
    rsx_vertex_layout_plan layout; rsx_vertex_layout_plan_init(&layout, 0x1F);
    static rsx_vertex_fetch_plan plan;
    const u32 per_draw = 126, draws = 3216918 / per_draw;
    rsx_vertex_ref refs[126]; u8* out = malloc((size_t)per_draw * layout.stride);
    const double t0 = now(); u32 base = 0;
    for (u32 d = 0; d < draws; d++) {
        rsx_vertex_fetch_plan_init(&plan, r, &layout, gp, NULL);
        for (u32 i = 0; i < per_draw; i++) { refs[i].vertex_id = base + i; refs[i].base_index = 0; }
        rsx_vertex_fetch_plan_prepare(&plan, refs, per_draw);
        if (!rsx_vertex_fetch_many(&plan, refs, per_draw, out, layout.stride)) { printf("fetch failed\n"); return 1; }
        base = (base + per_draw) % (nverts - per_draw);
    }
    const double t = now() - t0;
    printf("%u draws x %u verts (%u B payload/vertex): %.1f ms per frame, %.1f ns/vertex, %.1f us/draw\n",
           draws, per_draw, layout.stride, t * 1e3, t * 1e9 / ((double)draws * per_draw), t * 1e6 / draws);
    return 0;
}
