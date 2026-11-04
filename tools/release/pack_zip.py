#!/usr/bin/env python3
"""Pack the launcher preview's portable zip for Windows x64 (the release workflow's only download). Standard library
only, Python 3.8+.

    pack_zip.py --exe build/launcher-release/launcher.exe --sdl3 work/release/sdl3 \\
                --python work/release/toolchain-cache/trees/python --version X.Y.Z --out DIR [--root R] [--replace]

From the Killzone 3 port's tools/release/pack_zip.py (same author, MIT): the same deterministic zip and .sha256 file;
R&C has no installer payload yet, so the files are gathered here instead of by an assemble step. The zip
(names.launcher_zip_name(version), every entry under names.launcher_zip_top(version)/) holds:

  names.LAUNCHER_EXE                      the launcher (--exe; must be a 64-bit Windows program, not a DLL)
  SDL3.dll                                from --sdl3 (a folder stage_sdl3.py wrote: SDL3.dll + LICENSE.txt)
  fonts/*.ttf                             launcher/fonts (the launcher loads <exe dir>/fonts)
  python/                                 the Python embeddable package (--python, as python.org ships it); the
                                          launcher runs <exe dir>/python/pythonw.exe <exe dir>/tools/release/catalog.py
  tools/release/catalog.py                the setup engine (standard library only)
  data/titles/catalog.json                the games list catalog.py reads (../../data/titles from it)
  data/titles/library.example.json        a made-up library to look at the screens
  licenses/                               the port's LICENSE, SDL3 (zlib), Dear ImGui (MIT), each font's OFL text,
                                          Python (PSF; the package's LICENSE.txt)
  README.txt                              what the preview does and does not do

Refused (nothing written): a missing input, an exe (or the package's python.exe / pythonw.exe) that is not a 64-bit
Windows program, a --python folder that is not an unpacked embeddable package, an SDL3.dll that is not a 64-bit DLL or
a licence that is not SDL's, a symlink, a file with a PS3 game-data header (SELF, PSARC, PARAM.SFO) or a game file
extension, two entries whose paths differ only by letter case (one file on NTFS), a --version that is not X.Y.Z.
Deterministic: entries sorted by path, fixed 1980-01-01 dates, MS-DOS attributes, Deflate level 9, no extra fields or
comment, so the same inputs give the same bytes with the same Python/zlib. `<zip>.sha256` ("HASH  NAME", LF, no BOM;
sha256sum -c reads it) is written beside it. Exit 0 = written, 1 = refused, 2 = usage.
"""
import argparse
import hashlib
import os
import re
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402  (tools/release/names.py)
import stage_sdl3  # noqa: E402  (pe_x64_problem)

ROOT = os.path.dirname(os.path.dirname(HERE))
FIXED_DATE = (1980, 1, 1, 0, 0, 0)
# PS3 game data never ships: SELF/SPRX ("SCE\0"), PSARC, PARAM.SFO ("\0PSF"), and these extensions.
GAME_MAGICS = (b"SCE\0", b"PSAR", b"\0PSF")
GAME_EXTS = (".elf", ".self", ".sprx", ".pkg", ".psarc", ".sfo", ".rap", ".edat", ".iso")
PYTHON_NEEDS = ("python.exe", "pythonw.exe", "LICENSE.txt")
PYTHON_STDLIB = re.compile(r"python3\d*\.zip", re.I)        # the embeddable package's zipped standard library


def repo_file(root, rel):
    """A repository file in the private layout (public/<rel>) or the public tree's (<rel>)."""
    for c in (os.path.join(root, "public", rel), os.path.join(root, rel)):
        if os.path.isfile(c):
            return c
    return os.path.join(root, rel)


def python_version(python):
    """'3.13' from the package's python3NN.zip / python3NN._pth names, else ''."""
    for n in sorted(os.listdir(python)) if os.path.isdir(python) else []:
        m = re.fullmatch(r"python3(\d+)\.zip", n, re.I)
        if m:
            return f"3.{m.group(1)}"
    return ""


def readme(version, pyver=""):
    exe = names.LAUNCHER_EXE
    py = f"Python {pyver}" if pyver else "Python"
    return f"""{names.PRODUCT} {version}: launcher preview for Windows x64
{names.TAG}
{names.REPO_URL}

This is a preview of the launcher. It lists the games the port knows and shows which of your files it found, and
it lets you choose those files and folders by hand. It does not build or start a game yet: the Build button is a
stand-in that shows progress and builds nothing.

No game files are included, and nothing is downloaded. The port works only with your own copy of each game.

Getting started
  Unpack the whole folder to a place you can write to, and start {exe}.
  On the first start it looks for your games in RPCS3's usual folders (reading only) and writes library.json
  next to the launcher. Add the folders that hold your games under Settings, Locations.
  {py} is included (python\\): nothing else needs to be installed.

What is in this folder
  {exe}   the launcher
  SDL3.dll                     SDL 3 (zlib licence), which the launcher uses for its window and controllers
  fonts\\                       the launcher's fonts (SIL Open Font License)
  python\\                      {py} for Windows, the official embeddable package (PSF licence); it runs the
                               setup engine
  tools\\release\\catalog.py      the setup engine that looks for your files
  data\\titles\\catalog.json      the list of games the engine knows
  data\\titles\\library.example.json   a made-up library, to look at the screens:
                               {exe} --library data\\titles\\library.example.json
  licenses\\                    the licence texts

{names.NAME_NOTE}
The port's own code is under the MIT licence (licenses\\LICENSE.txt); the other licences are in licenses\\.
SHA-256 sums of the release files are in SHA256SUMS on the release page.
"""


def gather(exe, sdl3, root, python):
    """-> (problems, [(zip path, source path or bytes)])."""
    problems, files = [], []

    def need(rel, src):
        if not os.path.isfile(src):
            problems.append(f"{rel}: missing ({src})")
        files.append((rel, src))

    if os.path.isfile(exe):
        bad = exe_problem(exe)
        if bad:
            problems.append(f"{exe}: {bad}")
    need(names.LAUNCHER_EXE, exe)
    dll = os.path.join(sdl3, "SDL3.dll")
    lic = os.path.join(sdl3, "LICENSE.txt")
    if os.path.isfile(dll):
        bad = stage_sdl3.pe_x64_problem(dll)
        if bad:
            problems.append(f"{dll}: {bad}")
    if os.path.isfile(lic):
        with open(lic, encoding="utf-8", errors="replace") as f:
            t = f.read()
        if "Sam Lantinga" not in t or "provided 'as-is'" not in t:
            problems.append(f"{lic}: not SDL's zlib licence text")
    need("SDL3.dll", dll)
    need("licenses/SDL3-LICENSE.txt", lic)
    fonts = os.path.join(root, "launcher", "fonts")
    ttf = sorted(n for n in os.listdir(fonts) if n.lower().endswith(".ttf")) if os.path.isdir(fonts) else []
    ofl = sorted(n for n in os.listdir(fonts) if n.startswith("OFL")) if os.path.isdir(fonts) else []
    if not ttf or not ofl:
        problems.append(f"{fonts}: no fonts or no OFL texts")
    for n in ttf:
        need(f"fonts/{n}", os.path.join(fonts, n))
    for n in ofl:
        need(f"licenses/{n}", os.path.join(fonts, n))
    problems += gather_python(python, files)
    need("licenses/imgui-LICENSE.txt", os.path.join(root, "third_party", "imgui", "LICENSE.txt"))
    need("licenses/LICENSE.txt", repo_file(root, "LICENSE"))
    need("tools/release/catalog.py", os.path.join(root, "tools", "release", "catalog.py"))
    need("data/titles/catalog.json", os.path.join(root, "data", "titles", "catalog.json"))
    need("data/titles/library.example.json", os.path.join(root, "data", "titles", "library.example.json"))
    return problems, files


def gather_python(python, files):
    """The embeddable package as python/<its own names> (case kept), its licence as licenses/Python-LICENSE.txt."""
    if not os.path.isdir(python):
        return [f"{python}: no Python embeddable package folder"]
    have = os.listdir(python)
    if not all(n in have for n in PYTHON_NEEDS) or not any(PYTHON_STDLIB.fullmatch(n) for n in have):
        return [f"{python}: not an unpacked Python embeddable package (needs {', '.join(PYTHON_NEEDS)}, python3NN.zip)"]
    problems = []
    for n in ("python.exe", "pythonw.exe"):
        bad = exe_problem(os.path.join(python, n))
        if bad:
            problems.append(f"{python}/{n}: {bad}")
    for d, dirs, fs in os.walk(python):
        dirs.sort()
        for n in sorted(fs):
            src = os.path.join(d, n)
            rel = "python/" + os.path.relpath(src, python).replace(os.sep, "/")
            if os.path.islink(src):
                problems.append(f"{rel}: symlink")
            else:
                files.append((rel, src))
    files.append(("licenses/Python-LICENSE.txt", os.path.join(python, "LICENSE.txt")))
    return problems


def exe_problem(path):
    """-> "" for a 64-bit Windows program (PE, AMD64, not a DLL), else why not."""
    bad = stage_sdl3.pe_x64_problem(path)
    if bad == "a PE but not a DLL":
        return ""
    return bad or "a DLL, not a program"


def content_problems(files):
    """Game data and NTFS case clashes, over the gathered files."""
    problems, seen = [], {}
    for rel, src in files:
        k = rel.casefold()
        if k in seen:
            problems.append(f"{rel}: differs from {seen[k]} only by letter case (one file on NTFS)")
        seen[k] = rel
        if rel.lower().endswith(GAME_EXTS):
            problems.append(f"{rel}: a game file extension")
        if isinstance(src, bytes):
            head = src[:4]
        elif os.path.isfile(src):
            with open(src, "rb") as f:
                head = f.read(4)
        else:
            continue
        if head in GAME_MAGICS:
            problems.append(f"{rel}: PS3 game data header")
    return problems


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def write_zip(out, top, files):
    part = out + ".partial"
    with zipfile.ZipFile(part, "w") as z:
        for rel, src in sorted(files):
            zi = zipfile.ZipInfo(f"{top}/{rel}", date_time=FIXED_DATE)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.create_system = 0
            zi.external_attr = 0x20                     # MS-DOS "archive"
            if isinstance(src, bytes):
                data = src
            else:
                with open(src, "rb") as f:
                    data = f.read()
            z.writestr(zi, data, compresslevel=9)
    os.replace(part, out)
    digest = sha256_file(out)
    with open(out + ".sha256", "w", encoding="utf-8", newline="\n") as f:
        f.write(f"{digest}  {os.path.basename(out)}\n")
    return digest


def main(argv=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--sdl3", required=True, help="folder with SDL3.dll and LICENSE.txt (stage_sdl3.py --out)")
    ap.add_argument("--python", required=True, help="the unpacked Python embeddable package (fetch_toolchain.py trees/python)")
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True, help="folder for the zip and its .sha256")
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--replace", action="store_true")
    a = ap.parse_args(argv)
    if not re.fullmatch(r"\d+\.\d+\.\d+", a.version):
        print(f"--version {a.version!r} is not X.Y.Z", file=out)
        return 2
    dest = os.path.abspath(os.path.join(a.out, names.launcher_zip_name(a.version)))
    if os.path.lexists(dest) and not a.replace:
        print(f"{dest} exists (--replace overwrites it)", file=out)
        return 1
    problems, files = gather(a.exe, a.sdl3, os.path.abspath(a.root), a.python)
    text = readme(a.version, python_version(a.python))
    files.append(("README.txt", text.replace("\n", "\r\n").encode("utf-8")))  # Notepad-friendly
    problems += content_problems(files)
    for p in problems:
        print(f"REFUSED {p}", file=out)
    if problems:
        print(f"zip refused: {len(problems)} problem(s); nothing written", file=out)
        return 1
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    digest = write_zip(dest, names.launcher_zip_top(a.version), files)
    print(f"wrote {dest}: {len(files)} files, {os.path.getsize(dest):,} bytes, sha256 {digest}", file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
