#!/usr/bin/env python3
"""Pack the release downloads for Windows x64 (launcher phase 4: one release, two downloads from the same file
list). Standard library only, Python 3.8+.

    pack_zip.py --make-kit-manifest KIT.json --toolkit work/ps3recomp-patched \\
                --toolchain work/release/toolchain-cache/trees --version X.Y.Z [--root R]
    pack_zip.py --exe build/launcher-release/launcher.exe --sdl3 work/release/sdl3 \\
                --python work/release/toolchain-cache/trees/python --version X.Y.Z --out DIR [--root R] [--replace]
                [--kit-manifest KIT.json --toolkit DIR --toolchain TREES]

The order of a release: (1) --make-kit-manifest writes the build kit's list (kit.json: every kit file's path, SHA-256
and size, the release, the full zip's URL and an estimate of its size); (2) the launcher is built with that file
(CMake -DLAUNCHER_KIT_MANIFEST=KIT.json compiles its SHA-256, release, URL and sizes in); (3) the zips. Without
--kit-manifest only the bare launcher zip is written (a launcher built without a kit list never downloads one).

From the Killzone 3 port's tools/release/pack_zip.py (same author, MIT): the same deterministic zip and .sha256 file.

The bare launcher zip (names.launcher_zip_name(version), every entry under names.launcher_zip_top(version)/):
  names.LAUNCHER_EXE                      the launcher (--exe; must be a 64-bit Windows program, not a DLL)
  SDL3.dll                                from --sdl3 (a folder stage_sdl3.py wrote: SDL3.dll + LICENSE.txt)
  fonts/*.ttf                             launcher/fonts (the launcher loads <exe dir>/fonts)
  python/                                 the Python embeddable package (--python, as python.org ships it); the
                                          launcher runs <exe dir>/python/pythonw.exe <exe dir>/tools/release/catalog.py
  tools/release/catalog.py, kit.py        the setup engine (offline) and the one module that downloads (on request)
  data/titles/catalog.json                the games list catalog.py reads (../../data/titles from it)
  data/titles/library.example.json        a made-up library to look at the screens
  licenses/                               the port's LICENSE, SDL3 (zlib), Dear ImGui (MIT), each font's OFL text,
                                          Python (PSF; the package's LICENSE.txt)
  README.txt                              what the download does, what goes online

--symbols FILE also writes names.symbols_zip_name(version) holding that split debug file, for the launcher's
"Get launcher symbols" button (kit.py symbols); refused unless the exe's .gnu_debuglink names the file and its CRC-32.

The full build zip (names.build_zip_name(version), under names.build_zip_top(version)/) = the same files (its own
README.txt) + the build kit + kit.json:
  port/, data/titles/ (the rest), tools/spu/, tools/release/firstrun.py, firstrun_check.py, cmake/*.cmake
                                          this release's source subset that firstrun.py builds from
  ps3recomp/                              the patched ps3recomp (--toolkit, with its .rc-patchset stamp)
  toolchain/llvm-mingw, cmake, ninja      the toolchain.json entries with a "kit" block (--toolchain = fetch_toolchain
                                          trees/), unmodified, the entry's "prune" patterns left out
  licenses/<kit_license>                  the licence texts of the toolchain.json entries that name one, CMake's
                                          LICENSE.rst, ps3recomp's LICENSE

Refused (nothing written): a missing input, an exe (or the package's python.exe / pythonw.exe) that is not a 64-bit
Windows program, a --python folder that is not an unpacked embeddable package, an SDL3.dll that is not a 64-bit DLL or
a licence that is not SDL's, a symlink, a file with a PS3 game-data header (SELF, PSARC, PARAM.SFO) or a game file
extension, two entries whose paths differ only by letter case (one file on NTFS), a --version that is not X.Y.Z; with
--kit-manifest also a kit file that differs from the list, a list of another version, and an exe that does not carry
the list's SHA-256 (built for another kit). Deterministic: entries sorted by path, fixed 1980-01-01 dates, MS-DOS
attributes, Deflate level 9, no extra fields or comment, so the same inputs give the same bytes with the same
Python/zlib. `<zip>.sha256` ("HASH  NAME", LF, no BOM; sha256sum -c reads it) is written beside each zip.
Exit 0 = written, 1 = refused, 2 = usage.
"""
import argparse
import fnmatch
import hashlib
import json
import os
import re
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402  (tools/release/names.py)
import stage_sdl3  # noqa: E402  (pe_x64_problem)
import kit as kitmod  # noqa: E402  (manifest_bytes, check_manifest)

ROOT = os.path.dirname(os.path.dirname(HERE))
FIXED_DATE = (1980, 1, 1, 0, 0, 0)
# PS3 game data never ships: SELF/SPRX ("SCE\0"), PSARC, PARAM.SFO ("\0PSF"), and these extensions.
GAME_MAGICS = (b"SCE\0", b"PSAR", b"\0PSF")
GAME_EXTS = (".elf", ".self", ".sprx", ".pkg", ".psarc", ".sfo", ".rap", ".edat", ".iso")
PYTHON_NEEDS = ("python.exe", "pythonw.exe", "LICENSE.txt")
PYTHON_STDLIB = re.compile(r"python3\d*\.zip", re.I)        # the embeddable package's zipped standard library
# the build kit's part of the source tree: folders (every file below, minus SKIP) and single files
KIT_TREE = ("port", "data/titles", "tools/spu", "cmake")
KIT_FILES = ("tools/release/firstrun.py", "tools/release/firstrun_check.py")
SKIP_DIRS = {".git", "__pycache__", ".firstrun"}
SKIP_FILES = re.compile(r".*\.(pyc|pyo)$|\.DS_Store$", re.I)
TOOLKIT_NEEDS = ("CMakeLists.txt", "tools/ppu_lifter.py", "tools/ppu_loader.py", ".rc-patchset", "LICENSE")
BARE_ALLOWANCE = 16 << 20       # the bare launcher's part of the full zip, compressed (launcher, Python, SDL3, fonts)


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


ONLINE = """What goes online
  Nothing, unless you choose it. The launcher asks before every download, and each one comes from this project's
  GitHub releases page only:
  - the build kit (bare launcher download only), on your first Build, after a dialog that names its size: every
    file is checked against the list built into the launcher, and anything that does not match is refused;
  - the launcher's debug symbols, only when you click "Get launcher symbols" in Settings;
  - "Check for new releases" in Settings (off by default): one request to GitHub's release list when the launcher
    starts or when you click Check now. Nothing is downloaded or installed by it.
  The online settings in Settings only write the shared online settings files the games will read; the games of
  this release do not go online. Game updates are never downloaded: the launcher uses the update you give it."""


def readme(version, pyver="", full=False):
    exe = names.LAUNCHER_EXE
    py = f"Python {pyver}" if pyver else "Python"
    if full:
        what = f"""This is the full build download: the launcher and the build kit, so building works without a connection.
The build kit is the port's source code of this release, the recompiler (ps3recomp, with the port's changes), the
compiler (llvm-mingw), CMake and Ninja. The launcher finds it next to itself (kit.json lists every file)."""
        files = """  port\\, ps3recomp\\, cmake\\,
  tools\\spu\\, data\\titles\\     the build kit: what the build is made from
  toolchain\\                   llvm-mingw, CMake and Ninja (their licence texts are in licenses\\)
  kit.json                     the build kit's file list
"""
    else:
        what = f"""This is the launcher download. It lists the games the port knows, finds your files, and builds and starts
the games. To build, it needs the build kit (the port's source code, the recompiler and the compiler, about the size
of the full build download): on your first Build it asks before downloading it. The full build download
({names.build_zip_name(version)}) already holds it."""
        files = ""
    return f"""{names.PRODUCT} {version} for Windows x64
{names.TAG}
{names.REPO_URL}

{what}

No game files are included. The port works only with your own copy of each game and the decrypted EBOOT made from it.

Getting started
  Unpack the whole folder to a place you can write to, and start {exe}.
  On the first start it looks for your games in RPCS3's usual folders (reading only) and writes library.json
  next to the launcher. Add the folders that hold your games under Settings, Locations.
  {py} is included (python\\): nothing else needs to be installed.

What is in this folder
  {exe}   the launcher
  SDL3.dll                     SDL 3 (zlib licence), which the launcher and the games use for windows and controllers
  fonts\\                       the launcher's fonts (SIL Open Font License)
  python\\                      {py} for Windows, the official embeddable package (PSF licence); it runs the
                               setup engine
  tools\\release\\               the setup engine (catalog.py) and the downloader (kit.py)
{files}  data\\titles\\catalog.json      the list of games the engine knows
  data\\titles\\library.example.json   a made-up library, to look at the screens:
                               {exe} --library data\\titles\\library.example.json
  licenses\\                    the licence texts

{ONLINE}

{names.NAME_NOTE}
The port's own code is under the MIT licence (licenses\\LICENSE.txt); the other licences are in licenses\\.
SHA-256 sums of the release files are in SHA256SUMS on the release page.
"""


def gather(exe, sdl3, root, python):
    """The bare launcher -> (problems, [(zip path, source path or bytes)])."""
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
    need("tools/release/kit.py", os.path.join(root, "tools", "release", "kit.py"))
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


def walk_tree(top, prefix, prune=(), problems=None):
    """[(prefix/rel, src)] of every file under top, sorted, minus SKIP_DIRS / SKIP_FILES and the `prune` patterns
    (fnmatch on the path relative to top, forward slashes). A symlink is a problem."""
    out = []
    for d, dirs, fs in os.walk(top):
        dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS)
        for n in sorted(fs):
            src = os.path.join(d, n)
            rel = os.path.relpath(src, top).replace(os.sep, "/")
            if SKIP_FILES.fullmatch(n) or any(fnmatch.fnmatchcase(rel, p) for p in prune):
                continue
            if os.path.islink(src):
                if problems is not None:
                    problems.append(f"{prefix}/{rel}: symlink")
                continue
            out.append((f"{prefix}/{rel}", src))
    return out


def gather_kit(root, toolkit, trees, toolchain_manifest=None, bare=()):
    """The build kit -> (problems, [(zip path, source)]). `bare` = the bare launcher's zip paths (never repeated)."""
    problems, files = [], []
    for rel in KIT_TREE:
        top = os.path.join(root, *rel.split("/"))
        if not os.path.isdir(top):
            problems.append(f"{rel}/: missing ({top})")
            continue
        files += walk_tree(top, rel, problems=problems)
    for rel in KIT_FILES:
        src = os.path.join(root, *rel.split("/"))
        if not os.path.isfile(src):
            problems.append(f"{rel}: missing ({src})")
        files.append((rel, src))
    miss = [n for n in TOOLKIT_NEEDS if not os.path.isfile(os.path.join(toolkit, *n.split("/")))]
    if miss:
        problems.append(f"{toolkit}: not a patched ps3recomp tree (missing {', '.join(miss)})")
    else:
        files += walk_tree(toolkit, "ps3recomp", problems=problems)
        files.append(("licenses/ps3recomp-LICENSE.txt", os.path.join(toolkit, "LICENSE")))
    m = toolchain_manifest or json.load(open(os.path.join(HERE, "toolchain.json"), encoding="utf-8"))
    for c in m["components"]:
        tree = os.path.join(trees, c["name"])
        if c.get("kit"):
            k = c["kit"]
            if not os.path.isdir(tree):
                problems.append(f"{c['name']}: no unpacked tree ({tree}; run fetch_toolchain.py)")
                continue
            files += walk_tree(tree, k["dest"], k.get("prune", ()), problems)
            if k.get("license"):
                lic = os.path.join(tree, *k["license"].split("/"))
                if not os.path.isfile(lic):
                    problems.append(f"{c['name']}: licence {k['license']} missing")
                files.append((f"licenses/{c['name']}/{os.path.basename(lic)}", lic))
        if c.get("kit_license"):
            src = os.path.join(tree, c.get("file_name", ""))
            if not os.path.isfile(src):
                problems.append(f"{c['name']}: licence text missing ({src}; run fetch_toolchain.py)")
            files.append((f"licenses/{c['kit_license']}", src))
    skip = set(bare)
    return problems, [(r, s) for r, s in files if r not in skip]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def deflated_size(src):
    """The size of src's data after Deflate level 9 (what write_zip stores; a stored entry when it does not shrink)."""
    c, n, raw = zlib.compressobj(9, zlib.DEFLATED, -15), 0, 0
    with open(src, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            raw += len(blk)
            n += len(c.compress(blk))
    return n + len(c.flush())


def make_kit_manifest(files, version, estimate=True):
    """kit.json for the gathered kit files (sorted by path)."""
    entries, unpacked, packed = [], 0, BARE_ALLOWANCE
    for rel, src in sorted(files):
        size = os.path.getsize(src)
        entries.append({"path": rel, "sha256": sha256_file(src), "size": size})
        unpacked += size
        packed += (deflated_size(src) if estimate else size) + 2 * (46 + len(rel.encode())) + 30
    name = names.build_zip_name(version)
    return {"format": kitmod.FORMAT, "release": version, "product": names.PRODUCT, "download_file": name,
            "download_url": names.release_asset_url(version, name), "download_bytes": packed,
            "unpacked_bytes": unpacked, "files": entries}


def kit_problems(manifest, files, version):
    """The gathered kit against a kit.json: same version, same paths, same hashes and sizes."""
    bad = kitmod.check_manifest(manifest)
    if bad:
        return bad
    if manifest.get("release") != version:
        return [f"kit manifest is for release {manifest.get('release')}, not {version}"]
    want = {f["path"]: f for f in manifest["files"]}
    have = dict(files)
    for p in sorted(set(want) - set(have)):
        bad.append(f"{p}: in the kit manifest, not in the kit")
    for p in sorted(set(have) - set(want)):
        bad.append(f"{p}: in the kit, not in the kit manifest (make it again)")
    for p in sorted(set(want) & set(have)):
        src = have[p]
        if os.path.getsize(src) != want[p]["size"] or sha256_file(src) != want[p]["sha256"]:
            bad.append(f"{p}: changed since the kit manifest was made")
    return bad


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


def write_zip(out, top, files):
    part = out + ".partial"
    with zipfile.ZipFile(part, "w", allowZip64=True) as z:
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
    ap.add_argument("--exe")
    ap.add_argument("--sdl3", help="folder with SDL3.dll and LICENSE.txt (stage_sdl3.py --out)")
    ap.add_argument("--python", help="the unpacked Python embeddable package (fetch_toolchain.py trees/python)")
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", help="folder for the zips and their .sha256")
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--replace", action="store_true")
    ap.add_argument("--make-kit-manifest", metavar="FILE", help="write the build kit's list (kit.json) and stop")
    ap.add_argument("--kit-manifest", metavar="FILE", help="also write the full build zip, with this kit.json")
    ap.add_argument("--toolkit", help="the patched ps3recomp tree (work/ps3recomp-patched)")
    ap.add_argument("--toolchain", help="fetch_toolchain.py's trees/ folder (llvm-mingw, cmake, ninja, licence texts)")
    ap.add_argument("--symbols", metavar="FILE", help="the launcher's split debug file: also writes "
                    "names.symbols_zip_name; refused unless the exe's .gnu_debuglink names this file and its CRC-32")
    ap.add_argument("--no-estimate", action="store_true", help=argparse.SUPPRESS)   # tests: sizes instead of Deflate
    a = ap.parse_args(argv)
    if not re.fullmatch(r"\d+\.\d+\.\d+", a.version):
        print(f"--version {a.version!r} is not X.Y.Z", file=out)
        return 2
    root = os.path.abspath(a.root)
    kit_wanted = a.make_kit_manifest or a.kit_manifest
    if kit_wanted and not (a.toolkit and a.toolchain):
        print("the build kit needs --toolkit and --toolchain", file=out)
        return 2
    if a.make_kit_manifest:
        bare = {r for r, _ in gather("", "", root, "")[1]} | {"README.txt"}
        problems, kfiles = gather_kit(root, os.path.abspath(a.toolkit), os.path.abspath(a.toolchain), bare=bare)
        problems += content_problems(kfiles)
        for p in problems:
            print(f"REFUSED {p}", file=out)
        if problems:
            print(f"kit manifest refused: {len(problems)} problem(s); nothing written", file=out)
            return 1
        m = make_kit_manifest(kfiles, a.version, estimate=not a.no_estimate)
        data = kitmod.manifest_bytes(m)
        os.makedirs(os.path.dirname(os.path.abspath(a.make_kit_manifest)), exist_ok=True)
        with open(a.make_kit_manifest, "wb") as f:
            f.write(data)
        print(f"wrote {a.make_kit_manifest}: {len(m['files'])} files, {m['unpacked_bytes']:,} bytes unpacked, "
              f"download about {m['download_bytes']:,} bytes, sha256 {hashlib.sha256(data).hexdigest()}", file=out)
        return 0
    if not (a.exe and a.sdl3 and a.python and a.out):
        print("the zips need --exe, --sdl3, --python and --out", file=out)
        return 2
    dest = os.path.abspath(os.path.join(a.out, names.launcher_zip_name(a.version)))
    full = os.path.abspath(os.path.join(a.out, names.build_zip_name(a.version)))
    sym = os.path.abspath(os.path.join(a.out, names.symbols_zip_name(a.version)))
    for d in [dest] + ([full] if a.kit_manifest else []) + ([sym] if a.symbols else []):
        if os.path.lexists(d) and not a.replace:
            print(f"{d} exists (--replace overwrites it)", file=out)
            return 1
    problems, files = gather(a.exe, a.sdl3, root, a.python)
    pyver = python_version(a.python)
    files.append(("README.txt", readme(a.version, pyver).replace("\n", "\r\n").encode("utf-8")))  # Notepad-friendly
    problems += content_problems(files)
    kfiles, mbytes = [], b""
    if a.kit_manifest:
        try:
            with open(a.kit_manifest, "rb") as f:
                mbytes = f.read()
            manifest = json.loads(mbytes.decode("utf-8"))
        except (OSError, ValueError) as e:
            manifest = None
            problems.append(f"{a.kit_manifest}: not readable ({e})")
        kp, kfiles = gather_kit(root, os.path.abspath(a.toolkit), os.path.abspath(a.toolchain), bare={r for r, _ in files})
        problems += kp
        if manifest is not None:
            problems += kit_problems(manifest, kfiles, a.version)
            if mbytes != kitmod.manifest_bytes(manifest):
                problems.append(f"{a.kit_manifest}: not in kit.py's byte form (written by hand?)")
        digest = hashlib.sha256(mbytes).hexdigest().encode("ascii")
        if os.path.isfile(a.exe):
            with open(a.exe, "rb") as f:
                if digest not in f.read():
                    problems.append(f"{a.exe}: built without this kit manifest (its SHA-256 is not in the exe)")
        problems += content_problems(kfiles)
    symdata = b""
    if a.symbols:
        try:
            with open(a.symbols, "rb") as f:
                symdata = f.read()
            link = kitmod.debuglink(a.exe)
            if not link:
                problems.append(f"{a.exe}: no .gnu_debuglink (built without LAUNCHER_SYMBOLS=1)")
            elif link[0] != os.path.basename(a.symbols):
                problems.append(f"{a.symbols}: the exe names the debug file {link[0]}")
            elif zlib.crc32(symdata) & 0xFFFFFFFF != link[1]:
                problems.append(f"{a.symbols}: its CRC-32 is not the one the exe names")
        except (OSError, ValueError) as e:
            problems.append(f"{a.symbols}: cannot check against the exe ({e})")
    for p in problems:
        print(f"REFUSED {p}", file=out)
    if problems:
        print(f"zip refused: {len(problems)} problem(s); nothing written", file=out)
        return 1
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    digest = write_zip(dest, names.launcher_zip_top(a.version), files)
    print(f"wrote {dest}: {len(files)} files, {os.path.getsize(dest):,} bytes, sha256 {digest}", file=out)
    if a.symbols:
        digest = write_zip(sym, names.launcher_zip_top(a.version) + "-symbols", [(os.path.basename(a.symbols), symdata)])
        print(f"wrote {sym}: 1 file, {os.path.getsize(sym):,} bytes, sha256 {digest}", file=out)
    if a.kit_manifest:
        ffiles = [(r, s) for r, s in files if r != "README.txt"] + kfiles + [
            (names.KIT_MANIFEST, mbytes),
            ("README.txt", readme(a.version, pyver, full=True).replace("\n", "\r\n").encode("utf-8"))]
        digest = write_zip(full, names.build_zip_top(a.version), ffiles)
        print(f"wrote {full}: {len(ffiles)} files, {os.path.getsize(full):,} bytes, sha256 {digest}", file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
