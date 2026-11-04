#!/usr/bin/env python3
"""Host test for tools/release/toolchain.json and fetch_toolchain.py (from the Killzone 3 port's test of the same
tool, same author, MIT). No network: the "downloads" are file:// URLs to zips built in a temp folder. Exit 0 = PASS.

    python3 tools/release/test_fetch_toolchain.py

Cases: the real manifest passes --check --strict and pins llvm-mingw, SDL3's MinGW devel package, CMake, Ninja and the
Python embeddable package over https; placeholders expand; the validator refuses http URLs, bad hashes, duplicate names, a bad archive kind,
TO-FILL / TO-CONFIRM under --strict. Fetch: a TO-FILL entry is refused without --fill (download deleted) and filled
with it (hash written, the rest of the file unchanged); a later hash mismatch is refused and deleted; a published-list
mismatch is refused; zip-slip, symlink, case-clash and strip_top problems are refused; a Linux tarball keeps a link
inside its tree and refuses links out of it, absolute links, ".." members and two top folders; --only picks one entry.
"""
import hashlib
import io
import json
import os
import pathlib
import shutil
import stat
import tarfile
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fetch_toolchain as ft  # noqa: E402

FAILS = []
SCHEMES = ("https", "file")


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:800]))
    if not cond:
        FAILS.append(what)


def make_zip(path, members, symlink=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with zipfile.ZipFile(path, "w") as z:
        for name, data in members.items():
            z.writestr(name, data)
        if symlink:
            zi = zipfile.ZipInfo(symlink)
            zi.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(zi, "target")
    return path


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def url(p):
    return pathlib.Path(os.path.abspath(p)).as_uri()   # file:///C:/... on Windows too


def file_opener(u):
    """urllib refuses nothing for file://, but keep the test independent of proxies and handlers."""
    from urllib.request import url2pathname
    from urllib.parse import urlparse
    return open(url2pathname(urlparse(u).path), "rb")


def fetch(args):
    buf = io.StringIO()
    rc = ft.main(args, out=buf, opener=file_opener, schemes=SCHEMES)
    return rc, buf.getvalue()


def write_manifest(path, comps):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"format": 1, "about": "test", "components": comps}, f, indent=1)
        f.write("\n")


def comp(name, u, archive="zip", **kw):
    c = {"name": name, "version": "1.0", "pin": "confirmed", "release_page": "https://example.org/",
         "url": u, "sha256": "TO-FILL", "archive": archive, "license": "MIT"}
    if archive == "zip":
        c["strip_top"] = True
    c.update(kw)
    return c


def test_real_manifest():
    m = ft.load()
    check(not ft.validate(m, strict=True), "real toolchain.json passes --strict", ft.validate(m, strict=True))
    names = {c["name"] for c in m["components"]}
    check(names == {"llvm-mingw", "llvm-mingw-linux", "sdl3", "cmake", "ninja", "python"},
          "manifest pins llvm-mingw (Windows and Linux hosts), sdl3, cmake, ninja, python",
          names)
    for c in m["components"]:
        e = ft.expand(c)
        check(e["url"].startswith(("https://github.com/", "https://www.python.org/ftp/")) and "{" not in e["url"],
              f"{c['name']}: https URL from the project's own release site, expanded", e["url"])
    py = ft.expand([c for c in m["components"] if c["name"] == "python"][0])
    check(py["url"].endswith(f"python-{py['version']}-embed-amd64.zip") and not py.get("strip_top"),
          "python is the official Windows x64 embeddable package, unpacked as is", py["url"])
    sdl = ft.expand([c for c in m["components"] if c["name"] == "sdl3"][0])
    check(sdl["url"].endswith(f"SDL3-devel-{sdl['version']}-mingw.zip"), "sdl3 is the official MinGW devel package", sdl["url"])
    cm = ft.expand([c for c in m["components"] if c["name"] == "cmake"][0])
    check(cm.get("sha256_list", "").endswith("-SHA-256.txt"), "cmake is checked against Kitware's SHA-256 list")
    buf = io.StringIO()
    check(ft.main(["--check", "--strict"], out=buf) == 0, "fetch_toolchain.py --check --strict exits 0", buf.getvalue())


def test_validate():
    good = comp("a", "https://example.org/a-{version}.zip", sha256="0" * 64)
    check(ft.validate({"format": 1, "components": [good]}) == [], "a good entry validates")
    check(ft.expand(good)["url"] == "https://example.org/a-1.0.zip", "{version} expands")
    cases = {
        "http URL": comp("a", "http://example.org/a.zip"),
        "bad hash": comp("a", "https://example.org/a.zip", sha256="ABC"),
        "bad archive": comp("a", "https://example.org/a.rar", archive="rar"),
        "file without file_name": comp("a", "https://example.org/a.txt", archive="file"),
        "bad name": comp("A b", "https://example.org/a.zip"),
        "missing field": {k: v for k, v in comp("a", "https://example.org/a.zip").items() if k != "license"},
        "bad pin": comp("a", "https://example.org/a.zip", pin="maybe"),
    }
    for what, c in cases.items():
        check(ft.validate({"format": 1, "components": [c]}) != [], f"validate refuses: {what}")
    dup = [good, dict(good)]
    check(any("duplicate" in p for p in ft.validate({"format": 1, "components": dup})), "validate refuses duplicate names")
    check(ft.validate({"format": 2, "components": [good]}) != [], "validate refuses another format")
    tf = comp("a", "https://example.org/a.zip")
    check(ft.validate({"format": 1, "components": [tf]}) == [] and
          ft.validate({"format": 1, "components": [tf]}, strict=True) != [], "TO-FILL allowed, refused by --strict")
    tc = comp("a", "https://example.org/a.zip", sha256="0" * 64, pin="TO-CONFIRM")
    check(ft.validate({"format": 1, "components": [tc]}, strict=True) != [], "TO-CONFIRM refused by --strict")


def test_fetch(tmp):
    z = make_zip(os.path.join(tmp, "src", "tool-1.0.zip"), {"tool-1.0/bin/tool.exe": b"MZ", "tool-1.0/LICENSE": b"MIT"})
    man = os.path.join(tmp, "toolchain.json")
    cache = os.path.join(tmp, "cache")
    write_manifest(man, [comp("tool", url(z))])
    before = open(man, encoding="utf-8").read()
    rc, out = fetch(["--manifest", man, "--cache", cache])
    check(rc == 1 and "TO-FILL" in out and not os.path.exists(os.path.join(cache, "downloads", "tool", "tool-1.0.zip")),
          "TO-FILL refused without --fill, download deleted", out)
    check(open(man, encoding="utf-8").read() == before, "manifest unchanged without --fill")
    rc, out = fetch(["--manifest", man, "--cache", cache, "--fill"])
    data = json.load(open(man, encoding="utf-8"))
    check(rc == 0 and data["components"][0]["sha256"] == sha(z), "--fill writes the download's hash", out)
    check(open(man, encoding="utf-8").read() == before.replace('"TO-FILL"', f'"{sha(z)}"'), "--fill changes only the hash")
    check(os.path.isfile(os.path.join(cache, "trees", "tool", "bin", "tool.exe")), "strip_top unpacks under trees/<name>/")
    rc, out = fetch(["--manifest", man, "--cache", cache])
    check(rc == 0, "a filled entry fetches again", out)
    make_zip(z, {"tool-1.0/bin/tool.exe": b"MZ changed"})
    rc, out = fetch(["--manifest", man, "--cache", cache])
    check(rc == 1 and "mismatch" in out and not os.path.exists(os.path.join(cache, "downloads", "tool", "tool-1.0.zip")),
          "a changed download is refused and deleted", out)

    # published SHA-256 list (CMake's form)
    z2 = make_zip(os.path.join(tmp, "src", "c.zip"), {"c/x": b"x"})
    lst = os.path.join(tmp, "src", "SHA-256.txt")
    with open(lst, "w") as f:
        f.write("0" * 64 + "  c.zip\n")
    write_manifest(man, [comp("c", url(z2), sha256=sha(z2), sha256_list=url(lst))])
    rc, out = fetch(["--manifest", man, "--cache", cache])
    check(rc == 1 and "published" in out, "a hash that differs from the published list is refused", out)
    with open(lst, "w") as f:
        f.write(sha(z2) + "  c.zip\n")
    rc, out = fetch(["--manifest", man, "--cache", cache])
    check(rc == 0, "a hash in the published list passes", out)

    # unsafe zips
    for what, members, link in (("zip-slip", {"t/../../evil": b"x"}, None),
                                ("absolute path", {"/abs/evil": b"x"}, None),
                                ("symlink", {"t/a": b"x"}, "t/link"),
                                ("case clash", {"t/LICENSE.TXT": b"a", "t/License.txt": b"b"}, None),
                                ("two top folders", {"t/a": b"x", "u/b": b"y"}, None)):
        bz = make_zip(os.path.join(tmp, "bad", what.replace(" ", "_") + ".zip"), members, link)
        write_manifest(man, [comp("bad", url(bz), sha256=sha(bz))])
        rc, out = fetch(["--manifest", man, "--cache", cache])
        check(rc == 1 and "unpack" in out, f"unpack refuses: {what}", out)

    # Linux tarball (the CI cross build's llvm-mingw): a link inside the tree is kept, escapes are refused
    if hasattr(tarfile, "data_filter") and hasattr(os, "symlink"):
        def make_tar(path, files, links=()):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with tarfile.open(path, "w:xz") as t:
                for name, data in files.items():
                    ti = tarfile.TarInfo(name)
                    ti.size = len(data)
                    t.addfile(ti, io.BytesIO(data))
                for name, target in links:
                    ti = tarfile.TarInfo(name)
                    ti.type, ti.linkname = tarfile.SYMTYPE, target
                    t.addfile(ti)
            return path
        good = make_tar(os.path.join(tmp, "src", "tc.tar.xz"), {"tc/bin/clang": b"#!/bin/sh\n"}, [("tc/bin/clang++", "clang")])
        write_manifest(man, [comp("tc", url(good), archive="tar", sha256=sha(good), strip_top=True)])
        rc, out = fetch(["--manifest", man, "--cache", cache])
        link = os.path.join(cache, "trees", "tc", "bin", "clang++")
        check(rc == 0 and os.path.islink(link) and os.readlink(link) == "clang", "tar: strip_top, a link inside the tree kept", out)
        for what, files, links in (("a link out of the tree", {"tc/a": b"x"}, [("tc/evil", "../../../etc/passwd")]),
                                   ("an absolute link", {"tc/a": b"x"}, [("tc/evil", "/etc/passwd")]),
                                   ("a .. member", {"tc/../evil": b"x"}, []),
                                   ("two top folders", {"tc/a": b"x", "td/b": b"y"}, [])):
            bt = make_tar(os.path.join(tmp, "badtar", what.replace(" ", "_") + ".tar.xz"), files, links)
            write_manifest(man, [comp("badtar", url(bt), archive="tar", sha256=sha(bt), strip_top=True)])
            rc, out = fetch(["--manifest", man, "--cache", cache])
            check(rc == 1 and "unpack" in out, f"tar: refuses {what}", out)
    else:
        print("skip tar cases (no tarfile extraction filters or no symlinks here)")

    # a plain file and --only
    lic = os.path.join(tmp, "src", "COPYING")
    with open(lic, "w") as f:
        f.write("licence")
    write_manifest(man, [comp("lic", url(lic), archive="file", file_name="X-COPYING.txt", sha256=sha(lic)),
                         comp("other", url(os.path.join(tmp, "nope.zip")), sha256="0" * 64)])
    rc, out = fetch(["--manifest", man, "--cache", cache, "--only", "lic"])
    check(rc == 0 and os.path.isfile(os.path.join(cache, "trees", "lic", "X-COPYING.txt")), "--only fetches one file entry", out)
    rc, out = fetch(["--manifest", man, "--cache", cache, "--only", "nosuch"])
    check(rc == 2, "--only with an unknown name is a usage error", out)


def main():
    test_real_manifest()
    test_validate()
    tmp = tempfile.mkdtemp()
    try:
        test_fetch(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_fetch_toolchain:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
