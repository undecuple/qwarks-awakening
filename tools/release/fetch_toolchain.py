#!/usr/bin/env python3
"""Fetch the pinned build toolchain (tools/release/toolchain.json) into a local cache: download, check SHA-256,
unpack. Standard library only, Python 3.8+. The release workflow runs it on the Windows runner before it builds the
launcher; nothing it fetches is a game file, and nothing of the toolchain ships except SDL3.dll and its licence.

From the Killzone 3 port's tools/release/fetch_toolchain.py (same author, MIT). R&C ships no toolchain to players
(no installer yet), so the payload bookkeeping (dest, licence groups, prune, notice lines) is left out; the download,
hash, published-list and unpack checks are KZ3's.

    fetch_toolchain.py [--manifest FILE] [--cache DIR] [--only NAME[,NAME]] [--fill]
    fetch_toolchain.py --check [--strict] [--manifest FILE]

  --cache   default work/release/toolchain-cache (downloads/<name>/, trees/<name>/, fetched.json)
  --fill    entries whose sha256 is TO-FILL are fetched and their hash written into the manifest (trust on first
            fetch: compare it with the release page's published hash where one exists; CMake's SHA-256 list is
            checked automatically through the entry's sha256_list). Without --fill a TO-FILL entry is refused.
  --check   validate the manifest only (no network); --strict also refuses TO-FILL hashes and TO-CONFIRM pins
            (the release build's gate)

A filled hash that does not match the download is refused and the download deleted (if the version was changed
on purpose, set its sha256 back to TO-FILL and use --fill). Zip (and tar) members with absolute paths, "..", drive letters,
zip symlink modes, or two zip names that differ only by letter case (one file on NTFS) are refused; a tarball is
extracted through tarfile's "data" filter (links that stay inside the tree are kept). Exit 0 = ok, 1 = a check
failed, 2 = usage.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import tarfile
import tempfile
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
MANIFEST = os.path.join(HERE, "toolchain.json")
CACHE = os.path.join(REPO, "work", "release", "toolchain-cache")
TO_FILL, TO_CONFIRM = "TO-FILL", "TO-CONFIRM"
HEX64 = re.compile(r"[0-9a-f]{64}")
NAME_RX = re.compile(r"[a-z0-9][a-z0-9.-]*")
MAX_DOWNLOAD = 1 << 30            # 1 GiB: llvm-mingw 20261006's Windows zip is 190,836,485 bytes
REQUIRED = ("name", "version", "url", "sha256", "archive", "license", "release_page")
FORMAT_FIELDS = ("url", "sha256_list", "notice")


# ---------------------------------------------------------------------------------------------------------- manifest
def load(path=MANIFEST):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def expand(entry):
    """The entry with '{version}', '{llvm}' (any of its own string fields) filled into url, sha256_list, notice."""
    e = dict(entry)
    fields = {k: v for k, v in entry.items() if isinstance(v, str)}
    for k in FORMAT_FIELDS:
        if k in e:
            e[k] = e[k].format(**fields)
    return e


def safe_rel(p):
    """True for a relative, forward-slash path without '..', drive letters or empty parts."""
    if not p or p.startswith(("/", "\\")) or "\\" in p or ":" in p:
        return False
    return all(part not in ("", ".", "..") for part in p.split("/"))


def validate(m, schemes=("https",), strict=False):
    """-> list of problems (empty = ok). strict: TO-FILL hashes and TO-CONFIRM pins are problems too."""
    bad = []
    if m.get("format") != 1:
        bad.append("format: expected 1")
    comps = m.get("components")
    if not isinstance(comps, list) or not comps:
        return bad + ["components: missing or empty"]
    names = set()
    for i, raw in enumerate(comps):
        where = f"components[{i}]"
        miss = [k for k in REQUIRED if not isinstance(raw.get(k), str) or not raw.get(k)]
        if miss:
            bad.append(f"{where}: missing {', '.join(miss)}")
            continue
        try:
            e = expand(raw)
        except (KeyError, IndexError, ValueError) as ex:
            bad.append(f"{where} {raw['name']}: bad placeholder {ex}")
            continue
        n = e["name"]
        where = f"{n}"
        if not NAME_RX.fullmatch(n):
            bad.append(f"{where}: name must be lower-case letters, digits, '.', '-'")
        if n in names:
            bad.append(f"{where}: duplicate name")
        names.add(n)
        for k in ("url", "sha256_list", "release_page"):
            if k in e and e[k].split(":", 1)[0] not in schemes:
                bad.append(f"{where}: {k} must use {'/'.join(schemes)}")
        if e["sha256"] == TO_FILL:
            if strict:
                bad.append(f"{where}: sha256 is TO-FILL (run fetch_toolchain.py --fill)")
        elif not HEX64.fullmatch(e["sha256"]):
            bad.append(f"{where}: sha256 must be 64 lower-case hex digits or TO-FILL")
        if e.get("pin") not in (None, "", TO_CONFIRM, "confirmed"):
            bad.append(f"{where}: pin must be TO-CONFIRM or confirmed")
        if strict and e.get("pin") == TO_CONFIRM:
            bad.append(f"{where}: pin is TO-CONFIRM (check the release page, then set 'confirmed')")
        if e["archive"] == "file":
            if not e.get("file_name") or not safe_rel(e["file_name"]) or "/" in e["file_name"]:
                bad.append(f"{where}: archive 'file' needs file_name (a plain file name)")
        elif e["archive"] not in ("zip", "tar"):
            bad.append(f"{where}: archive must be zip, tar or file")
    return bad


def fill_hash(path, name, digest):
    """Write `digest` into entry `name`'s TO-FILL sha256, keeping the file's layout (text edit, then re-parse)."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r'"name":\s*"%s"' % re.escape(name), text)
    if not m:
        raise ValueError(f"{name}: not found in {path}")
    rest = text[m.end():]
    nxt = re.search(r'"name":\s*"', rest)
    block = rest[:nxt.start()] if nxt else rest
    h = re.search(r'"sha256":\s*"%s"' % TO_FILL, block)
    if not h:
        raise ValueError(f"{name}: no TO-FILL sha256 to fill")
    pos = m.end() + h.start()
    new = text[:pos] + f'"sha256": "{digest}"' + text[pos + (h.end() - h.start()):]
    data = json.loads(new)
    got = [c for c in data["components"] if c.get("name") == name][0]["sha256"]
    if got != digest:
        raise ValueError(f"{name}: fill landed in the wrong place")
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(new)
    os.replace(tmp, path)


# ---------------------------------------------------------------------------------------------------------- download
def open_url(url):
    req = urllib.request.Request(url, headers={"User-Agent": "qwarks-awakening-release-fetch/1"})
    return urllib.request.urlopen(req, timeout=120)    # noqa: S310 (scheme checked by validate)


def download(url, dest, opener=open_url):
    """Stream url to dest (via dest.part), return its SHA-256. Refuses more than MAX_DOWNLOAD bytes."""
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    part, h, n = dest + ".part", hashlib.sha256(), 0
    try:
        with opener(url) as r, open(part, "wb") as f:
            for chunk in iter(lambda: r.read(1 << 20), b""):
                n += len(chunk)
                if n > MAX_DOWNLOAD:
                    raise ValueError(f"{url}: more than {MAX_DOWNLOAD:,} bytes")
                h.update(chunk)
                f.write(chunk)
        os.replace(part, dest)
    finally:
        if os.path.exists(part):
            os.remove(part)
    return h.hexdigest()


def hash_from_list(text, filename):
    """The hash for `filename` in a '<hex>  <name>' list (CMake's cmake-X-SHA-256.txt), else None."""
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].lstrip("*") == filename and HEX64.fullmatch(parts[0].lower()):
            return parts[0].lower()
    return None


# ---------------------------------------------------------------------------------------------------------- unpack
class PublicationError(OSError):
    """Publication failed; preserve_temporary keeps owned extraction recovery data."""
    def __init__(self, message, preserve_temporary=False):
        super().__init__(message)
        self.preserve_temporary = preserve_temporary


def new_stage(out):
    """Own a fresh sibling directory; never remove a pre-existing .partial path."""
    parent = os.path.dirname(os.path.abspath(out))
    os.makedirs(parent, exist_ok=True)
    return tempfile.mkdtemp(prefix="." + os.path.basename(out) + ".partial-", dir=parent)


def publish_tree(src, out):
    """Publish a prepared owned tree, restoring an existing tree on rename failure.

    This is rollback, not concurrent-reader atomic visibility or a writer lock.
    If restore also fails, keep both the old backup and prepared replacement.
    """
    backup_root = backup = None
    if os.path.lexists(out):
        if os.path.islink(out) or not os.path.isdir(out):
            raise OSError("cache tree destination is not a plain directory: " + out)
        backup_root = tempfile.mkdtemp(prefix="." + os.path.basename(out) + ".backup-",
                                       dir=os.path.dirname(os.path.abspath(out)))
        backup = os.path.join(backup_root, "previous")
        try:
            os.replace(out, backup)
        except OSError:
            os.rmdir(backup_root)  # Still empty; the old tree has not moved.
            raise
    try:
        os.replace(src, out)
    except OSError as publication:
        if backup:
            try:
                os.replace(backup, out)
            except OSError as restore:
                raise PublicationError(
                    f"cache publication failed ({publication}); previous tree restore also failed ({restore}); "
                    f"old tree retained at {backup}; prepared replacement retained at {src}", True) from publication
            os.rmdir(backup_root)
            raise PublicationError(f"cache publication failed ({publication}); previous tree restored") from publication
        raise
    if backup_root:
        try:
            shutil.rmtree(backup_root)
        except OSError as cleanup:
            raise PublicationError(f"new cache tree published at {out}, but previous-tree backup cleanup failed "
                                   f"at {backup_root}: {cleanup}") from cleanup


def finish_stage(part, src, out):
    """Only clean staging we created; a failed rollback explicitly retains it."""
    try:
        publish_tree(src, out)
    except PublicationError as error:
        if error.preserve_temporary:
            part = None
        raise
    finally:
        if part and os.path.lexists(part):
            shutil.rmtree(part)


def unpack_zip(path, out, strip_top):
    """Extract and publish into out. Refuses unsafe member names and symlinks; strip_top drops the one
    top-level folder every member shares (refused when there is not exactly one)."""
    with zipfile.ZipFile(path) as z:
        infos = [i for i in z.infolist() if not i.is_dir()]
        names = [i.filename for i in infos]
        for i in infos:
            if not safe_rel(i.filename.rstrip("/")):
                raise ValueError(f"unsafe zip member: {i.filename!r}")
            if stat.S_ISLNK(i.external_attr >> 16):
                raise ValueError(f"symlink in zip: {i.filename!r}")
        folded = {}
        for n in names:
            k = n.casefold()
            if k in folded:
                raise ValueError(f"two zip members differ only by case: {folded[k]!r}, {n!r}")
            folded[k] = n
        if strip_top:
            tops = {n.split("/", 1)[0] for n in names}
            if len(tops) != 1 or any("/" not in n for n in names):
                raise ValueError(f"strip_top: expected one top folder, found {sorted(tops)[:5]}")
        part = new_stage(out)
        try:
            for i in infos:
                rel = i.filename.split("/", 1)[1] if strip_top else i.filename
                d = os.path.join(part, *rel.split("/"))
                os.makedirs(os.path.dirname(d), exist_ok=True)
                with z.open(i) as src, open(d, "wb") as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
        except BaseException:
            shutil.rmtree(part)
            raise
    finish_stage(part, part, out)
    return len(infos)


def unpack_tar(path, out, strip_top):
    """A Linux tarball (the CI cross build's llvm-mingw): extracted with tarfile's "data" filter, which refuses
    absolute names, "..", links that leave the tree, device files and set-uid bits; symlinks inside the tree are kept
    (llvm-mingw's compiler names are links). strip_top as in unpack_zip. Python without extraction filters (before
    3.8.17 / 3.12) is refused rather than extracting unchecked."""
    if not hasattr(tarfile, "data_filter"):
        raise ValueError("this Python has no tarfile extraction filters; use Python 3.12+ (or 3.8.17+)")
    with tarfile.open(path, "r:*") as t:
        members = t.getmembers()
        names = [m.name for m in members if not m.isdir()]
        for m in members:
            if not safe_rel(m.name.rstrip("/")):
                raise ValueError(f"unsafe tar member: {m.name!r}")
        tops = {n.split("/", 1)[0] for n in names}
        if strip_top and (len(tops) != 1 or any("/" not in n for n in names)):
            raise ValueError(f"strip_top: expected one top folder, found {sorted(tops)[:5]}")
        part = new_stage(out)
        try:
            t.extractall(part, filter="data")
        except BaseException:
            shutil.rmtree(part)
            raise
    src = os.path.join(part, tops.pop()) if strip_top else part
    finish_stage(part, src, out)
    return len(names)


def unpack_file(path, out, name):
    """The plain-file archive kind uses the same owned publication/rollback."""
    if not safe_rel(name) or "/" in name:
        raise ValueError("plain-file archive needs a safe file name")
    part = new_stage(out)
    try:
        shutil.copyfile(path, os.path.join(part, name))
    except BaseException:
        shutil.rmtree(part)
        raise
    finish_stage(part, part, out)
    return 1


# ---------------------------------------------------------------------------------------------------------- fetch
def fetched_path(cache):
    return os.path.join(cache, "fetched.json")


def read_fetched(cache):
    try:
        with open(fetched_path(cache), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def write_fetched(cache, data):
    os.makedirs(cache, exist_ok=True)
    tmp = fetched_path(cache) + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=1, sort_keys=True)
        f.write("\n")
    os.replace(tmp, fetched_path(cache))


def drop(e, path):
    """Delete a refused download."""
    os.remove(path)


def fetch_one(raw, cache, fill, manifest_path, opener=open_url, out=sys.stdout):
    """Download, verify, unpack one entry. -> True on success (problems are printed)."""
    e = expand(raw)
    name = e["name"]
    url = e["url"]
    fname = url.rstrip("/").rsplit("/", 1)[-1]
    dl = os.path.join(cache, "downloads", name, fname)
    try:
        got = download(url, dl, opener)
    except (OSError, ValueError) as ex:
        print(f"FAIL {name}: download {url}: {ex} (check the version on {e['release_page']})", file=out)
        return False
    want = e["sha256"]
    if e.get("sha256_list"):
        try:
            lst = os.path.join(cache, "downloads", name, "sha256-list.txt")
            download(e["sha256_list"], lst, opener)
            with open(lst, encoding="utf-8", errors="replace") as f:
                published = hash_from_list(f.read(), fname)
        except (OSError, ValueError) as ex:
            published = None
            print(f"FAIL {name}: published hash list {e['sha256_list']}: {ex}", file=out)
        if published != got:
            print(f"FAIL {name}: SHA-256 {got} is not the published one ({published})", file=out)
            drop(e, dl)
            return False
    if want == TO_FILL:
        if not fill:
            print(f"FAIL {name}: sha256 is TO-FILL; re-run with --fill to record {got}", file=out)
            drop(e, dl)
            return False
        fill_hash(manifest_path, name, got)
        print(f"FILL {name}: sha256 {got} written to {os.path.basename(manifest_path)}", file=out)
    elif got != want:
        print(f"FAIL {name}: SHA-256 mismatch: manifest {want}, download {got}; refused and deleted (a deliberate "
              "version change: set sha256 to TO-FILL and use --fill)", file=out)
        drop(e, dl)
        return False
    tree = os.path.join(cache, "trees", name)
    try:
        if e["archive"] == "zip":
            n = unpack_zip(dl, tree, bool(e.get("strip_top")))
        elif e["archive"] == "tar":
            n = unpack_tar(dl, tree, bool(e.get("strip_top")))
        else:
            n = unpack_file(dl, tree, e["file_name"])
    except (OSError, ValueError, zipfile.BadZipFile, tarfile.TarError) as ex:
        print(f"FAIL {name}: unpack: {ex}", file=out)
        return False
    data = read_fetched(cache)
    data[name] = {"sha256": got, "url": url, "version": e["version"], "files": n}
    write_fetched(cache, data)
    print(f"ok   {name} {e['version']}: {n} file(s) in trees/{name}", file=out)
    return True


def main(argv=None, out=sys.stdout, opener=open_url, schemes=("https",)):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--manifest", default=MANIFEST)
    ap.add_argument("--cache", default=CACHE)
    ap.add_argument("--only", default="")
    ap.add_argument("--fill", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--strict", action="store_true")
    a = ap.parse_args(argv)
    try:
        m = load(a.manifest)
    except (OSError, ValueError) as ex:
        print(f"FAIL manifest {a.manifest}: {ex}", file=out)
        return 1
    problems = validate(m, schemes, strict=a.strict)
    for p in problems:
        print(f"FAIL {p}", file=out)
    comps = m.get("components", [])
    if a.check:
        unfilled = sum(1 for c in comps if c.get("sha256") == TO_FILL)
        unconfirmed = sum(1 for c in comps if c.get("pin") == TO_CONFIRM)
        print(f"manifest: {len(comps)} entries, {unfilled} TO-FILL hash(es), {unconfirmed} TO-CONFIRM pin(s); "
              f"{'ok' if not problems else f'{len(problems)} problem(s)'}", file=out)
        return 1 if problems else 0
    if problems:
        return 1
    only = {x for x in a.only.split(",") if x}
    unknown = only - {c["name"] for c in comps}
    if unknown:
        print(f"--only: unknown {sorted(unknown)}", file=out)
        return 2
    ok = True
    for c in comps:
        if not only or c["name"] in only:
            ok = fetch_one(c, os.path.abspath(a.cache), a.fill, a.manifest, opener, out) and ok
    print(f"fetch: {'ok' if ok else 'FAILED'} (cache {a.cache})", file=out)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
