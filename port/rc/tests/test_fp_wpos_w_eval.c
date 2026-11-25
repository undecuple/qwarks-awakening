/* Value check for R&C patch 7108: evaluates the decompiler's WPOS helper macros (dumped by test_fp_wpos_w.c) as C.
 * Build with -include <dump.h>, -DHOST_W=<SV_POSITION.w the host gives> and optionally -DRSX_FRAGCOORD_W_IS_RCP.
 * The fragment's clip w is 4, so the RSX's WPOS.w is 0.25: FXC gives SV_POSITION.w = 4, glslang's FragCoord.w = 0.25.
 * x, y and z pass through (fp_alpha is zero: no window flip, no pixel-centre bias). */
#include <stdio.h>

typedef struct { float x, y, z; } f3;
typedef struct { union { struct { float x, y, z; }; f3 xyz; }; float w; } f4;

static f4 mk4_1111(float x, float y, float z, float w) { f4 r; r.x = x; r.y = y; r.z = z; r.w = w; return r; }
static f4 mk4_31(f3 v, float w) { f4 r; r.xyz = v; r.w = w; return r; }
/* HLSL's float4 constructor: four scalars or a float3 and a scalar. */
#define float4(a, ...) _Generic((a), f3: mk4_31, default: mk4_1111)((a), __VA_ARGS__)

static f4 fp_alpha;

int main(void)
{
    f4 pos;
    pos.x = 10.5f; pos.y = 20.5f; pos.z = 0.5f; pos.w = (float)(HOST_W);
    const f4 wp = _rsx_wpos(pos);
    const int ok = wp.x == 10.5f && wp.y == 20.5f && wp.z == 0.5f && wp.w == 0.25f;
    printf("%s: WPOS = (%g, %g, %g, %g) from SV_POSITION.w = %g, expected w 0.25\n",
           ok ? "ok" : "FAIL", wp.x, wp.y, wp.z, wp.w, pos.w);
    (void)fp_alpha;
    return ok ? 0 : 1;
}
