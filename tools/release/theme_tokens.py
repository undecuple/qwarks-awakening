#!/usr/bin/env python3
"""Write launcher/src/theme_tokens.h from data/theme.json (one token file for the launcher and the website).

    python3 -I tools/release/theme_tokens.py            write the header
    python3 -I tools/release/theme_tokens.py --check    exit 1 when the header differs from what the JSON gives,
                                                        or the tokens break a rule below
    python3 -I tools/release/theme_tokens.py --contrast print every text pair's contrast ratio, both modes

Rules checked (both modes): the light and dark sets have the same keys; every colour parses; every pair in "pairs"
(text, background[, the colours a translucent background sits on, innermost last]) has a WCAG contrast of 4.5:1 or more after alpha
compositing; every "derived" token equals libadwaita's rule applied to its fill (oklab lightness clamped to at most 0.5
in light mode, at least 0.85 in dark, clipped to sRGB), within one step per channel.
Colours in the header are 0xRRGGBBAA. Standard library only.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
JSON_PATH = os.path.join(ROOT, "data", "theme.json")
HEADER_PATH = os.path.join(ROOT, "launcher", "src", "theme_tokens.h")
MIN_RATIO = 4.5


def parse(h):
    """'#rrggbb' or '#rrggbbaa' -> [r, g, b, a], each 0..1."""
    s = h.lstrip("#")
    if len(s) not in (6, 8) or any(c not in "0123456789abcdefABCDEF" for c in s):
        raise ValueError(f"not a colour: {h!r}")
    v = [int(s[i:i + 2], 16) / 255 for i in range(0, len(s), 2)]
    return v if len(v) == 4 else v + [1.0]


def over(fg, bg):
    """fg composited over an opaque bg (sRGB, as ImGui and CSS blend)."""
    a = fg[3]
    return [fg[i] * a + bg[i] * (1 - a) for i in range(3)] + [1.0]


def _lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _unlin(c):
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def luminance(c):
    return 0.2126 * _lin(c[0]) + 0.7152 * _lin(c[1]) + 0.0722 * _lin(c[2])


def ratio(a, b):
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def _oklab(c):
    r, g, b = (_lin(x) for x in c[:3])
    l = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def _from_oklab(L, a, b):
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    rgb = (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
           -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
           -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)
    return [max(0.0, min(1.0, _unlin(max(0.0, min(1.0, x))))) for x in rgb] + [1.0]


def derive(fill, dark):
    """libadwaita's text colour for a fill: oklab(from fill min(l, 0.5) a b) light, max(l, 0.85) dark."""
    L, a, b = _oklab(parse(fill))
    return _from_oklab(max(L, 0.85) if dark else min(L, 0.5), a, b)


def load(path=JSON_PATH):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def problems(theme):
    out = []
    modes = theme.get("modes") or {}
    if set(modes) != {"light", "dark"}:
        return [f"modes must be light and dark, got {sorted(modes)}"]
    keys = list(modes["light"])
    if list(modes["dark"]) != keys:
        out.append("light and dark sets differ in keys or order: "
                   f"{sorted(set(keys) ^ set(modes['dark']))} / order")
    for mode, toks in modes.items():
        for k, v in toks.items():
            try:
                parse(v)
            except ValueError as e:
                out.append(f"{mode}.{k}: {e}")
    if out:
        return out
    if theme.get("accent_bg") != modes["light"].get("accent_bg") or theme.get("accent_bg") != modes["dark"].get("accent_bg"):
        out.append("accent_bg differs between the top-level value and a mode")
    for mode, toks in modes.items():
        for tok, fill in (theme.get("derived") or {}).items():
            want, got = derive(toks[fill], mode == "dark"), parse(toks[tok])
            if max(abs(want[i] - got[i]) for i in range(3)) > 1.5 / 255:
                out.append(f"{mode}.{tok} {toks[tok]} is not libadwaita's rule on {fill} {toks[fill]} "
                           f"(want {hexstr(want)})")
        for row in contrast_rows(theme, mode):
            if row[-1] < MIN_RATIO:
                out.append(f"contrast {mode}: {row[0]} on {row[1]} is {row[-1]:.2f} < {MIN_RATIO}")
    return out


def contrast_rows(theme, mode):
    toks = theme["modes"][mode]
    rows = []
    for p in theme.get("pairs") or []:
        # p = [text, background, what the background sits on, ...]: composited from the last layer up
        layers = [parse(toks[k]) for k in p[1:]]
        bg = layers[-1]
        if bg[3] < 1:
            rows.append((p[0], " over ".join(p[1:]), 0.0))
            continue
        for lay in reversed(layers[:-1]):
            bg = over(lay, bg)
        rows.append((p[0], " over ".join(p[1:]), ratio(over(parse(toks[p[0]]), bg), bg)))
    return rows


def hexstr(c):
    return "#" + "".join("%02x" % round(x * 255) for x in c[:3])


def u32(h):
    c = parse(h)
    v = 0
    for x in c:
        v = (v << 8) | round(x * 255)
    return "0x%08x" % v


def camel(k):
    p = k.split("_")
    return p[0] + "".join(x.capitalize() for x in p[1:])


def render(theme):
    keys = list(theme["modes"]["light"])
    lines = ["// Generated by tools/release/theme_tokens.py from data/theme.json. Do not edit: change the JSON and",
             "// run the tool; tools/release/test_theme_tokens.py fails when this file drifts from it.",
             "#pragma once", "", "namespace theme {", "",
             "// Colours are 0xRRGGBBAA (sRGB; alpha blends over what is below, as libadwaita's translucent colours do).",
             "struct Tokens {"]
    lines += [f"    unsigned {camel(k)};" for k in keys]
    lines += ["};", ""]
    for mode in ("light", "dark"):
        toks = theme["modes"][mode]
        lines.append(f"static const Tokens k{mode.capitalize()} = {{")
        lines += [f"    {u32(toks[k])},   // {k} {toks[k]}" for k in keys]
        lines += ["};", ""]
    for k, v in (theme.get("metrics") or {}).items():
        lines.append(f"static const float k{camel(k)[0].upper() + camel(k)[1:]} = {float(v):.1f}f;")
    lines += ["", "}  // namespace theme", ""]
    return "\n".join(lines)


def main(argv):
    theme = load()
    if "--contrast" in argv:
        for mode in ("light", "dark"):
            for fg, bg, r in contrast_rows(theme, mode):
                print(f"{mode:<5} {r:5.2f}  {fg} on {bg}")
        return 0
    bad = problems(theme)
    for b in bad:
        print("theme_tokens:", b, file=sys.stderr)
    text = render(theme)
    if "--check" in argv:
        try:
            with open(HEADER_PATH, encoding="utf-8") as f:
                cur = f.read()
        except OSError:
            cur = None
        if cur != text:
            print(f"theme_tokens: {os.path.relpath(HEADER_PATH, ROOT)} is stale: run tools/release/theme_tokens.py",
                  file=sys.stderr)
            return 1
        return 1 if bad else 0
    if bad:
        return 1
    with open(HEADER_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {os.path.relpath(HEADER_PATH, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
