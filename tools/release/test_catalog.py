#!/usr/bin/env python3
"""Host test for catalog.py with SYNTHETIC fixtures written into a temp dir (no game data is read).

    python3 -I tools/release/test_catalog.py
"""
import json
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import catalog  # noqa: E402

FAILS = []
N = 0


def check(cond, what):
    global N
    N += 1
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


# ------------------------------------------------------------------------------------------ fixture builders
def sfo(**kv):
    keys, data, ents = b"", b"", []
    for k, v in kv.items():
        ko, do = len(keys), len(data)
        keys += k.encode() + b"\0"
        if isinstance(v, int):
            raw, fmt, ln, mx = struct.pack("<I", v), 0x0404, 4, 4
        else:
            raw = v.encode() + b"\0"
            fmt, ln, mx = 0x0204, len(raw), (len(raw) + 3) // 4 * 4
            raw = raw.ljust(mx, b"\0")
        ents.append(struct.pack("<HHIII", ko, fmt, ln, mx, do))
        data += raw
    kt = 20 + 16 * len(ents)
    dt = kt + (len(keys) + 3) // 4 * 4
    return (b"\0PSF" + struct.pack("<IIII", 0x101, kt, dt, len(ents)) + b"".join(ents)
            + keys.ljust(dt - kt, b"\0") + data)


def wr(path, data=b""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def disc(root, name, tid, ver="01.00", art=True):
    d = os.path.join(root, name)
    wr(os.path.join(d, "PS3_GAME", "PARAM.SFO"), sfo(TITLE_ID=tid, APP_VER=ver, VERSION="01.00", CATEGORY="DG", TITLE="x"))
    if art:
        wr(os.path.join(d, "PS3_GAME", "ICON0.PNG"), b"p")
        wr(os.path.join(d, "PS3_GAME", "PIC1.PNG"), b"p")
    return d


def psn(root, name, tid, ver="01.00"):
    d = os.path.join(root, name)
    wr(os.path.join(d, "PARAM.SFO"), sfo(TITLE_ID=tid, APP_VER=ver, VERSION="01.00", CATEGORY="HG", TITLE="x"))
    return d


def elf(path, machine=21, magic=b"\x7fELF"):
    h = magic + bytes([2, 2, 1, 0]) + b"\0" * 8 + struct.pack(">HH", 2, machine)
    wr(path, h + b"\0" * 64)
    return path


def pkg(path, cid, ctype=4):
    h = bytearray(0x80)
    h[:4] = b"\x7fPKG"
    struct.pack_into(">II", h, 8, 0x80, 1)
    h[0x30:0x30 + len(cid)] = cid.encode()
    meta = struct.pack(">II", 2, 4) + struct.pack(">I", ctype)
    wr(path, bytes(h) + meta)
    return path


def dirrec(lba, size, name, isdir, more=False):
    nl = len(name)
    ln = 33 + nl + (1 if nl % 2 == 0 else 0)
    r = bytearray(ln)
    r[0] = ln
    struct.pack_into("<I", r, 2, lba)
    struct.pack_into(">I", r, 6, lba)
    struct.pack_into("<I", r, 10, size)
    struct.pack_into(">I", r, 14, size)
    r[25] = (2 if isdir else 0) | (0x80 if more else 0)
    r[32] = nl
    r[33:33 + nl] = name
    return bytes(r)


def build_iso(path, prim, jol, files, encrypted_table=False):
    """prim/jol: {name: ('f', key) | ('d', subtree)}; files: {key: [chunk, ...]} (several chunks = multi-extent)."""
    nxt = [19]
    dirs = {}

    def alloc(tree, tag):
        dirs[tag] = nxt[0]
        nxt[0] += 1
        for n, (k, v) in tree.items():
            if k == "d":
                alloc(v, tag + "/" + n)
    alloc(prim, "P")
    alloc(jol, "J")
    ext = {}
    for key, chunks in files.items():
        ext[key] = []
        for c in chunks:
            ext[key].append((nxt[0], len(c)))
            nxt[0] += (len(c) + SECT - 1) // SECT
    img = bytearray(nxt[0] * SECT)

    def put(lba, data):
        img[lba * SECT:lba * SECT + len(data)] = data

    for key, chunks in files.items():
        for (lba, _), c in zip(ext[key], chunks):
            put(lba, c)

    def render(tree, tag, parent_tag, joliet):
        enc = (lambda s: s.encode("utf-16-be")) if joliet else (lambda s: s.encode("latin-1"))
        me = dirs[tag]
        pr = dirs[parent_tag]
        recs = dirrec(me, SECT, b"\0", True) + dirrec(pr, SECT, b"\1", True)
        for n, (k, v) in tree.items():
            nm = enc(n if joliet else (n if k == "d" else n + ";1"))
            if k == "d":
                recs += dirrec(dirs[tag + "/" + n], SECT, nm, True)
                render(v, tag + "/" + n, tag, joliet)
            else:
                es = ext[v]
                for i, (lba, size) in enumerate(es):
                    recs += dirrec(lba, size, nm, False, more=i < len(es) - 1)
        put(me, recs)
    render(prim, "P", "P", False)
    render(jol, "J", "J", True)

    def vd(kind, tag, esc):
        d = bytearray(SECT)
        d[0] = kind
        d[1:6] = b"CD001"
        d[6] = 1
        if esc:
            d[88:91] = esc
        struct.pack_into("<H", d, 128, SECT)
        d[156:190] = dirrec(dirs[tag], SECT, b"\0", True)
        return bytes(d)
    put(16, vd(1, "P", None))
    put(17, vd(2, "J", b"%/E"))
    t = bytearray(SECT)
    t[0] = 255
    t[1:6] = b"CD001"
    put(18, bytes(t))
    if encrypted_table:
        put(0, struct.pack(">II", 1, 0) + struct.pack(">II", 0, 40))
    wr(path, bytes(img))


SECT = catalog.SECTOR


def iso_with_sfo(path, tid, ver="01.00", multi=False, encrypted=False):
    good = sfo(TITLE_ID=tid, APP_VER=ver, VERSION="01.00", CATEGORY="DG", TITLE="x")
    bad = sfo(TITLE_ID="WRONG00000", APP_VER="09.99", VERSION="09.99", CATEGORY="DG", TITLE="x")
    chunks = [good[:30], good[30:]] if multi else [good]
    # primary names are 8.3 and point at a DIFFERENT sfo; Joliet names are the real tree
    prim = {"PS3_GA00": ("d", {"PARAM.SFO": ("f", "bad")})}
    jol = {"PS3_GAME": ("d", {"PARAM.SFO": ("f", "good")})}
    build_iso(path, prim, jol, {"bad": [bad], "good": chunks}, encrypted_table=encrypted)


def by_id(games, i):
    return next(g for g in games if g["id"] == i)


# ------------------------------------------------------------------------------------------------- tests
def main():
    tmp = tempfile.mkdtemp(prefix="rc_catalog_test_")
    try:
        run(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"\n{N - len(FAILS)}/{N} checks passed")
    return 1 if FAILS else 0


def run(tmp):
    cat = catalog.load()
    g = cat["games"]
    check([x["id"] for x in g] == ["tods", "qfb", "acit", "a4o", "ffa", "nexus", "collection", "dlhd"], "catalogue: 8 entries in series order")
    check([c["id"] for c in by_id(g, "collection")["children"]] == ["rc1", "rc2", "rc3"], "catalogue: Collection children rc1..rc3")
    check([x["id"] for x in g if x["supported"]] == ["tods", "acit"], "catalogue: only tods and acit supported")
    check(by_id(g, "acit")["update"]["version"] == "01.20" and by_id(g, "tods")["update"] is None, "catalogue: update versions")
    check(all(set(x["igdb"]) == {"game", "cover", "artwork"} for x in g + by_id(g, "collection")["children"])
          and by_id(g, "tods")["igdb"] == {"game": 1792, "cover": "co8542", "artwork": None}, "catalogue: igdb field")
    readme = os.path.join(HERE, "..", "..", "public", "README.md")
    if os.path.isfile(readme):
        t = open(readme, encoding="utf-8").read()
        check(all("| " + x["readme_state"] + " |" in t for x in g), "catalogue: readme_state matches public/README.md rows")
    ids = [i for x in g for m in x["title_ids"].values() for v in m.values() for i in v]
    check("BCET70051" not in ids and "NPUA70224" not in ids, "catalogue: A4O beta id and unmatched package are not entries")

    # PARAM.SFO
    s = catalog.read_param_sfo(sfo(TITLE_ID="BCUS98127", APP_VER="01.00", VERSION="02.00", CATEGORY="DG", TITLE="T", PARENTAL=5))
    check(s["TITLE_ID"] == "BCUS98127" and s["VERSION"] == "02.00" and s["PARENTAL"] == 5, "PARAM.SFO: strings and ints")
    try:
        catalog.read_param_sfo(b"nope")
        check(False, "PARAM.SFO: bad magic rejected")
    except ValueError:
        check(True, "PARAM.SFO: bad magic rejected")

    # ISO: Joliet over primary, multi-extent, encrypted table
    iso1 = os.path.join(tmp, "iso1", "a.iso")
    os.makedirs(os.path.dirname(iso1))
    iso_with_sfo(iso1, "BCUS98175")
    r, enc = catalog.sfo_from_iso(iso1)
    check(r and r["TITLE_ID"] == "BCUS98175" and not enc, "ISO: Joliet tree preferred over the 8.3 primary tree")
    iso2 = os.path.join(tmp, "iso2", "b.iso")
    os.makedirs(os.path.dirname(iso2))
    iso_with_sfo(iso2, "BCUS98175", multi=True)
    with catalog.Iso(iso2) as iso:
        e = iso.lookup("PS3_GAME/PARAM.SFO")
    r, _ = catalog.sfo_from_iso(iso2)
    check(e and len(e[1]) == 2 and r and r["TITLE_ID"] == "BCUS98175", "ISO: multi-extent file read as one")
    iso3 = os.path.join(tmp, "iso3", "c.iso")
    os.makedirs(os.path.dirname(iso3))
    iso_with_sfo(iso3, "BCUS98175", encrypted=True)
    check(catalog.sfo_from_iso(iso3)[1] is True, "ISO: region table flags an encrypted image")
    notiso = os.path.join(tmp, "iso3", "d.iso")
    wr(notiso, b"x" * 100)
    check(catalog.sfo_from_iso(notiso) == (None, False), "ISO: junk file gives None")

    # PKG
    pk = pkg(os.path.join(tmp, "p", "UP9000-BCUS98124_00-ACRACKINTIME0000-A0120-V0101-PE.pkg"), "UP9000-BCUS98124_00-ACRACKINTIME0000")
    h = catalog.pkg_header(pk)
    check(h["title_id"] == "BCUS98124" and h["version"] == "01.20" and h["content_type"] == 4, "PKG: clear header fields")
    wr(os.path.join(tmp, "p", "x.pkg"), b"nope" * 40)
    check(catalog.pkg_header(os.path.join(tmp, "p", "x.pkg")) is None, "PKG: bad magic gives None")

    # empty scan
    empty = os.path.join(tmp, "empty")
    os.makedirs(empty)
    res = catalog.scan([empty])
    check(len(res) == 8 and all(x["state"] == "not_added" and x["reason"] == "Game files not found" for x in res), "scan: nothing found, 8 entries not_added")

    # a rich library
    lib = os.path.join(tmp, "lib")
    disc(lib, "tods_disc", "BCUS98127", "01.00")
    elf(os.path.join(lib, "work", "bin", "tods", "EBOOT.elf"))
    disc(lib, "acit_disc", "BCUS98124", "01.00")
    elf(os.path.join(lib, "acit_disc", "EBOOT.elf"))
    disc(lib, "nexus_disc", "BCUS99245", "01.00")
    psn(lib, "ffa_psn", "NPUA80642", "01.00")
    disc(lib, "col_disc", "BCUS98282", "01.00")
    psn(lib, "qfb_psn", "NPUA80145")
    res = catalog.scan([lib])
    t = by_id(res, "tods")
    check(t["state"] == "ready_to_build" and t["title_id"] == "BCUS98127" and t["files"]["eboot"].endswith("EBOOT.elf"), "state: ToD ready_to_build")
    check(t["art"]["icon"].endswith("ICON0.PNG") and t["art"]["background"].endswith("PIC1.PNG"), "art: disc ICON0/PIC1 paths recorded")
    check(t["estimate"]["minutes"] == 12, "estimate carried")
    a = by_id(res, "acit")
    check(a["state"] == "wrong_version" and a["reason"].startswith("Needs update 01.20") and a["needs_update"] == "01.20", "state: ACiT wrong_version without the update")
    check(by_id(res, "nexus")["state"] == "wrong_version" and by_id(res, "ffa")["state"] == "wrong_version", "state: Nexus and FFA wrong_version (unsupported but update missing)")
    check(by_id(res, "qfb")["state"] == "not_ported" and by_id(res, "qfb")["reason"] == "Not ported yet", "state: QfB (PSN folder) not_ported")
    col = by_id(res, "collection")
    check(col["state"] == "not_ported" and [c["state"] for c in col["children"]] == ["not_ported"] * 3, "Collection: disc found -> parent and children not_ported")
    check(by_id(res, "a4o")["state"] == "not_added" and by_id(res, "dlhd")["state"] == "not_added", "partial set: others not_added")

    # update PKG fixes ACiT; installed update folder too
    pkg(os.path.join(lib, "upd", "UP9000-BCUS98124_00-ACRACKINTIME0000-A0120-V0101-PE.pkg"), "UP9000-BCUS98124_00-ACRACKINTIME0000")
    a = by_id(catalog.scan([lib]), "acit")
    check(a["state"] == "ready_to_build" and a["files"]["update"].endswith(".pkg"), "state: update PKG satisfies ACiT")
    os.remove(os.path.join(lib, "upd", os.listdir(os.path.join(lib, "upd"))[0]))
    psn(lib, "acit_patch", "BCUS98124", "01.20")
    a = by_id(catalog.scan([lib]), "acit")
    check(a["state"] == "ready_to_build" and a["files"]["update"].endswith("acit_patch"), "state: installed update folder satisfies ACiT")
    pkg(os.path.join(lib, "upd", "mystery.pkg"), "UP9000-BCUS98124_00-ACRACKINTIME0000")
    shutil.rmtree(os.path.join(lib, "acit_patch"))
    check(by_id(catalog.scan([lib]), "acit")["state"] == "wrong_version", "state: PKG of unknown version does not satisfy")
    os.remove(os.path.join(lib, "upd", "mystery.pkg"))

    # EBOOT missing -> needs_attention
    os.remove(os.path.join(lib, "acit_disc", "EBOOT.elf"))
    psn(lib, "acit_patch", "BCUS98124", "01.20")
    a = by_id(catalog.scan([lib]), "acit")
    check(a["state"] == "needs_attention" and a["reason"] == "EBOOT.elf missing", "state: EBOOT.elf missing -> needs_attention")
    # wrong machine / not an elf are ignored
    elf(os.path.join(lib, "acit_disc", "EBOOT.elf"), machine=62)
    check(by_id(catalog.scan([lib]), "acit")["files"]["eboot"] is None, "ELF: x86-64 ELF not recognised as PPC64")
    elf(os.path.join(lib, "acit_disc", "EBOOT.elf"), magic=b"NOPE")
    check(by_id(catalog.scan([lib]), "acit")["files"]["eboot"] is None, "ELF: bad magic ignored")
    elf(os.path.join(lib, "acit_disc", "EBOOT.elf"))
    its = [i for i in catalog.discover([lib]) if i["kind"] == "eboot"]
    check(all(len(i["sha256"]) == 64 for i in its), "ELF: SHA-256 recorded")

    # ISO as the game, encrypted ISO, ISO with update
    isod = os.path.join(tmp, "isos")
    os.makedirs(isod)
    iso_with_sfo(os.path.join(isod, "a4o.iso"), "BCUS98175", encrypted=True)
    a4 = by_id(catalog.scan([isod]), "a4o")
    check(a4["state"] == "needs_attention" and "encrypted" in a4["reason"], "state: encrypted ISO -> needs_attention")
    os.remove(os.path.join(isod, "a4o.iso"))
    iso_with_sfo(os.path.join(isod, "tods.iso"), "BCUS98127")
    elf(os.path.join(isod, "tods", "EBOOT.elf"))
    t = by_id(catalog.scan([isod]), "tods")
    check(t["state"] == "ready_to_build" and t["files"]["disc"].endswith("tods.iso") and t["art"]["icon"] is None, "state: ISO read in place, ToD ready")

    # a disc's PKGDIR (category DP, the disc's install package) is part of the disc, never an update (Collection, 2026-11-03)
    pk = os.path.join(tmp, "pkgdir")
    d = disc(pk, "coll", "BCUS98282")
    wr(os.path.join(d, "PS3_GAME", "PKGDIR", "PARAM.SFO"), sfo(TITLE_ID="BCUS98282", CATEGORY="DP", TITLE="x"))
    c = by_id(catalog.scan([pk]), "collection")
    check(c["state"] != "not_added" and c["files"]["update"] is None, "disc: PKGDIR inside a disc is not an update")
    stray = os.path.join(tmp, "stray")
    wr(os.path.join(stray, "PKGDIR", "PARAM.SFO"), sfo(TITLE_ID="BCUS98282", CATEGORY="DP", TITLE="x"))
    check(by_id(catalog.scan([stray]), "collection")["state"] == "not_added", "folder: a DP PARAM.SFO alone is not a game")

    # depth limit and symlinks
    deep = os.path.join(tmp, "deep")
    disc(os.path.join(deep, "a", "b", "c", "d", "e"), "g", "BCUS98127")
    check(by_id(catalog.scan([deep], depth=3), "tods")["state"] == "not_added", "depth limit: too deep is not found")
    check(by_id(catalog.scan([deep], depth=8), "tods")["state"] != "not_added", "depth limit: deeper limit finds it")
    outside = os.path.join(tmp, "outside")
    disc(outside, "tods_out", "BCUS98127")
    link_root = os.path.join(tmp, "linkroot")
    os.makedirs(link_root)
    try:
        os.symlink(outside, os.path.join(link_root, "ln"))
        check(by_id(catalog.scan([link_root]), "tods")["state"] != "not_added", "symlink: a linked game folder is found")
        os.symlink(link_root, os.path.join(link_root, "loop"))   # a link back to its own parent
        check(by_id(catalog.scan([link_root], depth=8), "tods")["state"] != "not_added", "symlink: a link loop ends, game still found")
    except (OSError, NotImplementedError):
        print("skip symlink test (cannot create symlinks)")

    # built / port_updated / missing exe / building
    exe = os.path.join(tmp, "games", "tods.exe")
    wr(exe, b"MZ")
    prior = {"schema": 1, "games": [{"id": "tods", "state": "built", "files": {"disc": None, "update": None, "eboot": None},
                                      "build": {"exe": exe, "port_version": cat["port_version"], "exe_sha256": "0" * 64, "built_at": "x"}}]}
    t = by_id(catalog.scan([lib], prior), "tods")
    check(t["state"] == "built" and t["build"]["exe"] == exe, "state: recorded build with its exe -> built")
    prior["games"][0]["build"]["port_version"] = "0.0.9"
    check(by_id(catalog.scan([lib], prior), "tods")["state"] == "port_updated", "state: older port version -> port_updated")
    os.remove(exe)
    t = by_id(catalog.scan([empty], prior), "tods")
    check(t["state"] == "needs_attention" and "missing" in t["reason"], "state: build recorded but exe gone -> needs_attention")
    prior["games"][0].update(state="building", build=None)
    check(by_id(catalog.scan([lib], prior), "tods")["state"] == "building", "state: building is kept")

    # library.json round trip, atomic write, CLI
    libf = os.path.join(tmp, "out", "library.json")
    res = catalog.scan([lib])
    catalog.write_library(libf, catalog.make_library(res))
    back = catalog.read_library(libf)
    check(back["schema"] == 1 and [x["id"] for x in back["games"]] == [x["id"] for x in res]
          and not [f for f in os.listdir(os.path.dirname(libf)) if f.endswith(".tmp")], "library.json: write/read, no temp left")
    need = {"id", "title", "year", "order", "readme_state", "supported", "state", "reason", "title_id", "version", "needs_update",
            "files", "art", "build", "estimate", "children", "igdb"}
    check(all(need <= set(x) for x in back["games"]) and set(back["games"][0]["files"]) == {"disc", "update", "eboot"}, "library.json: schema fields present")
    check(catalog.read_library(os.path.join(tmp, "nope.json")) is None, "library.json: missing file reads as None")
    ex = os.path.join(HERE, "..", "..", "data", "titles", "library.example.json")
    if os.path.isfile(ex):
        e = catalog.read_library(ex)
        check(e and [x["id"] for x in e["games"]] == [x["id"] for x in g] and all(need <= set(x) for x in e["games"])
              and all(x["state"] in catalog.STATES for x in e["games"]), "library.example.json: shape and states")
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = catalog.main(["scan", lib, "--json", "--library", os.path.join(tmp, "cli.json")])
    check(rc == 0 and json.loads(buf.getvalue())["schema"] == 1 and os.path.isfile(os.path.join(tmp, "cli.json")), "CLI: scan --json --library")
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        catalog.main(["scan", lib])
    check("tods" in buf.getvalue() and "ready_to_build" in buf.getvalue(), "CLI: table output")
    # the launcher's first start: no library.json yet, no folder, --find-rpcs3 finding nothing -> a library of every
    # game, none added (this machine's real RPCS3 folders are kept out by stubbing find_rpcs3)
    first = os.path.join(tmp, "first", "library.json")
    os.makedirs(os.path.dirname(first))
    real_find = catalog.find_rpcs3
    catalog.find_rpcs3 = lambda *a, **k: []
    try:
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            rc = catalog.main(["scan", "--library", first, "--find-rpcs3"])
    finally:
        catalog.find_rpcs3 = real_find
    fl = catalog.read_library(first) if os.path.isfile(first) else None
    check(rc == 0 and fl and [x["id"] for x in fl["games"]] == [x["id"] for x in g]
          and all(x["state"] == "not_added" for x in fl["games"]), "CLI: first start (scan --library F --find-rpcs3, nothing found) writes every game")
    try:
        with contextlib.redirect_stderr(io.StringIO()):
            rc = catalog.main(["scan"])
    except SystemExit as e:
        rc = e.code
    check(rc == 2, "CLI: scan with nothing to look at is still a usage error")
    run_rpcs3(tmp)
    run_locations(tmp)


# ------------------------------------------------------------------------------------------------- RPCS3 source
def hdd_game(root, tid, cat_, ver, elf_too=False):
    d = os.path.join(root, "dev_hdd0", "game", tid)
    wr(os.path.join(d, "PARAM.SFO"), sfo(TITLE_ID=tid, APP_VER=ver, VERSION=ver, CATEGORY=cat_, TITLE="x"))
    wr(os.path.join(d, "USRDIR", "EBOOT.BIN"), b"SCE")
    if elf_too:
        elf(os.path.join(d, "USRDIR", "EBOOT.elf"))
    return d


def forbidden_touch(p):
    low = p.replace("\\", "/").lower()
    return ("/exdata" in low or low.endswith((".rap", ".rif", ".edat", "act.dat")) or "/dev_flash" in low
            or "/cache" in low or "/logs" in low or "/shaders" in low)


def run_rpcs3(tmp):
    import builtins
    import contextlib
    import io
    r3 = os.path.join(tmp, "rpcs3_fix")
    discs = os.path.join(tmp, "rpcs3_discs")
    acit = disc(discs, "ACiT game", "BCUS98124", "01.00")
    elf(os.path.join(acit, "PS3_GAME", "USRDIR", "EBOOT.elf"))
    wr(os.path.join(acit, "PS3_GAME", "USRDIR", "EBOOT.BIN"), b"SCE")
    other = disc(discs, "other", "BLUS30000", "01.00")
    qdisc = disc(discs, "tods q", "BCUS98127", "01.00")
    elf(os.path.join(qdisc, "PS3_GAME", "USRDIR", "EBOOT.elf"))
    wr(os.path.join(r3, "config", "games.yml"),
       ('# a comment\n\n---\nBCUS98124: "%s"\nBLUS30000: %s\nBCUS98127: \'%s\'  # tail\nbad line\n'
        % (acit, other, qdisc)).encode())
    hdd_game(r3, "BCUS98124", "GD", "01.20")
    hdd_game(r3, "NPUA80145", "HG", "01.00", elf_too=True)
    hdd_game(r3, "BLUS30000", "HG", "01.00")                     # not a catalogue ID: never opened
    hdd_game(r3, "BCUS98175", "DG", "01.00")                     # other category: ignored
    h1 = os.path.join(r3, "dev_hdd0", "home", "00000001")
    ex = os.path.join(h1, "exdata")
    wr(os.path.join(ex, "game.rap"), b"secret")
    wr(os.path.join(ex, "x.rif"), b"secret")
    wr(os.path.join(h1, "savedata", "BCUS98124-AUTO", "DATA.BIN"), b"x" * 100)
    wr(os.path.join(h1, "savedata", "BCUS98124-AUTO", "sub", "ICON0.PNG"), b"y" * 28)
    wr(os.path.join(h1, "savedata", "BCUS98124-AUTO", "x.edat"), b"never")
    wr(os.path.join(h1, "savedata", "BLUS30000-OTHER", "D.BIN"), b"z")
    wr(os.path.join(h1, "savedata", "NPUA80145_SLOT", "D.BIN"), b"z" * 7)
    wr(os.path.join(r3, "dev_hdd0", "dev_flash", "sys", "x.sprx"), b"fw")
    wr(os.path.join(r3, "dev_hdd0", "game", "BCUS98124", "cache", "c"), b"c")
    wr(os.path.join(r3, "cache", "shaders", "s"), b"s")
    wr(os.path.join(h1, "act.dat"), b"a")
    wr(os.path.join(r3, "rpcs3.exe"), b"MZ")

    # every filesystem entry point is wrapped: a forbidden path is recorded and fails the check
    hits = []
    names = ("scandir", "listdir", "stat", "lstat", "walk")
    real = {n: getattr(os, n) for n in names}
    real_open = builtins.open

    def guard(fn):
        def f(p, *a, **k):
            if isinstance(p, (str, os.PathLike)) and forbidden_touch(os.fspath(p)):
                hits.append(str(p))
            return fn(p, *a, **k)
        return f
    for n in names:
        setattr(os, n, guard(real[n]))
    builtins.open = guard(real_open)
    try:
        r = catalog.scan_rpcs3(r3)
        games = catalog.scan([], rpcs3=[r3])
        pf0 = catalog.find_rpcs3(extra_paths=[tmp], env={}, launcher_dir="")
    finally:
        for n in names:
            setattr(os, n, real[n])
        builtins.open = real_open
    check(not hits, "rpcs3: no licence/firmware/cache/log/shader path is listed, stat'ed or opened %s" % hits[:2])
    check(len(r["items"]) > 0 and len(pf0) >= 1, "rpcs3: guarded scan did real work")
    check(catalog._forbidden("/a/b/dev_hdd0/home/1/exdata/x") and catalog._forbidden("/a/game.RAP")
          and catalog._forbidden("/a/act.dat") and catalog._forbidden("/a/b.edat") and not catalog._forbidden("/a/b/PARAM.SFO")
          and catalog._forbidden(r3 + "/cache/x", r3) and not catalog._forbidden("/games/cache/x/PS3_GAME", r3),
          "rpcs3: forbidden-path test (cache only below the RPCS3 folder)")
    if hasattr(os, "geteuid") and os.geteuid() != 0:
        os.chmod(ex, 0)                      # a real permission wall as well
        try:
            check(len(catalog.scan_rpcs3(r3)["items"]) == len(r["items"]), "rpcs3: unreadable exdata does not matter")
        finally:
            os.chmod(ex, 0o755)
    check([t for t, _ in r["disc_folders"]] == ["BCUS98124", "BLUS30000", "BCUS98127"],
          "rpcs3: games.yml parsed (quoted, unquoted, single quotes, comments, bad line)")
    check(catalog.parse_games_yml('BCUS98124: "C:\\\\Games\\\\A"\nBCES00511: \'it\'\'s\'\n')
          == [("BCUS98124", "C:\\Games\\A"), ("BCES00511", "it's")], "rpcs3: games.yml escapes")
    kinds = sorted((i["kind"], i["title_id"]) for i in r["items"] if i["kind"] != "eboot")
    check(kinds == [("disc", "BCUS98124"), ("disc", "BCUS98127"), ("psn", "NPUA80145"), ("update_folder", "BCUS98124")]
          and all(i["source"] == "rpcs3" for i in r["items"]), "rpcs3: discs, HG, GD found; other IDs/categories not read")
    upd = next(i for i in r["items"] if i["kind"] == "update_folder")
    check(upd["version"] == "01.20", "rpcs3: GD update carries its real APP_VER")
    ebs = [i["path"] for i in r["items"] if i["kind"] == "eboot"]
    check(len(ebs) == 3 and all(e.endswith("EBOOT.elf") for e in ebs), "rpcs3: EBOOT.elf beside EBOOT.BIN found (disc + PSN)")
    check(sorted((s["dir"], s["bytes"]) for s in r["saves"]) == [("BCUS98124-AUTO", 128), ("NPUA80145_SLOT", 7)],
          "rpcs3: saves listed with sizes (catalogue IDs only; .edat inside not counted)")
    a = by_id(games, "acit")
    check(a["state"] == "ready_to_build" and a["files"]["update"] == upd["path"],
          "rpcs3: ACiT disc via games.yml + installed GD 01.20 = no 'Needs update' (%s)" % a["reason"])
    check(a["sources"] == {"disc": "rpcs3", "update": "rpcs3", "eboot": "rpcs3"}
          and a["rpcs3_saves"] == [{"dir": "BCUS98124-AUTO", "bytes": 128}], "rpcs3: library entry records sources and saves")
    q = by_id(games, "qfb")
    check(q["sources"]["disc"] == "rpcs3" and q["files"]["eboot"] and q["rpcs3_saves"][0]["dir"] == "NPUA80145_SLOT"
          and q["title_id"] == "NPUA80145", "rpcs3: PSN HG game found with source, EBOOT.elf, save (state %s)" % q["state"])
    check("sources" not in by_id(catalog.scan([], rpcs3=[]), "acit"), "rpcs3: nothing found -> no sources field")
    no_gd = os.path.join(tmp, "rpcs3_nogd")
    wr(os.path.join(no_gd, "config", "games.yml"), ("BCUS98124: %s\n" % acit).encode())
    check(by_id(catalog.scan([], rpcs3=[no_gd]), "acit")["state"] == "wrong_version", "rpcs3: without the GD update -> Needs update")
    p = pkg(os.path.join(tmp, "rpcs3_pkgs", "UP0001-BCUS98124_00-ACIT0000000000-A0120-V0100-PE.pkg"),
            "UP0001-BCUS98124_00-ACIT0000000000", 4)
    a2 = by_id(catalog.scan([os.path.dirname(p)], rpcs3=[r3]), "acit")
    check(a2["files"]["update"] == upd["path"] and a2["sources"]["update"] == "rpcs3",
          "rpcs3: real APP_VER preferred over the PKG file-name guess")
    check(by_id(catalog.scan([discs]), "acit")["sources"]["disc"] == "folder", "rpcs3: plain folder scan says source folder")

    # find_rpcs3 on fake portable folders with a fake environment
    home = os.path.join(tmp, "fakehome")
    pt = os.path.join(home, "Downloads", "rpcs3-portable")
    wr(os.path.join(pt, "rpcs3.exe"), b"MZ")
    wr(os.path.join(pt, "config", "games.yml"), b"")
    os.makedirs(os.path.join(pt, "dev_hdd0"))
    wr(os.path.join(home, "Downloads", "notrpcs3", "readme.txt"), b"")
    wr(os.path.join(home, ".config", "rpcs3", "config", "games.yml"), b"")
    os.makedirs(os.path.join(home, ".config", "rpcs3", "dev_hdd0"))
    deep = os.path.join(home, "Documents", "a", "rpcs3")
    wr(os.path.join(deep, "rpcs3.exe"), b"MZ")
    wr(os.path.join(deep, "config", "games.yml"), b"")
    env = {"USERPROFILE": home, "LOCALAPPDATA": os.path.join(home, "AppData", "Local")}
    f = catalog.find_rpcs3(env=env, launcher_dir=os.path.join(home, "nowhere"))
    got = sorted(os.path.relpath(x["path"], home) for x in f)
    check(got == [os.path.join(".config", "rpcs3"), os.path.join("Downloads", "rpcs3-portable")],
          "rpcs3: find_rpcs3 finds the portable folder and the Linux config dir, depth 1 only %s" % got)
    pf = next(x for x in f if x["path"].endswith("rpcs3-portable"))
    check(pf["games_yml"].endswith("games.yml") and pf["dev_hdd0"].endswith("dev_hdd0"),
          "rpcs3: find_rpcs3 returns games_yml and dev_hdd0")
    f2 = catalog.find_rpcs3(extra_paths=[deep], env={}, launcher_dir="")
    check([x["path"] for x in f2] == [deep] and f2[0]["dev_hdd0"] is None, "rpcs3: extra path used, missing dev_hdd0 is None")

    # dev_hdd0 moved in vfs.yml (RPCS3 sources: default "$(EmulatorDir)dev_hdd0/", file in the config dir)
    moved = os.path.join(home, "Downloads", "rpcs3-moved")
    wr(os.path.join(moved, "rpcs3.exe"), b"MZ")
    other = os.path.join(home, "big-drive", "hdd0")
    os.makedirs(other)
    wr(os.path.join(moved, "config", "vfs.yml"), ("/dev_flash/: $(EmulatorDir)dev_flash/\n/dev_hdd0/: %s/\n" % other).encode())
    fm = catalog.find_rpcs3(extra_paths=[moved], env={}, launcher_dir="")
    check(fm and os.path.normpath(fm[0]["dev_hdd0"]) == os.path.normpath(other), "rpcs3: dev_hdd0 moved by vfs.yml is followed")
    wr(os.path.join(moved, "config", "vfs.yml"), b"/dev_hdd0: x\n'/dev_hdd0/': '$(EmulatorDir)dev_hdd0/'\n")
    os.makedirs(os.path.join(moved, "dev_hdd0"))
    fm = catalog.find_rpcs3(extra_paths=[moved], env={}, launcher_dir="")
    check(fm and os.path.normpath(fm[0]["dev_hdd0"]) == os.path.normpath(os.path.join(moved, "dev_hdd0")),
          "rpcs3: quoted $(EmulatorDir) entry resolves to the RPCS3 folder")

    # import_save
    src = os.path.join(h1, "savedata", "BCUS98124-AUTO")
    dest = os.path.join(tmp, "port_saves")
    res = catalog.import_save(src, dest)
    d1 = os.path.join(dest, "BCUS98124-AUTO")
    check(res["ok"] and os.path.isfile(os.path.join(d1, "DATA.BIN")) and os.path.isfile(os.path.join(src, "DATA.BIN"))
          and os.path.getsize(os.path.join(d1, "sub", "ICON0.PNG")) == 28
          and not os.path.exists(os.path.join(d1, "x.edat")), "import_save: copies (source kept, .edat skipped)")
    wr(os.path.join(d1, "DATA.BIN"), b"mine")
    res = catalog.import_save(src, dest)
    with open(os.path.join(d1, "DATA.BIN"), "rb") as fh:
        kept = fh.read()
    check(not res["ok"] and res["reason"] and kept == b"mine", "import_save: refuses to overwrite, destination untouched")
    check(not catalog.import_save(ex, dest)["ok"] and not os.path.exists(os.path.join(dest, "exdata")),
          "import_save: refuses exdata")
    check([n for n in os.listdir(dest) if n.endswith(".importing")] == [], "import_save: no temp left")
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = catalog.main(["scan", "--rpcs3", r3, "--json"])
    j = json.loads(buf.getvalue())
    check(rc == 0 and by_id(j["games"], "acit")["sources"]["disc"] == "rpcs3", "CLI: scan --rpcs3")


# ------------------------------------------------------------------------------------------------- Locations
def guard(fn, default=None):
    """Run a new-API call; on the old code (missing parameter / key) the check fails instead of crashing the run."""
    try:
        return fn()
    except Exception as ex:  # noqa: BLE001
        print("     (raised %s)" % ex.__class__.__name__)
        return default


def with_ov(games, gid, **ov):
    """A prior library with overrides on one game (as the launcher/CLI store them)."""
    lib = catalog.make_library(games)
    by_id(lib["games"], gid)["overrides"] = dict(ov)
    return lib


def run_locations(tmp):
    import contextlib
    import io
    root = os.path.join(tmp, "loc")
    found = os.path.join(root, "found")
    chosen = os.path.join(root, "chosen")
    td = disc(found, "tods_found", "BCUS98127", "01.00")
    fe = elf(os.path.join(found, "work", "bin", "tods", "EBOOT.elf"))
    tc = disc(chosen, "tods_mine", "BCUS98127", "01.00", art=False)
    ac = disc(found, "acit_found", "BCUS98124", "01.00")
    elf(os.path.join(ac, "EBOOT.elf"))
    upd = pkg(os.path.join(found, "upd", "UP9000-BCUS98124_00-ACRACKINTIME0000-A0120-V0101-PE.pkg"),
              "UP9000-BCUS98124_00-ACRACKINTIME0000")
    base = catalog.scan([found])
    check(by_id(base, "tods")["state"] == "ready_to_build" and by_id(base, "acit")["state"] == "ready_to_build",
          "locations: fixture library is ready without overrides")

    # --- override wins over a found item
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", disc=tc)), "tods"))
    check(r and r["files"]["disc"] == tc and r["sources"]["disc"] == "override" and r["state"] == "ready_to_build"
          and "suggested" not in r and r["overrides"] == {"disc": tc}, "override: a valid disc override wins over the found disc")
    check(r and r["sources"]["eboot"] == "folder", "override: other slots keep their own source")
    nov = by_id(catalog.scan([found], with_ov(base, "tods")), "tods")
    check("overrides" not in nov and nov["files"]["disc"] == td and "suggested" not in nov, "override: empty overrides change nothing")

    # --- missing override -> needs_attention + suggested, never the found path
    gone = os.path.join(root, "gone", "disc")
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", disc=gone)), "tods"))
    check(r and r["state"] == "needs_attention" and r["reason"] == "The disc folder you chose is missing"
          and r["files"]["disc"] == gone and r["sources"]["disc"] == "override", "override: missing disc -> needs_attention, path in use is the chosen one")
    check(r and r.get("suggested") == {"disc": td}, "override: missing disc reports the found disc as suggested, not used")
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", disc=gone + ".iso")), "tods"))
    check(r and r["reason"] == "The disc image you chose is missing", "override: missing ISO has its own reason")

    # --- wrong game
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", disc=ac)), "tods"))
    check(r and r["state"] == "needs_attention" and r["reason"] == "The disc you chose is a different game (BCUS98124)"
          and r["suggested"] == {"disc": td}, "override: disc of another game refused with its title ID")
    nogame = os.path.join(root, "nogame")
    os.makedirs(nogame)
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", disc=nogame)), "tods"))
    check(r and r["reason"] == "The disc folder you chose holds no PS3 game (no PARAM.SFO)", "override: folder without PARAM.SFO refused")

    # --- update version
    old_upd = pkg(os.path.join(root, "old", "UP9000-BCUS98124_00-ACRACKINTIME0000-A0110-V0101-PE.pkg"),
                  "UP9000-BCUS98124_00-ACRACKINTIME0000")
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "acit", update=old_upd)), "acit"))
    check(r and r["state"] == "needs_attention" and r["reason"] == "The update you chose is 01.10; 01.20 is needed"
          and r["suggested"] == {"update": upd}, "override: update of the wrong version refused, found update suggested")
    new_upd = psn(root, "upd_mine", "BCUS98124", "01.20")
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "acit", update=new_upd)), "acit"))
    check(r and r["state"] == "ready_to_build" and r["files"]["update"] == new_upd and r["sources"]["update"] == "override",
          "override: a good update folder wins over the found PKG")
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "acit", update=ac)), "acit"))
    check(r and r["state"] == "needs_attention" and "update" in r["reason"], "override: a disc given as the update is refused")
    other_upd = pkg(os.path.join(root, "o", "UP9000-BCUS98127_00-X-A0100-V0101-PE.pkg"), "UP9000-BCUS98127_00-X")
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "acit", update=other_upd)), "acit"))
    check(r and r["reason"] == "The update you chose is a different game (BCUS98127)", "override: update of another game refused")

    # --- EBOOT
    bad = elf(os.path.join(root, "badelf", "EBOOT.elf"), machine=62)
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", eboot=bad)), "tods"))
    check(r and r["state"] == "needs_attention" and r["reason"] == "The file you chose is not a PS3 EBOOT.elf"
          and r["suggested"] == {"eboot": fe}, "override: x86 ELF as EBOOT refused, found EBOOT suggested")
    good = elf(os.path.join(root, "myelf", "EBOOT.elf"))
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", eboot=good)), "tods"))
    check(r and r["state"] == "ready_to_build" and r["files"]["eboot"] == good and r["sources"]["eboot"] == "override",
          "override: an EBOOT.elf anywhere is accepted and wins")
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", eboot=os.path.join(root, "nope.elf"))), "tods"))
    check(r and r["reason"] == "The EBOOT.elf you chose is missing", "override: missing EBOOT refused")

    # --- saves
    sv = os.path.join(root, "mysaves")
    os.makedirs(sv)
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", saves=sv)), "tods"))
    check(r and r["state"] == "ready_to_build" and r["files"].get("saves") == sv and r["sources"]["saves"] == "override",
          "override: save folder recorded")
    r = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", saves=sv + "_x")), "tods"))
    check(r and r["state"] == "needs_attention" and r["reason"] == "The save folder you chose is missing", "override: missing save folder")

    # --- forbidden paths are refused before any open
    ex_elf = elf(os.path.join(root, "x", "exdata", "EBOOT.elf"))
    ex_disc = disc(os.path.join(root, "x", "dev_flash"), "d", "BCUS98127")
    opened = []
    orig = (catalog._elf_ok, catalog.sfo_from_folder, catalog._sha256)
    catalog._elf_ok = lambda p: opened.append(p) or orig[0](p)
    catalog.sfo_from_folder = lambda p: opened.append(p) or orig[1](p)
    catalog._sha256 = lambda p: opened.append(p) or orig[2](p)
    try:
        r1 = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", eboot=ex_elf, disc=ex_disc)), "tods"))
        rap = guard(lambda: by_id(catalog.scan([found], with_ov(base, "tods", eboot=os.path.join(root, "k.rap"))), "tods"))
    finally:
        catalog._elf_ok, catalog.sfo_from_folder, catalog._sha256 = orig
    check(r1 and r1["state"] == "needs_attention" and "never read" in r1["reason"] and rap and "never read" in rap["reason"]
          and not [o for o in opened if forbidden_touch(o)], "override: path into exdata / dev_flash / .rap refused, not opened")

    # --- round trip: overrides and locations survive rescan + write/read
    libf = os.path.join(root, "out", "library.json")
    ov = {"disc": tc, "eboot": good}
    lib0 = catalog.make_library(base)
    by_id(lib0["games"], "tods")["overrides"] = ov
    lib0["locations"] = {"library_folders": [found], "excluded": [os.path.join(found, "skip")], "rpcs3_auto": False,
                         "build": os.path.join(root, "b")}
    catalog.write_library(libf, lib0)
    back = catalog.read_library(libf)
    games = catalog.scan([], back)
    lib1 = guard(lambda: catalog.make_library(games, prior=back))
    catalog.write_library(libf, lib1 or catalog.make_library(games))
    back2 = catalog.read_library(libf)
    check(back2 and back2.get("locations") == lib0["locations"], "library.json: locations survive a rescan and write/read")
    check(back2 and by_id(back2["games"], "tods").get("overrides") == ov
          and by_id(back2["games"], "tods")["files"]["disc"] == tc, "library.json: overrides survive a rescan and write/read")
    kid = catalog.make_library(catalog.scan([found]))
    col = by_id(kid["games"], "collection")
    col["children"][1]["overrides"] = {"disc": gone}
    res = catalog.scan([found], kid)
    c2 = by_id(res, "collection")["children"][1]
    check(c2.get("overrides") == {"disc": gone} and c2["state"] == "needs_attention", "override: a Collection child keeps and uses its override")
    old = catalog.read_library(os.path.join(HERE, "..", "..", "data", "titles", "library.example.json"))
    check(old and len(catalog.scan([], old)) == 8, "library.json: the example file (with the new keys) loads as schema 1")

    # --- locations in scan
    only = catalog.make_library([])
    only["locations"] = {"library_folders": [found]}
    r = guard(lambda: by_id(catalog.scan([], only), "tods"))
    check(r and r["state"] == "ready_to_build", "locations: library_folders are scanned without a CLI folder")
    skip = os.path.join(root, "lib2", "skip")
    disc(skip, "tods_skipped", "BCUS98127")
    disc(os.path.join(root, "lib2"), "acit_ok", "BCUS98124")
    lib2 = {"locations": {"library_folders": [os.path.join(root, "lib2")], "excluded": [skip]}}
    res = guard(lambda: catalog.scan([], lib2))
    check(res and by_id(res, "tods")["state"] == "not_added" and by_id(res, "acit")["state"] != "not_added",
          "locations: an excluded subfolder is skipped, its sibling found")
    lib2["locations"]["excluded"] = []
    check(guard(lambda: by_id(catalog.scan([], lib2), "tods")["state"] != "not_added"), "locations: without the exclusion it is found")

    r3 = os.path.join(root, "r3")
    wr(os.path.join(r3, "config", "games.yml"), ('BCUS98127: "%s"\n' % tc).encode())
    os.makedirs(os.path.join(r3, "dev_hdd0"))
    on = {"locations": {"rpcs3_folders": [r3]}}
    off = {"locations": {"rpcs3_folders": [r3], "rpcs3_auto": False}}
    r = guard(lambda: by_id(catalog.scan([], on), "tods"))
    check(r and r["files"]["disc"] == tc and r["sources"]["disc"] == "rpcs3", "locations: rpcs3_folders are read (auto absent = on)")
    r = guard(lambda: by_id(catalog.scan([], off), "tods"))
    check(r and r["files"]["disc"] == tc, "locations: rpcs3_auto false still reads the folders the player named")
    fr = []
    orig_find = catalog.find_rpcs3
    catalog.find_rpcs3 = lambda *a, **k: fr.append(1) or []
    try:
        guard(lambda: catalog.scan([], off, find=True))
        n_off = len(fr)
        guard(lambda: catalog.scan([], on, find=True))
    finally:
        catalog.find_rpcs3 = orig_find
    check(n_off == 0 and len(fr) == 1, "locations: find_rpcs3 is skipped when rpcs3_auto is false")

    # --- CLI round trips
    lf = os.path.join(root, "cli", "library.json")

    def cli(*args):
        out, err = io.StringIO(), io.StringIO()
        try:
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                rc = catalog.main(list(args))
        except SystemExit as e:
            rc = e.code
        return rc, out.getvalue(), err.getvalue()

    rc, o, _ = cli("set-location", "--library", lf, "rpcs3_auto", "false")
    check(rc == 0 and catalog.read_library(lf)["locations"] == {"rpcs3_auto": False}, "CLI: set-location rpcs3_auto false is written")
    rc, o, _ = cli("set-location", "--library", lf, "library_folders", found, "--add")
    lib = catalog.read_library(lf)
    check(rc == 0 and lib["locations"]["library_folders"] == [os.path.abspath(found)] and "tods" in o
          and by_id(lib["games"], "tods")["state"] == "ready_to_build", "CLI: set-location --add stores the folder and rescans")
    rc, o, _ = cli("set-location", "--library", lf, "library_folders", found, "--add")
    check(catalog.read_library(lf)["locations"]["library_folders"] == [os.path.abspath(found)], "CLI: adding a folder twice keeps one")
    rc, o, _ = cli("override", "--library", lf, "--game", "tods", "--slot", "disc", tc)
    lib = catalog.read_library(lf)
    t = by_id(lib["games"], "tods")
    check(rc == 0 and t["overrides"] == {"disc": os.path.abspath(tc)} and t["files"]["disc"] == tc
          and "override" in o and "ready_to_build" in o, "CLI: override stores the path, rescans and prints the new state")
    check(lib["locations"]["rpcs3_auto"] is False, "CLI: an override keeps the locations block")
    rc, o, _ = cli("override", "--library", lf, "--game", "tods", "--slot", "update", gone)
    t = by_id(catalog.read_library(lf)["games"], "tods")
    check(rc == 0 and t["overrides"]["update"] == gone and "disc" in t["overrides"] and t["state"] == "needs_attention"
          and "needs_attention" in o, "CLI: a second slot is added; a bad one prints needs_attention")
    rc, o, _ = cli("clear-override", "--library", lf, "--game", "tods", "--slot", "update")
    rc, o, _ = cli("clear-override", "--library", lf, "--game", "tods", "--slot", "disc")
    t = by_id(catalog.read_library(lf)["games"], "tods")
    check(rc == 0 and "overrides" not in t and t["files"]["disc"] == td and t["state"] == "ready_to_build",
          "CLI: clear-override returns the slot to detection")
    rc, o, _ = cli("override", "--library", lf, "--game", "rc2", "--slot", "disc", gone)
    check(rc == 0 and by_id(by_id(catalog.read_library(lf)["games"], "collection")["children"], "rc2")["overrides"]["disc"] == gone,
          "CLI: a Collection child can be overridden by id")
    rc, o, _ = cli("set-location", "--library", lf, "library_folders", found, "--remove")
    check(rc == 0 and "library_folders" not in catalog.read_library(lf)["locations"], "CLI: --remove drops the folder (and the empty key)")
    rc, o, _ = cli("set-location", "--library", lf, "build", os.path.join(root, "bld"))
    check(rc == 0 and catalog.read_library(lf)["locations"]["build"] == os.path.abspath(os.path.join(root, "bld")), "CLI: set-location sets a path key")
    rc, o, _ = cli("set-location", "--library", lf, "build", "--reset")
    check(rc == 0 and "build" not in catalog.read_library(lf)["locations"], "CLI: --reset drops the key back to the default")
    before = open(lf, "rb").read()
    rc1, _, e1 = cli("override", "--library", lf, "--game", "nosuch", "--slot", "disc", tc)
    rc2, _, e2 = cli("override", "--library", lf, "--game", "tods", "--slot", "icon", tc)
    rc3, _, e3 = cli("set-location", "--library", lf, "nokey", "x")
    rc4, _, e4 = cli("set-location", "--library", lf, "library_folders", found)
    rc5, _, e5 = cli("set-location", "--library", lf, "build", os.path.join(root, "exdata", "b"))
    check(rc1 == 2 and "nosuch" in e1 and rc2 == 2 and rc3 == 2 and rc4 == 2 and "--add" in e4 and rc5 == 2,
          "CLI: unknown game / slot / key, list key without --add, forbidden path -> exit 2 with a message")
    check(open(lf, "rb").read() == before, "CLI: a refused command leaves library.json untouched")

    # --- launcher preferences: prefs block, no rescan, kept by rescans and other edits
    gen = catalog.read_library(lf)["generated"]
    rc, o, _ = cli("set-pref", "--library", lf, "theme", "dark")
    lib = catalog.read_library(lf)
    check(rc == 0 and lib["prefs"] == {"theme": "dark"} and lib["generated"] == gen, "CLI: set-pref theme dark is written without a rescan")
    rc, o, _ = cli("set-location", "--library", lf, "build", os.path.join(root, "bld"))
    rc2, _, _ = cli("scan", "--library", lf, found)
    check(rc == 0 and rc2 == 0 and catalog.read_library(lf).get("prefs") == {"theme": "dark"}, "CLI: set-location and scan keep the prefs block")
    before = open(lf, "rb").read()
    rc1, _, e1 = cli("set-pref", "--library", lf, "theme", "purple")
    rc2, _, e2 = cli("set-pref", "--library", lf, "theme")
    check(rc1 == 2 and "system, light, dark" in e1 and rc2 == 2 and open(lf, "rb").read() == before,
          "CLI: set-pref with a bad or no value -> exit 2, file untouched")
    rc, o, _ = cli("set-pref", "--library", lf, "theme", "--reset")
    check(rc == 0 and "prefs" not in catalog.read_library(lf), "CLI: set-pref --reset drops the key (and the empty block)")


if __name__ == "__main__":
    sys.exit(main())
