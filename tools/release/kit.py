#!/usr/bin/env python3
"""Everything the launcher fetches from the internet, and nothing runs it without the player's click or opt-in
(launcher-plan.md phase 4; README "What goes online"). Standard library only, Python 3.8+, Windows and
Linux; started by path with the bundled embeddable Python, so it imports no module of its own folder.

    kit.py status   --root DIR --expect SHA256                  is the build kit next to the launcher, and the right one
    kit.py verify   --root DIR --expect SHA256                  hash every kit file against kit.json (slow, read only)
    kit.py fetch    --root DIR --expect SHA256 --url URL [--stop-file F] [--allow-local]
                                                                download the full build zip, install the kit files
    kit.py install  --root DIR --expect SHA256 --zip FILE       the same from a zip already on disk (no network)
    kit.py symbols  --exe EXE --url URL [--allow-local]         the launcher's own debug information, on request
    kit.py check-release --api URL --current X.Y.Z [--prerelease] [--allow-local]
                                                                the opt-in "check for new releases" (one GET)

The build kit is what firstrun.py needs besides the bare launcher: the release's port/, data/titles, tools/spu,
tools/release/firstrun*.py, cmake/, the patched ps3recomp (ps3recomp/), llvm-mingw, CMake and Ninja (toolchain/) and
their licence texts. Its list is kit.json at the install root: {"format": 1, "release", "files": [{"path", "sha256",
"size"}], ...}, written by pack_zip.py. The launcher carries kit.json's SHA-256 (compiled in from the same file), so:

  - status: kit.json missing = "missing"; another kit.json (another release) = "other"; a key file gone = "damaged";
  - fetch: the URL must be https://github.com/<owner>/<repo>/releases/download/... and the download may only be
    served from github.com or *.githubusercontent.com (--allow-local adds file:// and http://127.0.0.1 / localhost,
    for tests and only when the launcher was told so; its dialog shows the URL). The zip's <top>/kit.json must hash to
    --expect, every file it lists must be in the zip with its exact SHA-256 and size, and nothing else is taken from
    the zip (the launcher's own files in it are left alone). Any mismatch refuses the whole kit, deletes the download
    and the staging folder, and changes nothing. Files are staged in <root>/.kit-staging, then moved into place;
    kit.json is written last, so an interrupted install leaves no kit.json of the new release behind;
  - symbols: the launcher exe names its debug file and that file's CRC-32 in its .gnu_debuglink section (written by
    objcopy --add-gnu-debuglink); the asset zip must hold a file of that name with that CRC-32, else it is refused;
  - check-release: one anonymous GET of the GitHub releases list; prints the newest release and whether it is newer.

Output lines (the launcher parses them; every other line is plain text):
  PROGRESS step=N/M name=<download|verify|install> done=X total=Y elapsed=S pct=P
  REASON <text>                 the player's message; never a path
  RESULT ok=1|0 step=<step|cancelled> elapsed=S
  STATUS {json}                 status / check-release
Exit 0 = ok, 1 = refused or failed, 2 = usage, 130 = cancelled.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import threading
import time
import urllib.parse
import urllib.request
import zipfile
import zlib

FORMAT = 1
MANIFEST = "kit.json"
STAGING = ".kit-staging"
DOWNLOADS = ".kit-download"
MAX_KIT_ZIP = 3 << 30            # 3 GiB: the full build zip is a few hundred MB
MAX_SYMBOLS_ZIP = 1 << 30
MAX_API_BYTES = 1 << 20
HEX64 = re.compile(r"[0-9a-f]{64}")
# files the kit never writes: the running launcher, its Python, its SDL3 and the kit list itself
PROTECTED_TOP = re.compile(r"[^/]+\.(exe|dll)$", re.I)
# a kit is usable when these are there (besides kit.json); Windows names (the kit is for the Windows launcher)
KEY_FILES = ("port/rc/CMakeLists.txt", "ps3recomp/CMakeLists.txt", "tools/release/firstrun.py", "cmake/llvm-mingw.cmake")
UA = "qwarks-awakening-launcher/1"


class Refused(Exception):
    """A check failed: the message is the player's (no path)."""


class Cancelled(Exception):
    pass


# ------------------------------------------------------------------------------------------------------ manifest
def manifest_bytes(m):
    """The one byte form of kit.json (its SHA-256 is compiled into the launcher)."""
    return (json.dumps(m, indent=1, sort_keys=True, ensure_ascii=True) + "\n").encode("ascii")


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


def safe_rel(p):
    """A relative forward-slash path without '..', drive letters, empty parts or a backslash."""
    if not isinstance(p, str) or not p or p.startswith("/") or "\\" in p or ":" in p:
        return False
    return all(part not in ("", ".", "..") for part in p.split("/"))


def check_manifest(m):
    """-> list of problems of a parsed kit.json."""
    bad = []
    if not isinstance(m, dict) or m.get("format") != FORMAT:
        return ["kit.json: not format 1"]
    files = m.get("files")
    if not isinstance(files, list) or not files:
        return ["kit.json: no files"]
    seen = set()
    for f in files:
        p = f.get("path") if isinstance(f, dict) else None
        if not safe_rel(p):
            bad.append(f"kit.json: unsafe path {p!r}")
            continue
        if p.casefold() in seen:
            bad.append(f"kit.json: {p} twice (or differing only by letter case)")
        seen.add(p.casefold())
        if p == MANIFEST or p.startswith(("python/", STAGING + "/", DOWNLOADS + "/")) or PROTECTED_TOP.fullmatch(p):
            bad.append(f"kit.json: {p} is a launcher file, never part of the kit")
        if not HEX64.fullmatch(str(f.get("sha256", ""))) or not isinstance(f.get("size"), int) or f["size"] < 0:
            bad.append(f"kit.json: {p} has no sha256/size")
    return bad


def read_manifest(root):
    """-> (bytes, dict) of <root>/kit.json, or (None, None)."""
    try:
        with open(os.path.join(root, MANIFEST), "rb") as f:
            raw = f.read(16 << 20)
        return raw, json.loads(raw.decode("utf-8"))
    except (OSError, ValueError, UnicodeDecodeError):
        return None, None


def status(root, expect):
    """-> {state: ready|missing|other|damaged|none, release, reason}. `expect` "" = this launcher carries no kit hash
    (a developer build): state "none"."""
    if not expect:
        return {"state": "none", "release": None, "reason": "This launcher has no build kit list (a developer build)."}
    raw, m = read_manifest(root)
    if raw is None:
        return {"state": "missing", "release": None, "reason": "The build kit is not next to the launcher."}
    rel = m.get("release") if isinstance(m, dict) else None
    if sha256_bytes(raw) != expect:
        return {"state": "other", "release": rel,
                "reason": f"The build kit next to the launcher is from another release ({rel or 'unknown'})."}
    for k in KEY_FILES:
        if not os.path.isfile(os.path.join(root, *k.split("/"))):
            return {"state": "damaged", "release": rel, "reason": "Files of the build kit are missing."}
    return {"state": "ready", "release": rel, "reason": "The build kit is ready."}


def verify(root, expect, out=sys.stdout):
    """Hash every kit file -> list of problems (empty = every file is as kit.json lists it)."""
    raw, m = read_manifest(root)
    if raw is None:
        return ["kit.json is missing"]
    if expect and sha256_bytes(raw) != expect:
        return ["kit.json is not the one this launcher expects"]
    bad = check_manifest(m)
    if bad:
        return bad
    for f in m["files"]:
        p = os.path.join(root, *f["path"].split("/"))
        if not os.path.isfile(p):
            bad.append(f"{f['path']}: missing")
        elif os.path.getsize(p) != f["size"] or sha256_file(p) != f["sha256"]:
            bad.append(f"{f['path']}: changed")
    return bad


# ------------------------------------------------------------------------------------------------------ network
def url_ok(url, allow_local, kind="download"):
    """The first URL: GitHub only (https://github.com/... for downloads, https://api.github.com/... for the release
    list); --allow-local also takes file:// and http://127.0.0.1 / localhost (tests)."""
    u = urllib.parse.urlsplit(url)
    if allow_local and (u.scheme == "file" or (u.scheme == "http" and u.hostname in ("127.0.0.1", "localhost"))):
        return True
    if u.scheme != "https" or u.username or u.password or u.port not in (None, 443):
        return False
    if kind == "api":
        return u.hostname == "api.github.com" and u.path.startswith("/repos/")
    return u.hostname == "github.com" and "/releases/download/" in u.path


def served_ok(url, allow_local):
    """Where the download really came from after redirects: github.com or *.githubusercontent.com (GitHub's asset
    hosts), api.github.com for the list."""
    u = urllib.parse.urlsplit(url)
    if allow_local and (u.scheme == "file" or (u.scheme == "http" and u.hostname in ("127.0.0.1", "localhost"))):
        return True
    h = (u.hostname or "").lower()
    return u.scheme == "https" and (h in ("github.com", "api.github.com") or h.endswith(".githubusercontent.com"))


def open_url(url, timeout=60):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "*/*"})
    return urllib.request.urlopen(req, timeout=timeout)    # noqa: S310 (scheme and host checked by url_ok)


class Progress:
    def __init__(self, out, steps, stop_file=None):
        self.out, self.steps, self.stop, self.t0, self.last = out, steps, stop_file, time.time(), -1.0

    def check_stop(self):
        if self.stop and os.path.exists(self.stop):
            raise Cancelled()

    def emit(self, i, name, done, total, lo, hi, force=False):
        pct = lo + (hi - lo) * (done / total if total else 1.0)
        if force or pct - self.last >= 0.5:
            self.last = pct
            print(f"PROGRESS step={i}/{self.steps} name={name} done={done} total={total} "
                  f"elapsed={int(time.time() - self.t0)} pct={pct:.1f}", file=self.out, flush=True)


def download(url, dest, cap, allow_local, prog=None, lo=0.0, hi=0.0, opener=open_url):
    """Stream url to dest (via dest.part) -> (bytes, sha256). Refuses a host that is not GitHub and more than `cap`."""
    if not url_ok(url, allow_local):
        raise Refused("The download address is not this project's GitHub releases page.")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    part, h, n = dest + ".part", hashlib.sha256(), 0
    try:
        with opener(url) as r:
            final = r.geturl() if hasattr(r, "geturl") else url
            if not served_ok(final, allow_local):
                raise Refused("The download was sent from a place that is not GitHub; nothing was kept.")
            total = int(r.headers.get("Content-Length") or 0) if getattr(r, "headers", None) else 0
            if total > cap:
                raise Refused("The download is larger than the build kit can be; nothing was kept.")
            with open(part, "wb") as f:
                for chunk in iter(lambda: r.read(1 << 20), b""):
                    n += len(chunk)
                    if n > cap:
                        raise Refused("The download is larger than the build kit can be; nothing was kept.")
                    h.update(chunk)
                    f.write(chunk)
                    if prog:
                        prog.check_stop()
                        prog.emit(1, "download", n, total or n, lo, hi)
        os.replace(part, dest)
    except (OSError, ValueError) as e:
        raise Refused(f"The download did not finish ({type(e).__name__}). Check the connection and try again.")
    finally:
        if os.path.exists(part):
            os.remove(part)
    return n, h.hexdigest()


# ------------------------------------------------------------------------------------------------------ install
def _zip_top(z):
    names = [i.filename for i in z.infolist() if not i.is_dir()]
    tops = {n.split("/", 1)[0] for n in names}
    if len(tops) != 1 or any("/" not in n for n in names):
        raise Refused("The download is not a build kit zip (it has no single top folder).")
    return tops.pop()


def install(zip_path, root, expect, out=sys.stdout, prog=None, lo=0.0, hi=100.0):
    """Check and install the kit from a full build zip -> number of files installed. Raises Refused / Cancelled;
    on any of them nothing outside <root>/.kit-staging was changed (and that folder is removed)."""
    if not HEX64.fullmatch(expect or ""):
        raise Refused("This launcher carries no build kit list, so it cannot check a download.")
    try:
        z = zipfile.ZipFile(zip_path)
    except (OSError, zipfile.BadZipFile):
        raise Refused("The download is not a readable zip file.")
    stage = os.path.join(root, STAGING)
    with z:
        top = _zip_top(z)
        try:
            raw = z.read(f"{top}/{MANIFEST}")
        except KeyError:
            raise Refused("The download holds no build kit list.")
        if sha256_bytes(raw) != expect:
            raise Refused("The download is not the build kit this launcher was made for; nothing was installed.")
        m = json.loads(raw.decode("utf-8"))
        bad = check_manifest(m)
        if bad:
            raise Refused("The build kit list in the download is not valid; nothing was installed.")
        infos = {i.filename: i for i in z.infolist()}
        files = m["files"]
        total = sum(f["size"] for f in files) or 1
        if os.path.lexists(stage):
            shutil.rmtree(stage)
        os.makedirs(stage)
        done = 0
        try:
            for f in files:
                zi = infos.get(f"{top}/{f['path']}")
                if zi is None or zi.file_size != f["size"]:
                    raise Refused("A file of the build kit is missing from the download or has the wrong size; "
                                  "nothing was installed.")
                dst = os.path.join(stage, *f["path"].split("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                h = hashlib.sha256()
                with z.open(zi) as src, open(dst, "wb") as d:
                    for blk in iter(lambda: src.read(1 << 20), b""):
                        h.update(blk)
                        d.write(blk)
                        done += len(blk)
                        if prog:
                            prog.check_stop()
                            prog.emit(2, "verify", done, total, lo, hi)
                if h.hexdigest() != f["sha256"]:
                    raise Refused("A file of the build kit does not match the list built into the launcher; "
                                  "nothing was installed.")
        except BaseException:
            shutil.rmtree(stage, ignore_errors=True)
            raise
    # every file checked: move into place, drop the files of the old kit this one no longer has, kit.json last
    _, old = read_manifest(root)
    keep = {f["path"] for f in files}
    for f in files:
        src = os.path.join(stage, *f["path"].split("/"))
        dst = os.path.join(root, *f["path"].split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        os.replace(src, dst)
    if isinstance(old, dict) and not check_manifest(old):
        for f in old["files"]:
            if f["path"] not in keep:
                p = os.path.join(root, *f["path"].split("/"))
                if os.path.isfile(p):
                    os.remove(p)
    tmp = os.path.join(root, MANIFEST + ".part")
    with open(tmp, "wb") as fh:
        fh.write(raw)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, os.path.join(root, MANIFEST))
    shutil.rmtree(stage, ignore_errors=True)
    if prog:
        prog.emit(2, "install", 1, 1, lo, hi, force=True)
    return len(files)


def fetch(root, url, expect, out=sys.stdout, stop_file=None, allow_local=False, opener=open_url):
    """Download the full build zip and install its kit -> exit code. Prints PROGRESS / REASON / RESULT."""
    prog = Progress(out, 2, stop_file)
    dl_dir = os.path.join(root, DOWNLOADS)
    name = os.path.basename(urllib.parse.urlsplit(url).path) or "build.zip"
    dest = os.path.join(dl_dir, name)
    step = "download"
    try:
        if not HEX64.fullmatch(expect or ""):
            raise Refused("This launcher carries no build kit list, so it cannot download one.")
        prog.emit(1, "download", 0, 1, 0, 0, force=True)
        n, _ = download(url, dest, MAX_KIT_ZIP, allow_local, prog, 0.0, 70.0, opener)
        print(f"downloaded {n} bytes", file=out, flush=True)
        step = "verify"
        count = install(dest, root, expect, out, prog, 70.0, 100.0)
        print(f"installed {count} kit files", file=out, flush=True)
    except Cancelled:
        print("RESULT ok=0 step=cancelled elapsed=%d" % int(time.time() - prog.t0), file=out, flush=True)
        return 130
    except Refused as e:
        print(f"REASON {e}", file=out, flush=True)
        print(f"RESULT ok=0 step={step} elapsed={int(time.time() - prog.t0)}", file=out, flush=True)
        return 1
    finally:
        shutil.rmtree(dl_dir, ignore_errors=True)
        if stop_file:
            try:
                os.remove(stop_file)
            except OSError:
                pass
    print(f"RESULT ok=1 step=- elapsed={int(time.time() - prog.t0)}", file=out, flush=True)
    return 0


# ------------------------------------------------------------------------------------------------------ symbols
def pe_sections(data):
    """-> {name: bytes} of a PE image's sections (long names through the COFF string table)."""
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", data, 60)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    nsec, = struct.unpack_from("<H", data, pe + 6)
    symptr, nsym = struct.unpack_from("<II", data, pe + 12)
    optsz, = struct.unpack_from("<H", data, pe + 20)
    off = pe + 24 + optsz
    strtab = symptr + nsym * 18 if symptr else 0
    out = {}
    for i in range(nsec):
        raw = data[off + 40 * i: off + 40 * i + 40]
        name = raw[:8].rstrip(b"\0").decode("latin-1")
        if name.startswith("/") and name[1:].isdigit() and strtab:
            s = strtab + int(name[1:])
            e = data.find(b"\0", s)
            if s >= len(data) or e < 0:
                continue                                     # a cut file: the name is outside it
            name = data[s:e].decode("latin-1")
        vsize, _, rsize, rptr = struct.unpack_from("<IIII", raw, 8)
        out[name] = data[rptr:rptr + min(vsize or rsize, rsize)]
    return out


def debuglink(exe):
    """-> (file name, CRC-32) from the exe's .gnu_debuglink, or None."""
    with open(exe, "rb") as f:
        secs = pe_sections(f.read())
    sec = secs.get(".gnu_debuglink") or secs.get(".gnu_deb")   # a long name kept in the string table, or cut at 8
    if not sec or b"\0" not in sec:
        return None
    end = sec.index(b"\0")
    name = sec[:end].decode("utf-8", "replace")
    pos = (end + 4) & ~3
    if pos + 4 > len(sec) or not name or "/" in name or "\\" in name:
        return None
    return name, struct.unpack_from("<I", sec, pos)[0]


def symbols(exe, url, out=sys.stdout, allow_local=False, opener=open_url):
    """Fetch the launcher's debug file next to it, refused unless its CRC-32 is the one the exe names."""
    t0 = time.time()
    try:
        link = debuglink(exe)
        if not link:
            raise Refused("This launcher was built without a separate debug file, so there is nothing to fetch.")
        name, crc = link
        tmp = os.path.join(os.path.dirname(os.path.abspath(exe)), DOWNLOADS)
        z = os.path.join(tmp, "symbols.zip")
        try:
            download(url, z, MAX_SYMBOLS_ZIP, allow_local, Progress(out, 1), 0.0, 90.0, opener)
            with zipfile.ZipFile(z) as zf:
                hit = [i for i in zf.infolist() if i.filename.rsplit("/", 1)[-1] == name]
                if len(hit) != 1:
                    raise Refused("The symbols download does not hold this launcher's debug file.")
                data = zf.read(hit[0])
            if zlib.crc32(data) & 0xFFFFFFFF != crc:
                raise Refused("The symbols download is for another build of the launcher; nothing was kept.")
            dst = os.path.join(os.path.dirname(os.path.abspath(exe)), name)
            with open(dst + ".part", "wb") as f:
                f.write(data)
            os.replace(dst + ".part", dst)
        except zipfile.BadZipFile:
            raise Refused("The symbols download is not a readable zip file.")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    except Refused as e:
        print(f"REASON {e}", file=out, flush=True)
        print(f"RESULT ok=0 step=symbols elapsed={int(time.time() - t0)}", file=out, flush=True)
        return 1
    print(f"RESULT ok=1 step=- elapsed={int(time.time() - t0)}", file=out, flush=True)
    return 0


# ------------------------------------------------------------------------------------------------------ release check
def vkey(v):
    """'0.3.0', a launcher tag 'launcher-v0.3.0' or a tag of the releases before 0.3.0 'v0.2.1' -> (0, 3, 0); a game's
    notes-only tag ('tods-v0.3.0') or anything else -> None."""
    m = re.fullmatch(r"(?:launcher-v|v)?(\d+)\.(\d+)\.(\d+)", (v or "").strip())
    return tuple(int(x) for x in m.groups()) if m else None


def newest(releases, prerelease):
    """The newest non-draft launcher release (pre-releases only when asked) -> (version, html_url) or (None, None).
    Game releases (<id>-vX.Y.Z, notes only) are not launcher releases and are skipped."""
    best = None
    for r in releases if isinstance(releases, list) else []:
        if not isinstance(r, dict) or r.get("draft") or (r.get("prerelease") and not prerelease):
            continue
        k = vkey(r.get("tag_name"))
        if k and (best is None or k > best[0]):
            best = (k, ".".join(map(str, k)), r.get("html_url") or "")
    return (best[1], best[2]) if best else (None, None)


def check_release(api, current, prerelease, out=sys.stdout, allow_local=False, opener=open_url):
    if not url_ok(api, allow_local, "api"):
        print("REASON The release list address is not this project's GitHub page.", file=out)
        return 1
    try:
        with opener(api.rstrip("/") + ("/releases?per_page=20" if api.startswith("https://") else "")) as r:
            if not served_ok(r.geturl() if hasattr(r, "geturl") else api, allow_local):
                raise ValueError("served from elsewhere")
            data = json.loads(r.read(MAX_API_BYTES + 1)[:MAX_API_BYTES].decode("utf-8"))
    except (OSError, ValueError) as e:
        print(f"REASON The release list could not be read ({type(e).__name__}).", file=out)
        return 1
    ver, page = newest(data, prerelease)
    cur = vkey(current)
    st = {"latest": ver, "url": page, "newer": bool(ver and cur and vkey(ver) > cur), "current": current}
    print("STATUS " + json.dumps(st, sort_keys=True), file=out, flush=True)
    return 0


# ------------------------------------------------------------------------------------------------------ CLI
def main(argv=None, out=sys.stdout, opener=open_url):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for n in ("status", "verify", "fetch", "install"):
        p = sub.add_parser(n)
        p.add_argument("--root", required=True)
        p.add_argument("--expect", default="")
        if n == "fetch":
            p.add_argument("--url", required=True)
            p.add_argument("--stop-file")
            p.add_argument("--allow-local", action="store_true")
        if n == "install":
            p.add_argument("--zip", required=True)
    s = sub.add_parser("symbols")
    s.add_argument("--exe", required=True)
    s.add_argument("--url", required=True)
    s.add_argument("--allow-local", action="store_true")
    c = sub.add_parser("check-release")
    c.add_argument("--api", required=True)
    c.add_argument("--current", required=True)
    c.add_argument("--prerelease", action="store_true")
    c.add_argument("--allow-local", action="store_true")
    a = ap.parse_args(argv)
    if a.cmd == "status":
        print("STATUS " + json.dumps(status(os.path.abspath(a.root), a.expect), sort_keys=True), file=out)
        return 0
    if a.cmd == "verify":
        bad = verify(os.path.abspath(a.root), a.expect, out)
        for b in bad[:50]:
            print(f"BAD {b}", file=out)
        print(f"verify: {'ok' if not bad else f'{len(bad)} problem(s)'}", file=out)
        return 1 if bad else 0
    if a.cmd == "fetch":
        return fetch(os.path.abspath(a.root), a.url, a.expect, out, a.stop_file, a.allow_local, opener)
    if a.cmd == "install":
        try:
            n = install(a.zip, os.path.abspath(a.root), a.expect, out, Progress(out, 2))
        except (Refused, Cancelled) as e:
            print(f"REASON {e}", file=out)
            print("RESULT ok=0 step=verify elapsed=0", file=out)
            return 1
        print(f"installed {n} kit files", file=out)
        print("RESULT ok=1 step=- elapsed=0", file=out)
        return 0
    if a.cmd == "symbols":
        return symbols(a.exe, a.url, out, a.allow_local, opener)
    return check_release(a.api, a.current, a.prerelease, out, a.allow_local, opener)


if __name__ == "__main__":
    sys.exit(main())
