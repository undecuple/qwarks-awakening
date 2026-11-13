#!/usr/bin/env python3
"""The GitHub release body (and title) for a release tag, built from the version's CHANGELOG section.

  python3 tools/release/release_notes.py --tag TAG [--root R] [--out FILE] [--title-out FILE]

TAG is `launcher-vX.Y.Z` or `<id>-vX.Y.Z` for a catalogue game id (data/titles/catalog.json: tods, qfb, acit, a4o,
ffa, nexus, rc1, rc2, rc3, dlhd): the launcher release carries the downloads, a game release only notes (which
replaces the single vX.Y.Z tag). From the Killzone 3 port's tools/release/release_notes.py.

Reads `## [X.Y.Z] - DATE (pre-release)` from public/CHANGELOG.md (or CHANGELOG.md at the root of the public export;
--root defaults to the repo or export root; bump_version.py writes the heading). The section's ### groups are picked
by the tag:
  - launcher: the groups "[All games]" and "[Launcher]" (the runtime, the launcher, the setup engine and the build kit);
  - a game: every group whose heading names the game in brackets, by its catalogue badge name or its id ("[ToD] ...",
    "[R&C 1], [R&C 2], [R&C 3] ...", "[DLHD] ..."), and a line with the game's playability state from the catalogue;
  - the "Test setup" group (where and how the runs were made): always for the launcher; for a game when one of its
    items names a date the group names.
The body: a first paragraph (what the release is; "a pre-release (STATUS)" with a link to docs/RELEASE-CRITERIA.md
while the heading says "(pre-release)", STATUS being the README's "**Status: ...**" line in lower case), the section's
intro (the text between its heading and its first item or ### heading, each paragraph joined into one line), the
picked groups (item text verbatim), "See CHANGELOG.md for the full list.", a known-issues line when
docs/KNOWN_ISSUES.md exists and the intro does not link it already, the AI line in general terms and a fixed last
paragraph (the launcher's: inputs and sums; a game's: inputs and where its build comes from). The title (--title-out):
"Launcher vX.Y.Z" or "<badge name> vX.Y.Z".
Every link in the body is absolute and pinned to the tag (https://github.com/OWNER/REPO/blob/TAG/PATH, tree/ for a
folder): a release body has no folder of its own, and a link to main would drift after the release. The CHANGELOG's
relative links are rewritten that way (they are relative to the repository root); the file names the body adds itself
are such links too.
Exit 1 with a message when the tag is malformed or names no catalogue game, the section is missing or only
[Unreleased], or the tag picks no item: a release must never go out with generic text silently. No network, no git; the
release workflow runs it before the build and publishes its output as the draft release's notes. The fixed sentences
below are public text.
"""
import argparse
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import names  # noqa: E402

AI_LINE = "AI tools were used in making this port; the README section \"How AI was used\" says how."
PARA2 = (f"You need your own copy of the game (the US disc, {names.SERIAL}) and the decrypted EBOOT made from it; "
         "neither is included or downloaded. SHA-256 sums of the four files: SHA256SUMS. See the README for "
         "what it does and what goes online.")
PARA2_GAME = ("You need your own copy of the game and the decrypted EBOOT made from it; neither is included or "
              "downloaded. This release has no download: the game is built on your PC from your copy by the launcher "
              "of the same version. See the README for what it does and what goes online.")
LAUNCHER_GROUPS = ("[All games]", "[Launcher]")


def downloads(version):
    """The launcher release's download list (0.3.1): the installer first, the zips as the portable option."""
    return ["**Downloads**", "",
            f"- **Installer (recommended):** `{names.setup_exe_name(version)}`. It installs the launcher and the build "
            "kit for your Windows user (no administrator rights) and adds Start-menu and desktop shortcuts. The program "
            "is not signed: if Windows shows \"Windows protected your PC\", choose \"More info\", then \"Run anyway\".",
            f"- **Portable (no install):** `{names.build_zip_name(version)}` holds the launcher and the build kit. "
            f"`{names.launcher_zip_name(version)}` holds the launcher only; it asks before it downloads the build kit "
            f"on your first Build. Unpack the whole folder, then start `{names.LAUNCHER_EXE}`.",
            f"- `{names.symbols_zip_name(version)}`: the launcher's debug information, only for bug reports.", ""]
SETUP_GROUP = "Test setup"
STATE = {"not_built": "not built", "boots": "boots", "menus": "menus", "in_game": "in game",
         "playable": "playable", "completed": "completed"}          # badges.py's labels


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


def catalog_games(root):
    """{id: game} for every catalogue game that can have a release (children of a grouping entry, not the group)."""
    p = at(root, "data/titles/catalog.json")
    if not p.is_file():
        raise NotesError(f"no catalogue at {p}")
    cat = json.loads(p.read_text(encoding="utf-8"))
    return {g["id"]: g for e in cat["games"] for g in [e] + list(e.get("children", [])) if not g.get("children")}


def parse_tag(tag):
    """'launcher-v0.3.0' -> ('launcher', '0.3.0'); 'tods-v0.3.0' -> ('tods', '0.3.0')."""
    m = re.fullmatch(r"([a-z0-9]+)-v(\d+\.\d+\.\d+)", tag or "")
    if not m:
        raise NotesError(f"'{tag}' is not launcher-vX.Y.Z or <game id>-vX.Y.Z")
    return m.group(1), m.group(2)


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


def split(body):
    """-> (intro paragraphs, [(group name, [items])]) of a section body."""
    intro, groups, cur = [[]], [], None
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
        elif cur is None:                    # the intro: paragraphs before the first item or ### heading
            if ln.strip():
                intro[-1].append(ln.strip())
            elif intro[-1]:
                intro.append([])
    return [" ".join(p) for p in intro if p], [g for g in groups if g[1]]


def tagged(tag, path):
    """The file at `path` (repo-relative) as of the release's tag; a folder (ending in /) as its tree view."""
    return f"{names.REPO_URL}/{'tree' if path.endswith('/') else 'blob'}/{tag}/{path}"


def pin_links(text, tag):
    """Relative Markdown links (relative to the repository root, as in CHANGELOG.md) -> links pinned to the tag."""
    def fix(m):
        url = m.group(2)
        if re.match(r"[A-Za-z][A-Za-z0-9+.-]*:", url):
            return m.group(0)
        path = url[2:] if url.startswith("./") else url
        if path.startswith("#"):                 # an anchor of the CHANGELOG itself
            path = "CHANGELOG.md" + path
        return f"[{m.group(1)}]({tagged(tag, path)})"
    return re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", fix, text)


def names_game(group, game):
    """Does a group heading name the game in brackets (its badge name or its id, case ignored)?"""
    tokens = {t.strip().lower() for t in re.findall(r"\[([^\]]+)\]", group.split("*")[0])}
    return bool(tokens & {game.get("badge_name", "").lower(), game["id"].lower()})


def build(text, tag, status=None, known_issues=False, games=None):
    """-> (title, body) for a release tag."""
    who, version = parse_tag(tag)
    heading, body = section(text, version)
    intro, groups = split(body)
    pre = "(pre-release)" in heading
    if pre and not status:
        raise NotesError("the README has no '**Status: ...**' line for the pre-release wording")
    link = lambda path, label=None: f"[{label or path}]({tagged(tag, path)})"
    pre_txt = f", a pre-release ({status}), see {link('docs/RELEASE-CRITERIA.md')}" if pre else ""
    if who == "launcher":
        title = f"Launcher v{version}"
        picked = [g for g in groups if g[0].startswith(LAUNCHER_GROUPS)]
        # the launcher release is a full release marked Latest, so its first line never says "pre-release"; the
        # README's state stays (the v0.3.0 published form). Game bodies keep the pre-release wording.
        word = (status or "").split(",")[0].strip()
        state = f"{word}: no game is playable start to finish yet" if "not playable" in (status or "") else word
        launcher_txt = f" ({state}), see {link('docs/RELEASE-CRITERIA.md')}" if pre else ""
        first = [f"{names.PRODUCT} launcher {version}{launcher_txt}. It builds the games on your PC from your own copy; "
                 "the release notes of each game are its own release of the same version.", ""] + downloads(version)
        last = PARA2
    else:
        game = (games or {}).get(who)
        if game is None:
            raise NotesError(f"'{who}' is not a game id of the catalogue ({', '.join(sorted(games or {}))})")
        badge = game.get("badge_name") or who
        title = f"{badge} v{version}"
        picked = [g for g in groups if names_game(g[0], game)]
        state = STATE.get(game.get("playability"), game.get("playability") or "not given")
        when = "; ".join(x for x in (game.get("playability_checked") and f"last checked {game['playability_checked']}",
                                     game.get("playability_note")) if x)
        first = [f"{names.PRODUCT}: {badge} in release {version}{pre_txt}. This release holds notes only; the game is "
                 f"built with [the launcher release]({names.REPO_URL}/releases/tag/launcher-v{version}) of the same "
                 "version.", "",
                 f"State in this release: **{state}**" + (f" ({when})" if when else "")
                 + f". The states are defined in {link('docs/RELEASE-CRITERIA.md', 'RELEASE-CRITERIA.md')}.", ""]
        last = PARA2_GAME
    if not picked:
        raise NotesError(f"the '## [{version}]' section has no items for {tag}")
    # the test setup names the runs of given dates: a game's body takes it only when its own items name one of them
    day = re.compile(r"\d{4}-\d{2}-\d{2}")
    days = {d for _, items in picked for i in items for d in day.findall(i)}
    picked += [g for g in groups if g[0] == SETUP_GROUP and g not in picked
               and (who == "launcher" or days & {d for i in g[1] for d in day.findall(i)})]
    out = first
    for p in intro:
        out += [pin_links(p, tag), ""]
    out += [f"New in {version}:", ""]
    for name, items in picked:
        out += [f"**{name}**", ""] + [pin_links(i, tag) for i in items] + [""]
    out += [f"See {link('CHANGELOG.md')} for the full list."]
    if known_issues and not any("KNOWN_ISSUES.md" in p for p in intro):
        out += [f"Known issues: {link('docs/KNOWN_ISSUES.md')}."]
    out += ["", AI_LINE, "", last]
    return title, "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--tag", required=True, help="launcher-vX.Y.Z or <game id>-vX.Y.Z")
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--out")
    ap.add_argument("--title-out")
    a = ap.parse_args(argv)
    root = pathlib.Path(a.root)
    try:
        who, _ = parse_tag(a.tag)
        games = catalog_games(root) if who != "launcher" else None
        title, notes = build(at(root, "public/CHANGELOG.md").read_text(encoding="utf-8"), a.tag, readme_status(root),
                             at(root, "public/docs/KNOWN_ISSUES.md").is_file(), games)
    except (OSError, ValueError, KeyError, NotesError) as e:
        print(f"release_notes: {e}", file=sys.stderr)
        return 1
    if a.out:
        pathlib.Path(a.out).write_text(notes, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(notes)
    if a.title_out:
        pathlib.Path(a.title_out).write_text(title + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
