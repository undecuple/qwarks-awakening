/* Host test (no GPU, no game data): the CLEAR_SURFACE rectangle on an antialiased live surface (R&C patch 7010,
 * libs/video/rsx_clear_rect.h), driven through the real rsx_dispatch.c register decode (rsx_dsp_get_surface).
 * Case "tod": ToD's Metropolis scene clear (tods-cyan-c2 #clear row): SURFACE_FORMAT antialias 3 (2x horizontal),
 * RT 960x704 pixels -> clip 1920x704 samples, scissor 960x704 at 0,0 -> the clear must cover samples 0..1920 x 0..704.
 * The "old" line evaluates the pre-7010 rule (scissor not scaled) on the same registers for comparison.
 * Also: no AA, 4x (2x2) with an offset scissor, an empty scissor, no scissor written. Exit 0 = PASS. */
#include "rsx_clear_rect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
static void set(rsx_dispatch* r, u32 m, u32 v) { r->regs[m >> 2] = v; r->seen[m >> 2]++; }
static int old_rule(const rsx_dsp_surface* sf, u32 sh, u32 sv, rsx_clear_rect_t* o)   /* the code 7010 replaces */
{
    long l = 0, t = 0, r = (long)sf->clip_w, b = (long)sf->clip_h;
    const long sx = sh & 0xFFFF, sw = sh >> 16, sy = sv & 0xFFFF, svh = sv >> 16;
    if (sx > l) l = sx; if (sy > t) t = sy; if (sx + sw < r) r = sx + sw; if (sy + svh < b) b = sy + svh;
    if (r <= l || b <= t) return -1;
    o->left = l; o->top = t; o->right = r; o->bottom = b; return 1;
}
static void check(const char* name, u32 aa, u32 w, u32 h, int scis, u32 sx, u32 sy, u32 sw, u32 shh,
                  int want_k, long wl, long wt, long wr, long wb)
{
    rsx_dispatch* r = calloc(1, sizeof *r);
    set(r, 0x0208, (aa << 12) | 5u);            /* SURFACE_FORMAT: colour A8R8G8B8, antialias field */
    set(r, 0x0200, w << 16); set(r, 0x0204, h << 16);
    const u32 sh = (sw << 16) | sx, sv = (shh << 16) | sy;
    if (scis) { set(r, 0x08C0, sh); set(r, 0x08C4, sv); }
    rsx_dsp_surface sf; memset(&sf, 0, sizeof sf); rsx_dsp_get_surface(r, &sf);
    rsx_clear_rect_t c = {0, 0, 0, 0}, o = {0, 0, 0, 0};
    const int k = rsx_clear_rect(&sf, scis, sh, sv, &c);
    const int ok = k == want_k && (k != 1 || (c.left == wl && c.top == wt && c.right == wr && c.bottom == wb));
    printf("%-26s clip %ux%u -> k=%d rect %ld,%ld..%ld,%ld", name, sf.clip_w, sf.clip_h, k, c.left, c.top, c.right, c.bottom);
    if (scis) { const int ko = old_rule(&sf, sh, sv, &o); printf("   (old rule: k=%d %ld,%ld..%ld,%ld)", ko, o.left, o.top, o.right, o.bottom); }
    printf("  %s\n", ok ? "ok" : "FAIL");
    if (!ok) fails++;
    free(r);
}
int main(void)
{
    check("tod 2x horizontal",     3,  960, 704, 1,   0,  0, 960, 704,  1, 0, 0, 1920, 704);
    check("no AA",                 0, 1280, 720, 1,   0,  0, 1280, 704, 1, 0, 0, 1280, 704);
    check("4x (2x2), offset",      4,  640, 360, 1, 100, 20, 200, 100,  1, 200, 40, 600, 240);
    check("empty scissor",         3,  960, 704, 1, 100,  0,   0, 704, -1, 0, 0, 0, 0);
    check("no scissor written",    3,  960, 704, 0,   0,  0,   0,   0,  1, 0, 0, 1920, 704);
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
