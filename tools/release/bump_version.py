#!/usr/bin/env python3
"""Set the release version everywhere in one go (the release step).

  python3 tools/release/bump_version.py [--check] [--date YYYY-MM-DD] [--root R] X.Y.Z
  python3 tools/release/bump_version.py --agree [--root R]

Adapted from the same tool in Project Helghast, the Killzone 3 port. names.VERSION is the single source; R&C's mirrors are
port/rc/CMakeLists.txt (project VERSION: the exe; tools/build-rc.sh checks RC_RELEASE_TAG against it) and
launcher/CMakeLists.txt (the launcher's project VERSION) and
data/titles/catalog.json (port_version: the setup engine's rebuild notification) and
public/CITATION.cff (version and date-released: the fields are added with the first release, so a CITATION
without them gets them). Every `<!-- version -->...<!-- /version -->` span in the public README/BUILDING/ROADMAP has its
X.Y.Z replaced (none exist yet). public/CHANGELOG.md is required: its `## [Unreleased]` heading becomes
`## [X.Y.Z] - DATE (pre-release)` (a stub with an empty "### Added" goes above the newest entry when there is no
[Unreleased]; an existing section for the version is left alone). Version form: tags vMAJOR.MINOR.PATCH only, every release
a pre-release until 1.0.0, so "(pre-release)" is written for 0.x and left out from 1.0.0 on.
Order: the card check (recomp_card.py) runs first and stops the bump with nothing written; then the edits; then
--agree on the result (exit 1 on a mismatch). Running it again with the same version changes nothing. A version older
than names.VERSION, or not X.Y.Z (no v, no -alpha/-rc suffix), is refused. --check reports what would change and
writes nothing. --agree only checks that the mirrors equal names.VERSION. Local tool: no network, no git, no push;
the result goes through the gate as one commit, then release_notes.py makes the notes.
"""
import argparse
import datetime
import json
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]

MARKER = re.compile(r"(<!-- version -->)(.*?)(<!-- /version -->)")
SEMVER = re.compile(r"\d+\.\d+\.\d+")
PUBLIC_MARKER_FILES = ("public/README.md", "public/docs/BUILDING.md", "public/docs/ROADMAP.md")
CMAKE = "port/rc/CMakeLists.txt"
CMAKE_RE = r"^(\s+VERSION\s+)(\d+\.\d+\.\d+)(\s*)$"   # the project() block's line (cmake_minimum_required is not indented)
LAUNCHER_CMAKE = "launcher/CMakeLists.txt"              # project(qwark_launcher VERSION X.Y.Z ...): the launcher's version
LAUNCHER_RE = r"^(project\(qwark_launcher\s+VERSION\s+)(\d+\.\d+\.\d+)(\b)"
CATALOG = "data/titles/catalog.json"


def at(root, rel):
    """The file for a repo path: the public export puts public/ at its root."""
    if rel.startswith("public/") and not (root / "public").is_dir():
        return root / rel[len("public/"):]
    return root / rel


def parse(v):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", v)
    return tuple(int(x) for x in m.groups()) if m else None


def current_version(root):
    text = (root / "tools/release/names.py").read_text(encoding="utf-8")
    m = re.search(r'^VERSION\s*=\s*"(\d+\.\d+\.\d+)"', text, re.M)
    if not m:
        raise SystemExit("bump_version: names.VERSION not found")
    return m.group(1)


def read_catalog(text):
    """Require a valid version mirror before making any release edits."""
    try:
        cat = json.loads(text)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"bump_version: {CATALOG} is invalid JSON: {exc}") from exc
    version = cat.get("port_version") if isinstance(cat, dict) else None
    if not isinstance(version, str) or parse(version) is None:
        raise SystemExit(f"bump_version: {CATALOG} port_version must be an X.Y.Z string")
    return cat


def catalog_version(text, version):
    cat = read_catalog(text)
    cat["port_version"] = version
    return json.dumps(cat, indent=1, ensure_ascii=False) + "\n"


def fill_markers(text, version):
    return MARKER.sub(lambda m: m.group(1) + SEMVER.sub(version, m.group(2)) + m.group(3), text)


def sub1(text, pattern, repl, what):
    new, n = re.subn(pattern, repl, text, flags=re.M)
    if n != 1:
        raise SystemExit(f"bump_version: expected exactly one {what}, found {n}")
    return new


def changelog_stub(text, version, date):
    if re.search(rf"^## \[{re.escape(version)}\]", text, re.M):
        return text
    pre = " (pre-release)" if version.startswith("0.") else ""
    if re.search(r"^## \[Unreleased\][ \t]*$", text, re.M):   # Keep a Changelog: the release takes the unreleased entries
        return re.sub(r"^## \[Unreleased\][ \t]*$", f"## [{version}] - {date}{pre}", text, count=1, flags=re.M)
    m = re.search(r"^## \[", text, re.M)
    if not m:
        raise SystemExit("bump_version: no '## [' entry in CHANGELOG.md to put the stub above")
    return text[:m.start()] + f"## [{version}] - {date}{pre}\n\n### Added\n\n" + text[m.start():]


def cff(text, version, date, changed):
    if re.search(r"^version:", text, re.M):
        text = sub1(text, r"^(version:\s*)\"?[^\s\"]+\"?\s*$", rf"\g<1>{version}", "CITATION version")
        if re.search(r"^date-released:", text, re.M) and changed:
            text = re.sub(r"^(date-released:\s*).*$", rf'\g<1>"{date}"', text, flags=re.M)
        return text
    # First release: add version and date-released after the license line (or at the end).
    add = f'version: {version}\ndate-released: "{date}"\n'
    m = re.search(r"^license:.*\n", text, re.M)
    return text[:m.end()] + add + text[m.end():] if m else text.rstrip("\n") + "\n" + add


def edits(root, version, date):
    """{relative path: new text} for every file whose text changes."""
    out = {}
    changed = version != current_version(root)

    def edit(rel, fn, required=True):
        p = at(root, rel)
        if not p.is_file():
            if required:
                raise SystemExit(f"bump_version: {rel} is missing"
                                 + (" (public text: write it through the gate before the release)"
                                    if rel.startswith("public/") else ""))
            return
        old = p.read_text(encoding="utf-8")  # universal newlines; the write restores the file's own
        new = fn(old)
        if new != old:
            out[rel] = new

    edit("tools/release/names.py",
         lambda t: sub1(t, r'^(VERSION\s*=\s*")\d+\.\d+\.\d+(")', rf"\g<1>{version}\2", "names.VERSION"))
    edit(CMAKE, lambda t: sub1(t, CMAKE_RE, rf"\g<1>{version}\3", "project VERSION"))
    edit(LAUNCHER_CMAKE, lambda t: sub1(t, LAUNCHER_RE, rf"\g<1>{version}\3", "launcher project VERSION"), required=False)
    edit(CATALOG, lambda t: catalog_version(t, version))
    edit("public/CITATION.cff", lambda t: cff(t, version, date, changed))
    for rel in PUBLIC_MARKER_FILES:
        edit(rel, lambda t: fill_markers(t, version), required=False)
    edit("public/CHANGELOG.md", lambda t: changelog_stub(t, version, date))
    return out


def agree(root):
    """Problem lines: a mirror that is not names.VERSION."""
    want, bad = current_version(root), []
    p = at(root, CMAKE)
    if p.is_file():
        m = re.search(CMAKE_RE, p.read_text(encoding="utf-8"), re.M)
        if not m or m.group(2) != want:
            bad.append(f"{CMAKE} project VERSION {m.group(2) if m else None} is not names.VERSION {want}")
    else:
        bad.append(f"{CMAKE} is missing")
    p = at(root, LAUNCHER_CMAKE)
    if p.is_file():
        m = re.search(LAUNCHER_RE, p.read_text(encoding="utf-8"), re.M)
        if not m or m.group(2) != want:
            bad.append(f"{LAUNCHER_CMAKE} project VERSION {m.group(2) if m else None} is not names.VERSION {want}")
    p = at(root, CATALOG)
    if p.is_file():
        try:
            version = read_catalog(p.read_text(encoding="utf-8"))["port_version"]
        except SystemExit as exc:
            bad.append(str(exc))
        else:
            if version != want:
                bad.append(f"{CATALOG} port_version {version} is not names.VERSION {want}")
    else:
        bad.append(f"{CATALOG} is missing")
    p = at(root, "public/CITATION.cff")
    m = re.search(r"^version:\s*\"?([^\s\"]+)", p.read_text(encoding="utf-8"), re.M) if p.is_file() else None
    if m and m.group(1) != want:
        bad.append(f"public/CITATION.cff version {m.group(1)} is not names.VERSION {want}")
    p = at(root, "public/CHANGELOG.md")
    m = re.search(r"^## \[(\d+\.\d+\.\d+)\]", p.read_text(encoding="utf-8"), re.M) if p.is_file() else None
    if m and m.group(1) != want:
        bad.append(f"public/CHANGELOG.md newest release {m.group(1)} is not names.VERSION {want}")
    for rel in PUBLIC_MARKER_FILES:
        p = at(root, rel)
        for span in MARKER.findall(p.read_text(encoding="utf-8")) if p.is_file() else ():
            for v in SEMVER.findall(span[1]):
                if v != want:
                    bad.append(f"{rel} version marker says {v}, not names.VERSION {want}")
    return bad


def card_check(root):
    r = subprocess.run([sys.executable, str(root / "tools/release/recomp_card.py"), "--root", str(root)],
                       capture_output=True, text=True)
    return r.returncode, (r.stderr or r.stdout).strip()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("version", nargs="?", help="the new version, X.Y.Z, not older than names.VERSION")
    ap.add_argument("--check", action="store_true", help="report what would change; write nothing")
    ap.add_argument("--agree", action="store_true", help="only check that every mirror equals names.VERSION")
    ap.add_argument("--date", default=datetime.date.today().isoformat(), help="CHANGELOG/CITATION date (default today)")
    ap.add_argument("--root", default=str(ROOT), help="repo root (tests use a temp copy)")
    a = ap.parse_args(argv)
    root = pathlib.Path(a.root)
    if a.agree:
        bad = agree(root)
        for b in bad:
            print("bump_version: " + b, file=sys.stderr)
        print("bump_version: versions agree (%s)" % current_version(root) if not bad else "bump_version: versions disagree")
        return 1 if bad else 0
    if a.version is None:
        ap.error("the version is required (or --agree)")
    new = parse(a.version)
    if new is None:
        print(f"bump_version: {a.version!r} is not X.Y.Z (tags are vX.Y.Z, no suffix; pass the number)", file=sys.stderr)
        return 2
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", a.date):
        print(f"bump_version: --date {a.date!r} is not YYYY-MM-DD", file=sys.stderr)
        return 2
    cur = current_version(root)
    if new < parse(cur):
        print(f"bump_version: {a.version} is older than the current {cur}", file=sys.stderr)
        return 2
    rc, msg = card_check(root)
    if rc:
        print("bump_version: the card check fails, nothing written:\n" + msg[-2000:], file=sys.stderr)
        return 1
    changed = edits(root, a.version, a.date)
    for rel in sorted(changed):
        print(("would change " if a.check else "changed ") + rel)
    if not changed:
        print(f"bump_version: nothing to change (already {a.version})")
    if a.check:
        return 0
    for rel, text in changed.items():
        p = at(root, rel)   # keep each file's own line endings (KZ3 issue #9)
        eol = "\r\n" if b"\r\n" in p.read_bytes() else "\n"
        p.write_text(text, encoding="utf-8", newline=eol)
    bad = agree(root)
    for b in bad:
        print("bump_version: " + b, file=sys.stderr)
    rc, msg = card_check(root)
    print("recomp_card: " + ("OK" if not rc else "FAILED\n" + msg[-2000:]))
    return 1 if bad or rc else 0


if __name__ == "__main__":
    sys.exit(main())
