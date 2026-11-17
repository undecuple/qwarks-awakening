#!/usr/bin/env python3
"""Pack the Linux launcher as an AppImage beside the tarball (0.4.0): the same files as pack_linux.py's tar.gz, as an
AppDir with an AppRun, made into one file by appimagetool. Standard library only, Python 3.8+.

    pack_appimage.py --program build/launcher-linux/qwarks-awakening --sdl3-licenses build/launcher-linux/licenses \\
                     --toolkit work/ps3recomp-patched --version X.Y.Z --out DIR \\
                     --appimagetool TREES/appimagetool/appimagetool-x86_64.AppImage \\
                     --runtime TREES/appimage-runtime/runtime-x86_64 \\
                     --runtime-license TREES/appimage-runtime-license/LICENSE
                     [--root R] [--commit HASH] [--glibc-max 2.35] [--replace] [--keep-appdir DIR]
    pack_appimage.py --appdir-only DIR ...    write the AppDir and stop (no appimagetool; tests, a look inside)

TREES = fetch_toolchain.py --only appimagetool,appimage-runtime,appimage-runtime-license (toolchain.json pins all
three; appimagetool runs with --appimage-extract-and-run, as the release runner has no FUSE, and with --runtime-file,
so it downloads nothing). The AppImage = names.linux_appimage_name(version).

The AppDir: every file of the tarball (pack_linux.gather: the program, the engine, the build files, fonts, licences,
NOTICE.md, docs/LINUX.md, cmake/release-ident.txt) except install.sh (an AppImage is run in place, not installed),
with README.txt in its AppImage form, plus:
  AppRun                              sh: exports PYTHONDONTWRITEBYTECODE=1 (the engine never writes __pycache__ into
                                      the read-only mount) and runs the program with the arguments
  qwarks-awakening.desktop            the menu entry template with Exec=qwarks-awakening, Icon=qwarks-awakening
  qwarks-awakening.png, .DirIcon      the 256 px icon (.DirIcon a copy: appimagetool wants a file, a link is refused)
  .qwarks-awakening-appimage          the marker firstrun.appimage_install() reads: a read-only install, builds and
                                      everything else go to the XDG folders (as the tarball install)
  licenses/appimage-runtime-LICENSE.txt, licenses/lgpl-2.1.txt, licenses/{musl,squashfuse,zstd,zlib,mimalloc}-*.txt
                                      the AppImage runtime's licence (MIT; it links musl, libfuse, squashfuse, zstd
                                      and zlib in) and the LGPL 2.1 text for libfuse: the runtime is the AppImage's
                                      first part, so it ships
Refused (nothing written): what pack_linux.py refuses (program checks, game files, missing inputs), a missing tool,
runtime or licence text, an existing AppImage without --replace. Same file modes as the tarball; every AppDir file's
time is 1980-01-01 and SOURCE_DATE_EPOCH is set for mksquashfs (best effort: appimagetool's own squashfs settings).
Exit 0 = written, 1 = refused or appimagetool failed, 2 = usage.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402
import pack_linux  # noqa: E402
import pack_zip  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
MARKER = ".qwarks-awakening-appimage"          # = firstrun.APPIMAGE_MARKER
LGPL = os.path.join(HERE, "licenses", "lgpl-2.1.txt")
# The licence texts of the libraries the runtime links in statically (verbatim copies kept in tools/release/licenses/,
# SHA-256-pinned here; type2-runtime 20251108 build: libfuse 3.15.0 and squashfuse 0.5.2 (version strings in the runtime
# binary), zlib 1.3.1 (inflate string), musl, zstd and mimalloc from Alpine 3.21's packages (musl 1.2.5, zstd 1.5.6,
# mimalloc2 2.1.7 in its package index; the runtime build records no version for them). AppDir file, repo copy,
# SHA-256, words the text must contain, the upstream file it is a copy of.
LIBRARY_LICENSES = (
    ("licenses/musl-COPYRIGHT.txt", "musl-COPYRIGHT.txt",
     "f9bc4423732350eb0b3f7ed7e91d530298476f8fec0c6c427a1c04ade22655af", ("standard MIT license",),
     "https://git.musl-libc.org/cgit/musl/plain/COPYRIGHT?h=v1.2.5"),
    ("licenses/squashfuse-LICENSE.txt", "squashfuse-LICENSE.txt",
     "9e909cc8a8ba27b1a649c964cf9eda37de911f3138c2b64e6d2f01703904ac13", ("Dave Vasilevsky", "Redistribution"),
     "https://raw.githubusercontent.com/vasi/squashfuse/0.5.2/LICENSE"),
    ("licenses/zstd-LICENSE.txt", "zstd-LICENSE.txt",
     "7055266497633c9025b777c78eb7235af13922117480ed5c674677adc381c9d8", ("BSD License", "Zstandard"),
     "https://raw.githubusercontent.com/facebook/zstd/v1.5.6/LICENSE"),
    ("licenses/zlib-LICENSE.txt", "zlib-LICENSE.txt",
     "845efc77857d485d91fb3e0b884aaa929368c717ae8186b66fe1ed2495753243", ("Jean-loup Gailly and Mark Adler", "as-is"),
     "https://raw.githubusercontent.com/madler/zlib/v1.3.1/LICENSE"),
    ("licenses/mimalloc-LICENSE.txt", "mimalloc-LICENSE.txt",
     "19c99805e7a44a34b297a75d1edea9985e300066dfc024d5c99d4236d4573b5d", ("MIT License", "Daan Leijen"),
     "https://raw.githubusercontent.com/microsoft/mimalloc/v2.1.7/LICENSE"),
)
APPRUN = """#!/bin/sh
# Qwark's Awakening launcher, AppImage entry (tools/release/pack_appimage.py). The AppImage is a read-only mount:
# Python writes no bytecode beside the engine, and the launcher keeps everything in the XDG folders.
HERE="$(dirname "$(readlink -f "$0")")"
export PYTHONDONTWRITEBYTECODE=1
exec "$HERE/{program}" "$@"
"""


def desktop_entry(template):
    """The tarball's desktop template with the AppImage's Exec and Icon (the program and icon names in the AppDir)."""
    keep = [ln for ln in template.splitlines() if not ln.startswith(("Exec=", "Icon=", "TryExec=", "Path="))]
    out = []
    for ln in keep:
        out.append(ln)
        if ln.strip() == "[Desktop Entry]":
            out += [f"Exec={names.LINUX_PROGRAM}", f"Icon={names.LINUX_STEM}"]
    return ("\n".join(out) + "\n").encode("utf-8")


def gather(program, sdl3_licenses, toolkit, root, version, commit, runtime_license):
    """-> (problems, [(AppDir path, source path or bytes, executable)])."""
    problems, tfiles = pack_linux.gather(program, sdl3_licenses, toolkit, root, version, commit)
    files = []
    for rel, src, exe in tfiles:
        if rel == "install.sh":
            continue
        if rel == "README.txt":
            src = pack_linux.readme(version, appimage=True).encode("utf-8")
        elif rel == f"{names.LINUX_STEM}.desktop":
            try:
                with open(src, encoding="utf-8") as f:
                    src = desktop_entry(f.read())
            except OSError:
                pass   # pack_linux.gather already lists it as missing
        files.append((rel, src, exe))
    icon = next((s for r, s, _x in tfiles if r == f"{names.LINUX_STEM}.png"), None)
    if isinstance(icon, bytes):
        files.append((".DirIcon", icon, False))
    files.append(("AppRun", APPRUN.format(program=names.LINUX_PROGRAM).encode("ascii"), True))
    files.append((MARKER, b"Qwark's Awakening launcher AppImage: a read-only install (firstrun.appimage_install).\n",
                  False))
    for rel, src, words in (("licenses/appimage-runtime-LICENSE.txt", runtime_license, ("MIT License", "libfuse")),
                            ("licenses/lgpl-2.1.txt", LGPL, ("GNU LESSER GENERAL PUBLIC LICENSE", "Version 2.1"))):
        if not src or not os.path.isfile(src):
            problems.append(f"{rel}: missing ({src or 'no --runtime-license'})")
        else:
            with open(src, encoding="utf-8", errors="replace") as f:
                t = f.read()
            if not all(w in t for w in words):
                problems.append(f"{src}: not the expected licence text ({words[0]})")
        files.append((rel, src, False))
    for rel, name, digest, words, _url in LIBRARY_LICENSES:
        src = os.path.join(HERE, "licenses", name)
        if not os.path.isfile(src):
            problems.append(f"{rel}: missing ({src})")
        else:
            with open(src, "rb") as f:
                raw = f.read()
            if hashlib.sha256(raw).hexdigest() != digest:
                problems.append(f"{src}: SHA-256 differs from the pin (the verbatim upstream text is required)")
            elif not all(w in raw.decode("utf-8", "replace") for w in words):
                problems.append(f"{src}: not the expected licence text ({words[0]})")
        files.append((rel, src, False))
    seen = set()
    for rel, _s, _x in files:
        if rel in seen:
            problems.append(f"{rel}: listed twice")
        seen.add(rel)
    return problems, sorted(files, key=lambda x: x[0])


def write_appdir(appdir, files):
    """The AppDir from the gathered list: modes 0755/0644, times 1980-01-01."""
    for rel, src, exe in files:
        dst = os.path.join(appdir, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if isinstance(src, bytes):
            with open(dst, "wb") as f:
                f.write(src)
        else:
            shutil.copyfile(src, dst)
        os.chmod(dst, 0o755 if exe else 0o644)
    for d, dirs, fs in os.walk(appdir, topdown=False):
        for n in fs + dirs:
            os.utime(os.path.join(d, n), (pack_linux.FIXED_MTIME, pack_linux.FIXED_MTIME), follow_symlinks=False)
    os.chmod(appdir, 0o755)
    os.utime(appdir, (pack_linux.FIXED_MTIME, pack_linux.FIXED_MTIME))


def run_appimagetool(tool, runtime, appdir, dest, out, run=subprocess.run):
    env = dict(os.environ, ARCH="x86_64", SOURCE_DATE_EPOCH=str(pack_linux.FIXED_MTIME), APPIMAGE_EXTRACT_AND_RUN="1")
    argv = [tool, "--appimage-extract-and-run", "--no-appstream", "--runtime-file", runtime, appdir, dest]
    r = run(argv, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=os.path.dirname(dest))
    for line in (r.stdout or "").splitlines()[-30:]:
        print(f"appimagetool: {line}", file=out)
    return r.returncode


def main(argv=None, out=sys.stdout, run=subprocess.run):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--program")
    ap.add_argument("--sdl3-licenses")
    ap.add_argument("--toolkit")
    ap.add_argument("--version")
    ap.add_argument("--out", help="folder for the AppImage")
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--commit", default="", help="the public commit (default $GITHUB_SHA): cmake/release-ident.txt")
    ap.add_argument("--glibc-max", default=pack_linux.GLIBC_MAX)
    ap.add_argument("--appimagetool")
    ap.add_argument("--runtime", help="the AppImage type 2 runtime (toolchain.json appimage-runtime)")
    ap.add_argument("--runtime-license", help="its LICENSE (toolchain.json appimage-runtime-license)")
    ap.add_argument("--replace", action="store_true")
    ap.add_argument("--appdir-only", metavar="DIR", help="write the AppDir there and stop")
    ap.add_argument("--keep-appdir", metavar="DIR", help="build the AppImage from this folder and keep it")
    a = ap.parse_args(argv)
    if not (a.program and a.sdl3_licenses and a.toolkit and a.version and a.runtime_license
            and (a.appdir_only or (a.out and a.appimagetool and a.runtime))):
        print("needs --program, --sdl3-licenses, --toolkit, --version, --runtime-license and either --appdir-only or "
              "--out, --appimagetool and --runtime", file=out)
        return 2
    if not re.fullmatch(r"\d+\.\d+\.\d+", a.version):
        print(f"--version {a.version!r} is not X.Y.Z", file=out)
        return 2
    commit, bad = pack_zip.release_commit(a.commit)
    if bad:
        print(bad, file=out)
        return 2
    root = os.path.abspath(a.root)
    problems, files = gather(os.path.abspath(a.program), os.path.abspath(a.sdl3_licenses), os.path.abspath(a.toolkit),
                             root, a.version, commit, os.path.abspath(a.runtime_license))
    if os.path.isfile(a.program):
        problems += pack_linux.program_problems(a.program, a.glibc_max, root)
    if not a.appdir_only:
        for what, p in (("--appimagetool", a.appimagetool), ("--runtime", a.runtime)):
            if not os.path.isfile(p):
                problems.append(f"{what}: missing ({p})")
    target = a.appdir_only or a.keep_appdir
    if target and os.path.lexists(target) and os.listdir(target):
        problems.append(f"{target}: not empty (the AppDir is written into an empty or new folder)")
    dest = "" if a.appdir_only else os.path.abspath(os.path.join(a.out, names.linux_appimage_name(a.version)))
    if dest and os.path.lexists(dest) and not a.replace:
        problems.append(f"{dest} exists (--replace overwrites it)")
    for p in problems:
        print(f"REFUSED {p}", file=out)
    if problems:
        print(f"AppImage refused: {len(problems)} problem(s); nothing written", file=out)
        return 1
    if a.appdir_only:
        write_appdir(os.path.abspath(a.appdir_only), files)
        print(f"wrote AppDir {os.path.abspath(a.appdir_only)}: {len(files)} files", file=out)
        return 0
    tmp = None
    try:
        if a.keep_appdir:
            appdir = os.path.abspath(a.keep_appdir)
        else:
            tmp = tempfile.mkdtemp(prefix="qa-appdir-")
            appdir = os.path.join(tmp, f"{names.LINUX_STEM}.AppDir")
        write_appdir(appdir, files)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        part = dest + ".partial"
        if os.path.lexists(part):
            os.remove(part)
        rc = run_appimagetool(os.path.abspath(a.appimagetool), os.path.abspath(a.runtime), appdir, part, out, run)
        if rc != 0 or not os.path.isfile(part):
            print(f"appimagetool failed (exit {rc}); nothing written", file=out)
            if os.path.lexists(part):
                os.remove(part)
            return 1
        os.chmod(part, 0o755)
        os.replace(part, dest)
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
    print(f"wrote {dest}: {len(files)} files, {os.path.getsize(dest):,} bytes, sha256 {pack_zip.sha256_file(dest)}",
          file=out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
