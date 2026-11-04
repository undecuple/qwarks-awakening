#!/usr/bin/env python3
"""Game catalogue and detection: the first part of the shared setup engine (launcher-plan.md phase 1).

Python 3.8+, standard library only, runs on Windows and Linux. Reads only clear header fields; decrypts nothing and
ships no keys. The ISO9660 reader follows the idea of the Killzone 3 sibling port's tools/release/iso9660.py (MIT, same
owner) but is written fresh: it prefers the Joliet tree (the primary tree's names are 8.3 and wrong), reads
multi-extent files, and reads files in place.

    catalog.py scan [DIR...] [--library FILE] [--json] [--depth N] [--rpcs3 FOLDER]... [--find-rpcs3]

With --library the file is read for build records (built games) and rewritten atomically with the new scan.

    catalog.py override --library FILE --game ID --slot disc|update|eboot|saves PATH
    catalog.py clear-override --library FILE --game ID --slot SLOT
    catalog.py set-location --library FILE KEY [VALUE] [--add|--remove|--reset]

Locations (launcher-plan.md "Locations"): detection is the default, every location can be set by hand. library.json
keeps per game `overrides` {disc, update, eboot, saves} and a top-level `locations` {library_folders, excluded,
rpcs3_folders, rpcs3_auto, build, saves, logs, art_cache}; absent key = detection / default. An override wins over
anything found, is checked like a found item, and when it is missing or wrong the game is needs_attention with the
reason and the found candidate in `suggested`; a found item never replaces it silently.

    load()                      the catalogue (data/titles/catalog.json)
    read_param_sfo(data)        PARAM.SFO bytes -> {TITLE_ID, APP_VER, VERSION, CATEGORY, TITLE, ...}
    sfo_from_folder / sfo_from_iso / pkg_header
    scan(paths, library=None, rpcs3=None)   -> list of per-entry dicts in library.json shape (overrides and locations
                                with the prior library applied)
    find_rpcs3 / scan_rpcs3     the "Coming from RPCS3" source (read-only; licence files, firmware, caches never touched)
    import_save(src, dest_root) opt-in copy of one RPCS3 save folder (never a move, never an overwrite)
    read_library / write_library
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import stat
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOG_PATH = os.path.join(HERE, "..", "..", "data", "titles", "catalog.json")
SECTOR = 2048
DEFAULT_DEPTH = 4
STATES = ("not_added", "wrong_version", "not_ported", "ready_to_build", "building", "built", "port_updated",
          "needs_attention")
SFO_KEYS = ("TITLE_ID", "APP_VER", "VERSION", "CATEGORY", "TITLE")
OVERRIDE_SLOTS = ("disc", "update", "eboot", "saves")
LOCATION_LISTS = ("library_folders", "excluded", "rpcs3_folders")
LOCATION_PATHS = ("build", "saves", "logs", "art_cache")
LOCATION_KEYS = LOCATION_LISTS + ("rpcs3_auto",) + LOCATION_PATHS
# PARAM.SFO categories a folder may be: disc game (DG), installed game (HG), game update (GD); None = no CATEGORY key.
FOLDER_CATEGORIES = ("DG", "HG", "GD", None)


def load(path=None):
    with open(path or CATALOG_PATH, encoding="utf-8") as f:
        return json.load(f)


def _flat_ids(entry):
    return [i for m in entry["title_ids"].values() for v in m.values() for i in v]


# ------------------------------------------------------------------------------------------------ PARAM.SFO
def read_param_sfo(data):
    """PARAM.SFO bytes -> dict of all entries (str or int). Raises ValueError when it is not a PARAM.SFO."""
    if len(data) < 20 or data[:4] != b"\0PSF":
        raise ValueError("not a PARAM.SFO")
    _ver, kt, dt, n = struct.unpack_from("<IIII", data, 4)
    if 20 + 16 * n > len(data) or kt > len(data) or dt > len(data):
        raise ValueError("PARAM.SFO is truncated")
    out = {}
    for i in range(n):
        ko, fmt, ln, _mx, do = struct.unpack_from("<HHIII", data, 20 + 16 * i)
        ke = data.find(b"\0", kt + ko)
        key = data[kt + ko:ke if ke >= 0 else len(data)].decode("ascii", "replace")
        raw = data[dt + do:dt + do + ln]
        if fmt == 0x0404:
            out[key] = struct.unpack_from("<I", raw.ljust(4, b"\0"))[0]
        else:
            out[key] = raw.split(b"\0", 1)[0].decode("utf-8", "replace")
    return out


def _pick(sfo):
    return {k: sfo.get(k) for k in SFO_KEYS}


def sfo_from_folder(path):
    """A disc folder (PS3_GAME/PARAM.SFO), a PS3_GAME folder or a PSN folder (PARAM.SFO) -> (kind, sfo dict, root)
    or None. kind is 'disc' or 'psn'."""
    for rel, kind in ((("PS3_GAME", "PARAM.SFO"), "disc"), (("PARAM.SFO",), "psn")):
        p = _ci_path(path, rel)
        if p and os.path.isfile(p):
            try:
                with open(p, "rb") as f:
                    return kind, _pick(read_param_sfo(f.read(1 << 20))), p
            except (OSError, ValueError):
                return None
    return None


def _ci_path(base, parts):
    cur = base
    for part in parts:
        try:
            names = os.listdir(cur)
        except OSError:
            return None
        hit = part if part in names else next((n for n in names if n.lower() == part.lower()), None)
        if hit is None:
            return None
        cur = os.path.join(cur, hit)
    return cur


# ------------------------------------------------------------------------------------------------ ISO 9660
class IsoError(Exception):
    pass


def _b32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class Iso:
    """Read-only ISO9660 reader; the Joliet tree is used when it exists."""

    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        try:
            self.f.seek(0, os.SEEK_END)
            self.size = self.f.tell()
            self.encrypted = self._region_table()
            self._open()
        except BaseException:
            self.f.close()
            raise

    def close(self):
        self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def _raw(self, off, n):
        if off < 0 or n < 0 or off + n > self.size:
            raise IsoError("the image is truncated")
        self.f.seek(off)
        b = self.f.read(n)
        if len(b) != n:
            raise IsoError("the image is truncated")
        return b

    def _region_table(self):
        """PS3 raw images start with a table of encrypted regions (ASSUMPTION: big-endian count at 0, then
        start/end sector pairs from offset 8, as seen in the A4O image)."""
        if self.size < SECTOR:
            return False
        h = self._raw(0, SECTOR)
        n = struct.unpack_from(">I", h, 0)[0]
        if not 1 <= n <= 200 or 8 + 8 * n > SECTOR:
            return False
        v = [struct.unpack_from(">I", h, 8 + 4 * i)[0] for i in range(2 * n)]
        return v[0] == 0 and all(a <= b for a, b in zip(v, v[1:])) and v[-1] > 16

    def _open(self):
        pvd = jol = None
        for sec in range(16, 40):
            if (sec + 1) * SECTOR > self.size:
                break
            d = self._raw(sec * SECTOR, SECTOR)
            if d[1:6] != b"CD001" or d[0] == 255:
                break
            if d[0] == 1 and pvd is None:
                pvd = d
            elif d[0] == 2 and d[88:91] in (b"%/@", b"%/C", b"%/E") and jol is None:
                jol = d
        if pvd is None:
            raise IsoError("not an ISO image")
        self.joliet = jol is not None
        vd = jol if jol is not None else pvd
        self.root = (_b32(vd, 158), _b32(vd, 166))

    def _dir(self, lba, size):
        data = self._raw(lba * SECTOR, size)
        out, p, pend = [], 0, None
        while p < size:
            ln = data[p]
            if ln == 0:
                p = (p // SECTOR + 1) * SECTOR
                continue
            if ln < 34 or p + ln > size:
                raise IsoError("a directory record is damaged")
            flags, nl = data[p + 25], data[p + 32]
            raw = data[p + 33:p + 33 + nl]
            elba, esize = _b32(data, p + 2), _b32(data, p + 10)
            p += ln
            if raw in (b"\0", b"\1"):
                continue
            name = raw.decode("utf-16-be", "replace") if self.joliet else raw.decode("latin-1")
            name = re.sub(r";\d+$", "", name)
            if name.endswith(".") and len(name) > 1:
                name = name[:-1]
            if pend and pend[0] == name:
                pend[2].append((elba, esize))
                ext = pend
            else:
                ext = (name, bool(flags & 2), [(elba, esize)])
            if flags & 0x80:                       # more extents of this file follow
                pend = ext
                continue
            pend = None
            out.append(ext)
        return out

    def lookup(self, path):
        """-> (is_dir, [(lba, size) extents]) or None."""
        cur = (True, [self.root])
        for part in [p for p in path.replace("\\", "/").split("/") if p]:
            if not cur[0]:
                return None
            for name, isdir, ext in self._dir(*cur[1][0]):
                if name.lower() == part.lower():
                    cur = (isdir, ext)
                    break
            else:
                return None
        return cur

    def read(self, path, limit=1 << 20):
        e = self.lookup(path)
        if e is None or e[0]:
            return None
        buf = b""
        for lba, size in e[1]:
            buf += self._raw(lba * SECTOR, min(size, limit - len(buf)))
            if len(buf) >= limit:
                break
        return buf


def sfo_from_iso(path):
    """-> sfo dict (and .encrypted flag via the second value) or None when the image has no PARAM.SFO.
    Returns (sfo or None, encrypted)."""
    try:
        with Iso(path) as iso:
            data = iso.read("PS3_GAME/PARAM.SFO")
            enc = iso.encrypted
    except (IsoError, OSError):
        return None, False
    if data is None:
        return None, enc
    try:
        return _pick(read_param_sfo(data)), enc
    except ValueError:
        return None, enc


# ------------------------------------------------------------------------------------------------ PKG
def pkg_header(path):
    """Clear header fields of a PS3 PKG -> {content_id, title_id, content_type, version} or None. No decryption.
    Header: magic 7F 'PKG', content id string at 0x30 (36 bytes, embeds the title ID at 7..16), metadata offset at
    0x08 (u32 BE), count at 0x0C, size at 0x10; metadata entry id 2 holds the content type. The update version is
    not in the clear header: ASSUMPTION it is taken from the 'A0120' token of the usual file name, else None."""
    try:
        with open(path, "rb") as f:
            h = f.read(0x80)
            if len(h) < 0x54 or h[:4] != b"\x7fPKG":
                return None
            cid = h[0x30:0x54].split(b"\0", 1)[0].decode("ascii", "replace")
            ctype = None
            moff, mcount = struct.unpack_from(">II", h, 8)
            if 0 < moff < (1 << 20) and mcount < 64:
                f.seek(moff)
                m = f.read(512)
                p = 0
                for _ in range(mcount):
                    if p + 8 > len(m):
                        break
                    mid, ml = struct.unpack_from(">II", m, p)
                    if mid == 2 and ml >= 4:
                        ctype = struct.unpack_from(">I", m, p + 8)[0]
                    p += 8 + ml
    except OSError:
        return None
    mt = re.search(r"-([A-Z]{4}\d{5})_", cid)
    tid = mt.group(1) if mt else (re.search(r"([A-Z]{4}\d{5})", cid) or [None, None])[1]
    mv = re.search(r"-A(\d\d)(\d\d)-", os.path.basename(path))
    return {"content_id": cid, "title_id": tid, "content_type": ctype,
            "version": f"{mv.group(1)}.{mv.group(2)}" if mv else None}


def _elf_ok(path):
    try:
        with open(path, "rb") as f:
            h = f.read(20)
    except OSError:
        return False
    return len(h) == 20 and h[:4] == b"\x7fELF" and h[4] == 2 and h[5] == 2 and struct.unpack_from(">H", h, 18)[0] == 21


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


# ------------------------------------------------------------------------------------------------ scanning
def _vkey(v):
    try:
        return tuple(int(x) for x in v.split("."))
    except (AttributeError, ValueError):
        return ()


def _is_excluded(path, excluded):
    """True when path is an excluded folder or lies below one (locations.excluded)."""
    p = os.path.normcase(os.path.abspath(path))
    return any(_under(p, os.path.normcase(os.path.abspath(x))) for x in excluded or ())


def _walk(root, depth, excluded=()):
    """Yield (path, is_dir, depth) below root. Symlinks and junctions are followed (players keep game folders behind
    them, e.g. on another drive), but every real folder is entered once, so a link loop ends; depth still bounds it."""
    stack = [(root, 0)]
    seen = {os.path.realpath(root)}
    while stack:
        d, lvl = stack.pop()
        try:
            it = sorted(os.scandir(d), key=lambda e: e.name)
        except OSError:
            continue
        for e in it:
            try:
                isdir = e.is_dir()          # follows links; a dangling link is neither dir nor file
                if not isdir and not e.is_file():
                    continue
            except OSError:
                continue
            if isdir:
                if _is_excluded(e.path, excluded):
                    continue
                real = os.path.realpath(e.path)
                if real in seen:
                    continue
                seen.add(real)
            yield e.path, isdir, lvl
            if isdir and lvl + 1 < depth:
                stack.append((e.path, lvl + 1))


def _item_from_folder(path, source=None):
    """A folder holding a PARAM.SFO -> discover() item (kind disc / psn / update_folder ...) or None."""
    r = sfo_from_folder(path)
    if r and r[1].get("CATEGORY") not in FOLDER_CATEGORIES:
        r = None                        # e.g. DP: a disc's PKGDIR install package, not a game or an update
    if not r:
        return None
    kind, sfo, sfo_path = r
    if kind == "psn" and os.path.basename(path.rstrip("/\\")).upper() == "PS3_GAME":
        kind = "disc"                   # the player picked the PS3_GAME folder itself
    it = {"kind": kind, "path": path, "title_id": sfo.get("TITLE_ID"), "version": sfo.get("APP_VER"),
          "sfo": sfo, "category": sfo.get("CATEGORY")}
    base = os.path.dirname(sfo_path)
    for key, nm in (("icon", "ICON0.PNG"), ("background", "PIC1.PNG")):
        p = _ci_path(base, (nm,))
        it[key] = p if p and os.path.isfile(p) else None
    if source:
        it["source"] = source
    return it


def discover(paths, depth=DEFAULT_DEPTH, excluded=()):
    """Recognise items below the given folders. Item: {kind, path, title_id, version, ...}.
    kinds: disc, iso, psn, update_pkg, game_pkg, update_folder, eboot."""
    items, consumed, seen = [], set(), set()
    for root in paths:
        root = os.path.abspath(root)
        if not os.path.isdir(root) or _is_excluded(root, excluded):
            continue
        cand = [(root, True, -1)] + list(_walk(root, depth, excluded))
        for path, isdir, lvl in cand:
            if path in seen:
                continue
            seen.add(path)
            if isdir:
                if any(path.startswith(c + os.sep) for c in consumed):
                    continue                    # inside a disc already found (PS3_GAME, its PKGDIR, ...)
                it = _item_from_folder(path)
                if it:
                    if it["kind"] == "disc":
                        consumed.add(path)
                    items.append(it)
                continue
            low = path.lower()
            if low.endswith(".iso"):
                sfo, enc = sfo_from_iso(path)
                if sfo:
                    items.append({"kind": "iso", "path": path, "title_id": sfo.get("TITLE_ID"),
                                  "version": sfo.get("APP_VER"), "sfo": sfo, "encrypted": enc,
                                  "icon": None, "background": None})
            elif low.endswith(".pkg"):
                h = pkg_header(path)
                if h and h["title_id"]:
                    kind = "game_pkg" if h["content_type"] in (5, 0x15) else "update_pkg"
                    items.append({"kind": kind, "path": path, "title_id": h["title_id"], "version": h["version"],
                                  "content_type": h["content_type"]})
            elif low.endswith(".elf") and _elf_ok(path):
                items.append({"kind": "eboot", "path": path, "sha256": _sha256(path)})
    return items


def _is_base(it):
    return it["kind"] in ("disc", "iso") or (it["kind"] == "psn" and not (it["title_id"] or "").startswith("BC"))


def _under(path, folder):
    return path == folder or path.startswith(folder.rstrip(os.sep) + os.sep)


def _eboot_for(entry, base, ebs, base_items):
    """ASSUMPTION: an EBOOT.elf belongs to a game when its hash is in the catalogue, or its path holds a folder named
    like the game (id, build title), or it lies inside the game's folder, or next to it when that folder holds only
    one game."""
    names = {entry["id"].lower(), entry.get("build_title", entry["id"]).lower()} | {i.lower() for i in _flat_ids(entry)}
    for e in ebs:
        if e.get("forced"):             # the player's override, already checked
            return e
    for e in ebs:
        if e["sha256"] in entry.get("eboot_sha256", []):
            return e
    for e in ebs:
        if names & {p.lower() for p in e["path"].split(os.sep)[:-1]}:
            return e
    if base:
        for e in ebs:
            if base["kind"] != "iso" and _under(e["path"], base["path"]):
                return e
        for e in ebs:
            d = os.path.dirname(e["path"])
            if os.path.dirname(base["path"]) == d and sum(1 for b in base_items if os.path.dirname(b["path"]) == d) == 1:
                return e
    return None


def _empty(entry, state, reason):
    e = {k: entry[k] for k in ("id", "title", "year", "order", "readme_state", "supported")}
    e.update(state=state, reason=reason, title_id=None, version=None, needs_update=(entry.get("update") or {}).get("version"),
             files={"disc": None, "update": None, "eboot": None}, art={"icon": None, "background": None}, build=None,
             estimate=entry.get("estimate") and {"minutes": entry["estimate"]["minutes"],
                                                 "disk_gb": entry["estimate"]["disk_gb"]},
             igdb=entry.get("igdb") or {"game": None, "cover": None, "artwork": None}, children=[])
    return e


def _resolve_found(entry, items, prior, port_version, saves=()):
    ids = set(_flat_ids(entry))
    base_items = [i for i in items if _is_base(i)]
    mine = [i for i in base_items if i.get("title_id") in ids]
    base = mine[0] if mine else None
    r = _empty(entry, "not_added", "Game files not found")
    pb = (prior or {}).get("build")
    if base is None and not pb:
        gp = [i for i in items if i["kind"] == "game_pkg" and i.get("title_id") in ids]
        if gp:
            r.update(state="needs_attention", reason="Game package is encrypted; give the installed game folder",
                     files=dict(r["files"], disc=gp[0]["path"]), title_id=gp[0]["title_id"])
        return r
    if base is not None:
        r.update(title_id=base["title_id"], version=base["version"])
        r["files"]["disc"] = base["path"]
        r["art"] = {"icon": base.get("icon"), "background": base.get("background")}
    elif prior:
        r["files"] = dict(prior.get("files") or r["files"])
        r["art"] = dict(prior.get("art") or r["art"])
        r.update(title_id=prior.get("title_id"), version=prior.get("version"))
    upd_item = None
    for i in items:
        if i.get("title_id") not in ids:
            continue
        if i["kind"] in ("update_pkg", "update_folder") or (i["kind"] == "psn" and i["title_id"].startswith("BC")):
            # the highest version wins; at equal versions an RPCS3-installed one (real APP_VER) beats a PKG file name
            if upd_item is None or (_vkey(i["version"]), i.get("source") == "rpcs3") > \
                    (_vkey(upd_item["version"]), upd_item.get("source") == "rpcs3"):
                upd_item = i
    if upd_item:
        r["files"]["update"] = upd_item["path"]
    elif prior and (prior.get("files") or {}).get("update"):
        r["files"]["update"] = prior["files"]["update"]
    src = {}
    if base is not None:
        src["disc"] = base.get("source") or ("iso" if base["kind"] == "iso" else "folder")
    if upd_item:
        src["update"] = upd_item.get("source") or "folder"
    eb = _eboot_for(entry, base, [i for i in items if i["kind"] == "eboot"], base_items)
    if eb:
        r["files"]["eboot"] = eb["path"]
        src["eboot"] = eb.get("source") or "folder"
    elif prior and (prior.get("files") or {}).get("eboot") and os.path.isfile(prior["files"]["eboot"]):
        r["files"]["eboot"] = prior["files"]["eboot"]
    if src:
        r["sources"] = src
    mine_saves = [{"dir": v["dir"], "bytes": v["bytes"]} for v in saves
                  if any(v["dir"].upper().startswith(x.upper()) for x in ids)]
    if mine_saves:
        r["rpcs3_saves"] = mine_saves
    if prior and prior.get("state") == "building":
        r.update(state="building", reason="Build in progress")
        return r
    if pb:
        exe = pb.get("exe")
        if exe and os.path.isfile(exe):
            r["build"] = pb
            if pb.get("port_version") != port_version:
                r.update(state="port_updated", reason="Port updated: rebuild")
            else:
                r.update(state="built", reason="Built")
            return r
        r.update(state="needs_attention", reason="The built game program is missing; rebuild or locate it")
        return r
    if base.get("encrypted"):
        r.update(state="needs_attention", reason="Disc image is encrypted; use a decrypted dump")
        return r
    need = (entry.get("update") or {}).get("version")
    if need:
        have = [base["version"]] + [i["version"] for i in items
                                    if i.get("title_id") in ids and i["kind"] in ("update_pkg", "update_folder", "psn", "disc", "iso")]
        best = max((v for v in have if v), key=_vkey, default=None)
        if _vkey(best) < _vkey(need):
            r.update(state="wrong_version", reason=f"Needs update {need}" + (f" (found {best})" if best else ""))
            return r
    if not entry["supported"]:
        r.update(state="not_ported", reason="Not ported yet")
        return r
    if not r["files"]["eboot"]:
        r.update(state="needs_attention", reason="EBOOT.elf missing")
        return r
    r.update(state="ready_to_build", reason="Ready to build")
    return r


def _clean_overrides(prior):
    ov = (prior or {}).get("overrides")
    if not isinstance(ov, dict):
        return {}
    return {k: v for k, v in ov.items() if k in OVERRIDE_SLOTS and isinstance(v, str) and v}


def _check_override(entry, slot, path):
    """The player's path for one slot, checked like a found item -> (item or None, problem or None). Forbidden paths
    (licence files, firmware ...) are refused by name before any filesystem call."""
    ids = set(_flat_ids(entry))
    noun = {"disc": "disc", "update": "update", "eboot": "EBOOT.elf", "saves": "save folder"}[slot]
    if _forbidden(path):
        return None, f"The {noun} you chose is in a place that is never read (licence or firmware files)"
    p = os.path.abspath(path)
    low = p.lower()
    if slot == "disc":
        if low.endswith(".iso"):
            if not os.path.isfile(p):
                return None, "The disc image you chose is missing"
            sfo, enc = sfo_from_iso(p)
            if not sfo:
                return None, "The disc image you chose cannot be read as a PS3 disc"
            it = {"kind": "iso", "path": p, "title_id": sfo.get("TITLE_ID"), "version": sfo.get("APP_VER"), "sfo": sfo,
                  "encrypted": enc, "icon": None, "background": None, "source": "override"}
        else:
            if not os.path.isdir(p):
                return None, "The disc folder you chose is missing" if not os.path.exists(p) else \
                    "The disc you chose is a file; choose the disc folder or an ISO"
            it = _item_from_folder(p, "override")
            if not it:
                return None, "The disc folder you chose holds no PS3 game (no PARAM.SFO)"
        if not _is_base(it):
            return None, "The disc you chose is an update, not a game"
        if it["title_id"] not in ids:
            return None, f"The disc you chose is a different game ({it['title_id']})"
        return it, None
    if slot == "update":
        if low.endswith(".pkg"):
            if not os.path.isfile(p):
                return None, "The update you chose is missing"
            h = pkg_header(p)
            if not h or not h["title_id"]:
                return None, "The update you chose is not a readable package"
            if h["content_type"] in (5, 0x15):
                return None, "The update you chose is a game package, not an update"
            it = {"kind": "update_pkg", "path": p, "title_id": h["title_id"], "version": h["version"],
                  "content_type": h["content_type"], "source": "override"}
        else:
            if not os.path.isdir(p):
                return None, "The update you chose is missing"
            it = _item_from_folder(p, "override")
            if not it or it["kind"] != "psn" or not (it["title_id"] or "").startswith("BC"):
                return None, "The update folder you chose holds no installed update"
        if it["title_id"] not in ids:
            return None, f"The update you chose is a different game ({it['title_id']})"
        need = (entry.get("update") or {}).get("version")
        if need and _vkey(it["version"]) < _vkey(need):
            have = it["version"]
            return None, (f"The update you chose is {have}; {need} is needed" if have else
                          f"The update you chose has no readable version; {need} is needed")
        return it, None
    if slot == "eboot":
        if os.path.isdir(p):
            return None, "The EBOOT.elf you chose is a folder"
        if not os.path.isfile(p):
            return None, "The EBOOT.elf you chose is missing"
        if not _elf_ok(p):
            return None, "The file you chose is not a PS3 EBOOT.elf"
        return {"kind": "eboot", "path": p, "sha256": _sha256(p), "source": "override", "forced": True}, None
    if not os.path.isdir(p):                                    # saves
        return None, "The save folder you chose is missing"
    return {"path": p}, None


def _resolve(entry, items, prior, port_version, saves=()):
    """_resolve_found with the player's overrides on top. An override replaces what was found for its slot; a bad
    one makes the game needs_attention (reason + `suggested` = the found candidate), it is never swapped silently."""
    ov = _clean_overrides(prior)
    if not ov:
        return _resolve_found(entry, items, prior, port_version, saves)
    ids = set(_flat_ids(entry))
    good, bad = {}, {}
    for slot in OVERRIDE_SLOTS:
        if slot in ov:
            it, why = _check_override(entry, slot, ov[slot])
            if why:
                bad[slot] = why
            else:
                good[slot] = it

    def replaced(i):
        if i["kind"] == "eboot":
            return "eboot" in ov
        if i.get("title_id") not in ids:
            return False
        if _is_base(i):
            return "disc" in ov
        return "update" in ov and (i["kind"] in ("update_pkg", "update_folder") or i["kind"] == "psn")

    detected = _resolve_found(entry, items, prior, port_version, saves) if bad else None
    items2 = [i for i in items if not replaced(i)] + [good[k] for k in ("disc", "update", "eboot") if k in good]
    r = _resolve_found(entry, items2, prior, port_version, saves)
    r["overrides"] = dict(ov)
    src = r.setdefault("sources", {})
    sug = {}
    for slot in ov:
        src[slot] = "override"
    if "saves" in good:
        r["files"]["saves"] = good["saves"]["path"]
    for slot, why in bad.items():
        r["files"][slot] = ov[slot]
        cand = detected["files"].get(slot)
        if cand and os.path.normcase(os.path.abspath(cand)) != os.path.normcase(os.path.abspath(ov[slot])):
            sug[slot] = cand
    if bad and r["state"] != "building":
        r.update(state="needs_attention", reason=bad[next(k for k in OVERRIDE_SLOTS if k in bad)])
    if sug:
        r["suggested"] = sug
    return r


def rpcs3_folders_for(library=None, extra=(), find=False):
    """The RPCS3 folders to read: `extra` (named by the caller) and locations.rpcs3_folders (named by the player), always;
    with `find` and unless locations.rpcs3_auto is false, also what find_rpcs3 finds in the usual places (rpcs3_auto is
    the plan's "look in the usual places" switch, not an off switch for the folders the player chose)."""
    loc = (library or {}).get("locations") or {}
    out = [os.path.abspath(x) for x in extra]
    out += [os.path.abspath(x) for x in loc.get("rpcs3_folders") or []]
    if find and loc.get("rpcs3_auto") is not False:
        out += [f["path"] for f in find_rpcs3(list(out))]
    seen, res = set(), []
    for f in out:
        k = os.path.normcase(os.path.normpath(f))
        if k not in seen:
            seen.add(k)
            res.append(f)
    return res


def scan(paths, library=None, catalog=None, depth=DEFAULT_DEPTH, rpcs3=None, find=False):
    """Scan folders -> list of per-entry dicts (library.json 'games' shape), one per catalogue entry in order.
    `library` is a prior library.json dict (build records of built games are kept). `rpcs3` is a list of RPCS3
    folders (see find_rpcs3); their games, installed updates, EBOOT.elf files and saves feed the same state logic.
    The library's `locations` add library_folders and rpcs3_folders, skip `excluded` subtrees, and turn the RPCS3
    folders are always read; `find` also looks in the usual RPCS3 places unless rpcs3_auto is false (rpcs3_folders_for)."""
    cat = catalog or load()
    pv = cat.get("port_version", "0.1.0")
    loc = (library or {}).get("locations") or {}
    items = discover(list(paths) + [x for x in loc.get("library_folders") or [] if x not in paths], depth,
                     loc.get("excluded") or ())
    saves = []
    for rf in rpcs3_folders_for(library, rpcs3 or (), find):
        r3 = scan_rpcs3(rf, cat)
        items += r3["items"]
        saves += r3["saves"]
    prior = {g["id"]: g for g in (library or {}).get("games", [])}
    out = []
    for entry in cat["games"]:
        if entry.get("children"):
            kids = []
            for c in entry["children"]:
                pc = {g["id"]: g for g in (prior.get(entry["id"]) or {}).get("children", [])}.get(c["id"])
                kids.append(_resolve(c, items, pc, pv, saves))
            r = _empty(entry, "not_added", "Game files not found")
            found = [k for k in kids if k["state"] != "not_added"]
            if found:
                f0 = found[0]
                r.update(state="not_ported", reason="Not ported yet", title_id=f0["title_id"], version=f0["version"],
                         files=dict(f0["files"]), art=dict(f0["art"]))
                if all(k["state"] in ("built", "port_updated") for k in kids):
                    r.update(state="built", reason="Built")
            r["children"] = kids
            pov = _clean_overrides(prior.get(entry["id"]))
            if pov:
                r["overrides"] = pov
            out.append(r)
        else:
            out.append(_resolve(entry, items, prior.get(entry["id"]), pv, saves))
    return out


# ------------------------------------------------------------------------------------------------ RPCS3 source
# Read-only. Layout as RPCS3 keeps it (checked 2026-11-03 against its sources: games_config.cpp, vfs_config.{h,cpp},
# System.cpp; [src:RPCS3]): config/games.yml (title ID: disc folder; older versions: games.yml at the root),
# config/vfs.yml (may move dev_hdd0), dev_hdd0/game/<TITLE_ID>/PARAM.SFO (+ USRDIR) for installed games and updates,
# dev_hdd0/home/<user>/savedata/<save dir>. Licence files, firmware and caches are skipped BY PATH before any
# filesystem call: every listing, stat and open in this section goes through _forbidden().
_NEVER_DIRS = {"exdata", "dev_flash", "dev_flash2", "dev_flash3"}                       # anywhere in a path
_NEVER_IN_RPCS3 = {"cache", "caches", "logs", "log", "shaders", "license", "licenses"}  # below an RPCS3 folder
_NEVER_EXT = (".rap", ".rif", ".edat")
_NEVER_FILES = {"act.dat"}


def _norm(p):
    return os.path.normpath(p).replace("\\", "/")


def _forbidden(path, base=None):
    """True for a licence file, firmware, cache, log or shader path. Pure string test: no filesystem call.
    With `base` (an RPCS3 folder) the cache/log/shader names count only below it."""
    p = _norm(path)
    parts = [x.lower() for x in p.split("/") if x]
    if parts and (parts[-1] in _NEVER_FILES or parts[-1].endswith(_NEVER_EXT)):
        return True
    if _NEVER_DIRS & set(parts):
        return True
    if base:
        b = _norm(base).rstrip("/")
        if p == b or p.startswith(b + "/"):
            rel = [x.lower() for x in p[len(b):].split("/") if x]
            if _NEVER_IN_RPCS3 & set(rel):
                return True
    return False


def _ls(path, base=None):
    """Sorted [(name, full path)] of a folder; a forbidden folder is not listed and forbidden children are dropped by
    name (so nothing forbidden is ever stat'ed through a DirEntry)."""
    if _forbidden(path, base):
        return []
    try:
        with os.scandir(path) as it:
            names = sorted(e.name for e in it)
    except OSError:
        return []
    return [(n, os.path.join(path, n)) for n in names if not _forbidden(os.path.join(path, n), base)]


def _isdir(path, base=None):
    return not _forbidden(path, base) and os.path.isdir(path)


def _isfile(path, base=None):
    return not _forbidden(path, base) and os.path.isfile(path)


def _rpcs3_layout(folder):
    """-> (games_yml or None, dev_hdd0 or None) by fixed names, no listing."""
    games = None
    for rel in (("config", "games.yml"), ("games.yml",)):
        p = os.path.join(folder, *rel)
        if _isfile(p, folder):
            games = p
            break
    d = _vfs_dev_hdd0(folder) or os.path.join(folder, "dev_hdd0")
    return games, (d if _isdir(d, folder) else None)


def _vfs_dev_hdd0(folder):
    """dev_hdd0 moved by the player in RPCS3's vfs.yml (config/vfs.yml, or vfs.yml at the root in older versions):
    a line '/dev_hdd0/: <path>' where <path> may start with $(EmulatorDir) (= the RPCS3 folder). Checked against
    RPCS3's sources (rpcs3/Emu/vfs_config.{h,cpp}: default "$(EmulatorDir)dev_hdd0/", file under the config dir)
    2026-11-03. None = not moved (or unreadable): the default folder is used."""
    for rel in (("config", "vfs.yml"), ("vfs.yml",)):
        p = os.path.join(folder, *rel)
        if not _isfile(p, folder):
            continue
        try:
            with open(p, encoding="utf-8", errors="replace") as f:
                text = f.read(1 << 16)
        except OSError:
            return None
        for line in text.splitlines():
            m = re.match(r"""\s*['"]?/dev_hdd0/['"]?\s*:\s*(.+?)\s*$""", line)
            if not m:
                continue
            v = m.group(1).strip().strip("'\"")
            if not v:
                return None
            v = v.replace("$(EmulatorDir)", folder.rstrip("/\\") + os.sep)
            return os.path.normpath(v)
        return None
    return None


def _looks_like_rpcs3(folder):
    games, hdd = _rpcs3_layout(folder)
    exe = os.path.isfile(os.path.join(folder, "rpcs3.exe"))
    cfg = games is not None or os.path.isfile(os.path.join(folder, "config", "config.yml"))
    return (exe and (cfg or hdd is not None)) or (cfg and hdd is not None)


def find_rpcs3(extra_paths=(), env=None, launcher_dir=None):
    """RPCS3 folders -> [{path, games_yml, dev_hdd0}]. Candidates: extra_paths, the launcher's folder, Downloads,
    Desktop, Documents, %LOCALAPPDATA%, C:\\RPCS3, D:\\RPCS3 (Windows), $HOME/.config/rpcs3; each is checked itself and its
    direct sub-folders only (depth 1). `env` (a dict) replaces os.environ so a test never looks at the real machine."""
    env = os.environ if env is None else env
    home = env.get("USERPROFILE") or env.get("HOME") or ""
    cands = [os.path.abspath(p) for p in extra_paths]
    ld = launcher_dir if launcher_dir is not None else os.path.dirname(os.path.abspath(sys.argv[0] or "."))
    if ld:
        cands.append(ld)
    if home:
        cands += [os.path.join(home, x) for x in ("Downloads", "Desktop", "Documents")]
        cands.append(os.path.join(home, ".config", "rpcs3"))
    if env.get("LOCALAPPDATA"):
        cands.append(env["LOCALAPPDATA"])
    if os.name == "nt":
        cands += ["C:\\RPCS3", "D:\\RPCS3"]
    found, seen = [], set()
    for c in cands:
        if _forbidden(c) or not os.path.isdir(c):
            continue
        for f in [c] + [p for _n, p in _ls(c) if os.path.isdir(p)]:
            real = os.path.realpath(f)
            if real in seen:
                continue
            seen.add(real)
            if _looks_like_rpcs3(f):
                games, hdd = _rpcs3_layout(f)
                found.append({"path": f, "games_yml": games, "dev_hdd0": hdd})
    return found


def parse_games_yml(text):
    """games.yml lines 'TITLEID: path' (path quoted or not) -> [(title_id, path)]. No YAML library: comments, blank
    lines and '---' are ignored; double quotes take backslash escapes, single quotes double their quote."""
    out = []
    for line in text.splitlines():
        line = line.strip().lstrip("\ufeff")
        m = re.match(r"^['\"]?([A-Za-z0-9]{9})['\"]?\s*:\s*(.*)$", line)
        if not m:
            continue
        v = m.group(2).strip()
        if v[:1] == '"':
            body, esc = "", False
            for ch in v[1:]:
                if esc:
                    body += {"n": "\n", "t": "\t"}.get(ch, ch)
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    break
                else:
                    body += ch
            v = body
        elif v[:1] == "'":
            body, k = "", 1
            while k < len(v):
                if v[k] == "'":
                    if v[k + 1:k + 2] == "'":
                        body += "'"
                        k += 2
                        continue
                    break
                body += v[k]
                k += 1
            v = body
        else:
            v = re.split(r"\s+#", v, 1)[0].strip()
        if v:
            out.append((m.group(1).upper(), v))
    return out


def _tree_bytes(path, base):
    """Size of a save folder from directory entries (no file is opened; links are not followed)."""
    total, stack = 0, [path]
    while stack:
        d = stack.pop()
        for _n, p in _ls(d, base):
            try:
                st = os.lstat(p)
            except OSError:
                continue
            if stat.S_ISDIR(st.st_mode):
                stack.append(p)
            elif stat.S_ISREG(st.st_mode):
                total += st.st_size
    return total


def _eboot_beside(parent, base):
    """USRDIR/EBOOT.elf (beside USRDIR/EBOOT.BIN) -> eboot item or None."""
    for n, p in _ls(os.path.join(parent, "USRDIR"), base):
        if n.lower() == "eboot.elf" and _isfile(p, base) and _elf_ok(p):
            return {"kind": "eboot", "path": p, "sha256": _sha256(p), "source": "rpcs3"}
    return None


def scan_rpcs3(folder, catalog=None):
    """Read one RPCS3 folder -> {folder, items, disc_folders, saves}. Only catalogue title IDs are looked at: a
    games.yml disc or a dev_hdd0/game/<ID> folder with another ID is never opened. items use the discover() shapes
    with source='rpcs3'; GD (installed update) becomes kind 'update_folder' with its real APP_VER, HG kind 'psn'.
    saves: [{user, dir, bytes, path}] for dev_hdd0/home/<user>/savedata/<dir> starting with a catalogue title ID
    (names and sizes only). disc_folders: every (title_id, path) games.yml names."""
    cat = catalog or load()
    ids = set()
    for e in cat["games"]:
        ids.update(i.upper() for i in _flat_ids(e))
        for c in e.get("children", []):
            ids.update(i.upper() for i in _flat_ids(c))
    folder = os.path.abspath(folder)
    out = {"folder": folder, "items": [], "disc_folders": [], "saves": []}
    games_yml, hdd = _rpcs3_layout(folder)
    seen_eb = set()

    def add_eboot(parent):
        eb = _eboot_beside(parent, folder)
        if eb and eb["path"] not in seen_eb:
            seen_eb.add(eb["path"])
            out["items"].append(eb)

    if games_yml:
        try:
            with open(games_yml, encoding="utf-8", errors="replace") as f:
                entries = parse_games_yml(f.read())
        except OSError:
            entries = []
        out["disc_folders"] = entries
        for tid, p in entries:
            if tid not in ids or _forbidden(p):
                continue
            if p.lower().endswith(".iso") and os.path.isfile(p):
                sfo, enc = sfo_from_iso(p)
                if sfo:
                    out["items"].append({"kind": "iso", "path": p, "title_id": sfo.get("TITLE_ID"),
                                         "version": sfo.get("APP_VER"), "sfo": sfo, "encrypted": enc,
                                         "icon": None, "background": None, "source": "rpcs3"})
                continue
            if not os.path.isdir(p):
                continue
            r = sfo_from_folder(p)
            if not r:
                continue
            kind, sfo, sfo_path = r
            if os.path.basename(p.rstrip("/\\")).upper() == "PS3_GAME":
                kind = "disc"
            it = {"kind": kind, "path": p, "title_id": sfo.get("TITLE_ID"), "version": sfo.get("APP_VER"), "sfo": sfo,
                  "category": sfo.get("CATEGORY"), "source": "rpcs3"}
            sfo_dir = os.path.dirname(sfo_path)
            for key, nm in (("icon", "ICON0.PNG"), ("background", "PIC1.PNG")):
                q = _ci_path(sfo_dir, (nm,))
                it[key] = q if q and _isfile(q) else None
            out["items"].append(it)
            add_eboot(sfo_dir if kind == "disc" else p)
    if hdd:
        for name, p in _ls(os.path.join(hdd, "game"), folder):
            if name.upper() not in ids or not _isdir(p, folder):
                continue
            sp = os.path.join(p, "PARAM.SFO")
            if not _isfile(sp, folder):
                continue
            try:
                with open(sp, "rb") as f:
                    sfo = _pick(read_param_sfo(f.read(1 << 20)))
            except (OSError, ValueError):
                continue
            cat_ = sfo.get("CATEGORY")
            if cat_ not in ("HG", "GD"):
                continue
            out["items"].append({"kind": "psn" if cat_ == "HG" else "update_folder", "path": p,
                                 "title_id": sfo.get("TITLE_ID") or name, "version": sfo.get("APP_VER"), "sfo": sfo,
                                 "category": cat_, "source": "rpcs3", "icon": None, "background": None})
            add_eboot(p)
        for user, up in _ls(os.path.join(hdd, "home"), folder):
            # fixed name: the user folder itself is never listed (it holds exdata)
            for name, p in _ls(os.path.join(up, "savedata"), folder):
                if name.upper().startswith(tuple(ids)) and _isdir(p, folder):
                    out["saves"].append({"user": user, "dir": name, "bytes": _tree_bytes(p, folder), "path": p})
    return out


def import_save(src_dir, dest_root):
    """Copy one save folder to dest_root/<its name>. Never moves, never overwrites, skips links and licence paths.
    Returns {ok, reason, dest}. scan never calls it; the UI calls it on the player's opt-in."""
    name = os.path.basename(os.path.normpath(src_dir))
    dest = os.path.join(dest_root, name)
    if not name or name in (".", "..") or _forbidden(src_dir):
        return {"ok": False, "reason": "That folder cannot be imported", "dest": None}
    if not os.path.isdir(src_dir):
        return {"ok": False, "reason": "The save folder is missing", "dest": dest}
    if os.path.lexists(dest):
        return {"ok": False, "reason": "A save with this name already exists; nothing was changed", "dest": dest}
    tmp = os.path.join(dest_root, f".{name}.{os.getpid()}.importing")
    try:
        os.makedirs(dest_root, exist_ok=True)

        def ignore(d, names):
            return [n for n in names if _forbidden(os.path.join(d, n)) or os.path.islink(os.path.join(d, n))]
        shutil.copytree(src_dir, tmp, ignore=ignore)
        os.rename(tmp, dest)
    except (OSError, shutil.Error) as ex:
        shutil.rmtree(tmp, ignore_errors=True)
        return {"ok": False, "reason": f"Copy failed: {ex.__class__.__name__}", "dest": dest}
    return {"ok": True, "reason": None, "dest": dest}


# ------------------------------------------------------------------------------------------------ library.json
def read_library(path):
    try:
        with open(path, encoding="utf-8") as f:
            d = json.load(f)
        return d if d.get("schema") == 1 else None
    except (OSError, ValueError):
        return None


def make_library(games, catalog=None, prior=None):
    """`prior` (a library.json dict) lends its `locations` block, so a rescan never drops a location setting; the
    games from scan() already carry their `overrides`."""
    cat = catalog or load()
    lib = {"schema": 1, "port_version": cat.get("port_version", "0.1.0"),
           "generated": datetime.datetime.now().astimezone().isoformat(timespec="seconds"), "games": games}
    if isinstance((prior or {}).get("locations"), dict) and prior["locations"]:
        lib["locations"] = dict(prior["locations"])
    return lib


def write_library(path, lib):
    """Atomic: write a temp file in the same folder, fsync, then replace."""
    d = os.path.dirname(os.path.abspath(path))
    os.makedirs(d, exist_ok=True)
    tmp = os.path.join(d, f".{os.path.basename(path)}.{os.getpid()}.tmp")
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(lib, f, indent=1, ensure_ascii=False)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)


# ------------------------------------------------------------------------------------------------ CLI
def _find_game(lib, gid):
    for g in lib["games"]:
        if g["id"] == gid:
            return g
        for c in g.get("children", []):
            if c["id"] == gid:
                return c
    return None


def _print_game(g):
    print(f"{g['id']:<11} {g['state']:<15} {g['reason']}")
    for slot in OVERRIDE_SLOTS:
        path = (g.get("files") or {}).get(slot)
        if path:
            print(f"  {slot:<9} {path} ({(g.get('sources') or {}).get(slot, 'found')})")
    for slot, path in (g.get("suggested") or {}).items():
        print(f"  suggested {slot}: {path}")


def _edit_library(path, edit):
    """Read (or start) library.json, let `edit(lib, cat)` change overrides/locations (it returns an error text or
    None), rescan with the result, write atomically -> (exit code, lib)."""
    cat = load()
    lib = read_library(path)
    if lib is None:
        lib = make_library(scan([], None, cat), cat)
    err = edit(lib, cat)
    if err:
        print(err, file=sys.stderr)
        return 2, lib
    find = (lib.get("locations") or {}).get("rpcs3_auto") is not False
    new = make_library(scan([], lib, cat, find=find), cat, lib)
    write_library(path, new)
    return 0, new


def _parse_bool(v):
    return {"true": True, "yes": True, "1": True, "on": True, "false": False, "no": False, "0": False,
            "off": False}.get(str(v).lower())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan")
    s.add_argument("dirs", nargs="*")
    s.add_argument("--rpcs3", action="append", default=[], metavar="FOLDER", help="an RPCS3 folder (repeatable)")
    s.add_argument("--find-rpcs3", action="store_true", help="look for RPCS3 folders in the usual places")
    s.add_argument("--library")
    s.add_argument("--json", action="store_true")
    s.add_argument("--depth", type=int, default=DEFAULT_DEPTH)
    o = sub.add_parser("override", help="use this path for one slot of one game instead of what a scan finds")
    c = sub.add_parser("clear-override", help="drop an override: the slot is detected again")
    for sp in (o, c):
        sp.add_argument("--library", required=True)
        sp.add_argument("--game", required=True)
        sp.add_argument("--slot", required=True, choices=OVERRIDE_SLOTS)
    o.add_argument("path")
    sl = sub.add_parser("set-location", help="set, add/remove (list keys) or reset a global location")
    sl.add_argument("--library", required=True)
    sl.add_argument("key", choices=LOCATION_KEYS)
    sl.add_argument("value", nargs="?")
    sl.add_argument("--add", action="store_true")
    sl.add_argument("--remove", action="store_true")
    sl.add_argument("--reset", action="store_true", help="back to the default (drops the key)")
    a = ap.parse_args(argv)

    if a.cmd in ("override", "clear-override"):
        def edit(lib, cat):
            g = _find_game(lib, a.game)
            if g is None:
                return f"Unknown game id: {a.game}"
            ov = dict(g.get("overrides") or {})
            if a.cmd == "override":
                ov[a.slot] = os.path.abspath(a.path)
            else:
                ov.pop(a.slot, None)
            if ov:
                g["overrides"] = ov
            else:
                g.pop("overrides", None)
            return None
        rc, lib = _edit_library(a.library, edit)
        if rc == 0:
            _print_game(_find_game(lib, a.game))
        return rc

    if a.cmd == "set-location":
        def edit(lib, cat):
            loc = lib.setdefault("locations", {})
            k = a.key
            if a.reset:
                loc.pop(k, None)
            elif a.value is None:
                return f"set-location {k}: give a VALUE (or --reset)"
            elif k == "rpcs3_auto":
                b = _parse_bool(a.value)
                if b is None:
                    return "rpcs3_auto takes true or false"
                loc[k] = b
            else:
                v = os.path.abspath(a.value)
                if _forbidden(v):
                    return f"{k}: that place is never read or written (licence or firmware files)"
                if k in LOCATION_LISTS:
                    if a.add == a.remove:
                        return f"{k} is a list: use --add or --remove"
                    cur = [x for x in loc.get(k) or [] if os.path.normcase(os.path.normpath(x)) !=
                           os.path.normcase(os.path.normpath(v))]
                    loc[k] = cur + [v] if a.add else cur
                    if not loc[k]:
                        del loc[k]
                else:
                    loc[k] = v
            if not loc:
                lib.pop("locations", None)
            return None
        rc, lib = _edit_library(a.library, edit)
        if rc == 0:
            print(json.dumps(lib.get("locations") or {}, indent=1, ensure_ascii=False))
            for g in lib["games"]:
                print(f"{g['id']:<11} {g['state']:<15} {g['reason']}")
        return rc

    prior = read_library(a.library) if a.library else None
    loc = (prior or {}).get("locations") or {}
    rp = list(a.rpcs3)
    if a.find_rpcs3 and loc.get("rpcs3_auto") is not False:
        for f in find_rpcs3(rp + list(loc.get("rpcs3_folders") or [])):
            if f["path"] not in rp:
                rp.append(f["path"])
            print(f"RPCS3 folder: {f['path']}", file=sys.stderr)
    if not a.dirs and not rp and not loc.get("library_folders") and not loc.get("rpcs3_folders"):
        ap.error("give a folder, --rpcs3 FOLDER or --find-rpcs3")
    games = scan(a.dirs, prior, depth=a.depth, rpcs3=rp)
    lib = make_library(games, prior=prior)
    if a.library:
        write_library(a.library, lib)
    if a.json:
        print(json.dumps(lib, indent=1, ensure_ascii=False))
        return 0
    for g in games:
        print(f"{g['order']}  {g['id']:<11} {g['state']:<15} {g['reason']}")
        for c in g["children"]:
            print(f"     {c['id']:<11} {c['state']:<15} {c['reason']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
