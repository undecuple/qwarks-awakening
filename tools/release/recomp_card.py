#!/usr/bin/env python3
"""Checks (and can regenerate) public/.recomp.json, the recomp.fyi project card (schema v1).

  python3 tools/release/recomp_card.py [--root R] [--card FILE] [--write [--out FILE]]

Adapted from the same tool in Project Helghast, the Killzone 3 port; it keeps .recomp.json current with the releases.
Reads public/.recomp.json (or .recomp.json at the root of the public export; --root
defaults to the repo or export root; --card checks another file, e.g. a draft, against the same tree). Rules, one
output line per problem, exit 1 if any:
  - it parses and uses only schema v1 fields (private extensions start with `x-`); no version field (link /releases);
  - wikidata matches ^Q[1-9][0-9]*$; every maintainers[].link and links[].url starts with https://;
  - status is a schema value and agrees with the README status line ("**Status: ...**": in-progress unless it says
    playable without "partly"/"not") and with the release criteria (public/docs/RELEASE-CRITERIA.md must exist; while
    names.VERSION is 0.x every release is a pre-release, so never "released" or "complete");
  - links into this repo (github.com/OWNER/REPO/blob|tree/main/PATH) name a file or folder that the public export holds
    (public/PATH, or port/... and the flake files, which the export copies from the repo root);
  - project is names.PRODUCT; original.serial / region equal names.SERIAL / REGION and original.revision starts with
    names.DISC_VERSION and, with names.UPDATE_VERSION None, says "no update" (otherwise names that update).
--write rewrites only what follows from one source: status (README), project (names.py), original.serial / region /
revision (names.py), the Repository and Releases links (names.REPO_URL), and drops repo links whose file is gone.
With --out it writes the result there and leaves the card alone (how docs/public-drafts/recomp.json is made: the card
is public text and goes through the gate). The default is check-only. Local tool: no network, no git.
Further games (recomp.fyi: one file per game, .recomp-2.json .. .recomp-9.json):
without --card every numbered card in public/ is checked too, with the same rules except the disc: its `game` must be a
title of data/titles/catalog.json, original.serial one of that game's title IDs, region the catalogue's region for that
ID, and revision must name the game's catalogue update ("update 01.20") or say "no update" when it has none. --card
on a numbered file checks it the same way. --write rewrites only .recomp.json.
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

TOP = {"$schema", "game", "wikidata", "system", "type", "original", "project", "status", "targets", "toolchain",
       "approach", "maintainers", "links", "help", "tags", "notes"}
ORIGINAL = {"region", "revision", "serial", "sha1"}
STATUS = ("exploring", "in-progress", "playable", "released", "complete", "paused", "unknown")
TYPES = ("recomp", "port", "decomp")
REPO_LINK = re.compile(r"^https://github\.com/" + re.escape(names.OWNER) + "/" + re.escape(names.REPO_SLUG)
                       + r"/(?:blob|tree)/main/([^#?]+)")
# Paths tools/export-public.sh copies from the repo root (its ALLOW list) rather than from public/.
FROM_ROOT = ("port/", "flake.nix", "flake.lock")
RELEASES_URL = names.REPO_URL + "/releases"


def at(root, rel):
    """The file for a repo path: the public export puts public/ at its root."""
    if rel.startswith("public/") and not (root / "public").is_dir():
        return root / rel[len("public/"):]
    return root / rel


def card_path(root):
    return at(root, "public/.recomp.json")


def readme_status(root):
    """The README's status line text, lower case ('early work', 'partly playable', ...), or None."""
    p = at(root, "public/README.md")
    m = re.search(r"\*\*Status: ([^*.]+?)\.?\*\*", p.read_text(encoding="utf-8")) if p.is_file() else None
    return m.group(1).strip().lower() if m else None


def expected_status(readme):
    """The card status that follows from the README status line."""
    if readme is None:
        return None
    if "playable" in readme and "partly" not in readme and "not " not in readme:
        return "playable"
    return "in-progress"


def expected_revision():
    upd = "no update" if names.UPDATE_VERSION is None else f"update {names.UPDATE_VERSION}"
    return f"{names.DISC_VERSION} (disc, {upd})"


def repo_target_exists(root, rel):
    rel = rel.rstrip("/")
    if at(root, "public/" + rel).exists():
        return True
    return rel.startswith(FROM_ROOT) and (root / rel).exists()


def catalog_game(root, title):
    """The catalogue entry whose title is `title`, or None."""
    p = root / "data/titles/catalog.json"
    if not p.is_file():
        return None
    c = json.loads(p.read_text(encoding="utf-8"))
    for g in (c.get("games", []) if isinstance(c, dict) else c):
        if g.get("title") == title:
            return g
    return None


def check_further_original(o, game):
    """Problem lines for the `original` of a numbered card, against its catalogue entry."""
    bad = []
    regions = {sid: reg for kind in (game.get("title_ids") or {}).values() for reg, ids in kind.items() for sid in ids}
    ser = o.get("serial")
    if ser not in regions:
        bad.append(f"original.serial {ser!r} is not a title ID of {game['title']} in the catalogue")
    elif "region" in o and o["region"] != regions[ser]:
        bad.append(f"original.region {o['region']!r} is not the catalogue's region for {ser} ({regions[ser]})")
    rev = str(o.get("revision", ""))
    upd = (game.get("update") or {}).get("version")
    if upd is None and "no update" not in rev:
        bad.append(f"original.revision {rev!r} does not say 'no update' (the catalogue lists none for this game)")
    elif upd is not None and f"update {upd}" not in rev:
        bad.append(f"original.revision {rev!r} does not name the catalogue's update ('update {upd}')")
    return bad


def check(root, card, further=False):
    """Problem lines for a parsed card (further: a numbered card for another game, its disc checked against the
    catalogue instead of names.py)."""
    bad = []
    if not isinstance(card, dict):
        return ["card is not a JSON object"]
    for k in card:
        if k not in TOP and not k.startswith("x-"):
            bad.append(f"field '{k}' is not in schema v1 (private fields start with x-)"
                       + (": link /releases instead of a version" if "version" in k.lower() else ""))
    w = card.get("wikidata")
    if w is not None and not (isinstance(w, str) and re.fullmatch(r"Q[1-9][0-9]*", w)):
        bad.append(f"wikidata {w!r} does not match ^Q[1-9][0-9]*$")
    t = card.get("type")
    for v in ([t] if isinstance(t, str) else t or []):
        if v not in TYPES:
            bad.append(f"type {v!r} is not one of {', '.join(TYPES)}")
    if card.get("project") != names.PRODUCT:
        bad.append(f"project {card.get('project')!r} is not names.PRODUCT ({names.PRODUCT!r})")
    for m in card.get("maintainers", []) or []:
        if not isinstance(m, dict) or not m.get("name"):
            bad.append("a maintainer has no name")
            continue
        if "link" in m and not str(m["link"]).startswith("https://"):
            bad.append(f"maintainer {m['name']}: link {m['link']!r} is not https://")
    for l in card.get("links", []) or []:
        u = l.get("url", "") if isinstance(l, dict) else ""
        label = l.get("label") if isinstance(l, dict) else l
        if not str(u).startswith("https://"):
            bad.append(f"link {label!r}: url {u!r} is not https://")
            continue
        m = REPO_LINK.match(u)
        if m and not repo_target_exists(root, m.group(1)):
            bad.append(f"link {label}: {m.group(1)} is not in the public export")
    orig = card.get("original")
    game = catalog_game(root, card.get("game")) if further else None
    if further and game is None:
        bad.append(f"game {card.get('game')!r} is not a title in data/titles/catalog.json")
    for o in ([orig] if isinstance(orig, dict) else orig or []):
        for k in o:
            if k not in ORIGINAL:
                bad.append(f"original.{k} is not in schema v1")
        if further:
            if game is not None:
                bad += check_further_original(o, game)
            continue
        if o.get("serial") != names.SERIAL:
            bad.append(f"original.serial {o.get('serial')!r} is not the disc the setup accepts ({names.SERIAL})")
        if "region" in o and o["region"] != names.REGION:
            bad.append(f"original.region {o['region']!r} is not the setup's ({names.REGION})")
        rev = str(o.get("revision", ""))
        if not rev.startswith(names.DISC_VERSION):
            bad.append(f"original.revision {rev!r} does not start with the disc version the setup accepts "
                       f"({names.DISC_VERSION})")
        elif names.UPDATE_VERSION is None and "no update" not in rev:
            bad.append(f"original.revision {rev!r} does not say 'no update' (the setup applies none)")
        elif names.UPDATE_VERSION is not None and names.UPDATE_VERSION not in rev:
            bad.append(f"original.revision {rev!r} does not name the update the setup accepts ({names.UPDATE_VERSION})")
    if not orig:
        bad.append("original is missing (serial and revision are required)")
    st = card.get("status")
    if st is not None and st not in STATUS:
        bad.append(f"status {st!r} is not one of {', '.join(STATUS)}")
    elif st is not None:
        if names.VERSION.startswith("0.") and st in ("released", "complete"):
            bad.append(f"status {st!r} for the pre-release {names.VERSION} (every release is a pre-release until 1.0.0)")
        rd = readme_status(root)
        exp = expected_status(rd)
        if rd is None:
            bad.append("README has no '**Status: ...**' line to compare the status with")
        elif st != exp and not (exp == "playable" and st in ("released", "complete")):
            bad.append(f"status {st!r} disagrees with the README status line ('{rd}', expects {exp!r})")
    if not at(root, "public/docs/RELEASE-CRITERIA.md").is_file():
        bad.append("docs/RELEASE-CRITERIA.md is missing (the release criteria the status is checked against)")
    return bad


def fixed(root, card):
    """The card with the single-source fields rewritten."""
    out = dict(card)
    exp = expected_status(readme_status(root))
    if exp:
        out["status"] = exp
    out["project"] = names.PRODUCT
    if isinstance(out.get("original"), dict) or not out.get("original"):   # a list of discs is left as it is
        o = {"region": names.REGION, "serial": names.SERIAL, "revision": expected_revision()}
        o.update({k: v for k, v in (out.get("original") or {}).items() if k not in o})
        out["original"] = o
    links = [l for l in card.get("links", []) or []
             if not (isinstance(l, dict) and REPO_LINK.match(l.get("url", ""))
                     and not repo_target_exists(root, REPO_LINK.match(l["url"]).group(1)))]
    urls = [l.get("url") for l in links if isinstance(l, dict)]
    if names.REPO_URL not in urls:
        links.insert(0, {"label": "Repository", "url": names.REPO_URL})
    if RELEASES_URL not in urls:
        links.append({"label": "Releases", "url": RELEASES_URL})
    out["links"] = links
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--card", help="the card to read (default: the root's public/.recomp.json)")
    ap.add_argument("--write", action="store_true", help="rewrite the single-source fields (in place, or into --out)")
    ap.add_argument("--out", help="with --write: write the result here and leave the card alone")
    a = ap.parse_args(argv)
    root = pathlib.Path(a.root)
    if a.out and not a.write:
        ap.error("--out needs --write")
    if not a.card and not a.write:
        rc = 0
        for n in range(2, 10):
            q = at(root, f"public/.recomp-{n}.json")
            if q.is_file():
                rc |= main(["--root", str(root), "--card", str(q)])
        p = card_path(root)
        return check_one(root, p, False, a) | rc
    p = pathlib.Path(a.card) if a.card else card_path(root)
    return check_one(root, p, re.fullmatch(r"\.recomp-[2-9]\.json", p.name) is not None, a)


def check_one(root, p, further, a):
    if not p.is_file():
        print(f"recomp_card: {p} not found", file=sys.stderr)
        return 1
    try:
        card = json.loads(p.read_text(encoding="utf-8"))
    except ValueError as e:
        print(f"recomp_card: {p.name} does not parse: {e}", file=sys.stderr)
        return 1
    if a.write and isinstance(card, dict) and not further:
        new = fixed(root, card)
        dst = pathlib.Path(a.out) if a.out else p
        if new != card or a.out:
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_text(json.dumps(new, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
            print(f"recomp_card: wrote {dst.name} (single-source fields)")
        card = new
    bad = check(root, card, further)
    for b in bad:
        print(f"recomp_card: {p.name}: " + b if further else "recomp_card: " + b, file=sys.stderr)
    if not bad:
        print(f"recomp_card: {p.name} OK" if further else "recomp_card: OK")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
