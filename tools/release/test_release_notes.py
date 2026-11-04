#!/usr/bin/env python3
"""Host test for release_notes.py (from the Killzone 3 port's test): grouped items, the pre-release wording from the
README status line, refusals ([Unreleased] only, missing, empty, no status line), a heading without (pre-release), the
known-issues line, both layouts (public/CHANGELOG.md and CHANGELOG.md at the root). R&C has no CHANGELOG yet, so every
case uses a temp tree. No game data needed. Run: python3 tools/release/test_release_notes.py
"""
import contextlib
import io
import os
import pathlib
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402
import release_notes  # noqa: E402

bad = 0
P = names.PRODUCT


def check(ok, what):
    global bad
    bad += not ok
    print(("ok   " if ok else "FAIL ") + what)


def run(root, *args):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = release_notes.main(["--root", str(root), *args])
    return rc, out.getvalue(), err.getvalue()


def layout(base, text, public, readme="> **Status: early work.** x\n", known=False):
    p = pathlib.Path(base) / ("public" if public else "")
    (p / "docs").mkdir(parents=True, exist_ok=True)
    (p / "CHANGELOG.md").write_text(text, encoding="utf-8")
    if readme is not None:
        (p / "README.md").write_text(readme, encoding="utf-8")
    if known:
        (p / "docs" / "KNOWN_ISSUES.md").write_text("x\n", encoding="utf-8")
    return pathlib.Path(base)


CL = """# Changelog

## [Unreleased]

### Added
- Not yet released.

## [1.2.3] - 2026-01-01
### Added
- First item.
- Second item,
  continued.

### Fixed
- A fix.

## [0.1.0] - 2025-12-01 (pre-release)

### Added
- Old.
"""

for public in (True, False):
    with tempfile.TemporaryDirectory() as d:
        root = layout(d, CL, public)
        tag = "public/" if public else "root"
        rc, out, _ = run(root, "1.2.3")
        check(rc == 0 and out.startswith(f"{P} 1.2.3. New in 1.2.3:\n"), f"{tag}: non-pre-release heading drops the pre-release wording")
        check("- First item.\n- Second item,\n  continued.\n\n**Fixed**\n\n- A fix.\n" in out, f"{tag}: list grouped, continuation kept")
        check(out.index("**Added**") < out.index("**Fixed**") and "Not yet" not in out and "Old." not in out,
              f"{tag}: order kept, no neighbouring section leaks in")
        check(out.rstrip().endswith(release_notes.PARA2) and release_notes.AI_LINE in out
              and "KNOWN_ISSUES" not in out, f"{tag}: AI line and fixed last paragraph, no known-issues line without the file")
        rc, out, _ = run(root, "0.1.0")
        check(rc == 0 and out.startswith(f"{P} 0.1.0, a pre-release (early work), see docs/RELEASE-CRITERIA.md. New in 0.1.0:\n"),
              f"{tag}: pre-release heading takes the README status")
        f = pathlib.Path(d) / "n.txt"
        rc, out, _ = run(root, "0.1.0", "--out", str(f))
        check(rc == 0 and out == "" and f.read_text().startswith(f"{P} 0.1.0"), f"{tag}: --out writes the file")

with tempfile.TemporaryDirectory() as d:
    rc, out, _ = run(layout(d, CL, True, known=True), "1.2.3")
    check(rc == 0 and "Known issues: docs/KNOWN_ISSUES.md." in out, "known-issues line when the file exists")
with tempfile.TemporaryDirectory() as d:
    rc, out, err = run(layout(d, CL, True, readme=None), "0.1.0")
    check(rc == 1 and "Status" in err, "pre-release without a README status line: refused")
with tempfile.TemporaryDirectory() as d:
    root = layout(d, "# Changelog\n\n## [Unreleased]\n\n### Added\n- Soon.\n", False)
    rc, out, err = run(root, "0.2.0")
    check(rc == 1 and out == "" and "Unreleased" in err, "only [Unreleased]: refused with a hint")
    rc, _, err = run(root, "Unreleased")
    check(rc == 1, "the name Unreleased is not a version")
    rc, _, err = run(root, "v0.1.0")
    check(rc == 1, "a tag name (v0.1.0) is not a version")
with tempfile.TemporaryDirectory() as d:
    rc, _, err = run(layout(d, CL, True), "9.9.9")
    check(rc == 1 and "9.9.9" in err, "missing version: refused")
with tempfile.TemporaryDirectory() as d:
    root = layout(d, "# C\n\n## [0.3.0] - 2026-01-01 (pre-release)\n\n### Added\n\n## [0.2.0] - 2026-01-01\n- x\n", True)
    rc, _, err = run(root, "0.3.0")
    check(rc == 1 and "no items" in err, "empty section: refused")
with tempfile.TemporaryDirectory() as d:
    rc, _, err = run(d, "0.1.0")
    check(rc == 1, "no CHANGELOG file: refused")

print("test_release_notes: " + ("FAIL" if bad else "PASS"))
sys.exit(1 if bad else 0)
