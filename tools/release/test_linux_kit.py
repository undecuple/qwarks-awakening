#!/usr/bin/env python3
"""Host tests for linux_kit.py (the Linux launcher's build tools). No network: a temporary manifest whose entries are
small archives served from file://, with the real manifest checked for the right entries. Checked: the entries are the
toolchain.json ones with "linux": "build" plus sdl3; status (missing / ready); fetch (PROGRESS per entry, archives
deleted afterwards, ninja made executable, only what is missing is fetched again); a wrong SHA-256 fails with a REASON;
Stop; the trees it makes are what firstrun.resolve_toolchain takes; catalog.py forwards `linux-kit`.
"""
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from unittest.mock import patch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import linux_kit as lk  # noqa: E402
import firstrun as fr  # noqa: E402

FAILS = []


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:1000]))
    if not cond:
        FAILS.append(what)


def sha(p):
    with open(p, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def tarball(path, top, files):
    src = tempfile.mkdtemp()
    for rel, mode in files.items():
        p = os.path.join(src, top, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "w").write("#!/bin/sh\n")
        os.chmod(p, mode)
    with tarfile.open(path, "w:xz") as t:
        t.add(os.path.join(src, top), arcname=top)
    shutil.rmtree(src)
    return path


def run(tmp):
    real = {c["name"] for c in lk.entries()}
    check(real == {"llvm-mingw-linux", "cmake-linux", "ninja-linux", "sdl3"},
          "the Linux build tools: llvm-mingw-linux, cmake-linux, ninja-linux, sdl3 (pinned in toolchain.json)", real)
    check(lk.default_dest({"XDG_DATA_HOME": "/x/data"}) == "/x/data/qwarks-awakening/build-tools"
          and lk.default_dest({"XDG_DATA_HOME": "rel"}).endswith("/.local/share/qwarks-awakening/build-tools"),
          "default folder: $XDG_DATA_HOME/qwarks-awakening/build-tools (a relative XDG value is ignored)")
    srv = os.path.join(tmp, "srv")
    os.makedirs(srv)
    lm = tarball(os.path.join(srv, "llvm.tar.xz"), "llvm-mingw-x", {"bin/x86_64-w64-mingw32-clang": 0o755})
    cm = tarball(os.path.join(srv, "cmake.tar.xz"), "cmake-x", {"bin/cmake": 0o755})
    nj = os.path.join(srv, "ninja-linux.zip")
    with zipfile.ZipFile(nj, "w") as z:
        z.writestr("ninja", "#!/bin/sh\n")
    sd = os.path.join(srv, "sdl.zip")
    with zipfile.ZipFile(sd, "w") as z:
        z.writestr("SDL3-x/x86_64-w64-mingw32/bin/SDL3.dll", "MZ")

    def ent(name, path, archive, **kw):
        e = {"name": name, "version": "1", "url": "file://" + path, "sha256": sha(path), "archive": archive,
             "license": "x", "release_page": "https://example.org/", "linux": "build"}
        e.update(kw)
        return e
    man = os.path.join(tmp, "toolchain.json")
    comps = [ent("llvm-mingw-linux", lm, "tar", strip_top=True), ent("cmake-linux", cm, "tar", strip_top=True),
             ent("ninja-linux", nj, "zip", strip_top=False, exec=["ninja"]),
             dict(ent("sdl3", sd, "zip", strip_top=True), linux=None),
             {"name": "llvm-mingw", "version": "1", "url": "file:///nope", "sha256": "0" * 64, "archive": "zip",
              "license": "x", "release_page": "https://example.org/"}]
    json.dump({"format": 1, "components": comps}, open(man, "w"))
    opener = lambda u: open(u[len("file://"):], "rb")
    dest = os.path.join(tmp, "data", "qwarks-awakening", "build-tools")
    st = lk.status(dest, man)
    check(not st["ready"] and set(st["missing"]) == {"llvm-mingw-linux", "cmake-linux", "ninja-linux", "sdl3"},
          "status: nothing fetched = not ready, four missing (the Windows entry is not one of them)", st)
    stopf = os.path.join(tmp, "stop")
    open(stopf, "w").close()
    out = io.StringIO()
    check(lk.fetch(dest, out, stopf, man, opener) == 130 and "step=cancelled" in out.getvalue(), "fetch: Stop before an entry")
    os.remove(stopf)
    out = io.StringIO()
    rc = lk.fetch(dest, out, None, man, opener)
    st = lk.status(dest, man)
    o = out.getvalue()
    check(rc == 0 and st["ready"] and "PROGRESS step=1/4 name=llvm-mingw-linux" in o and "RESULT ok=1" in o,
          "fetch: every entry, PROGRESS per entry, ready", o)
    check(not os.path.exists(os.path.join(dest, "downloads", "cmake-linux")), "fetch: the archives are deleted afterwards")
    check(os.access(os.path.join(dest, "trees", "ninja-linux", "ninja"), os.X_OK), "fetch: ninja from the zip is executable")
    l, c, n = fr.resolve_toolchain(tmp, st["toolchain"])
    check(l.endswith("trees/llvm-mingw-linux") and c.endswith("cmake-linux/bin/cmake") and n.endswith("ninja-linux/ninja"),
          "firstrun.resolve_toolchain takes the trees", (l, c, n))
    with patch.object(lk.ft, "MANIFEST", man):
        out = io.StringIO()
        check(lk.main(["toolchain", "--dest", dest], out=out) == 0 and out.getvalue().strip() == st["toolchain"],
              "toolchain: prints the trees folder catalog.py build passes as --toolchain")
        check(lk.main(["toolchain", "--dest", os.path.join(tmp, "none")], out=io.StringIO()) == 1,
              "toolchain: exit 1 when the tools are not in place (catalog.py build then passes nothing)")
        check(lk.main(["check", "--dest", dest], out=io.StringIO()) == 0, "check: fetched tools that start pass")
        ninja = os.path.join(dest, "trees", "ninja-linux", "ninja")
        keep = open(ninja, "rb").read()
        with open(ninja, "w") as f:   # as on NixOS without nix-ld: the interpreter it names is not there
            f.write("#!/nonexistent/lib64/ld-linux-x86-64.so.2\n")
        out = io.StringIO()   # the pre-check catalog.py build runs before firstrun.py
        check(lk.main(["check", "--dest", dest], out=out) == 1 and out.getvalue().startswith("REASON The Linux build tools")
              and "ninja could not be started" in out.getvalue(), "check: REASON and exit 1 when a fetched tool cannot start",
              out.getvalue())
        with open(ninja, "wb") as f:
            f.write(keep)
        check(lk.main(["check", "--dest", os.path.join(tmp, "none")], out=io.StringIO()) == 0,
              "check: nothing to check when the tools are not in place")
    out = io.StringIO()
    check(lk.fetch(dest, out, None, man, lambda u: 1 / 0) == 0 and "PROGRESS step=0/0" in out.getvalue(),
          "fetch again: nothing missing, nothing downloaded")
    # a pin that changed (new release) is fetched again; a wrong hash fails
    comps[1]["sha256"] = "1" * 64
    json.dump({"format": 1, "components": comps}, open(man, "w"))
    check(lk.status(dest, man)["missing"] == ["cmake-linux"], "status: an entry with another pinned hash is missing")
    out = io.StringIO()
    check(lk.fetch(dest, out, None, man, opener) == 1 and "REASON cmake-linux 1 could not be downloaded or did not match"
          in out.getvalue() and "RESULT ok=0 step=cmake-linux" in out.getvalue(), "fetch: a wrong SHA-256 fails with a REASON")
    # the pre-check: each tool started once; one the system cannot start (a missing ELF interpreter, as on NixOS without
    # nix-ld, gives the same ENOENT as the missing script interpreter used here) gets the NixOS message
    pt = os.path.join(tmp, "probe-trees")
    for tree, name in lk.PROBES:
        p = os.path.join(pt, tree, "bin", name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w") as f:
            f.write("#!/bin/sh\necho 'version 1'\n")
        os.chmod(p, 0o755)
    check(lk.start_problem(pt, nixos=True) is None, "check: tools that start pass")
    with open(os.path.join(pt, "cmake-linux", "bin", "cmake"), "w") as f:
        f.write("#!/nonexistent/lib64/ld-linux-x86-64.so.2\n")
    why = lk.start_problem(pt, nixos=True)
    check(why and "cmake could not be started" in why and "nix-ld" in why and "steam-run qwarks-awakening" in why,
          "check: a tool the system cannot start on NixOS names nix-ld and steam-run", why)
    why = lk.start_problem(pt, nixos=False)
    check(why and "nix-ld" not in why and "Download them again" in why, "check: elsewhere it says to download them again", why)
    why = lk.start_problem(pt, nixos=True, run=lambda *a, **k: subprocess.CompletedProcess(a, 127, b"", b""))
    check(why and "exit code 127" in why, "check: exit code 127 counts as not started", why)
    # catalog.py forwards
    r = subprocess.run([sys.executable, os.path.join(HERE, "catalog.py"), "linux-kit", "status", "--dest", dest],
                       capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.startswith("STATUS {"), "catalog.py linux-kit status -> linux_kit.py", r.stdout + r.stderr)


def main():
    if not sys.platform.startswith("linux"):
        print("test_linux_kit: SKIP (Linux only)")
        return 0
    tmp = tempfile.mkdtemp(prefix="test-linux-kit-")
    try:
        run(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_linux_kit:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
