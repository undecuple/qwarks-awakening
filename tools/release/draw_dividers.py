#!/usr/bin/env python3
"""Draw the README and site divider images (original art, no game material) with a pure-Python PNG writer.

Three transparent 900-px strips in the site's gadget vocabulary (public/site/style.css): a bolt-and-plate strip, a
holo-grid rule and a ring-and-orbit rule. Colours are the site palette's purple a78bfa, cyan 64dce8 and gold eab75c,
all mid-tone so they read on GitHub's light and dark themes. Output is deterministic (fixed supersampling, zlib level 9,
no timestamps).

Structure and CLI after the Killzone 3 port's tools/release/draw_dividers.py (same author, MIT: its renderer, PNG writer
and --check/--out interface); the drawings, the colours and the alpha blending are this port's own.

  draw_dividers.py            write the PNGs to public/assets/ (assets/ in the public repo)
  draw_dividers.py --check    exit 1 if a file on disk differs from a fresh render or is over 20 KB
  draw_dividers.py --out DIR  write elsewhere
"""
import argparse
import math
import os
import struct
import sys
import zlib

W = 900
SS = 4                      # supersampling per axis
MAX_BYTES = 20 * 1024
PURPLE, CYAN, GOLD = (167, 139, 250), (100, 220, 232), (234, 183, 92)
HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
# the private repo keeps the public tree under public/; the public export ships it as its root
DEFAULT_OUT = os.path.join(_ROOT, "public", "assets") if os.path.isdir(os.path.join(_ROOT, "public")) else os.path.join(_ROOT, "assets")


def rgba(c, a=1.0):
    return (c[0], c[1], c[2], a)


def render(h, shapes):
    """shapes: list of fn(x, y) -> (r, g, b, a) or None, later ones over earlier ones. Returns RGBA rows (bytes)."""
    rows, n = [], SS * SS
    for py in range(h):
        row = bytearray()
        for px in range(W):
            pr = pg = pb = pa = 0.0          # premultiplied sums over the sub-samples
            for sy in range(SS):
                for sx in range(SS):
                    x, y = px + (sx + .5) / SS, py + (sy + .5) / SS
                    r = g = b = a = 0.0
                    for fn in shapes:
                        c = fn(x, y)
                        if c is None:
                            continue
                        ca = c[3]
                        r, g, b = c[0] * ca + r * (1 - ca), c[1] * ca + g * (1 - ca), c[2] * ca + b * (1 - ca)
                        a = ca + a * (1 - ca)
                    pr += r; pg += g; pb += b; pa += a
            if pa > 0:
                row += bytes((min(255, round(pr / pa)), min(255, round(pg / pa)), min(255, round(pb / pa)),
                              min(255, round(pa * 255 / n))))
            else:
                row += b"\0\0\0\0"
        rows.append(bytes(row))
    return rows


def png(h, rows):
    raw = b"".join(b"\0" + r for r in rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def rect(x0, y0, x1, y1, col):
    return lambda x, y: col if x0 <= x < x1 and y0 <= y < y1 else None


def fade(x, lo=60, hi=330):
    """0 at the strip's ends, 1 from `hi` px inward: the rules fade out toward both ends."""
    d = min(x, W - x)
    return max(0.0, min(1.0, (d - lo) / (hi - lo)))


def hline(cy, t, col, a0=1.0, lo=60, hi=330):
    def f(x, y):
        k = fade(x, lo, hi) * a0
        return rgba(col, k) if k > 0 and abs(y - cy) <= t / 2 else None
    return f


def plate(cx, cy, hw, hh, cut, bevel=2.0):
    """A chamfered plate (corners cut by `cut` px): purple rim, darker body, a cyan inner highlight edge."""
    def inside(x, y, m):
        dx, dy = abs(x - cx), abs(y - cy)
        return dx <= hw - m and dy <= hh - m and dx + dy <= hw + hh - cut - 1.4 * m
    def f(x, y):
        if not inside(x, y, 0):
            return None
        if not inside(x, y, bevel):
            return rgba(PURPLE, 0.95)
        if not inside(x, y, bevel + 1.2):
            return rgba(CYAN, 0.75)
        return rgba((52, 44, 96), 0.88)
    return f


def bolt(cx, cy, r, col=GOLD):
    """A round bolt head with a slot."""
    def f(x, y):
        d = math.hypot(x - cx, y - cy)
        if d > r:
            return None
        if abs(y - cy) <= 0.7 and abs(x - cx) <= r * 0.7:
            return rgba((60, 46, 20), 0.95)
        return rgba(col, 0.95 if d < r - 1 else 0.7)
    return f


def plates():
    h, cy = 32, 16.0
    shapes = [hline(cy, 1.5, PURPLE, 0.75), hline(cy + 5, 1.0, CYAN, 0.35, 120, 380),
              hline(cy - 5, 1.0, CYAN, 0.35, 120, 380)]
    shapes.append(plate(450, cy, 150, 12, 14))
    shapes += [bolt(450 + s * 128, cy, 3.6) for s in (-1, 1)]
    for s in (-1, 1):                    # small side plates with one bolt each
        shapes.append(plate(450 + s * 196, cy, 26, 8, 8, 1.6))
        shapes.append(bolt(450 + s * 196, cy, 2.6, CYAN))
    shapes += [rect(444, cy - 3, 456, cy + 3, rgba(CYAN, 0.9)), rect(446, cy - 1, 454, cy + 1, rgba((20, 40, 52), 0.9))]
    return h, shapes


def hexring(cx, cy, r, col, a, t=1.3):
    """A flat-topped hexagon outline of circumradius r."""
    def f(x, y):
        dx, dy = abs(x - cx) / r, abs(y - cy) / r
        # hexagon metric: max(dy*2/sqrt3, dx + dy/sqrt3): 1 on the outline
        k = max(dy * 2 / math.sqrt(3), dx + dy / math.sqrt(3))
        return rgba(col, a) if abs(k - 1) * r <= t / 2 * 1.15 else None
    return f


def grid():
    h, cy = 32, 16.0
    shapes = [hline(cy, 1.0, PURPLE, 0.5, 40, 260)]
    for i in range(-11, 12):             # a row of hexagons, brighter at the middle, fading out
        cx = 450 + i * 34
        a = max(0.0, 0.95 - abs(i) * 0.075)
        if a > 0:
            shapes.append(hexring(cx, cy, 11, CYAN if i % 4 == 0 else PURPLE, a))
    shapes.append(hexring(450, cy, 11, GOLD, 0.95))
    for i in range(-9, 10):              # scan ticks along the top and bottom edge
        if i % 2 == 0:
            a = max(0.0, 0.7 - abs(i) * 0.06)
            shapes.append(rect(450 + i * 34 - 0.5, 1, 450 + i * 34 + 0.5, 4, rgba(CYAN, a)))
            shapes.append(rect(450 + i * 34 - 0.5, h - 4, 450 + i * 34 + 0.5, h - 1, rgba(CYAN, a)))
    return h, shapes


def ring(cx, cy, r, t, col, a):
    return lambda x, y: rgba(col, a) if abs(math.hypot(x - cx, y - cy) - r) <= t / 2 else None


def orbit():
    h, cy = 32, 16.0
    shapes = [hline(cy, 1.4, PURPLE, 0.7, 50, 300)]
    # the tilted orbit: an ellipse (rx 120, ry 8) tilted 0 degrees, drawn as a thin band, with its far arc dimmer
    def ell(x, y):
        k = math.hypot((x - 450) / 120.0, (y - cy) / 8.0)
        if abs(k - 1) * 8 > 0.7:
            return None
        return rgba(CYAN, 0.75 if y >= cy else 0.3)
    shapes += [ell, ring(450, cy, 11, 2.4, PURPLE, 0.95), ring(450, cy, 6.4, 1.4, CYAN, 0.8)]
    shapes.append(lambda x, y: rgba(GOLD, 0.95) if math.hypot(x - 450, y - cy) <= 3 else None)
    for ang in (0.9, 3.5):               # two small moons on the orbit
        mx, my = 450 + 120 * math.cos(ang), cy + 8 * math.sin(ang)
        shapes.append(lambda x, y, mx=mx, my=my: rgba(GOLD, 0.95) if math.hypot(x - mx, y - my) <= 2.6 else None)
    for s in (-1, 1):                    # small end rings
        shapes.append(ring(450 + s * 318, cy, 3.4, 1.4, PURPLE, 0.85))
    return h, shapes


IMAGES = {"divider-plate.png": plates, "divider-grid.png": grid, "divider-orbit.png": orbit}


def build():
    out = {}
    for name, fn in IMAGES.items():
        h, shapes = fn()
        out[name] = png(h, render(h, shapes))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--out", default=DEFAULT_OUT)
    a = ap.parse_args(argv)
    imgs = build()
    bad = []
    for name, data in imgs.items():
        p = os.path.join(a.out, name)
        if len(data) > MAX_BYTES:
            bad.append(f"{name}: {len(data)} bytes over {MAX_BYTES}")
        if a.check:
            if not os.path.exists(p) or open(p, "rb").read() != data:
                bad.append(f"{name}: differs from a fresh render")
        else:
            os.makedirs(a.out, exist_ok=True)
            with open(p, "wb") as fh:
                fh.write(data)
            print(f"wrote {p} ({len(data)} bytes)")
    for b in bad:
        print("FAIL", b, file=sys.stderr)
    if a.check and not bad:
        print("dividers ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
