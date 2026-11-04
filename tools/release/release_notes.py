#!/usr/bin/env python3
"""The GitHub release body for a version, built from the CHANGELOG section.

  python3 tools/release/release_notes.py VERSION [--root R] [--out FILE]

From the Killzone 3 port's tools/release/release_notes.py. Reads `## [VERSION] - DATE (pre-release)` from
public/CHANGELOG.md (or CHANGELOG.md at the root of the public export; --root defaults to the repo or export root;
bump_version.py writes the heading) and prints: a first paragraph ("Qwark's Awakening VERSION, a pre-release (STATUS),
see docs/RELEASE-CRITERIA.md. New in VERSION:", STATUS being the README's "**Status: ...**" line in lower case) with
the section's items as a Markdown list grouped under its ### headings in order (item text verbatim), "See CHANGELOG.md
for the full list.", a known-issues line when docs/KNOWN_ISSUES.md exists, the AI line in general terms and the fixed
last paragraph (inputs, sums). The pre-release wording is left out when the heading lacks "(pre-release)" (from
1.0.0 on). Exit 1 with a message when the section is missing, empty, or the version is only [Unreleased]: the release must
never go out with generic text silently. No network, no git; the release workflow runs it before the build and
publishes its output as the draft release's notes. The fixed sentences below are public text.
"""
import argparse
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import names  # noqa: E402

AI_LINE = "AI tools were used in making this port; the README section \"How AI was used\" says how."
PARA2 = (f"You need your own copy of the game (the US disc, {names.SERIAL}) and the decrypted EBOOT made from it; "
         "neither is included or downloaded. SHA-256 sums: SHA256SUMS (and the .sha256 files). See the README for "
         "what it does and what goes online.")


def at(root, rel):
    """The file for a repo path: the public export puts public/ at its root."""
    if rel.startswith("public/") and not (root / "public").is_dir():
        return root / rel[len("public/"):]
    return root / rel


class NotesError(Exception):
    pass


def readme_status(root):
    p = at(root, "public/README.md")
    m = re.search(r"\*\*Status: ([^*.]+?)\.?\*\*", p.read_text(encoding="utf-8")) if p.is_file() else None
    return m.group(1).strip().lower() if m else None


def section(text, version):
    """(heading line, body lines) of the `## [version]` section."""
    lines = text.splitlines()
    head = re.compile(r"^## \[" + re.escape(version) + r"\](?!\S)")
    for i, ln in enumerate(lines):
        if head.match(ln):
            body = []
            for nxt in lines[i + 1:]:
                if nxt.startswith("## "):
                    break
                body.append(nxt)
            return ln, body
    raise NotesError(f"no '## [{version}]' section in the CHANGELOG"
                     + (" (only [Unreleased]: bump_version.py turns it into the release)"
                        if re.search(r"^## \[Unreleased\]", text, re.M) else ""))


def build(text, version, status=None, known_issues=False):
    heading, body = section(text, version)
    groups, cur = [], None
    for ln in body:
        if ln.startswith("### "):
            cur = (ln[4:].strip(), [])
            groups.append(cur)
        elif ln.startswith("- "):
            if cur is None:
                cur = ("Changes", [])
                groups.append(cur)
            cur[1].append(ln)
        elif ln.strip() and cur and cur[1] and ln.startswith((" ", "\t")):
            cur[1][-1] += "\n" + ln          # a continuation line of the previous item
    groups = [g for g in groups if g[1]]
    if not groups:
        raise NotesError(f"the '## [{version}]' section has no items")
    pre = "(pre-release)" in heading
    if pre and not status:
        raise NotesError("the README has no '**Status: ...**' line for the pre-release wording")
    first = f"{names.PRODUCT} {version}" + (f", a pre-release ({status}), see docs/RELEASE-CRITERIA.md" if pre else "")
    out = [first + f". New in {version}:", ""]
    for name, items in groups:
        out += [f"**{name}**", ""] + items + [""]
    out += ["See CHANGELOG.md for the full list."]
    if known_issues:
        out += ["Known issues: docs/KNOWN_ISSUES.md."]
    out += ["", AI_LINE, "", PARA2]
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("version")
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--out")
    a = ap.parse_args(argv)
    if not re.fullmatch(r"\d+\.\d+\.\d+", a.version):
        print(f"release_notes: '{a.version}' is not X.Y.Z", file=sys.stderr)
        return 1
    root = pathlib.Path(a.root)
    path = at(root, "public/CHANGELOG.md")
    try:
        notes = build(path.read_text(encoding="utf-8"), a.version, readme_status(root),
                      at(root, "public/docs/KNOWN_ISSUES.md").is_file())
    except (OSError, NotesError) as e:
        print(f"release_notes: {e}", file=sys.stderr)
        return 1
    if a.out:
        pathlib.Path(a.out).write_text(notes, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(notes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
