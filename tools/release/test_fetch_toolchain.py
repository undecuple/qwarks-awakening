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
from unittest.mock import patch

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


def tree_snapshot(root):
    """Owned synthetic tree bytes/types/mtimes; reading does not compare atime."""
    root = pathlib.Path(root)
    return [(str(p.relative_to(root)), stat.S_IFMT(p.lstat().st_mode), p.lstat().st_mtime_ns,
             p.read_bytes() if p.is_file() else None)
            for p in sorted([root, *root.rglob("*")])]


def test_publication(tmp):
    """Actual extraction/publication with injected filesystem errors, never network."""
    root = pathlib.Path(tmp) / "publication"
    root.mkdir()
    zip_path = root / "source.zip"
    make_zip(str(zip_path), {"top/new.txt": b"NEW-TREE"})
    tar_path = root / "source.tar"
    if hasattr(tarfile, "data_filter"):
        with tarfile.open(tar_path, "w") as archive:
            item = tarfile.TarInfo("top/new.txt")
            item.size = 8
            archive.addfile(item, io.BytesIO(b"NEW-TREE"))
    plain_path = root / "plain.txt"
    plain_path.write_bytes(b"NEW-TREE")
    formats = [("zip-strip", lambda dest: ft.unpack_zip(str(zip_path), str(dest), True), "new.txt"),
               ("zip-full", lambda dest: ft.unpack_zip(str(zip_path), str(dest), False), "top/new.txt"),
               ("file", lambda dest: ft.unpack_file(str(plain_path), str(dest), "new.txt"), "new.txt")]
    if hasattr(tarfile, "data_filter"):
        formats += [("tar-strip", lambda dest: ft.unpack_tar(str(tar_path), str(dest), True), "new.txt"),
                    ("tar-full", lambda dest: ft.unpack_tar(str(tar_path), str(dest), False), "top/new.txt")]
    else:
        check(False, "publication tar controls incomplete: Python extraction filter unavailable")
    real_replace = os.replace
    for kind, extract, relative in formats:
        for scenario in ("success", "publish-fails", "restore-fails", "backup-fails", "fresh-fails", "cleanup-fails"):
            parent = root / kind / scenario
            parent.mkdir(parents=True)
            dest = parent / "tool"
            if scenario != "fresh-fails":
                (dest / "bin").mkdir(parents=True)
                (dest / "bin/old.txt").write_bytes(b"OLD-VALID-TREE")
                before = tree_snapshot(dest)
            unowned = parent / "tool.partial"
            unowned.mkdir()
            (unowned / "owner.txt").write_bytes(b"UNRELATED-PREEXISTING")
            def replace(source, target):
                source, target = pathlib.Path(source), pathlib.Path(target)
                if scenario == "backup-fails" and source == dest:
                    raise OSError("injected backup move failure")
                if target == dest and scenario in ("publish-fails", "restore-fails", "fresh-fails"):
                    if source.name != "previous" or scenario == "restore-fails":
                        raise OSError("injected restore failure" if source.name == "previous" else "injected publication failure")
                return real_replace(source, target)
            real_rmtree = shutil.rmtree
            def rmtree(path, *args, **kwargs):
                if scenario == "cleanup-fails" and ".backup-" in pathlib.Path(path).name:
                    raise OSError("injected backup cleanup failure")
                return real_rmtree(path, *args, **kwargs)
            error = None
            try:
                with patch.object(ft.os, "replace", side_effect=replace), patch.object(ft.shutil, "rmtree", side_effect=rmtree):
                    extract(dest)
            except OSError as caught:
                error = caught
            label = kind + " " + scenario
            backups = list(parent.glob(".tool.backup-*"))
            stages = list(parent.glob(".tool.partial-*"))
            check((unowned / "owner.txt").read_bytes() == b"UNRELATED-PREEXISTING", label + ": never deletes unowned .partial")
            if scenario == "success":
                check(error is None and (dest / relative).read_bytes() == b"NEW-TREE" and not (dest / "bin/old.txt").exists(),
                      label + ": actual replacement succeeds", error)
                check(not backups and not stages, label + ": owned temporaries cleaned")
            elif scenario in ("publish-fails", "backup-fails"):
                check(error is not None and dest.is_dir() and tree_snapshot(dest) == before,
                      label + ": previous tree preserved exactly", error)
                check(not backups and not stages, label + ": rollback/failed backup cleans only owned temporaries")
                if scenario == "publish-fails":
                    check("previous tree restored" in str(error), label + ": failure truthfully reports restore")
            elif scenario == "restore-fails":
                check(isinstance(error, ft.PublicationError) and error.preserve_temporary and not dest.exists(),
                      label + ": restore failure stays failure, no success assertion", error)
                check(len(backups) == 1 and tree_snapshot(backups[0] / "previous") == before,
                      label + ": old tree recovery data retained")
                prepared = list(parent.glob(".tool.partial-*/**/new.txt"))
                check(len(stages) == 1 and len(prepared) == 1 and prepared[0].read_bytes() == b"NEW-TREE",
                      label + ": prepared replacement retained")
                check(str(backups[0] / "previous") in str(error) and "prepared replacement retained at" in str(error),
                      label + ": both recovery locations disclosed", error)
            elif scenario == "fresh-fails":
                check(error is not None and not dest.exists() and not backups and not stages,
                      label + ": failed first install leaves no partial published tree", error)
            else:
                check(error is not None and "new cache tree published" in str(error) and
                      (dest / relative).read_bytes() == b"NEW-TREE" and len(backups) == 1 and not stages,
                      label + ": backup cleanup failure truthfully reports published state", error)

    # Extraction rejection/failure must also preserve the existing cache.
    dest = root / "invalid/tool"
    dest.mkdir(parents=True)
    (dest / "old.txt").write_bytes(b"OLD")
    before = tree_snapshot(dest)
    bad_zip = root / "bad.zip"
    make_zip(str(bad_zip), {"top/../../escape": b"BAD"})
    try:
        ft.unpack_zip(str(bad_zip), str(dest), True)
    except ValueError:
        pass
    else:
        check(False, "publication: unsafe ZIP was not rejected")
    check(tree_snapshot(dest) == before, "publication: unsafe archive preserves existing tree")
    malformed = root / "malformed.archive"
    malformed.write_bytes(b"not a ZIP or tar archive")
    for kind in ("zip", "tar"):
        try:
            getattr(ft, "unpack_" + kind)(str(malformed), str(dest), True)
        except (ValueError, zipfile.BadZipFile, tarfile.TarError):
            pass
        else:
            check(False, "publication: malformed " + kind + " was not rejected")
        check(tree_snapshot(dest) == before, "publication: malformed " + kind + " preserves old tree")
    with patch.object(ft.shutil, "copyfileobj", side_effect=OSError("injected extraction IO failure")):
        try:
            ft.unpack_zip(str(zip_path), str(dest), True)
        except OSError:
            pass
        else:
            check(False, "publication: ZIP extraction IO failure not raised")
    check(tree_snapshot(dest) == before and not list(dest.parent.glob(".tool.partial-*")),
          "publication: failed extraction preserves old cache and cleans owned staging")
    with patch.object(ft.shutil, "copyfile", side_effect=OSError("injected plain-file copy failure")):
        try:
            ft.unpack_file(str(plain_path), str(dest), "new.txt")
        except OSError:
            pass
        else:
            check(False, "publication: plain-file copy failure not raised")
    check(tree_snapshot(dest) == before and not list(dest.parent.glob(".tool.partial-*")),
          "publication: failed plain-file preparation preserves old cache")
    if hasattr(os, "symlink"):
        alias = root / "alias"
        try:
            alias.symlink_to(dest, target_is_directory=True)
        except OSError as error:
            check(False, "publication symlink-destination control incomplete", error)
        else:
            try:
                ft.unpack_zip(str(zip_path), str(alias), True)
            except OSError:
                pass
            else:
                check(False, "publication: destination symlink was accepted")
            check(alias.is_symlink() and tree_snapshot(dest) == before,
                  "publication: destination symlink refused without mutating target")

    # The public fetch path must return failure and leave fetched metadata old.
    cache = root / "fetch-cache"
    old = cache / "trees/tool"
    old.mkdir(parents=True)
    (old / "old.txt").write_bytes(b"OLD-FETCH")
    ft.write_fetched(str(cache), {"tool": {"version": "old"}})
    metadata = (cache / "fetched.json").read_bytes()
    def fail_fetch_publication(source, target):
        if pathlib.Path(target) == old and pathlib.Path(source).name != "previous":
            raise OSError("injected fetch publication failure")
        return real_replace(source, target)
    output = io.StringIO()
    with patch.object(ft.os, "replace", side_effect=fail_fetch_publication):
        ok = ft.fetch_one(comp("tool", url(str(zip_path)), sha256=sha(zip_path)), str(cache), False, "unused",
                          opener=file_opener, out=output)
    check(not ok and "previous tree restored" in output.getvalue() and (old / "old.txt").read_bytes() == b"OLD-FETCH",
          "fetch reports publication failure while restoring previous tree", output.getvalue())
    check((cache / "fetched.json").read_bytes() == metadata, "failed publication leaves fetched metadata unchanged")


def main():
    test_real_manifest()
    test_validate()
    tmp = tempfile.mkdtemp()
    try:
        test_fetch(tmp)
        test_publication(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_fetch_toolchain:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
