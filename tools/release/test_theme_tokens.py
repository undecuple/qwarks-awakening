#!/usr/bin/env python3
"""Host test for tools/release/theme_tokens.py and data/theme.json.

    python3 -I tools/release/test_theme_tokens.py

Cases: the committed launcher/src/theme_tokens.h is what the JSON gives (--check exits 0); the token rules hold
(same keys in light and dark, every text pair >= 4.5:1 after alpha compositing, derived text colours follow
libadwaita's oklab rule); the libadwaita values relies on are the ones read from its stylesheet; drift is caught:
a changed JSON value makes the header stale, a too-dim text colour fails the contrast rule, a missing dark key and a
wrong derived colour are both reported; the contrast maths matches known WCAG values (black on white 21:1).
Exit 0 = PASS.
"""
import copy
import os
import subprocess
import sys

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
    with open(tt.HEADER_PATH, encoding="utf-8") as f:
        check(f.read() == tt.render(theme), "theme_tokens.h equals render(theme.json)")

    # libadwaita facts (src/stylesheet/_colors.scss, _palette.scss, read 2026-11-09)
    L, D = theme["modes"]["light"], theme["modes"]["dark"]
    facts = {("light", "window_bg"): "#fafafb", ("light", "view_bg"): "#ffffff", ("light", "headerbar_bg"): "#ffffff",
             ("light", "sidebar_bg"): "#ebebed", ("light", "card_bg"): "#ffffff", ("light", "window_fg"): "#000006cc",
             ("dark", "window_bg"): "#222226", ("dark", "view_bg"): "#1d1d20", ("dark", "headerbar_bg"): "#2e2e32",
             ("dark", "sidebar_bg"): "#2e2e32", ("dark", "card_bg"): "#ffffff14", ("dark", "window_fg"): "#ffffff",
             ("dark", "popover_bg"): "#36363a", ("light", "destructive_bg"): "#e01b24",
             ("dark", "destructive_bg"): "#c01c28", ("light", "success_bg"): "#2ec27e", ("dark", "success_bg"): "#26a269",
             ("light", "warning_bg"): "#e5a50a", ("dark", "warning_bg"): "#cd9309"}
    wrong = {k: v for k, v in facts.items() if theme["modes"][k[0]][k[1]].lower() != v}
    check(not wrong, "libadwaita values as read from its stylesheet", wrong)
    check(L["accent_bg"] == D["accent_bg"] == "#1e70b9", "accent fill is the banner blue (decision 1)")

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
