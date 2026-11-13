#!/usr/bin/env python3
"""Host test for tools/release/pack_zip.py (the bare launcher zip and the full build zip). No network, no game data,
no compiler: the exe and SDL3.dll are synthetic PE headers, the repository, toolkit and toolchain trees are temp files.
Exit 0 = PASS.

    python3 tools/release/test_pack_zip.py

Cases: the bare zip holds exactly the expected entries under names.launcher_zip_top(version) (exe, SDL3.dll, fonts,
engine and downloader, catalogue, example library, the Python embeddable package under python/, licenses/ with the
port's LICENSE, SDL3, Dear ImGui, Python and every OFL text, README.txt) and its README says what goes online; same
inputs give the same bytes; no per-zip .sha256 is written (SHA256SUMS on the release lists every asset) and the printed
SHA-256 matches; refusals write nothing: a DLL as
the exe, a 32-bit exe, a missing font folder, a non-SDL licence, a game-data header, an NTFS case clash, an existing
zip without --replace, a bad --version, a Python folder without pythonw.exe or its zipped stdlib, a 32-bit pythonw.exe.
Phase 4: --make-kit-manifest lists the kit (source subset, ps3recomp, toolchain minus its prune patterns, licence
texts) and never a bare launcher file; the full build zip = the bare files + the kit + kit.json under
names.build_zip_top; refused: an exe without the kit list's SHA-256, a kit file changed after the list was made, a
list of another version, a hand-edited list, a toolkit that is not a patched ps3recomp; kit.py installs the kit from
the full zip into an unpacked bare launcher (status ready, every file verified).
"""
import contextlib
import hashlib
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402
import pack_zip  # noqa: E402
import kit  # noqa: E402
import test_kit as tkit  # noqa: E402  (pe_with_debuglink)

FAILS = []
SDL_LICENCE = ("Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>\n\nThis software is provided 'as-is', "
               "without any express or implied warranty.\n")


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:800]))
    if not cond:
        FAILS.append(what)


def pe(machine=0x8664, dll=False, extra=b""):
    """A minimal PE image header: MZ, e_lfanew, PE signature, COFF header (machine, characteristics)."""
    b = bytearray(512)
    b[0:2] = b"MZ"
    struct.pack_into("<I", b, 60, 128)
    b[128:132] = b"PE\0\0"
    struct.pack_into("<H", b, 132, machine)
    struct.pack_into("<H", b, 150, 0x2022 if dll else 0x0022)
    return bytes(b) + extra


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def tree(tmp):
    root = os.path.join(tmp, "repo")
    write(os.path.join(root, "public", "LICENSE"), "MIT License\n")
    for n in ("AdwaitaSans-Regular.ttf", "RussoOne-Regular.ttf"):
        write(os.path.join(root, "launcher", "fonts", n), b"\0\1\0\0font")
    for n in ("OFL-adwaita.txt", "OFL-russo-one.txt"):
        write(os.path.join(root, "launcher", "fonts", n), "SIL Open Font License\n")
    write(os.path.join(root, "third_party", "imgui", "LICENSE.txt"), "The MIT License (MIT)\n")
    write(os.path.join(root, "tools", "release", "catalog.py"), "print('engine')\n")
    write(os.path.join(root, "tools", "release", "kit.py"), "print('downloader')\n")
    write(os.path.join(root, "data", "titles", "catalog.json"), "{}\n")
    write(os.path.join(root, "data", "titles", "library.example.json"), "{}\n")
    write(os.path.join(tmp, "launcher.exe"), pe())
    write(os.path.join(tmp, "sdl3", "SDL3.dll"), pe(dll=True))
    write(os.path.join(tmp, "sdl3", "LICENSE.txt"), SDL_LICENCE)
    py = os.path.join(tmp, "python")
    for n, data in (("python.exe", pe()), ("pythonw.exe", pe()), ("python313.dll", pe(dll=True)),
                    ("python313.zip", b"PK\5\6" + bytes(18)), ("python313._pth", "python313.zip\n.\n"),
                    ("LICENSE.txt", "PSF LICENSE AGREEMENT FOR PYTHON\n")):
        write(os.path.join(py, n), data)
    return root


def kit_tree(tmp, root):
    """The kit's sources: the repository subset, a patched toolkit, toolchain trees (as fetch_toolchain unpacks them)."""
    for rel, data in (("port/rc/CMakeLists.txt", "project(rc)\n"), ("port/rc/tods/hle_tods.cpp", "// glue\n"),
                      ("port/common/log/kz3_log.c", "/* log */\n"), ("port/patches/0001-x.patch", "--- a\n"),
                      ("port/rc/__pycache__/x.cpython-313.pyc", b"\0junk"), ("data/titles/build_profiles.json", "{}\n"),
                      ("data/titles/tods_spu_config.json", "{}\n"), ("tools/spu/spu_lift.py", "pass\n"),
                      ("tools/release/firstrun.py", "pass\n"), ("tools/release/firstrun_check.py", "pass\n"),
                      ("cmake/llvm-mingw.cmake", "# tc\n"), ("cmake/mingw-w64.cmake", "# tc\n")):
        write(os.path.join(root, *rel.split("/")), data)
    tk = os.path.join(tmp, "toolkit")
    for rel, data in (("CMakeLists.txt", "project(ps3recomp)\n"), ("tools/ppu_lifter.py", "pass\n"),
                      ("tools/ppu_loader.py", "pass\n"), (".rc-patchset", "0123456789abcdef-4e8f3ca8\n"),
                      ("LICENSE", "MIT License (ps3recomp)\n"), ("runtime/a.c", "int a;\n"), (".git/HEAD", "ref\n")):
        write(os.path.join(tk, *rel.split("/")), data)
    trees = os.path.join(tmp, "trees")
    for rel, data in (("llvm-mingw/bin/x86_64-w64-mingw32-clang.exe", pe()), ("llvm-mingw/LICENSE.TXT", "LLVM\n"),
                      ("llvm-mingw/aarch64-w64-mingw32/lib/x.a", b"!<arch>\n"), ("llvm-mingw/python/bin/py", "x\n"),
                      ("llvm-mingw/x86_64-w64-mingw32/lib/libc.a", b"!<arch>\n"),
                      ("cmake/bin/cmake.exe", pe()), ("cmake/doc/cmake/LICENSE.rst", "BSD 3-Clause\n"),
                      ("cmake/doc/cmake/html/index.html", "<html>\n"), ("cmake/man/man1/cmake.1", "man\n"),
                      ("ninja/ninja.exe", pe())):
        write(os.path.join(trees, *rel.split("/")), data)
    m = json.load(open(os.path.join(HERE, "toolchain.json"), encoding="utf-8"))
    for c in m["components"]:
        if c.get("kit_license"):
            write(os.path.join(trees, c["name"], c["file_name"]), f"licence text of {c['name']}\n")
    return tk, trees


def run(tmp, root, *extra, version="1.2.3", out="out", exe=None):
    buf = io.StringIO()
    args = ["--exe", exe or os.path.join(tmp, "launcher.exe"), "--sdl3", os.path.join(tmp, "sdl3"),
            "--python", os.path.join(tmp, "python"), "--version", version,
            "--out", os.path.join(tmp, out), "--root", root] + list(extra)
    with contextlib.redirect_stderr(buf):
        try:
            rc = pack_zip.main(args, out=buf)
        except SystemExit as e:
            rc = e.code
    return rc, buf.getvalue()


def main_args(*args):
    buf = io.StringIO()
    with contextlib.redirect_stderr(buf):
        try:
            rc = pack_zip.main(list(args), out=buf)
        except SystemExit as e:
            rc = e.code
    return rc, buf.getvalue()


BARE = (names.LAUNCHER_EXE, "SDL3.dll", "README.txt", "fonts/AdwaitaSans-Regular.ttf", "fonts/RussoOne-Regular.ttf",
        "tools/release/catalog.py", "tools/release/kit.py", "data/titles/catalog.json",
        "data/titles/library.example.json", "licenses/LICENSE.txt", "licenses/SDL3-LICENSE.txt",
        "licenses/imgui-LICENSE.txt", "licenses/OFL-adwaita.txt", "licenses/OFL-russo-one.txt",
        "licenses/Python-LICENSE.txt", "python/python.exe", "python/pythonw.exe", "python/python313.dll",
        "python/python313.zip", "python/python313._pth", "python/LICENSE.txt")


def test_good(tmp, root):
    rc, out = run(tmp, root)
    out0 = out
    z = os.path.join(tmp, "out", names.launcher_zip_name("1.2.3"))
    check(rc == 0 and os.path.isfile(z), "packs the zip", out)
    if rc != 0:
        return
    check(not os.path.exists(os.path.join(tmp, "out", names.build_zip_name("1.2.3"))),
          "without --kit-manifest only the bare launcher zip is written")
    top = names.launcher_zip_top("1.2.3") + "/"
    with zipfile.ZipFile(z) as zf:
        got = sorted(zf.namelist())
        readme = zf.read(top + "README.txt").decode("utf-8")
        infos = zf.infolist()
    want = sorted(top + p for p in BARE)
    check(got == want, "exactly the expected entries under the version folder", (got, want))
    check(all(i.date_time == (1980, 1, 1, 0, 0, 0) and not i.extra for i in infos), "fixed dates, no extra fields")
    for needle in ("No game files are included", names.NAME_NOTE, "library.json", "Python 3.13 is included",
                   "nothing else needs to be installed", names.LAUNCHER_EXE, "What goes online",
                   "it asks before downloading it", names.build_zip_name("1.2.3"), "GitHub releases page only",
                   "off by default", "Game updates are never downloaded"):
        check(needle in readme, f"README.txt says {needle[:40]!r}")
    check("\r\n" in readme, "README.txt has Windows line ends")
    with open(z, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    check(not os.path.exists(z + ".sha256"), "no per-zip .sha256 (SHA256SUMS lists the release files)")
    check(f"sha256 {digest}" in out0, "the printed SHA-256 is the zip's", out0)
    first = open(z, "rb").read()
    rc, out = run(tmp, root, "--replace")
    check(rc == 0 and open(z, "rb").read() == first, "same inputs, same bytes", out)
    rc, out = run(tmp, root)
    check(rc == 1 and "exists" in out, "an existing zip is kept without --replace", out)


def refused(tmp, root, what, prepare, restore, **kw):
    prepare()
    try:
        rc, out = run(tmp, root, out="refused", **kw)
        z = os.path.join(tmp, "refused", names.launcher_zip_name(kw.get("version", "1.2.3")))
        check(rc != 0 and not os.path.exists(z), f"refused: {what}", out)
    finally:
        restore()


def test_refusals(tmp, root):
    exe = os.path.join(tmp, "launcher.exe")
    dll = os.path.join(tmp, "sdl3", "SDL3.dll")
    lic = os.path.join(tmp, "sdl3", "LICENSE.txt")
    fonts = os.path.join(root, "launcher", "fonts")
    put_back = lambda: (write(exe, pe()), write(dll, pe(dll=True)), write(lic, SDL_LICENCE))  # noqa: E731
    refused(tmp, root, "a DLL as the exe", lambda: write(exe, pe(dll=True)), put_back)
    refused(tmp, root, "a 32-bit exe", lambda: write(exe, pe(machine=0x14c)), put_back)
    refused(tmp, root, "an exe as SDL3.dll", lambda: write(dll, pe()), put_back)
    refused(tmp, root, "a licence that is not SDL's", lambda: write(lic, "GPL\n"), put_back)
    refused(tmp, root, "a missing font folder", lambda: shutil.move(fonts, fonts + ".x"),
            lambda: shutil.move(fonts + ".x", fonts))
    cat = os.path.join(root, "data", "titles", "catalog.json")
    refused(tmp, root, "a PS3 game-data header (SELF)", lambda: write(cat, b"SCE\0rest"), lambda: write(cat, "{}\n"))
    refused(tmp, root, "a bad --version", lambda: None, lambda: None, version="v1.2")
    pyw = os.path.join(tmp, "python", "pythonw.exe")
    pyz = os.path.join(tmp, "python", "python313.zip")
    refused(tmp, root, "a Python package without pythonw.exe", lambda: shutil.move(pyw, pyw + ".x"),
            lambda: shutil.move(pyw + ".x", pyw))
    refused(tmp, root, "a Python package without its zipped stdlib", lambda: shutil.move(pyz, pyz + ".x"),
            lambda: shutil.move(pyz + ".x", pyz))
    refused(tmp, root, "a 32-bit pythonw.exe", lambda: write(pyw, pe(machine=0x14c)), lambda: write(pyw, pe()))
    text = pack_zip.readme("1.2.3", "3.13")
    check("py tools" not in text and "python3 tools" not in text and "--engine" not in text,
          "README.txt has no manual Python step (Python is bundled)")
    # NTFS: two names that differ only by case
    probs = pack_zip.content_problems([("licenses/LICENSE.txt", b"a"), ("licenses/License.TXT", b"b")])
    check(any("letter case" in p for p in probs), "a case clash is refused", probs)
    probs = pack_zip.content_problems([("data/x.psarc", b"x")])
    check(any("extension" in p for p in probs), "a game file extension is refused", probs)


def test_kit(tmp, root):
    tk, trees = kit_tree(tmp, root)
    man = os.path.join(tmp, "kit", "kit.json")
    rc, out = main_args("--make-kit-manifest", man, "--toolkit", tk, "--toolchain", trees, "--version", "1.2.3",
                        "--root", root, "--no-estimate")
    check(rc == 0 and os.path.isfile(man), "--make-kit-manifest writes kit.json", out)
    if rc != 0:
        return
    raw = open(man, "rb").read()
    m = json.loads(raw)
    paths = {f["path"] for f in m["files"]}
    check(raw == kit.manifest_bytes(m) and not kit.check_manifest(m), "kit.json is in kit.py's byte form and valid")
    for p in ("port/rc/CMakeLists.txt", "port/patches/0001-x.patch", "data/titles/build_profiles.json",
              "tools/spu/spu_lift.py", "tools/release/firstrun.py", "cmake/llvm-mingw.cmake", "ps3recomp/.rc-patchset",
              "ps3recomp/runtime/a.c", "toolchain/llvm-mingw/bin/x86_64-w64-mingw32-clang.exe",
              "toolchain/cmake/bin/cmake.exe", "toolchain/ninja/ninja.exe", "licenses/ps3recomp-LICENSE.txt",
              "licenses/cmake/LICENSE.rst", "licenses/llvm-mingw/LLVM-LICENSE.txt", "licenses/llvm-mingw/LGPL-2.1.txt",
              "licenses/llvm-mingw/mingw-w64-COPYING.txt", "licenses/llvm-mingw/winpthreads-COPYING.txt",
              "licenses/llvm-mingw/llvm-mingw-LICENSE.txt", "licenses/ninja/COPYING.txt"):
        check(p in paths, f"kit lists {p}")
    for p in ("toolchain/llvm-mingw/aarch64-w64-mingw32/lib/x.a", "toolchain/llvm-mingw/python/bin/py",
              "toolchain/cmake/doc/cmake/html/index.html", "toolchain/cmake/man/man1/cmake.1", "ps3recomp/.git/HEAD",
              "port/rc/__pycache__/x.cpython-313.pyc", "data/titles/catalog.json", "tools/release/catalog.py",
              "tools/release/kit.py", "SDL3.dll", names.LAUNCHER_EXE):
        check(p not in paths, f"kit leaves out {p}")
    check(m["release"] == "1.2.3" and m["download_url"] == names.release_asset_url("1.2.3", names.build_zip_name("1.2.3"))
          and m["download_url"].startswith(names.REPO_URL + "/releases/download/launcher-v1.2.3/"), "kit.json names the release and its URL")
    sha = hashlib.sha256(raw).hexdigest()
    # an exe built with this kit list carries its SHA-256 (CMake compiles it in as text)
    good = os.path.join(tmp, "launcher-kit.exe")
    write(good, pe(extra=b"\0" + sha.encode() + b"\0"))
    kargs = ("--kit-manifest", man, "--toolkit", tk, "--toolchain", trees)
    rc, out = run(tmp, root, *kargs, out="full", exe=good)
    full = os.path.join(tmp, "full", names.build_zip_name("1.2.3"))
    bare = os.path.join(tmp, "full", names.launcher_zip_name("1.2.3"))
    check(rc == 0 and os.path.isfile(full) and os.path.isfile(bare) and not os.path.exists(full + ".sha256"),
          "--kit-manifest writes both zips", out)
    if rc != 0:
        return
    ftop = names.build_zip_top("1.2.3") + "/"
    with zipfile.ZipFile(full) as zf:
        fnames = set(zf.namelist())
        freadme = zf.read(ftop + "README.txt").decode("utf-8")
        check(zf.read(ftop + names.KIT_MANIFEST) == raw, "the full zip carries kit.json byte for byte")
    with zipfile.ZipFile(bare) as zf:
        bnames = {n.split("/", 1)[1] for n in zf.namelist()}
    check(bnames == set(BARE), "the bare zip is unchanged by the kit", sorted(bnames ^ set(BARE)))
    check({n.split("/", 1)[1] for n in fnames} == set(BARE) | paths | {names.KIT_MANIFEST},
          "full zip = bare files + kit files + kit.json")
    check("full build download" in freadme and "kit.json" in freadme and "What goes online" in freadme,
          "the full zip's README says what it holds")
    # the bare launcher, unpacked, installs the kit from the full zip
    inst = os.path.join(tmp, "installed")
    with zipfile.ZipFile(bare) as zf:
        zf.extractall(inst)
    inst = os.path.join(inst, names.launcher_zip_top("1.2.3"))
    check(kit.status(inst, sha)["state"] == "missing", "kit.py status: missing in a bare launcher")
    buf = io.StringIO()
    try:
        n = kit.install(full, inst, sha, buf)
    except kit.Refused as e:
        n = 0
        buf.write(str(e))
    check(n == len(m["files"]) and not kit.verify(inst, sha), "kit.py installs every kit file from the full zip",
          buf.getvalue())
    check(open(os.path.join(inst, names.LAUNCHER_EXE), "rb").read() == open(good, "rb").read(),
          "the launcher's own exe is left alone")
    check(kit.status(inst, sha)["state"] == "ready", "kit.py status: ready after the install", kit.status(inst, sha))
    # refusals of the full zip
    bad_exe = os.path.join(tmp, "launcher-other.exe")
    write(bad_exe, pe(extra=b"\0" + b"0" * 64 + b"\0"))
    rc, out = run(tmp, root, *kargs, out="r1", exe=bad_exe)
    check(rc == 1 and "built without this kit manifest" in out and not os.path.isdir(os.path.join(tmp, "r1")),
          "refused: an exe that does not carry the kit list's SHA-256", out)
    f = os.path.join(root, "port", "rc", "CMakeLists.txt")
    write(f, "project(rc) # changed\n")
    rc, out = run(tmp, root, *kargs, out="r2", exe=good)
    check(rc == 1 and "changed since the kit manifest" in out, "refused: a kit file changed after the list was made", out)
    write(f, "project(rc)\n")
    rc, out = run(tmp, root, *kargs, out="r3", exe=good, version="1.2.4")
    check(rc == 1 and "release 1.2.3, not 1.2.4" in out, "refused: a kit list of another version", out)
    hand = os.path.join(tmp, "kit", "hand.json")
    write(hand, json.dumps(m))
    rc, out = run(tmp, root, "--kit-manifest", hand, "--toolkit", tk, "--toolchain", trees, out="r4", exe=good)
    check(rc == 1 and "byte form" in out, "refused: a kit list not in kit.py's byte form", out)
    os.remove(os.path.join(tk, ".rc-patchset"))
    rc, out = main_args("--make-kit-manifest", os.path.join(tmp, "kit", "x.json"), "--toolkit", tk, "--toolchain",
                        trees, "--version", "1.2.3", "--root", root, "--no-estimate")
    check(rc == 1 and "not a patched ps3recomp" in out, "refused: a toolkit without its patch-set stamp", out)
    write(os.path.join(tk, ".rc-patchset"), "0123456789abcdef-4e8f3ca8\n")
    rc, out = main_args("--make-kit-manifest", man, "--version", "1.2.3", "--root", root)
    check(rc == 2, "--make-kit-manifest needs --toolkit and --toolchain", out)


def test_symbols(tmp, root):
    data = b"\x7fdebug information of the launcher" * 50
    exe = os.path.join(tmp, "symexe", "launcher.exe")
    write(exe, tkit.pe_with_debuglink("launcher.exe.debug", zlib.crc32(data) & 0xFFFFFFFF))
    dbg = os.path.join(tmp, "symexe", "launcher.exe.debug")
    write(dbg, data)
    rc, out = run(tmp, root, "--symbols", dbg, out="symout", exe=exe)
    z = os.path.join(tmp, "symout", names.symbols_zip_name("1.2.3"))
    check(rc == 0 and os.path.isfile(z) and not os.path.exists(z + ".sha256"), "--symbols writes the symbols zip (no .sha256)", out)
    if rc == 0:
        with zipfile.ZipFile(z) as zf:
            hit = [i.filename for i in zf.infolist() if i.filename.rsplit("/", 1)[-1] == "launcher.exe.debug"]
            check(len(hit) == 1 and zf.read(hit[0]) == data, "the symbols zip holds the debug file kit.py symbols looks for")
        check(os.path.isfile(os.path.join(tmp, "symout", names.launcher_zip_name("1.2.3"))), "the launcher zip is written too")
    write(dbg, data + b"x")
    rc, out = run(tmp, root, "--symbols", dbg, out="symout2", exe=exe)
    check(rc == 1 and "CRC-32" in out and not os.path.exists(os.path.join(tmp, "symout2")),
          "a debug file that is not the exe's debuglink target is refused, nothing written", out)
    rc, out = run(tmp, root, "--symbols", dbg, out="symout3")
    check(rc == 1 and "debuglink" in out, "an exe without a debuglink is refused", out)


def main():
    tmp = tempfile.mkdtemp()
    try:
        root = tree(tmp)
        test_good(tmp, root)
        test_refusals(tmp, root)
        test_kit(tmp, root)
        test_symbols(tmp, root)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_pack_zip:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
