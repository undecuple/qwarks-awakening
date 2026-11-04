/* Host test for R&C patch 7036 (rsx_cube_face.h): a swizzled cube map's faces start on 128-byte boundaries. */
#include <stdio.h>
#include <stdint.h>
#include "rsx_cube_face.h"

static int fails;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); fails++; } } while (0)

/* bytes of one face's mip chain, uncompressed square texture */
static uint32_t chain(uint32_t w, uint32_t texel, uint32_t mips)
{
    uint32_t s = 0;
    for (uint32_t m = 0; m < mips; m++) { uint32_t d = w >> m; if (!d) d = 1; s += d * d * texel; }
    return s;
}

int main(void)
{
    /* ToD's environment cube (Fastoon, unit 7, 1:0x266DC80 fmt 0x85, 32x32, 6 mips): 5460 bytes a face, faces
     * 5504 apart; the decoded faces are coherent only at 5504 (tods-gfx-post draw-inputs u07.bin). */
    CHECK(chain(32, 4, 6) == 5460, "ToD cube chain is 5460 bytes");
    CHECK(ld_cube_face_stride(5460, 0) == 5504, "ToD cube face stride 5504");
    CHECK(ld_cube_face_stride(5460, 1) == 5460, "a pitch-linear cube is not padded");
    /* a chain that is already a multiple of 128 does not move */
    CHECK(ld_cube_face_stride(chain(64, 4, 1), 0) == 16384, "64x64 A8R8G8B8 single level");
    CHECK(ld_cube_face_stride(128, 0) == 128, "exact 128");
    /* DXT1 16x16 with 3 levels (128 + 32 + 8 bytes of blocks) */
    CHECK(ld_cube_face_stride(168, 0) == 256, "DXT1 16x16 chain padded to 256");
    /* every swizzled stride is 128-aligned, at least the chain, and less than 128 bytes over it */
    for (uint32_t b = 1; b < 70000; b++) {
        const uint32_t s = ld_cube_face_stride(b, 0);
        if ((s & 127u) || s < b || s - b > 127u) { fprintf(stderr, "FAIL: stride of %u is %u\n", b, s); fails++; break; }
        if (ld_cube_face_stride(b, 1) != b) { fprintf(stderr, "FAIL: linear stride of %u\n", b); fails++; break; }
    }
    if (fails) { fprintf(stderr, "test_cube_face: %d failed\n", fails); return 1; }
    printf("test_cube_face: all passed\n");
    return 0;
}
