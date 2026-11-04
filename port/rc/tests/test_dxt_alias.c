/* Host test for R&C patch 7035 (rsx_dxt_alias.h): a block-compressed texture never samples a render surface. */
#include <stdio.h>
#include "rsx_dxt_alias.h"

static int fails;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); fails++; } } while (0)

int main(void)
{
    /* ToD's "no texture" unit: 16x16 DXT1 at 0:0x0, format byte 0x86 (tods-gfx-cap3 rows "2:0:0:86:16x16"). */
    CHECK(!ld_tex_may_alias_surface(0x86), "DXT1 swizzled");
    CHECK(!ld_tex_may_alias_surface(0x87), "DXT23");
    CHECK(!ld_tex_may_alias_surface(0x88), "DXT45 (ToD's 0x88 material maps)");
    /* the LN (0x20) and UN (0x40) modifier bits do not change the base format */
    CHECK(!ld_tex_may_alias_surface(0x86 | 0x20), "DXT1 linear");
    CHECK(!ld_tex_may_alias_surface(0x88 | 0x40), "DXT45 unnormalised");
    CHECK(!ld_tex_may_alias_surface(0x87 | 0x60), "DXT23 linear unnormalised");
    /* bits above the format byte are ignored */
    CHECK(!ld_tex_may_alias_surface(0x1A86), "DXT1 with high bits");
    /* every surface-capable read keeps the alias: ToD's resolve and bloom reads (0xE5 = A8R8G8B8 LN|UN), A8R8G8B8,
     * B8, R5G6B5, DEPTH24_D8, DEPTH16 (0xB2 shadow maps), Y16X16_FLOAT, W16Z16Y16X16_FLOAT, X32_FLOAT */
    const unsigned keep[] = { 0xE5, 0x85, 0xA5, 0x81, 0x84, 0x90, 0x92, 0xB2, 0x9F, 0x9A, 0x9B };
    for (unsigned i = 0; i < sizeof keep / sizeof keep[0]; i++) {
        char msg[64]; snprintf(msg, sizeof msg, "format 0x%02X keeps the alias", keep[i]);
        CHECK(ld_tex_may_alias_surface(keep[i]), msg);
    }
    /* exhaustive: exactly three base formats (x4 modifier combinations) are refused */
    unsigned refused = 0;
    for (unsigned f = 0; f < 256; f++) refused += !ld_tex_may_alias_surface(f);
    CHECK(refused == 12, "exactly DXT1/23/45 x {plain, LN, UN, LN|UN} refused");
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    printf("test_dxt_alias: all passed\n");
    return 0;
}
