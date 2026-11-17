#!/usr/bin/env python3
"""Host test for tools/release/theme_tokens.py and data/theme.json.

    python3 -I tools/release/test_theme_tokens.py

Cases: the committed launcher/src/theme_tokens.h is what the JSON gives (--check exits 0); the token rules hold
(same keys in light and dark, every text pair >= 4.5:1 after alpha compositing, derived text colours retain
the existing OKLab rule); drift is caught:
a changed JSON value makes the header stale, a too-dim text colour fails the contrast rule, a missing dark key and a
wrong derived colour are both reported; the contrast maths matches known WCAG values (black on white 21:1).
Exit 0 = PASS.
"""
import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import theme_tokens as tt  # noqa: E402

FAILS = []


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:600]))
    if not cond:
        FAILS.append(what)


def main():
    theme = tt.load()
    r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "theme_tokens.py"), "--check"],
                       capture_output=True, text=True)
    check(r.returncode == 0, "theme_tokens.py --check: header current, rules hold", r.stderr)
    check(tt.problems(theme) == [], "no rule problems in data/theme.json", tt.problems(theme))
    with open(os.path.join(tt.ROOT, "data/titles/catalog.json"), encoding="utf-8") as f:
        games = json.load(f)["games"]
    ids = {g["id"] for g in games} | {c["id"] for g in games for c in g.get("children", [])}
    check(set(theme["game_accents"]) == ids, "every catalogue parent and child has a title accent")
    variants = [tt.game_theme(theme, game) for game in ids]
    rows = [row for v in variants for mode in ("light", "dark") for row in tt.contrast_rows(v, mode)]
    check(min(row[-1] for row in rows) >= 4.5, f"all {len(rows)} per-game text pairs meet 4.5:1")
    semantic = ("destructive", "warning", "success", "window_bg", "card_bg", "window_fg")
    check(all(v["modes"][mode][key] == theme["modes"][mode][key]
              for v in variants for mode in ("light", "dark") for key in semantic),
          "game variants preserve semantic colors and text surfaces")
    for bad_table in (None, [], {}, {"bad\"id": {}}, dict(theme["game_accents"], tods=None)):
        t = copy.deepcopy(theme)
        t["game_accents"] = bad_table
        check(bool(tt.problems(t)), f"malformed/incomplete game table rejected: {type(bad_table).__name__}")
    for key, value in (("fill", "red; color: white"), ("light", None), ("dark", "#000000")):
        t = copy.deepcopy(theme)
        t["game_accents"]["tods"][key] = value
        check(bool(tt.problems(t)), f"invalid per-title {key} rejected")
    import website
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "data"))
        with open(os.path.join(tmp, "data/theme.json"), "w", encoding="utf-8") as f:
            json.dump(t, f)
        try:
            website.css_tokens(tmp)
        except ValueError:
            check(True, "website generator refuses the same invalid accent data")
        else:
            check(False, "website generator refuses the same invalid accent data")
        os.makedirs(os.path.join(tmp, "tools/release"))
        script = os.path.join(tmp, "tools/release/theme_tokens.py")
        shutil.copyfile(os.path.join(HERE, "theme_tokens.py"), script)
        rejected = subprocess.run([sys.executable, "-I", script, "--check"], check=False,
                                  capture_output=True, text=True)
        check(rejected.returncode == 1 and "Traceback" not in rejected.stderr,
              "actual header CLI rejects invalid data cleanly before rendering")
    with open(tt.HEADER_PATH, encoding="utf-8") as f:
        check(f.read() == tt.render(theme), "theme_tokens.h equals render(theme.json)")

    # Custom project colours supersede the old GNOME palette; keep role and contrast contracts.
    L, D = theme["modes"]["light"], theme["modes"]["dark"]
    check(L["accent_bg"] == D["accent_bg"] == theme["accent_bg"], "shared accent fill in both appearance modes")
    check(all(tt.parse(m["card_bg"])[3] == 1 for m in (L, D)), "card surfaces are opaque for predictable contrast")

    check(abs(tt.ratio(tt.parse("#000000"), tt.parse("#ffffff")) - 21.0) < 1e-9, "contrast: black on white is 21:1")
    check(abs(tt.ratio(tt.parse("#ffffff"), tt.parse("#1e70b9")) - 5.16) < 0.01, "contrast: white on #1e70b9 is 5.16:1")
    rows = [x for m in ("light", "dark") for x in tt.contrast_rows(theme, m)]
    check(len(rows) == 2 * len(theme["pairs"]) and min(x[-1] for x in rows) >= 4.5,
          f"all {len(rows)} text pairs >= 4.5:1", sorted(rows, key=lambda x: x[-1])[:3])

    # drift and rule violations are caught
    t = copy.deepcopy(theme)
    t["modes"]["dark"]["window_bg"] = "#222227"
    check(tt.render(t) != tt.render(theme), "a changed JSON value changes the header (so --check reports it stale)")
    t = copy.deepcopy(theme)
    t["modes"]["light"]["dim_fg"] = "#00000670"   # libadwaita's own 55 %: 3.2:1
    check(any("dim_fg on window_bg" in p for p in tt.problems(t)), "too-dim text fails the contrast rule")
    t = copy.deepcopy(theme)
    del t["modes"]["dark"]["toast_fg"]
    check(any("differ in keys" in p for p in tt.problems(t)), "a key missing in one mode is reported")
    t = copy.deepcopy(theme)
    t["modes"]["light"]["accent"] = "#1e70b9"
    check(any("light.accent" in p for p in tt.problems(t)), "a derived colour off libadwaita's rule is reported")
    t = copy.deepcopy(theme)
    t["modes"]["light"]["view_bg"] = "#zzzzzz"
    check(any("not a colour" in p for p in tt.problems(t)), "a malformed colour is reported")

    print("test_theme_tokens:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
