#!/usr/bin/env python3
"""Write one playability badge per game and the badge row in public/README.md (generated, never hand-edited).

    python3 -I tools/release/badges.py            write public/assets/badges/<id>.svg and the README block
    python3 -I tools/release/badges.py --check    exit 1 when a file differs from what the catalogue gives

Source: "playability" (not_built, boots, menus, in_game, playable, completed), "playability_checked" (YYYY-MM-DD),
"playability_note" and "badge_name" of each game in data/titles/catalog.json; colours from data/theme.json (light set,
text contrast checked in the test). The Collection entry itself has no badge: its three games have one each.
The block sits between <!-- badges:start --> and <!-- badges:end --> in public/README.md; each image links to the
game's section in docs/FEATURE-PARITY.md (a hand-written page: the links use GitHub's heading anchors, the test checks
the headings exist). The six states are defined in public/docs/RELEASE-CRITERIA.md. Standard library only.
"""
import json
import os
import re
import sys
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import theme_tokens as tt  # noqa: E402

CATALOG = os.path.join(ROOT, "data", "titles", "catalog.json")
PUB = os.path.join(ROOT, "public") if os.path.isdir(os.path.join(ROOT, "public")) else ROOT  # the public repo has public/ at its root
README = os.path.join(PUB, "README.md")
PARITY = os.path.join(PUB, "docs", "FEATURE-PARITY.md")
OUT_DIR = os.path.join(PUB, "assets", "badges")
START, END = "<!-- badges:start -->", "<!-- badges:end -->"

STATES = ("not_built", "boots", "menus", "in_game", "playable", "completed")
LABEL = {"not_built": "not built", "boots": "boots", "menus": "menus", "in_game": "in game",
         "playable": "playable", "completed": "completed"}
# state -> (background token, text token) of the right part; the left part is toast_bg / toast_fg
COLOURS = {"not_built": ("sidebar_bg", "window_fg"), "boots": ("warning_bg", "warning_fg"),
           "menus": ("warning_bg", "warning_fg"), "in_game": ("accent_bg", "accent_fg"),
           "playable": ("success", "accent_fg"), "completed": ("success", "accent_fg")}
COLLECTION_HEADING = "Ratchet & Clank Collection: Ratchet & Clank 1, 2 and 3"
H, PAD, FONT = 20, 7, 11


def games(cat):
    """Every catalogue game, children after their parent."""
    return [g for e in cat["games"] for g in [e] + list(e.get("children", []))]


def badge_games(cat):
    """The games that get a badge: all but an entry that only groups children."""
    return [g for g in games(cat) if not g.get("children")]


def anchor(heading):
    """GitHub's heading anchor: lower case, punctuation dropped, spaces to hyphens."""
    return re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")


def parity_heading(g, cat):
    for e in cat["games"]:
        if any(c["id"] == g["id"] for c in e.get("children", [])):
            return COLLECTION_HEADING
    return g["title"]


def text_width(s):
    w = 0.0
    for c in s:
        w += 3.6 if c in "il.,:;'!| tfjrI1" else 10.0 if c in "mwMW@" else 7.6 if c.isupper() or c.isdigit() else 6.6
    return int(w + 0.999)


def colours(theme, state):
    toks = theme["modes"]["light"]
    bg_k, fg_k = COLOURS[state]
    right = tt.over(tt.parse(toks[bg_k]), tt.parse(toks["window_bg"]))
    left = tt.parse(toks["toast_bg"])
    return {"left": tt.hexstr(left), "left_fg": tt.hexstr(tt.over(tt.parse(toks["toast_fg"]), left)),
            "right": tt.hexstr(right), "right_fg": tt.hexstr(tt.over(tt.parse(toks[fg_k]), right))}


def svg(g, theme):
    name, state = g["badge_name"], LABEL[g["playability"]]
    c = colours(theme, g["playability"])
    lw, rw = text_width(name) + 2 * PAD, text_width(state) + 2 * PAD
    w = lw + rw
    alt = f"{name}: {state}"
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{H}" viewBox="0 0 {w} {H}" role="img" '
            f'aria-label="{escape(alt)}">\n'
            f'<title>{escape(alt)}</title>\n'
            f'<clipPath id="r"><rect width="{w}" height="{H}" rx="3"/></clipPath>\n'
            f'<g clip-path="url(#r)"><rect width="{lw}" height="{H}" fill="{c["left"]}"/>'
            f'<rect x="{lw}" width="{rw}" height="{H}" fill="{c["right"]}"/></g>\n'
            f'<g font-family="DejaVu Sans,Verdana,Geneva,sans-serif" font-size="{FONT}" text-anchor="middle">'
            f'<text x="{lw / 2:g}" y="14" fill="{c["left_fg"]}">{escape(name)}</text>'
            f'<text x="{lw + rw / 2:g}" y="14" fill="{c["right_fg"]}">{escape(state)}</text></g>\n'
            f'</svg>\n')


def block(cat):
    items = []
    for g in badge_games(cat):
        alt = f'{g["badge_name"]}: {LABEL[g["playability"]]}'
        items.append(f'[![{alt}](assets/badges/{g["id"]}.svg)](docs/FEATURE-PARITY.md#{anchor(parity_heading(g, cat))})')
    date = max(g["playability_checked"] for g in badge_games(cat))
    line = (f"*Each state is a measured run (last checked {date}, the Windows programs under Proton on Linux). "
            f'"In game" means reached with scripted input, not played through by hand. '
            f"The six states are defined in [RELEASE-CRITERIA.md](docs/RELEASE-CRITERIA.md).*")
    return START + "\n" + " ".join(items) + "\n\n" + line + "\n" + END


def new_readme(text, cat):
    a, b = text.find(START), text.find(END)
    if a < 0 or b < a:
        raise SystemExit(f"README.md has no {START} ... {END} block")
    return text[:a] + block(cat) + text[b + len(END):]


def read(path):
    try:
        with open(path, encoding="utf-8", newline="") as f:
            return f.read()
    except OSError:
        return None


def expected(cat, theme):
    want = {os.path.join(OUT_DIR, g["id"] + ".svg"): svg(g, theme) for g in badge_games(cat)}
    want[README] = new_readme(read(README), cat)
    return want


def main(argv):
    check = "--check" in argv
    with open(CATALOG, encoding="utf-8") as f:
        cat = json.load(f)
    want = expected(cat, tt.load())
    stale = []
    for path, content in want.items():
        if read(path) != content:
            stale.append(os.path.relpath(path, ROOT))
            if not check:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(content)
    if os.path.isdir(OUT_DIR):
        extra = sorted(set(os.listdir(OUT_DIR)) - {os.path.basename(p) for p in want})
        stale += [f"{os.path.relpath(OUT_DIR, ROOT)}/{x} (no such game)" for x in extra]
    if check:
        for s in stale:
            print("stale:", s)
        print("badges:", "STALE" if stale else "current")
        return 1 if stale else 0
    print("badges: wrote", len(stale), "file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
