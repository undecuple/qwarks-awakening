#!/usr/bin/env python3
"""Host test for release_notes.py (from the Killzone 3 port's test), tag mode (launcher-vX.Y.Z and <id>-vX.Y.Z):
the launcher body takes [All games] and [Launcher], a game's body that game's groups (by badge name or id, also a
heading naming several games) with its playability state from the catalogue; the test setup only where it belongs; the
section's intro carried over; every link absolute and pinned to the tag (tree/ for a folder); titles; the pre-release
wording from the README status line; refusals (malformed tag, unknown game, [Unreleased] only, missing section, no
item for the tag, no status line, no CHANGELOG); the known-issues line; both layouts (public/CHANGELOG.md and
CHANGELOG.md at the root). Every case uses a temp tree. No game data needed.
Run: python3 tools/release/test_release_notes.py
"""
import contextlib
import io
import json
import pathlib
import re
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import names  # noqa: E402
import release_notes  # noqa: E402

bad = 0
P = names.PRODUCT
R = names.REPO_URL


def check(ok, what):
    global bad
    bad += not ok
    print(("ok   " if ok else "FAIL ") + what)


def run(root, *args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = release_notes.main(["--root", str(root), *args])
    return rc, out.getvalue(), err.getvalue()


CATALOG = {"games": [
    {"id": "tods", "badge_name": "ToD", "playability": "in_game", "playability_checked": "2026-01-02",
     "playability_note": "about 11 minutes"},
    {"id": "dlhd", "badge_name": "Deadlocked HD", "playability": "menus"},
    {"id": "qfb", "badge_name": "QfB", "playability": "boots"},
    {"id": "collection", "badge_name": "R&C Collection", "children": [
        {"id": "rc1", "badge_name": "R&C 1", "playability": "in_game"},
        {"id": "rc2", "badge_name": "R&C 2", "playability": "in_game"}]},
]}


def layout(base, text, public, readme="> **Status: early work.** x\n", known=False):
    p = pathlib.Path(base) / ("public" if public else "")
    (p / "docs").mkdir(parents=True, exist_ok=True)
    (p / "CHANGELOG.md").write_text(text, encoding="utf-8")
    if readme is not None:
        (p / "README.md").write_text(readme, encoding="utf-8")
    if known:
        (p / "docs" / "KNOWN_ISSUES.md").write_text("x\n", encoding="utf-8")
    c = pathlib.Path(base) / "data" / "titles"
    c.mkdir(parents=True, exist_ok=True)
    (c / "catalog.json").write_text(json.dumps(CATALOG), encoding="utf-8")
    return pathlib.Path(base)


CL = """# Changelog

## [Unreleased]

### [All games]
- Not yet released.

## [0.3.0] - 2026-01-01 (pre-release)

Since 0.2.1 things changed. Known issues:
[docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md).
Patch numbers name this repository's own fixes, the files in
[`port/patches/`](port/patches/).

Frame rates are frames presented per second.

### [All games]
- A fix (patch 7042), see [AGENTS.md](AGENTS.md), [the anchor](#020) and [a site](https://example.org/x).
  More in [RELEASE-CRITERIA.md](./docs/RELEASE-CRITERIA.md).

### [ToD] *Tools of Destruction*: in game
- ToD item, run 2026-01-05.

### [R&C 1], [R&C 2] *Collection*: in game
- Collection item.

### [DLHD] *Deadlocked HD*: menus
- DLHD item.

### [Launcher]
- Launcher item.

### Test setup
- The runs of 2026-01-05 used a test PC.

## [1.2.3] - 2026-02-01
### [All games]
- Final item.
### [Launcher]
- Final launcher item.

## [0.2.0] - 2025-12-01 (pre-release)

### Added
- Old.
"""

for public in (True, False):
    with tempfile.TemporaryDirectory() as d:
        root = layout(d, CL, public, known=True)
        lay = "public/" if public else "root"
        B = f"{R}/blob/launcher-v0.3.0/"
        f, t = pathlib.Path(d) / "n.md", pathlib.Path(d) / "t.txt"
        rc, out, _ = run(root, "--tag", "launcher-v0.3.0", "--out", str(f), "--title-out", str(t))
        body = f.read_text() if f.exists() else ""
        check(rc == 0 and out == "" and t.read_text() == "Launcher v0.3.0\n", f"{lay}: launcher title, --out and --title-out")
        check(body.startswith(f"{P} launcher 0.3.0 (early work), see "
                              f"[docs/RELEASE-CRITERIA.md]({B}docs/RELEASE-CRITERIA.md).")
              and "pre-release" not in body.split("\n")[0],
              f"{lay}: launcher first line gives the state and never says pre-release")
        check("**[All games]**" in body and "**[Launcher]**" in body and "**Test setup**" in body
              and "ToD item" not in body and "Collection item" not in body and "DLHD item" not in body,
              f"{lay}: launcher body = [All games] + [Launcher] + Test setup, no game group")
        check("\n\nSince 0.2.1 things changed. Known issues: [docs/KNOWN_ISSUES.md](" + B + "docs/KNOWN_ISSUES.md). "
              "Patch numbers name this repository's own fixes, the files in [`port/patches/`]("
              + B.replace("/blob/", "/tree/") + "port/patches/).\n\nFrame rates are frames presented per second.\n\n"
              "New in 0.3.0:\n" in body, f"{lay}: intro carried over, paragraphs joined, links pinned (a folder as tree/)")
        check(f"[AGENTS.md]({B}AGENTS.md)" in body and f"[the anchor]({B}CHANGELOG.md#020)" in body
              and "[a site](https://example.org/x)" in body and f"[RELEASE-CRITERIA.md]({B}docs/RELEASE-CRITERIA.md)" in body,
              f"{lay}: item links pinned to the tag (root-relative, ./, anchor); absolute links kept")
        check(body.count("KNOWN_ISSUES.md") == 2 and "\nKnown issues: [" not in body,
              f"{lay}: no second known-issues line when the intro links it")
        check(not re.search(r"\]\((?!https://)", body) and "/blob/main/" not in body and "/blob/v0.3.0" not in body,
              f"{lay}: no relative link, none to main, none to a bare vX.Y.Z tag")
        check(f"See [CHANGELOG.md]({B}CHANGELOG.md) for the full list." in body
              and body.rstrip().endswith(release_notes.PARA2) and release_notes.AI_LINE in body
              and "Not yet" not in body and "Old." not in body and "Final" not in body,
              f"{lay}: CHANGELOG link, AI line, launcher last paragraph, no neighbouring section leaks in")

        s, bz, lz = (names.setup_exe_name("0.3.0"), names.build_zip_name("0.3.0"), names.launcher_zip_name("0.3.0"))
        check(s in body and bz in body and lz in body and body.index(s) < body.index("Portable (no install)")
              < body.index(bz) < body.index("New in 0.3.0:") and "More info" in body and "Run anyway" in body
              and ".sha256" not in body,
              f"{lay}: launcher body lists the installer first, the zips as portable, the SmartScreen step, no .sha256")

        G = f"{R}/blob/tods-v0.3.0/"
        rc, body, _ = run(root, "--tag", "tods-v0.3.0", "--title-out", str(t))
        check(rc == 0 and t.read_text() == "ToD v0.3.0\n", f"{lay}: game title = badge name + version")
        check(body.startswith(f"{P}: ToD in release 0.3.0, a pre-release (early work), see [docs/RELEASE-CRITERIA.md]"
                              f"({G}docs/RELEASE-CRITERIA.md). This release holds notes only")
              and f"[the launcher release]({R}/releases/tag/launcher-v0.3.0)" in body,
              f"{lay}: game first line points at the launcher release of the same version")
        check("State in this release: **in game** (last checked 2026-01-02; about 11 minutes)." in body,
              f"{lay}: game body carries its playability state from the catalogue")
        check("**[ToD] *Tools of Destruction*: in game**" in body and "ToD item" in body and "**Test setup**" in body
              and "[All games]" not in body and "Launcher item" not in body and "DLHD item" not in body,
              f"{lay}: game body = its own group (+ the test setup its run date names)")
        check(body.rstrip().endswith(release_notes.PARA2_GAME) and "SHA256SUMS" not in body
              and not re.search(r"\]\((?!https://)", body) and f"({G}CHANGELOG.md)" in body,
              f"{lay}: game last paragraph has no download; links pinned to the game tag")

        rc, body, _ = run(root, "--tag", "rc2-v0.3.0", "--title-out", str(t))
        check(rc == 0 and t.read_text() == "R&C 2 v0.3.0\n" and "Collection item" in body and "Test setup" not in body,
              f"{lay}: a heading naming several games serves each (catalogue child); no test setup without its date")
        rc, body, _ = run(root, "--tag", "dlhd-v0.3.0", "--title-out", str(t))
        check(rc == 0 and t.read_text() == "Deadlocked HD v0.3.0\n" and "DLHD item" in body
              and "State in this release: **menus**." in body, f"{lay}: a group named by the id (DLHD) is found")

# with the README's real status line: the launcher's first line is the v0.3.0 published form (no "pre-release"),
# a game's first line keeps the pre-release wording
with tempfile.TemporaryDirectory() as d:
    root = layout(d, CL, True, readme="**Status: early work, not playable yet.** x\n")
    B = f"{R}/blob/launcher-v0.3.0/"
    rc, body, _ = run(root, "--tag", "launcher-v0.3.0")
    first = body.split("\n")[0]
    check(rc == 0 and first.startswith(f"{P} launcher 0.3.0 (early work: no game is playable start to finish yet), see "
                                       f"[docs/RELEASE-CRITERIA.md]({B}docs/RELEASE-CRITERIA.md). It builds")
          and "pre-release" not in first, f"launcher line 1 = the published form, no 'pre-release' ({first[:90]})")
    rc, body, _ = run(root, "--tag", "tods-v0.3.0")
    first = body.split("\n")[0]
    check(rc == 0 and ", a pre-release (early work, not playable yet), see" in first,
          f"a game's line 1 keeps the pre-release wording ({first[:90]})")

with tempfile.TemporaryDirectory() as d:
    root = layout(d, CL, True)
    rc, out, _ = run(root, "--tag", "launcher-v1.2.3")
    check(rc == 0 and out.startswith(f"{P} launcher 1.2.3. It builds") and "Final launcher item" in out
          and "\nKnown issues" not in out, "non-pre-release heading drops the pre-release wording; no known-issues file, no line")
    for tag, why, what in (("qfb-v0.3.0", "no items for qfb-v0.3.0", "a game with no group in the section: refused"),
                           ("zz-v0.3.0", "not a game id", "an unknown game id: refused"),
                           ("v0.3.0", "not launcher-vX.Y.Z", "a bare vX.Y.Z tag: refused"),
                           ("launcher-0.3.0", "not launcher-vX.Y.Z", "a tag without v: refused"),
                           ("launcher-v9.9.9", "9.9.9", "missing version: refused"),
                           ("collection-v0.3.0", "not a game id", "the Collection's grouping entry is not a game")):
        rc, out, err = run(root, "--tag", tag)
        check(rc == 1 and out == "" and why in err, what)
with tempfile.TemporaryDirectory() as d:
    rc, _, err = run(layout(d, CL, True, readme=None), "--tag", "launcher-v0.3.0")
    check(rc == 1 and "Status" in err, "pre-release without a README status line: refused")
with tempfile.TemporaryDirectory() as d:
    rc, out, err = run(layout(d, "# Changelog\n\n## [Unreleased]\n\n### [Launcher]\n- Soon.\n", False),
                       "--tag", "launcher-v0.4.0")
    check(rc == 1 and out == "" and "Unreleased" in err, "only [Unreleased]: refused with a hint")
with tempfile.TemporaryDirectory() as d:
    rc, _, err = run(d, "--tag", "launcher-v0.1.0")
    check(rc == 1, "no CHANGELOG file: refused")

print("test_release_notes: " + ("FAIL" if bad else "PASS"))
sys.exit(1 if bad else 0)
