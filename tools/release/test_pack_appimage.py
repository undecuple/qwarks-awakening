#!/usr/bin/env python3
"""Host test for tools/release/pack_appimage.py (the Linux launcher AppImage) and for the engine run from a read-only
AppImage mount. No network, no game data, no FUSE: the program is test_pack_linux's synthetic ELF, appimagetool is a
stand-in, the real appimagetool is not run here (release.yml and the local proof run it). Exit 0 = PASS.

    python3 tools/release/test_pack_appimage.py

Cases: the AppDir holds the tarball's list without install.sh, with AppRun (0755, PYTHONDONTWRITEBYTECODE, execs the
program), the desktop entry (Exec/Icon the AppImage's names, one each, the keys a desktop file needs), the icon and
.DirIcon, the read-only marker, the AppImage runtime's MIT text and the LGPL 2.1 text, README.txt in its AppImage
form; no file holds the build machine's folder; the AppImage name; appimagetool is called with
--appimage-extract-and-run and --runtime-file, its failure writes nothing; refusals (a missing runtime licence, a
licence text that is not the runtime's, an existing AppImage without --replace). Read-only run: the AppDir made from
this repository is copied, made read-only (chmod -R a-w) and the engine runs from it in an empty temporary HOME/XDG
(catalog.py scan, linux-kit status, linux-kit check, build --dry-run of a ready game) with an audit hook in every
Python process (sitecustomize on PYTHONPATH) that records any write, create, rename, delete or mode change inside the
AppDir; the test fails on any. A run without PYTHONDONTWRITEBYTECODE shows the hook sees Python's bytecode writes.
"""
import contextlib
import io
import hashlib
import os
import shutil
import stat
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402
import pack_appimage  # noqa: E402
import pack_linux  # noqa: E402
import pack_zip  # noqa: E402
import test_pack_linux as tpl  # noqa: E402  (synthetic ELF and repository)
import test_catalog as tc  # noqa: E402  (synthetic disc and EBOOT)

ROOT = os.path.dirname(os.path.dirname(HERE))
FAILS = []
N = 0


def check(cond, what, detail=""):
    global N
    N += 1
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"\n     {str(detail)[:800]}"))
    if not cond:
        FAILS.append(what)


def runtime_license(tmp):
    p = os.path.join(tmp, "rt", "LICENSE")
    tpl.wr(p, "MIT License\n\nCopyright (c) 2004-23 probonopd\n...\n* libfuse, licensed under the terms of LGPL2\n")
    return p


def args(r, tk, b, lic, *extra, version="1.2.3"):
    return ["--program", os.path.join(b, "qwarks-awakening"), "--sdl3-licenses", os.path.join(b, "licenses"),
            "--toolkit", tk, "--version", version, "--root", r, "--runtime-license", lic, "--commit", "0522150bf0fb",
            *extra]


def main_args(argv, run=subprocess.run):
    s = io.StringIO()
    rc = pack_appimage.main(argv, out=s, run=run)
    return rc, s.getvalue()


def files_under(top):
    out = {}
    for d, _dirs, fs in os.walk(top):
        for n in fs:
            p = os.path.join(d, n)
            out[os.path.relpath(p, top).replace(os.sep, "/")] = p
    return out


def test_appdir(tmp):
    r, tk, b = tpl.make_root(tmp)
    lic = runtime_license(tmp)
    ad = os.path.join(tmp, "AppDir")
    rc, o = main_args(args(r, tk, b, lic, "--appdir-only", ad))
    check(rc == 0 and os.path.isfile(os.path.join(ad, "AppRun")), "--appdir-only writes the AppDir", o)
    if rc:
        return
    have = files_under(ad)
    _p, tfiles = pack_linux.gather(os.path.join(b, "qwarks-awakening"), os.path.join(b, "licenses"), tk, r, "1.2.3",
                                   "0522150bf0fb")
    want = {rel for rel, _s, _x in tfiles} - {"install.sh"}
    want |= {"AppRun", ".DirIcon", pack_appimage.MARKER, "licenses/appimage-runtime-LICENSE.txt", "licenses/lgpl-2.1.txt"}
    want |= {rel for rel, *_x in pack_appimage.LIBRARY_LICENSES}
    check(set(have) == want, "the AppDir = the tarball's files without install.sh, plus AppRun, .DirIcon, the marker "
          "and the runtime's licence texts", sorted(set(have) ^ want))
    mode = lambda rel: stat.S_IMODE(os.stat(have[rel]).st_mode)   # noqa: E731
    apprun = open(have["AppRun"]).read()
    check(mode("AppRun") == 0o755 and mode(names.LINUX_PROGRAM) == 0o755 and mode("README.txt") == 0o644
          and apprun.startswith("#!/bin/sh\n") and "export PYTHONDONTWRITEBYTECODE=1" in apprun
          and f'exec "$HERE/{names.LINUX_PROGRAM}" "$@"' in apprun,
          "AppRun: executable sh, no bytecode writes, execs the program with the arguments", apprun)
    de = open(have[f"{names.LINUX_STEM}.desktop"]).read().splitlines()
    keys = [ln.split("=", 1)[0] for ln in de if "=" in ln]
    check(de[0] == "[Desktop Entry]" and f"Exec={names.LINUX_PROGRAM}" in de and f"Icon={names.LINUX_STEM}" in de
          and keys.count("Exec") == 1 and keys.count("Icon") == 1 and {"Type", "Name", "Categories"} <= set(keys)
          and len(keys) == len(set(keys)), "desktop entry: one Exec and Icon for the AppImage, Type/Name/Categories", de)
    check(open(have[".DirIcon"], "rb").read() == open(have[f"{names.LINUX_STEM}.png"], "rb").read()
          and open(have[".DirIcon"], "rb").read()[:8] == b"\x89PNG\r\n\x1a\n" and not os.path.islink(have[".DirIcon"]),
          ".DirIcon: the 256 px PNG, a file")
    rd = open(have["README.txt"]).read()
    check(names.linux_appimage_name("1.2.3") in rd and "appimage-run" in rd and "--appimage-extract-and-run" in rd
          and "install.sh" not in rd and "~/.local/share/qwarks-awakening/" in rd,
          "README.txt: the AppImage form (start, NixOS appimage-run, no FUSE, same data folders, no install.sh)")
    check("libfuse" in open(have["licenses/appimage-runtime-LICENSE.txt"]).read()
          and "LESSER GENERAL PUBLIC" in open(have["licenses/lgpl-2.1.txt"]).read(),
          "licences: the AppImage runtime's (MIT, libfuse named) and the LGPL 2.1 text")
    libs = {rel.split("/")[1].split("-")[0] for rel, *_x in pack_appimage.LIBRARY_LICENSES}
    check(libs == {"musl", "squashfuse", "zstd", "zlib", "mimalloc"}
          and all(hashlib.sha256(open(have[rel], "rb").read()).hexdigest() == d
                  for rel, _n, d, _w, _u in pack_appimage.LIBRARY_LICENSES)
          and "type2-runtime 20251108" not in rd and "release 20251108" in rd and "libfuse 3.15.0" in rd
          and "--runtime-file" in rd and "fuse-3.15.0" in rd,
          "licences: musl, squashfuse, zstd, zlib and mimalloc texts verbatim (SHA-256 pins); README.txt names the "
          "runtime release, its source, libfuse 3.15.0 and how to replace the runtime")
    check(open(have[pack_zip.IDENT], "rb").read() == pack_zip.release_ident("1.2.3", "0522150bf0fb"),
          "cmake/release-ident.txt names the version and the public commit")
    private = [rel for rel, p in have.items() if os.path.abspath(r).encode() in open(p, "rb").read()
               or tmp.encode() in open(p, "rb").read()]
    check(not private, "no file holds the build machine's folders", private)
    check(names.linux_appimage_name("0.4.0") == "qwarks-awakening-launcher-0.4.0-x86_64.AppImage", "the AppImage's name")

    # appimagetool (a stand-in): the arguments, the result, a failure writes nothing
    seen = {}

    def fake(argv, **kw):
        seen["argv"], seen["env"] = argv, kw.get("env", {})
        seen["apprun"] = os.path.isfile(os.path.join(argv[-2], "AppRun"))
        with open(argv[-1], "wb") as f:
            f.write(b"\x7fELF fake appimage")
        return subprocess.CompletedProcess(argv, 0, "ok\n")
    tool, rt = os.path.join(tmp, "tool"), os.path.join(tmp, "runtime-x86_64")
    tpl.wr(tool, "#!/bin/sh\n", 0o755)
    tpl.wr(rt, b"\x7fELF runtime")
    out = os.path.join(tmp, "out")
    rc, o = main_args(args(r, tk, b, lic, "--out", out, "--appimagetool", tool, "--runtime", rt), run=fake)
    dest = os.path.join(out, names.linux_appimage_name("1.2.3"))
    a = seen.get("argv", [])
    check(rc == 0 and os.path.isfile(dest) and os.access(dest, os.X_OK) and seen.get("apprun")
          and a[:5] == [os.path.abspath(tool), "--appimage-extract-and-run", "--no-appstream", "--runtime-file",
                        os.path.abspath(rt)] and seen["env"].get("ARCH") == "x86_64",
          "appimagetool gets the AppDir, --appimage-extract-and-run and the pinned --runtime-file; the AppImage is "
          "written executable", o + str(a))
    rc, o = main_args(args(r, tk, b, lic, "--out", out, "--appimagetool", tool, "--runtime", rt), run=fake)
    check(rc == 1 and "--replace" in o, "an existing AppImage needs --replace", o)
    out2 = os.path.join(tmp, "out2")
    rc, o = main_args(args(r, tk, b, lic, "--out", out2, "--appimagetool", tool, "--runtime", rt),
                      run=lambda argv, **kw: subprocess.CompletedProcess(argv, 3, "boom\n"))
    check(rc == 1 and not os.path.exists(os.path.join(out2, names.linux_appimage_name("1.2.3")))
          and "appimagetool: boom" in o, "appimagetool failing: nothing written, its output shown", o)
    rc, o = main_args(args(r, tk, b, os.path.join(tmp, "none"), "--appdir-only", os.path.join(tmp, "ad2")))
    check(rc == 1 and not os.path.exists(os.path.join(tmp, "ad2")) and "appimage-runtime-LICENSE.txt: missing" in o,
          "refused: no runtime licence", o)
    other = os.path.join(tmp, "other-LICENSE")
    tpl.wr(other, "Apache License\n")
    rc, o = main_args(args(r, tk, b, other, "--appdir-only", os.path.join(tmp, "ad3")))
    check(rc == 1 and "not the expected licence text" in o, "refused: a licence text that is not the runtime's", o)
    _n, name, _d, _w, _u = pack_appimage.LIBRARY_LICENSES[0]
    keep, bad = pack_appimage.LIBRARY_LICENSES, ((pack_appimage.LIBRARY_LICENSES[0][0], name, "0" * 64) +
                                                   pack_appimage.LIBRARY_LICENSES[0][3:],)
    pack_appimage.LIBRARY_LICENSES = bad + keep[1:]
    try:
        rc, o = main_args(args(r, tk, b, lic, "--out", out2, "--appimagetool", tool, "--runtime", rt),
                          run=lambda *a, **k: subprocess.CompletedProcess(a, 0, "", ""))
    finally:
        pack_appimage.LIBRARY_LICENSES = keep
    check(rc == 1 and "SHA-256 differs from the pin" in o, "refused: a library licence text that differs from its pin", o)
    rc, o = main_args(args(r, tk, b, lic, "--out", out2, "--runtime", rt))
    check(rc == 2, "usage: an AppImage needs --appimagetool and --runtime", o)


AUDIT = r'''
import os, sys
_TOP = os.environ.get("QA_AUDIT_TOP", "")
_LOG = os.environ.get("QA_AUDIT_LOG", "")
_W = os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_APPEND | os.O_TRUNC
def _inside(p):
    try:
        p = os.fsdecode(p) if not isinstance(p, int) else ""
    except Exception:
        return False
    if not p:
        return False
    p = os.path.realpath(os.path.abspath(p))
    return p == _TOP or p.startswith(_TOP + os.sep)
def _note(ev, p):
    with open(_LOG, "a") as f:
        f.write(f"{ev} {p}\n")
def _hook(ev, a):
    if not _TOP:
        return
    if ev == "open" and a:
        mode, flags = (a[1] if len(a) > 1 else None), (a[2] if len(a) > 2 else 0)
        w = (isinstance(mode, str) and any(c in mode for c in "wax+")) or (isinstance(flags, int) and flags & _W)
        if w and _inside(a[0]):
            _note(ev, a[0])
    elif ev in ("os.mkdir", "os.remove", "os.rmdir", "os.chmod", "os.chown", "os.utime", "os.truncate",
                "shutil.rmtree", "os.symlink", "os.link") and a and _inside(a[0]):
        _note(ev, a[0])
    elif ev in ("os.rename", "os.replace") and len(a) > 1 and (_inside(a[0]) or _inside(a[1])):
        _note(ev, f"{a[0]} -> {a[1]}")
if _TOP and _LOG:
    sys.addaudithook(_hook)
'''


def test_read_only(tmp):
    """The AppDir from this repository, read-only, the engine run from it in an empty home."""
    _r, tk, b = tpl.make_root(tmp)
    lic = runtime_license(tmp)
    built = os.path.join(tmp, "ro-src")
    rc, o = main_args(["--program", os.path.join(b, "qwarks-awakening"), "--sdl3-licenses", os.path.join(b, "licenses"),
                       "--toolkit", tk, "--version", "1.2.3", "--root", ROOT, "--runtime-license", lic,
                       "--commit", "0522150bf0fb", "--appdir-only", built])
    check(rc == 0, "the AppDir from this repository", o)
    if rc:
        return
    mount = os.path.join(tmp, "mount", "AppDir")          # a copy, as the squashfs mount would show it
    shutil.copytree(built, mount, symlinks=True)
    home = os.path.join(tmp, "home")
    os.makedirs(home)
    hook = os.path.join(tmp, "hook")
    tpl.wr(os.path.join(hook, "sitecustomize.py"), AUDIT)
    log = os.path.join(tmp, "audit.log")
    lib = os.path.join(home, "games-in")                  # the player's files: a ToD disc and an EBOOT (synthetic)
    tc.disc(lib, "tods_disc", "BCUS98127")
    tc.elf(os.path.join(lib, "tods_disc", "EBOOT.elf"))
    library = os.path.join(home, ".local", "share", "qwarks-awakening", "library.json")
    env = {"HOME": home, "XDG_DATA_HOME": os.path.join(home, ".local", "share"),
           "XDG_CONFIG_HOME": os.path.join(home, ".config"), "XDG_CACHE_HOME": os.path.join(home, ".cache"),
           "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "PYTHONPATH": hook, "PYTHONDONTWRITEBYTECODE": "1",
           "APPIMAGE": os.path.join(tmp, names.linux_appimage_name("1.2.3")), "APPDIR": mount,
           "QA_AUDIT_TOP": os.path.realpath(mount), "QA_AUDIT_LOG": log}
    for d, dirs, fs in os.walk(mount):                    # chmod -R a-w
        for n in fs + dirs:
            p = os.path.join(d, n)
            if not os.path.islink(p):
                os.chmod(p, stat.S_IMODE(os.lstat(p).st_mode) & ~0o222)
    os.chmod(mount, 0o555)
    eng = os.path.join(mount, "tools", "release", "catalog.py")
    try:
        runs = {}
        for what, argv in (("scan", ["scan", "--library", library, lib]),
                           ("linux-kit status", ["linux-kit", "status"]),
                           ("linux-kit check", ["linux-kit", "check"]),
                           ("build --dry-run", ["build", "--library", library, "--game", "tods", "--dry-run"])):
            p = subprocess.run([sys.executable, eng, *argv], env=env, capture_output=True, text=True, cwd=home,
                               timeout=300)
            runs[what] = p
            check(p.returncode == 0, f"read-only AppDir: catalog.py {what} runs (exit 0)",
                  p.stdout[-1500:] + p.stderr[-1500:])
        writes = open(log).read() if os.path.exists(log) else ""
        check(writes == "", "read-only AppDir: no write, create, rename, delete or mode change inside it", writes)
        out = runs["build --dry-run"].stdout
        want_build = os.path.join(home, ".local", "share", "qwarks-awakening", "build", "tods")
        check(want_build in out, "read-only AppDir: the build plan's folder is in the XDG data folder", out[-1500:])
        check(os.path.isfile(library) and "ready_to_build" in open(library).read(),
              "read-only AppDir: the library is written in the XDG data folder (ToD ready to build)")
        # the hook does see writes: linux-kit status again with bytecode writing on and the folder made writable
        os.chmod(os.path.join(mount, "tools", "release"), 0o755)
        e2 = dict(env)
        e2.pop("PYTHONDONTWRITEBYTECODE")
        subprocess.run([sys.executable, eng, "linux-kit", "status"], env=e2, capture_output=True, cwd=home,
                       timeout=300)   # linux_kit.py imports fetch_toolchain: Python would cache its bytecode
        seen = open(log).read() if os.path.exists(log) else ""
        check("__pycache__" in seen, "the audit hook sees writes (Python's bytecode without PYTHONDONTWRITEBYTECODE)",
              seen[:500])
    finally:
        for d, dirs, _fs in os.walk(os.path.join(tmp, "mount")):
            for n in dirs:
                os.chmod(os.path.join(d, n), 0o755)
        os.chmod(os.path.join(tmp, "mount"), 0o755)


def main():
    tmp = tempfile.mkdtemp(prefix="rc-pack-appimage-test-")
    try:
        test_appdir(os.path.join(tmp, "a"))
        if sys.platform.startswith("linux") and os.geteuid() != 0:   # root ignores the read-only modes
            test_read_only(os.path.join(tmp, "b"))
        else:
            print("SKIP read-only run (Linux, not root)")
    finally:
        for d, dirs, _fs in os.walk(tmp):
            for n in dirs:
                with contextlib.suppress(OSError):
                    os.chmod(os.path.join(d, n), 0o755)
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"\ntest_pack_appimage: {N - len(FAILS)}/{N} checks passed", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    with contextlib.suppress(BrokenPipeError):
        sys.exit(main())
