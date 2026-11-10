#!/usr/bin/env python3
"""Host test for tools/release/badges.py (playability badges, generated from data/titles/catalog.json).

    python3 -I tools/release/test_badges.py

Cases: every catalogue game (children too) has a valid state, a real YYYY-MM-DD date, a note and a badge name, and its
state agrees with the readme_state wording; the SVGs and the README block are what the catalogue gives (--check exits
0); --check catches a changed SVG, a changed README block, a missing SVG, an SVG of no game and a catalogue change
not yet generated, and a write restores them (run in a scratch copy); no SVG holds an external URL, a script or an
image; text pairs reach 4.5:1; alt text is present and names game and state; every link anchor resolves to a heading
of docs/FEATURE-PARITY.md; the six states are defined in RELEASE-CRITERIA.md. Exit 0 = PASS.
"""
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from xml.dom import minidom

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import badges  # noqa: E402
import theme_tokens as tt  # noqa: E402

FAILS = []
ROOT = badges.ROOT
PUBREL = os.path.relpath(badges.PUB, ROOT)  # "public" here, "." in the exported public repo


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:600]))
    if not cond:
        FAILS.append(what)


def run_tool(*args, root=HERE):
    return subprocess.run([sys.executable, "-I", os.path.join(root, "badges.py"), *args], capture_output=True, text=True)


def valid_date(d):
    try:
        datetime.date.fromisoformat(d)
    except (ValueError, TypeError):
        return False
    return bool(re.fullmatch(r"\d{4}-\d\d-\d\d", d))


def main():
    with open(badges.CATALOG, encoding="utf-8") as f:
        cat = json.load(f)
    theme = tt.load()
    allg = badges.games(cat)
    leaves = badges.badge_games(cat)

    ok = all(g.get("playability") in badges.STATES and valid_date(g.get("playability_checked"))
             and g.get("playability_note") and g.get("badge_name") for g in allg)
    check(ok, f"catalogue: all {len(allg)} games have a valid playability, date, note and badge name")
    check([g["id"] for g in leaves] == ["tods", "qfb", "acit", "a4o", "ffa", "nexus", "rc1", "rc2", "rc3", "dlhd"],
          "badges: one per game, the Collection's three games instead of the group entry")
    consistent = True
    for g in allg:
        rs, st = g["readme_state"], g["playability"]
        if st == "not_built" and not rs.startswith("coming soon"):
            consistent = False
        if st != "not_built" and "coming soon" in rs:
            consistent = False
        if st in badges.STATES[:4] and re.search(r"(?<!not )playable(?! through)", rs):
            consistent = False
        if st in badges.STATES[1:4] and "not playable" not in rs:
            consistent = False
    check(consistent, "catalogue: playability agrees with readme_state (below 'playable' says 'not playable')")

    # generated files are current
    r = run_tool("--check")
    check(r.returncode == 0, "--check: SVGs and README block are current", r.stdout + r.stderr)
    want = badges.expected(cat, theme)
    check(all(badges.read(p) == c for p, c in want.items()), "files on disk equal the generator's output")

    # SVG content
    bad = []
    for g in leaves:
        s = badges.read(os.path.join(badges.OUT_DIR, g["id"] + ".svg")) or ""
        alt = f'{g["badge_name"]}: {badges.LABEL[g["playability"]]}'.replace("&", "&amp;")
        if re.search(r"https?://(?!www\.w3\.org/2000/svg)|href=|<script|<image|@import|url\(\s*[\"']?(?!#)", s):
            bad.append(g["id"] + " external reference")
        if f'aria-label="{alt}"' not in s:
            bad.append(g["id"] + " aria-label")
        if f"<title>{alt}</title>" not in s:
            bad.append(g["id"] + " title")
        try:
            minidom.parseString(s)
        except Exception as e:  # noqa: BLE001
            bad.append(f"{g['id']} not well-formed: {e}")
    check(not bad and len(leaves) == 10, "SVGs: well formed, no external URL, script or image, label and title present", bad)

    # colours
    low = []
    for st in badges.STATES:
        c = badges.colours(theme, st)
        for fg, bg in (("left_fg", "left"), ("right_fg", "right")):
            q = tt.ratio(tt.parse(c[fg]), tt.parse(c[bg]))
            if q < 4.5:
                low.append((st, fg, round(q, 2)))
    check(not low, "badge text pairs >= 4.5:1 for all six states", low)

    # README block
    readme = badges.read(badges.README)
    a, b = readme.find(badges.START), readme.find(badges.END)
    blk = readme[a:b] if 0 <= a < b else ""
    check(0 <= a < b and readme.find("## Try the launcher preview") > b and a > readme.find("# Qwark's Awakening"),
          "README: block sits after the title and before 'Try the launcher preview'")
    imgs = re.findall(r"\[!\[([^\]]*)\]\(([^)]*)\)\]\(([^)]*)\)", blk)
    check(len(imgs) == len(leaves) and all(alt.strip() for alt, _, _ in imgs), "README: one image with alt text per game")
    check(len(imgs) == len(leaves) and all(alt == f'{g["badge_name"]}: {badges.LABEL[g["playability"]]}'
                                           for (alt, _, _), g in zip(imgs, leaves)),
          "README: alt text is '<game>: <state>'")
    check(len(imgs) == len(leaves) and all(src == f"assets/badges/{g['id']}.svg" and os.path.isfile(os.path.join(badges.PUB, src))
                                           for (_, src, _), g in zip(imgs, leaves)), "README: image paths exist")
    check("scripted input, not played through by hand" in blk and max(g["playability_checked"] for g in leaves) in blk,
          "README: the line says what 'in game' means and gives the date")
    parity = badges.read(badges.PARITY) or ""
    heads = {badges.anchor(m) for m in re.findall(r"^#{1,6} (.+?)\s*$", parity, re.M)}
    check(imgs and all(link.startswith("docs/FEATURE-PARITY.md#") and link.split("#", 1)[1] in heads for _, _, link in imgs),
          "README: every link anchor is a heading of FEATURE-PARITY.md")
    crit = badges.read(os.path.join(badges.PUB, "docs", "RELEASE-CRITERIA.md")) or ""
    check(all(f"| {badges.LABEL[s].capitalize()} |" in crit for s in badges.STATES),
          "RELEASE-CRITERIA.md: the six states are defined")

    # drift is caught, in a scratch copy
    tmp = tempfile.mkdtemp(prefix="badges-test-")
    try:
        for rel in ("tools/release/badges.py", "tools/release/theme_tokens.py", "data/theme.json",
                    "data/titles/catalog.json", PUBREL + "/README.md"):
            os.makedirs(os.path.join(tmp, os.path.dirname(rel)), exist_ok=True)
            shutil.copy(os.path.join(ROOT, rel), os.path.join(tmp, rel))
        shutil.copytree(badges.OUT_DIR, os.path.join(tmp, PUBREL, "assets", "badges"))
        tool = os.path.join(tmp, "tools", "release")
        bdir = os.path.join(tmp, PUBREL, "assets", "badges")
        check(run_tool("--check", root=tool).returncode == 0, "scratch copy: --check passes")
        svg = os.path.join(bdir, "tods.svg")
        with open(svg, encoding="utf-8") as f:
            orig = f.read()
        with open(svg, "w", encoding="utf-8") as f:
            f.write(orig.replace("in game", "playable"))
        r = run_tool("--check", root=tool)
        check(r.returncode == 1 and "tods.svg" in r.stdout, "--check catches a stale SVG", r.stdout)
        os.remove(svg)
        r = run_tool("--check", root=tool)
        check(r.returncode == 1 and "tods.svg" in r.stdout, "--check catches a missing SVG", r.stdout)
        with open(os.path.join(bdir, "zzz.svg"), "w") as f:
            f.write("<svg/>")
        r = run_tool("--check", root=tool)
        check(r.returncode == 1 and "zzz.svg" in r.stdout, "--check catches an SVG of no game", r.stdout)
        os.remove(os.path.join(bdir, "zzz.svg"))
        run_tool(root=tool)
        check(run_tool("--check", root=tool).returncode == 0, "a write restores the missing SVG")
        rd = os.path.join(tmp, PUBREL, "README.md")
        with open(rd, encoding="utf-8") as f:
            txt = f.read()
        with open(rd, "w", encoding="utf-8") as f:
            f.write(txt.replace("(last checked", "(last seen"))
        r = run_tool("--check", root=tool)
        check(r.returncode == 1 and "README.md" in r.stdout, "--check catches a hand-edited README block", r.stdout)
        run_tool(root=tool)
        check(run_tool("--check", root=tool).returncode == 0, "a write restores the README block")
        cp = os.path.join(tmp, "data", "titles", "catalog.json")
        with open(cp, encoding="utf-8") as f:
            c2 = json.load(f)
        c2["games"][0]["playability"] = "completed"
        with open(cp, "w", encoding="utf-8") as f:
            json.dump(c2, f, indent=1)
        r = run_tool("--check", root=tool)
        check(r.returncode == 1 and "tods.svg" in r.stdout and "README.md" in r.stdout,
              "--check catches a catalogue change not yet generated", r.stdout)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("test_badges:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
