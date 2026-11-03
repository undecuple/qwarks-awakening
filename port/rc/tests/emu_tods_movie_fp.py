"""Offline check (no GPU): the ToD movie fragment program P_AFBA7EF8AF0B48F1_000013DA reduces, for the menu draw's
constants and tint (1,1,1,1), to plain BT.601 limited-range YCbCr -> RGB:
  rgb = Y*c1.y + Cr*c2.xyz + Cb*c3.xyz + c4.xyz   (unit 0 = Y 1280x720, unit 1 = Cr, unit 2 = Cb, 640x360 doubled)
with c1.y=0x3F950200, c2=(0x3FCC4300,0xBF504000,0), c3=(0,0xBEC86C00,0x40012400), c4=(0xBF5EE340,0x3F079AC0,0xBF8A7420)
(inputs_f002041_i0000.txt of run tods-green-C). Usage: python3 -I emu_tods_movie_fp.py RUNDIR_SCRATCH OUT.png
(reads inputs_f002041_i0000_u00/u01/u02.bin from a LD_DRAW_INPUTS dump; writes the emulated picture).
Result 2026-10-20: emulated vs the draw's own target dump agree in static areas (mean diff ~1-2 levels) and reproduce the
bright green legs, so the shader/constants are not the cause: the plane data are (R&C pass 3)."""
import sys, numpy as np
from PIL import Image
f = lambda h: np.frombuffer(np.uint32(h).tobytes(), np.float32)[0]
c1y = f(0x3F950200); c2 = [f(0x3FCC4300), f(0xBF504000), 0]; c3 = [0, f(0xBEC86C00), f(0x40012400)]
c4 = [f(0xBF5EE340), f(0x3F079AC0), f(0xBF8A7420)]
d = sys.argv[1].rstrip("/") + "/inputs_f002041_i0000_"
r = lambda n, w, h: np.fromfile(d + n, np.uint8).reshape(h, w).astype(np.float32) / 255
Y, A, B = r("u00.bin", 1280, 720), r("u01.bin", 640, 360), r("u02.bin", 640, 360)
up = lambda p: np.kron(p, np.ones((2, 2), np.float32))
col = np.stack([Y * c1y + up(A) * c2[i] + up(B) * c3[i] + c4[i] for i in range(3)], -1)
Image.fromarray((np.clip(col, 0, 1) * 255 + .5).astype(np.uint8)).save(sys.argv[2])
