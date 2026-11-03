/* Host test (no GPU, no game data): INLINE_ARRAY vertices ignore the frequency divider (R&C patch 7009, upstream
 * issue #457 E1). Builds against the runtime's libs/video/rsx_vertex_compact.c + rsx_dispatch.c (the hoisted
 * vertex fetch live draw uses for every draw). RSX rule (RPCS3 RSXDrawCommands.cpp analyse_inputs_interleaved,
 * read): an inlined-array draw registers every attribute with frequency 1 and no modulo; vertex n reads element n.
 * Setup: ATTR0 = FLOAT x2, stride 8, with a stale frequency of 4 left in its format word (division, then modulo via
 * the 0x1FC0 mask); 8 inline vertices whose x is the vertex number. Checks rsx_vertex_fetch_many and
 * rsx_vertex_fetch_one: x of vertex n must be n. Exit 0 = PASS. */
#include "rsx_vertex_compact.h"
#include "rsx_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_be_f32(unsigned char* p, float f) { unsigned w; memcpy(&w, &f, 4); p[0] = w >> 24; p[1] = w >> 16; p[2] = w >> 8; p[3] = w; }

static int run(int modulo, int many)
{
    rsx_dispatch* rsx = calloc(1, sizeof *rsx);
    rsx->regs[0x1740 >> 2] = 2u /* FLOAT */ | (2u << 4) /* size 2 */ | (8u << 8) /* stride 8 */ | (4u << 16) /* stale frequency 4 */;
    rsx->regs[0x1FC0 >> 2] = modulo ? 1u : 0u;
    unsigned char data[8 * 8];
    for (int n = 0; n < 8; n++) { put_be_f32(data + n * 8, (float)n); put_be_f32(data + n * 8 + 4, 100.0f + n); }
    rsx_vertex_layout_plan layout; rsx_vertex_layout_plan_init(&layout, 1u);
    static rsx_vertex_fetch_plan plan;
    rsx_vertex_fetch_plan_init(&plan, rsx, &layout, NULL, NULL);
    rsx_vertex_fetch_plan_set_inline(&plan, rsx, data, sizeof data);
    rsx_vertex_ref refs[8];
    for (int n = 0; n < 8; n++) { refs[n].vertex_id = (unsigned)n; refs[n].base_index = 0; }
    rsx_vertex_fetch_plan_prepare(&plan, refs, 8);
    float out[8][4]; memset(out, 0, sizeof out);
    int ok = 1;
    if (many) ok = rsx_vertex_fetch_many(&plan, refs, 8, (u8*)out);   /* v0.18.0: the stride is the plan's (16 here) */
    else for (int n = 0; n < 8 && ok; n++) ok = rsx_vertex_fetch_one(&plan, &refs[n], (u8*)out[n]);
    int bad = !ok;
    for (int n = 0; n < 8; n++) if (out[n][0] != (float)n || out[n][1] != 100.0f + n) bad++;
    printf("%s %s: x =", modulo ? "modulo  " : "division", many ? "fetch_many" : "fetch_one ");
    for (int n = 0; n < 8; n++) printf(" %g", out[n][0]);
    printf("  -> %s\n", bad ? "FAIL (vertex n must read element n)" : "ok");
    free(rsx);
    return bad;
}

int main(void)
{
    int bad = run(0, 1) + run(0, 0) + run(1, 1) + run(1, 0);
    printf("%s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
