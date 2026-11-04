#!/usr/bin/env python3
"""Host test for tools/release/pack_zip.py (the launcher zip). No network, no game data, no compiler: the exe and
SDL3.dll are synthetic PE headers, the repository files are a temp copy. Exit 0 = PASS.

    python3 tools/release/test_pack_zip.py

Cases: the zip holds exactly the expected entries under names.launcher_zip_top(version) (exe, SDL3.dll, fonts,
engine, catalogue, example library, the Python embeddable package under python/, licenses/ with the port's LICENSE,
SDL3, Dear ImGui, Python and every OFL text, README.txt)
and the README says what the preview does not do; same inputs give the same bytes; the .sha256 is "HASH  NAME" + LF
with no BOM and matches; refusals write nothing: a DLL as the exe, a 32-bit exe, a missing font folder, a non-SDL
licence, a game-data header, an NTFS case clash, an existing zip without --replace, a bad --version, a Python folder
without pythonw.exe or its zipped stdlib, a 32-bit pythonw.exe.
"""
import contextlib
import hashlib
import io
import os
import shutil
import struct
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402
import pack_zip  # noqa: E402

FAILS = []
SDL_LICENCE = ("Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>\n\nThis software is provided 'as-is', "
               "without any express or implied warranty.\n")


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:800]))
    if not cond:
        FAILS.append(what)


def pe(machine=0x8664, dll=False):
    """A minimal PE image header: MZ, e_lfanew, PE signature, COFF header (machine, characteristics)."""
    b = bytearray(512)
    b[0:2] = b"MZ"
    struct.pack_into("<I", b, 60, 128)
    b[128:132] = b"PE\0\0"
    struct.pack_into("<H", b, 132, machine)
    struct.pack_into("<H", b, 150, 0x2022 if dll else 0x0022)
    return bytes(b)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def tree(tmp):
    root = os.path.join(tmp, "repo")
    write(os.path.join(root, "public", "LICENSE"), "MIT License\n")
    for n in ("Barlow-Regular.ttf", "Rajdhani-Bold.ttf"):
        write(os.path.join(root, "launcher", "fonts", n), b"\0\1\0\0font")
    for n in ("OFL-barlow.txt", "OFL-rajdhani.txt"):
        write(os.path.join(root, "launcher", "fonts", n), "SIL Open Font License\n")
    write(os.path.join(root, "third_party", "imgui", "LICENSE.txt"), "The MIT License (MIT)\n")
    write(os.path.join(root, "tools", "release", "catalog.py"), "print('engine')\n")
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


def run(tmp, root, *extra, version="1.2.3", out="out"):
    buf = io.StringIO()
    args = ["--exe", os.path.join(tmp, "launcher.exe"), "--sdl3", os.path.join(tmp, "sdl3"),
            "--python", os.path.join(tmp, "python"), "--version", version,
            "--out", os.path.join(tmp, out), "--root", root] + list(extra)
    with contextlib.redirect_stderr(buf):
        try:
            rc = pack_zip.main(args, out=buf)
        except SystemExit as e:
            rc = e.code
    return rc, buf.getvalue()


def test_good(tmp, root):
    rc, out = run(tmp, root)
    z = os.path.join(tmp, "out", names.launcher_zip_name("1.2.3"))
    check(rc == 0 and os.path.isfile(z), "packs the zip", out)
    top = names.launcher_zip_top("1.2.3") + "/"
    with zipfile.ZipFile(z) as zf:
        got = sorted(zf.namelist())
        readme = zf.read(top + "README.txt").decode("utf-8")
        infos = zf.infolist()
    want = sorted(top + p for p in (
        names.LAUNCHER_EXE, "SDL3.dll", "README.txt", "fonts/Barlow-Regular.ttf", "fonts/Rajdhani-Bold.ttf",
        "tools/release/catalog.py", "data/titles/catalog.json", "data/titles/library.example.json",
        "licenses/LICENSE.txt", "licenses/SDL3-LICENSE.txt", "licenses/imgui-LICENSE.txt",
        "licenses/OFL-barlow.txt", "licenses/OFL-rajdhani.txt", "licenses/Python-LICENSE.txt",
        "python/python.exe", "python/pythonw.exe", "python/python313.dll", "python/python313.zip",
        "python/python313._pth", "python/LICENSE.txt"))
    check(got == want, "exactly the expected entries under the version folder", (got, want))
    check(all(i.date_time == (1980, 1, 1, 0, 0, 0) and not i.extra for i in infos), "fixed dates, no extra fields")
    for needle in ("does not build or start a game", "No game files are included", names.NAME_NOTE,
                   "library.json", "Python 3.13 is included", "nothing else needs to be installed",
                   names.LAUNCHER_EXE):
        check(needle in readme, f"README.txt says {needle[:40]!r}")
    check("\r\n" in readme, "README.txt has Windows line ends")
    with open(z + ".sha256", "rb") as f:
        side = f.read()
    with open(z, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    check(side == f"{digest}  {os.path.basename(z)}\n".encode("ascii"), ".sha256 is 'HASH  NAME' + LF, no BOM", side)
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


def main():
    tmp = tempfile.mkdtemp()
    try:
        root = tree(tmp)
        test_good(tmp, root)
        test_refusals(tmp, root)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_pack_zip:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
