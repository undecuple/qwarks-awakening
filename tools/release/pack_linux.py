#!/usr/bin/env python3
"""Pack the Linux launcher download (linux-launcher-plan.md s.6): one tar.gz with the native launcher, its engine, the
build files and install.sh. Standard library only, Python 3.8+.

    pack_linux.py --program build/launcher-linux/qwarks-awakening --sdl3-licenses build/launcher-linux/licenses \\
                  --toolkit work/ps3recomp-patched --version X.Y.Z --out DIR [--root R] [--replace] [--glibc-max 2.35]
    pack_linux.py --check-elf FILE [--glibc-max 2.35]     the program's imports only (prints NEEDED and the glibc level)
    pack_linux.py --list ...                               print the file list (the same arguments as a pack), write nothing

The tarball (names.linux_tar_name(version), every entry under names.linux_tar_top(version)/):
  qwarks-awakening                    the launcher (names.LINUX_PROGRAM; SDL3, libstdc++ and libgcc linked in)
  install.sh                          user-local install / --uninstall (launcher/linux/install.sh)
  qwarks-awakening.desktop, .png      the menu entry template (launcher/) and its icon (the 256 px image of
                                      installer/art/rc-launcher.ico)
  fonts/*.ttf                         launcher/fonts
  tools/release/catalog.py, kit.py, proton.py, linux_kit.py, fetch_toolchain.py, toolchain.json
                                      the engine: library and settings, the opt-in release check, Play through Proton,
                                      the Linux build tools; run with the system's python3
  port/, data/titles/, tools/spu/, cmake/, tools/release/firstrun.py, firstrun_check.py, ps3recomp/
                                      the build files: the same source subset and patched ps3recomp as the Windows full
                                      build zip's kit (pack_zip.KIT_TREE / KIT_FILES), without its Windows toolchain/
                                      (Linux players fetch llvm-mingw, CMake and Ninja for Linux with linux_kit.py)
  licenses/                           the port's LICENSE, SDL3 (zlib) and the two third-party parts SDL links in
                                      (HIDAPI, BSD-style; yuv2rgb, BSD-3-Clause), Dear ImGui (MIT), each font's OFL
                                      text, ps3recomp (MIT); NOTICE.md beside them
  README.txt                          what the download holds, requirements, install and removal

Refused (nothing written): a missing input; a program that is not an x86-64 ELF executable for the standard
interpreter (/lib64/ld-linux-x86-64.so.2), that imports a library outside the C library's own (ALLOWED_NEEDED: a
shared SDL3, libstdc++ or anything else would have to be bundled), that carries an RPATH/RUNPATH, that needs a glibc
symbol version newer than --glibc-max (default 2.35: Ubuntu 22.04, the release runner), or that contains the build
machine's root folder name; a symlink; a PS3 game file (pack_zip.content_problems); two paths differing only by
letter case. Deterministic: entries sorted, directories listed, mtime 1980-01-01, owner 0/0 with no names, modes 0755
(directories, the program, install.sh and files executable in the source) or 0644, gzip without a name or time, so the
same inputs give the same bytes with the same Python/zlib.
Exit 0 = written, 1 = refused, 2 = usage.
"""
import argparse
import gzip
import hashlib
import io
import os
import re
import struct
import sys
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402
import pack_zip  # noqa: E402  (KIT_TREE, KIT_FILES, TOOLKIT_NEEDS, walk_tree, repo_file, content_problems)

ROOT = os.path.dirname(os.path.dirname(HERE))
FIXED_MTIME = 315532800                       # 1980-01-01 00:00:00 UTC, as the zips
INTERP = b"/lib64/ld-linux-x86-64.so.2"
# The C library's own parts (glibc 2.34+ merged libpthread, libdl and librt into libc; older names stay allowed)
ALLOWED_NEEDED = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1", "ld-linux-x86-64.so.2"}
GLIBC_MAX = "2.35"
ENGINE = ("catalog.py", "kit.py", "proton.py", "linux_kit.py", "fetch_toolchain.py", "toolchain.json")
ICON_SIZE = 256
# --sdl3-licenses DIR (build_launcher_linux.sh OUTDIR/licenses): SDL's own text and the parts of its source it links in
SDL3_LICENSES = {"SDL3-LICENSE.txt": ("Sam Lantinga", "provided 'as-is'"),
                 **{n: w for n, (_src, w) in pack_zip.stage_sdl3.PARTS.items()}}   # the same parts as the Windows zips


# ------------------------------------------------------------------------------------------------------------ ELF
def _vkey(v):
    return tuple(int(x) for x in v.split("."))


def elf_info(path):
    """-> {"interp", "needed", "rpath", "glibc", "type"} of a 64-bit little-endian x86-64 ELF, or raises ValueError."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    if d[4] != 2 or d[5] != 1:
        raise ValueError("not a 64-bit little-endian ELF")
    e_type, e_machine = struct.unpack_from("<HH", d, 16)
    if e_machine != 62:
        raise ValueError("not an x86-64 program")
    e_phoff, e_shoff = struct.unpack_from("<QQ", d, 32)
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from("<HHHH", d, 54)
    interp, dyn = None, None
    loads = []
    for i in range(e_phnum):
        p_type, _flags, p_off, p_vaddr, _pa, p_filesz, _memsz, _al = struct.unpack_from("<IIQQQQQQ", d, e_phoff + i * e_phentsize)
        if p_type == 3:
            interp = d[p_off:p_off + p_filesz].rstrip(b"\0")
        elif p_type == 2:
            dyn = (p_off, p_filesz)
        elif p_type == 1:
            loads.append((p_vaddr, p_off, p_filesz))

    def off_of(vaddr):
        for va, off, sz in loads:
            if va <= vaddr < va + sz:
                return off + (vaddr - va)
        raise ValueError(f"address {vaddr:#x} outside the file")

    entries = []
    if dyn:
        for o in range(dyn[0], dyn[0] + dyn[1], 16):
            tag, val = struct.unpack_from("<qQ", d, o)
            if tag == 0:
                break
            entries.append((tag, val))
    strtab = next((off_of(v) for t, v in entries if t == 5), None)

    def s(o):
        return d[strtab + o:d.index(b"\0", strtab + o)].decode("ascii", "replace")

    needed = [s(v) for t, v in entries if t == 1] if strtab is not None else []
    rpath = [s(v) for t, v in entries if t in (15, 29)] if strtab is not None else []
    # glibc symbol versions the program needs: the Verneed entries (DT_VERNEED 0x6ffffffe, DT_VERNEEDNUM 0x6fffffff)
    glibc = []
    vn = next((v for t, v in entries if t == 0x6FFFFFFE), None)
    vnum = next((v for t, v in entries if t == 0x6FFFFFFF), 0)
    if vn is not None and strtab is not None:
        o = off_of(vn)
        for _ in range(vnum):
            _ver, cnt, _file, aux, nxt = struct.unpack_from("<HHIII", d, o)
            a = o + aux
            for _ in range(cnt):
                _hash, _fl, _other, name, anext = struct.unpack_from("<IHHII", d, a)
                n = s(name)
                if n.startswith("GLIBC_") and re.fullmatch(r"GLIBC_\d+(\.\d+)*", n):
                    glibc.append(n[6:])
                if not anext:
                    break
                a += anext
            if not nxt:
                break
            o += nxt
    return {"type": e_type, "interp": interp, "needed": needed, "rpath": rpath,
            "glibc": max(glibc, key=_vkey) if glibc else ""}


def program_problems(path, glibc_max=GLIBC_MAX, root=None):
    try:
        info = elf_info(path)
    except (OSError, ValueError, struct.error) as e:
        return [f"{path}: {e}"]
    bad = []
    if info["type"] not in (2, 3):
        bad.append(f"{path}: not an executable (ELF type {info['type']})")
    if info["interp"] != INTERP:
        bad.append(f"{path}: interpreter {info['interp']!r}, not {INTERP.decode()} (built against a non-standard C library?)")
    extra = sorted(set(info["needed"]) - ALLOWED_NEEDED)
    if extra:
        bad.append(f"{path}: needs {', '.join(extra)} (only the C library's own libraries are allowed; link it in)")
    if info["rpath"]:
        bad.append(f"{path}: carries an RPATH/RUNPATH {info['rpath']}")
    if info["glibc"] and _vkey(info["glibc"]) > _vkey(glibc_max):
        bad.append(f"{path}: needs glibc {info['glibc']}, newer than {glibc_max}")
    if root:   # the folder as a path of its own (not inside a longer one: "sdl3/src/..." is not "/src")
        with open(path, "rb") as f:
            rx = re.compile(rb"(?<![\w./-])" + re.escape(os.path.abspath(root).encode()) + rb"(?=/|\0)")
            if rx.search(f.read()):
                bad.append(f"{path}: contains the build machine's folder {root}")
    return bad


# ---------------------------------------------------------------------------------------------------------- icon
def ico_png(path, size=ICON_SIZE):
    """The PNG image of `size` px stored in an .ico (an entry width/height of 0 means 256), or raises ValueError."""
    with open(path, "rb") as f:
        d = f.read()
    _r, typ, n = struct.unpack_from("<HHH", d, 0)
    if typ != 1:
        raise ValueError("not an icon file")
    for i in range(n):
        w, h, _c, _r2, _p, _b, sz, off = struct.unpack_from("<BBBBHHII", d, 6 + 16 * i)
        if (w or 256) == size and (h or 256) == size and d[off:off + 8] == b"\x89PNG\r\n\x1a\n":
            return d[off:off + sz]
    raise ValueError(f"no {size} px PNG image in it")


# ---------------------------------------------------------------------------------------------------------- files
def readme(version):
    p = names.LINUX_PROGRAM
    return f"""{names.PRODUCT} {version} for Linux x86-64
{names.TAG}
{names.REPO_URL}

The launcher lists the games the port knows, finds your files, builds each game from your own copy and starts it.
The games are Windows programs: on Linux the launcher runs natively and starts each game through Proton, with one
Wine prefix per game. Steam does not need to be installed. This download also holds the build files (the port's
source code of this release and the recompiler, ps3recomp, with the port's changes), so Build needs no other
download from this project. No game files are included. The port works only with your own copy of each game and the
decrypted EBOOT made from it.

Requirements
  - Linux on x86-64 with glibc 2.34 or newer (built on Ubuntu 22.04 and checked there) and an X11 or Wayland
    desktop.
  - Python 3.10 or newer as python3 (the launcher's engine; nothing else to install for it).
  - To play: the 64-bit Vulkan driver of your GPU. The launcher downloads the GE-Proton release the port is tested
    with when you choose it in Settings > Proton, or uses a Proton you already have.
  - To build a game: Settings > Building > "Linux build tools" downloads llvm-mingw, CMake, Ninja and SDL3's
    package after you confirm.

Install (for your user only, no administrator rights)
  tar xf {names.linux_tar_name(version)}
  cd {names.linux_tar_top(version)}
  ./install.sh
Then start "Qwark's Awakening" from your applications menu, or run ~/.local/bin/{p}.
install.sh copies this folder to ~/.local/share/qwarks-awakening/launcher/ ($XDG_DATA_HOME is used when set), links
~/.local/bin/{p} to it and adds a menu entry and its icon. Running the install.sh of a newer download replaces it.
To remove it: ~/.local/share/qwarks-awakening/launcher/install.sh --uninstall. That keeps your library, built games,
prefixes, Proton runtimes, build tools, saves and settings; it prints where they are.
Without installing: run ./{p} from this folder.

What is in this folder
  {p}            the launcher (SDL 3 is built into it)
  install.sh                  install for your user, or --uninstall
  tools/release/              the launcher's engine (Python): library, Play through Proton, the downloads
  port/, ps3recomp/, cmake/,
  tools/spu/, data/titles/    the build files: what a game's build is made from
  fonts/                      the launcher's fonts (SIL Open Font License)
  licenses/, NOTICE.md        the licence texts and credits: the port (MIT), SDL 3 (zlib) with the HIDAPI and
                              yuv2rgb code it includes (BSD), Dear ImGui (MIT), the fonts (OFL), ps3recomp (MIT)

What goes online
  Nothing, unless you choose it. The launcher asks before every download: GE-Proton from its own GitHub release, the
  Linux build tools from each project's own release (each checked against a pinned SHA-256), and "Check for new
  releases" in Settings (off by default), one request to this project's GitHub release list.

{names.NAME_NOTE}
The port's own code is under the MIT licence (licenses/LICENSE.txt); the other licences are in licenses/.
SHA-256 sums of the release files are in SHA256SUMS on the release page.
"""


def gather(program, sdl3_licenses, toolkit, root, version):
    """-> (problems, [(tar path, source path or bytes, executable)])."""
    problems, files = [], []

    def need(rel, src, exe=False):
        if not os.path.isfile(src):
            problems.append(f"{rel}: missing ({src})")
        files.append((rel, src, exe))

    need(names.LINUX_PROGRAM, program, True)
    need("install.sh", os.path.join(root, "launcher", "linux", "install.sh"), True)
    need(f"{names.LINUX_STEM}.desktop", os.path.join(root, "launcher", "qwarks-awakening.desktop"))
    try:
        files.append((f"{names.LINUX_STEM}.png", ico_png(os.path.join(root, "installer", "art", "rc-launcher.ico")), False))
    except (OSError, ValueError, struct.error) as e:
        problems.append(f"installer/art/rc-launcher.ico: {e}")
    fonts = os.path.join(root, "launcher", "fonts")
    ls = sorted(os.listdir(fonts)) if os.path.isdir(fonts) else []
    ttf = [n for n in ls if n.lower().endswith(".ttf")]
    ofl = [n for n in ls if n.startswith("OFL")]
    if not ttf or not ofl:
        problems.append(f"{fonts}: no fonts or no OFL texts")
    for n in ttf:
        need(f"fonts/{n}", os.path.join(fonts, n))
    for n in ofl:
        need(f"licenses/{n}", os.path.join(fonts, n))
    for n, words in SDL3_LICENSES.items():
        src = os.path.join(sdl3_licenses, n)
        if os.path.isfile(src):
            with open(src, encoding="utf-8", errors="replace") as f:
                t = f.read()
            if not all(w in t for w in words):
                problems.append(f"{src}: not the expected licence text ({words[0]})")
        need(f"licenses/{n}", src)
    need("licenses/imgui-LICENSE.txt", os.path.join(root, "third_party", "imgui", "LICENSE.txt"))
    need("licenses/LICENSE.txt", pack_zip.repo_file(root, "LICENSE"))
    need("NOTICE.md", pack_zip.repo_file(root, "NOTICE.md"))
    for n in ENGINE:
        need(f"tools/release/{n}", os.path.join(root, "tools", "release", n))
    # the build files: the Windows kit's source subset and toolkit, never its toolchain/
    for rel in pack_zip.KIT_TREE:
        top = os.path.join(root, *rel.split("/"))
        if not os.path.isdir(top):
            problems.append(f"{rel}/: missing ({top})")
            continue
        files += [(r, s, os.access(s, os.X_OK)) for r, s in pack_zip.walk_tree(top, rel, problems=problems)]
    for rel in pack_zip.KIT_FILES:
        need(rel, os.path.join(root, *rel.split("/")))
    miss = [n for n in pack_zip.TOOLKIT_NEEDS if not os.path.isfile(os.path.join(toolkit, *n.split("/")))]
    if miss:
        problems.append(f"{toolkit}: not a patched ps3recomp tree (missing {', '.join(miss)})")
    else:
        files += [(r, s, os.access(s, os.X_OK)) for r, s in pack_zip.walk_tree(toolkit, "ps3recomp", problems=problems)]
        need("licenses/ps3recomp-LICENSE.txt", os.path.join(toolkit, "LICENSE"))
    files.append(("README.txt", readme(version).encode("utf-8"), False))
    seen = {}
    for rel, _s, _x in files:
        if rel in seen:
            problems.append(f"{rel}: listed twice")
        seen[rel] = 1
    problems += pack_zip.content_problems([(r, s) for r, s, _x in files])
    return problems, sorted(files, key=lambda x: x[0])


def write_tar(out, top, files):
    """The deterministic tar.gz; -> its SHA-256."""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as t:
        dirs = {top}
        for rel, _s, _x in files:
            parts = rel.split("/")[:-1]
            for i in range(1, len(parts) + 1):
                dirs.add(top + "/" + "/".join(parts[:i]))

        def info(name, size, mode, kind=tarfile.REGTYPE):
            ti = tarfile.TarInfo(name)
            ti.size, ti.mode, ti.type, ti.mtime = size, mode, kind, FIXED_MTIME
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = ""
            return ti

        entries = [(d, None, None) for d in dirs] + [(f"{top}/{r}", s, x) for r, s, x in files]
        for name, src, exe in sorted(entries, key=lambda e: e[0]):
            if src is None:
                t.addfile(info(name, 0, 0o755, tarfile.DIRTYPE))
                continue
            if isinstance(src, bytes):
                data = src
            else:
                with open(src, "rb") as f:
                    data = f.read()
            t.addfile(info(name, len(data), 0o755 if exe else 0o644), io.BytesIO(data))
    part = out + ".partial"
    with open(part, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as g:
            g.write(buf.getvalue())
    os.replace(part, out)
    return pack_zip.sha256_file(out)


def main(argv=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--program", help="the built launcher (build_launcher_linux.sh OUTDIR/qwarks-awakening)")
    ap.add_argument("--sdl3-licenses", help="SDL3's licence texts (build_launcher_linux.sh OUTDIR/licenses)")
    ap.add_argument("--toolkit", help="the patched ps3recomp tree (work/ps3recomp-patched)")
    ap.add_argument("--version")
    ap.add_argument("--out", help="folder for the tarball")
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--replace", action="store_true")
    ap.add_argument("--glibc-max", default=GLIBC_MAX)
    ap.add_argument("--check-elf", metavar="FILE")
    ap.add_argument("--list", action="store_true", help="print the file list and stop")
    a = ap.parse_args(argv)
    if a.check_elf:
        try:
            info = elf_info(a.check_elf)
        except (OSError, ValueError, struct.error) as e:
            print(f"REFUSED {a.check_elf}: {e}", file=out)
            return 1
        print(f"interpreter {info['interp'].decode() if info['interp'] else '-'}", file=out)
        print(f"needed {' '.join(info['needed']) or '-'}", file=out)
        print(f"rpath {' '.join(info['rpath']) or '-'}", file=out)
        print(f"glibc {info['glibc'] or '-'} (newest symbol version it needs)", file=out)
        bad = program_problems(a.check_elf, a.glibc_max)
        for p in bad:
            print(f"REFUSED {p}", file=out)
        return 1 if bad else 0
    if not (a.program and a.sdl3_licenses and a.toolkit and a.version and (a.out or a.list)):
        print("a pack needs --program, --sdl3-licenses, --toolkit, --version and --out (or --list)", file=out)
        return 2
    if not re.fullmatch(r"\d+\.\d+\.\d+", a.version):
        print(f"--version {a.version!r} is not X.Y.Z", file=out)
        return 2
    root = os.path.abspath(a.root)
    problems, files = gather(os.path.abspath(a.program), os.path.abspath(a.sdl3_licenses), os.path.abspath(a.toolkit),
                             root, a.version)
    if os.path.isfile(a.program):
        problems += program_problems(a.program, a.glibc_max, root)
    if a.list:
        for rel, src, exe in files:
            size = len(src) if isinstance(src, bytes) else (os.path.getsize(src) if os.path.isfile(src) else 0)
            print(f"{'755' if exe else '644'} {size:>10} {rel}", file=out)
    for p in problems:
        print(f"REFUSED {p}", file=out)
    if problems:
        print(f"tarball refused: {len(problems)} problem(s); nothing written", file=out)
        return 1
    if a.list:
        return 0
    dest = os.path.abspath(os.path.join(a.out, names.linux_tar_name(a.version)))
    if os.path.lexists(dest) and not a.replace:
        print(f"{dest} exists (--replace overwrites it)", file=out)
        return 1
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    digest = write_tar(dest, names.linux_tar_top(a.version), files)
    print(f"wrote {dest}: {len(files)} files, {os.path.getsize(dest):,} bytes, sha256 {digest}", file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
