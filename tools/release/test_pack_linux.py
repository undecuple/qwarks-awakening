#!/usr/bin/env python3
"""Host test for tools/release/pack_linux.py (the Linux launcher tarball) and launcher/linux/install.sh. No network, no
game data, no compiler: the program is a synthetic ELF, the repository and toolkit are temp files. Exit 0 = PASS.

    python3 tools/release/test_pack_linux.py

Cases: the ELF reader finds the interpreter, NEEDED, RUNPATH and the newest GLIBC_ symbol version; refused: a library
outside the C library's own (a shared SDL3), an RPATH/RUNPATH, a glibc version above --glibc-max, a Nix interpreter, the
build machine's root folder inside the program (but "sdl3/src/..." is not the folder "/src"); the icon is the 256 px PNG
of the launcher .ico. The tarball holds exactly the packaging list under names.linux_tar_top(version) (program,
install.sh, desktop entry, icon, fonts, engine, build files, ps3recomp, licences with every bundled component's text
(SDL3 and the HIDAPI and yuv2rgb parts it links in),
NOTICE.md, README.txt), never a Windows toolchain/; the program and install.sh are 0755, other files 0644, owner 0/0,
fixed dates; the same inputs give the same bytes; refusals write nothing (a game file, a non-SDL licence, a bad program,
an existing tarball without --replace). install.sh (Linux only): installs into a temporary XDG home with a space in
its path (the desktop entry's Exec quoted), replaces an earlier install, keeps the player's data, refuses a folder it
did not make, refuses a Python older than 3.10, runs from the installed copy for --uninstall, which removes exactly
what it made and leaves a ~/.local/bin entry that is not its link alone.
"""
import contextlib
import io
import os
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import names  # noqa: E402
import pack_linux  # noqa: E402
import pack_zip  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))
FAILS = []
N = 0


def check(cond, what, detail=""):
    global N
    N += 1
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"\n     {str(detail)[:800]}"))
    if not cond:
        FAILS.append(what)


def wr(path, data=b"x", mode=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())
    if mode is not None:
        os.chmod(path, mode)


def elf(needed=("libc.so.6", "libm.so.6"), glibc=("2.2.5", "2.34"), runpath=None,
        interp=b"/lib64/ld-linux-x86-64.so.2", extra=b""):
    """A minimal x86-64 ELF executable: one PT_LOAD over the whole file, PT_INTERP, PT_DYNAMIC with NEEDED, STRTAB,
    RUNPATH and a Verneed for libc.so.6 naming GLIBC_<v> for each v."""
    base = 0x400000
    strs = b"\0"
    off = {}
    for s in list(needed) + ["libc.so.6"] + [f"GLIBC_{v}" for v in glibc] + ([runpath] if runpath else []):
        if s not in off:
            off[s] = len(strs)
            strs += s.encode() + b"\0"
    ph_off, ph_n = 64, 3
    interp_off = ph_off + 56 * ph_n
    str_off = interp_off + len(interp) + 1
    vn_off = (str_off + len(strs) + 7) & ~7
    verneed = struct.pack("<HHIII", 1, len(glibc), off["libc.so.6"], 16, 0)
    for i, v in enumerate(glibc):
        verneed += struct.pack("<IHHII", 0, 0, 2 + i, off[f"GLIBC_{v}"], 16 if i + 1 < len(glibc) else 0)
    dyn_off = (vn_off + len(verneed) + 7) & ~7
    dyn = b"".join(struct.pack("<qQ", 1, off[n]) for n in needed)
    if runpath:
        dyn += struct.pack("<qQ", 29, off[runpath])
    dyn += struct.pack("<qQ", 5, base + str_off) + struct.pack("<qQ", 0x6FFFFFFE, base + vn_off)
    dyn += struct.pack("<qQ", 0x6FFFFFFF, 1) + struct.pack("<qQ", 0, 0)
    end = dyn_off + len(dyn)
    total = end + len(extra)
    hdr = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\0" * 8
    hdr += struct.pack("<HHIQQQIHHHHHH", 3, 62, 1, base + 0x1000, ph_off, 0, 0, 64, 56, ph_n, 64, 0, 0)
    ph = struct.pack("<IIQQQQQQ", 1, 5, 0, base, base, total, total, 0x1000)
    ph += struct.pack("<IIQQQQQQ", 3, 4, interp_off, base + interp_off, base + interp_off, len(interp) + 1, len(interp) + 1, 1)
    ph += struct.pack("<IIQQQQQQ", 2, 6, dyn_off, base + dyn_off, base + dyn_off, len(dyn), len(dyn), 8)
    d = bytearray(total)
    d[0:64] = hdr
    d[ph_off:ph_off + len(ph)] = ph
    d[interp_off:interp_off + len(interp)] = interp
    d[str_off:str_off + len(strs)] = strs
    d[vn_off:vn_off + len(verneed)] = verneed
    d[dyn_off:dyn_off + len(dyn)] = dyn
    d[end:] = extra
    return bytes(d)


def make_root(tmp):
    """A repository with what pack_linux.py reads (the real .ico and install.sh, everything else synthetic)."""
    r = os.path.join(tmp, "repo")
    wr(os.path.join(r, "launcher", "linux", "install.sh"),
       open(os.path.join(ROOT, "launcher", "linux", "install.sh"), "rb").read(), 0o755)
    wr(os.path.join(r, "launcher", "qwarks-awakening.desktop"),
       "[Desktop Entry]\nType=Application\nName=Qwark's Awakening\nExec=launcher\nTerminal=false\nCategories=Game;\n")
    wr(os.path.join(r, "installer", "art", "rc-launcher.ico"),
       open(os.path.join(ROOT, "installer", "art", "rc-launcher.ico"), "rb").read())
    for n in ("AdwaitaSans-Regular.ttf", "RussoOne-Regular.ttf", "OFL-adwaita.txt", "OFL-russo-one.txt"):
        wr(os.path.join(r, "launcher", "fonts", n), n)
    wr(os.path.join(r, "third_party", "imgui", "LICENSE.txt"), "The MIT License (MIT) imgui")
    wr(os.path.join(r, "public", "LICENSE"), "MIT License port")
    wr(os.path.join(r, "public", "NOTICE.md"), "# Notice")
    for n in pack_linux.ENGINE:
        wr(os.path.join(r, "tools", "release", n), "# " + n)
    for rel in pack_zip.KIT_FILES:
        wr(os.path.join(r, *rel.split("/")), "# " + rel)
    wr(os.path.join(r, "port", "rc", "CMakeLists.txt"), "project(rc)")
    wr(os.path.join(r, "port", "rc", "tests", "run.sh"), "#!/bin/sh\n", 0o755)
    wr(os.path.join(r, "data", "titles", "catalog.json"), "{}")
    wr(os.path.join(r, "tools", "spu", "spu_lift.py"), "# lift")
    wr(os.path.join(r, "cmake", "llvm-mingw.cmake"), "# toolchain file")
    tk = os.path.join(tmp, "ps3recomp-patched")
    for n in pack_zip.TOOLKIT_NEEDS:
        wr(os.path.join(tk, *n.split("/")), n)
    wr(os.path.join(tk, "runtime", "x.c"), "int x;")
    b = os.path.join(tmp, "launcher-linux")
    wr(os.path.join(b, "qwarks-awakening"), elf(), 0o755)
    wr(os.path.join(b, "licenses", "SDL3-LICENSE.txt"), "Copyright (C) 1997-2026 Sam Lantinga\nThis software is provided 'as-is'")
    wr(os.path.join(b, "licenses", "SDL3-hidapi-LICENSE.txt"),
       "Copyright (c) 2010, Alan Ott\n* Redistributions in binary form must reproduce the above")
    wr(os.path.join(b, "licenses", "SDL3-yuv2rgb-LICENSE.txt"),
       "Copyright (c) 2016, Adrien Descamps\n* Redistributions in binary form must reproduce the above")
    return r, tk, b


def pack(r, tk, b, out, *extra, version="1.2.3"):
    s = io.StringIO()
    rc = pack_linux.main(["--program", os.path.join(b, "qwarks-awakening"), "--sdl3-licenses",
                          os.path.join(b, "licenses"), "--toolkit", tk, "--version", version, "--out", out,
                          "--root", r, *extra], out=s)
    return rc, s.getvalue()


def test_elf(tmp):
    p = os.path.join(tmp, "e")
    wr(p, elf(runpath="$ORIGIN/lib", glibc=("2.2.5", "2.14", "2.34")))
    i = pack_linux.elf_info(p)
    check(i["interp"] == b"/lib64/ld-linux-x86-64.so.2" and i["needed"] == ["libc.so.6", "libm.so.6"]
          and i["rpath"] == ["$ORIGIN/lib"] and i["glibc"] == "2.34", "ELF reader: interpreter, NEEDED, RUNPATH, newest GLIBC_", i)
    wr(p, elf())
    check(pack_linux.program_problems(p) == [], "a program needing only libc/libm and glibc 2.34 passes",
          pack_linux.program_problems(p))
    cases = {
        "a shared SDL3 refused (link it in)": elf(needed=("libc.so.6", "libSDL3.so.0")),
        "a shared libstdc++ refused": elf(needed=("libc.so.6", "libstdc++.so.6")),
        "a RUNPATH refused": elf(runpath="$ORIGIN"),
        "a glibc symbol newer than 2.35 refused": elf(glibc=("2.2.5", "2.38")),
        "a Nix interpreter refused": elf(interp=b"/nix/store/abc-glibc-2.40/lib/ld-linux-x86-64.so.2"),
    }
    for what, data in cases.items():
        wr(p, data)
        check(pack_linux.program_problems(p) != [], what)
    wr(p, elf(glibc=("2.2.5", "2.38")))
    check(pack_linux.program_problems(p, glibc_max="2.38") == [], "--glibc-max raises the limit")
    wr(p, elf(extra=b"\0/data/builder/qa/launcher/src/main.cpp\0"))
    check(pack_linux.program_problems(p, root="/data/builder/qa") != [], "the build machine's root folder inside refused")
    wr(p, elf(extra=b"\0sdl3/src/render/SDL_render.c\0"))
    check(pack_linux.program_problems(p, root="/src") == [], "'sdl3/src/...' is not the root folder /src")
    wr(p, b"MZ" + b"\0" * 200)
    check(pack_linux.program_problems(p) != [], "a Windows exe refused")
    png = pack_linux.ico_png(os.path.join(ROOT, "installer", "art", "rc-launcher.ico"))
    check(png[:8] == b"\x89PNG\r\n\x1a\n" and struct.unpack(">II", png[16:24]) == (256, 256), "icon: the .ico's 256 px PNG")


def members(path):
    with tarfile.open(path) as t:
        return {m.name: m for m in t.getmembers()}


def test_pack(tmp):
    r, tk, b = make_root(tmp)
    out = os.path.join(tmp, "out")
    rc, txt = pack(r, tk, b, out)
    tar = os.path.join(out, names.linux_tar_name("1.2.3"))
    check(rc == 0 and os.path.isfile(tar), "tarball written", txt)
    if rc:
        return None
    top = names.linux_tar_top("1.2.3")
    ms = members(tar)
    files = {n[len(top) + 1:] for n, m in ms.items() if m.isfile()}
    want = {"qwarks-awakening", "install.sh", "qwarks-awakening.desktop", "qwarks-awakening.png", "README.txt", "NOTICE.md",
            "fonts/AdwaitaSans-Regular.ttf", "fonts/RussoOne-Regular.ttf",
            "licenses/OFL-adwaita.txt", "licenses/OFL-russo-one.txt", "licenses/SDL3-LICENSE.txt",
            "licenses/SDL3-hidapi-LICENSE.txt", "licenses/SDL3-yuv2rgb-LICENSE.txt",
            "licenses/imgui-LICENSE.txt", "licenses/LICENSE.txt", "licenses/ps3recomp-LICENSE.txt",
            "port/rc/CMakeLists.txt", "port/rc/tests/run.sh", "data/titles/catalog.json", "tools/spu/spu_lift.py",
            "cmake/llvm-mingw.cmake", "ps3recomp/runtime/x.c"}
    want |= {f"tools/release/{n}" for n in pack_linux.ENGINE} | set(pack_zip.KIT_FILES)
    want |= {f"ps3recomp/{n}" for n in pack_zip.TOOLKIT_NEEDS}
    check(files == want, "the packaging list, exactly", sorted(files ^ want))
    check(all(n == top or n.startswith(top + "/") for n in ms), "every entry under the top folder")
    check(not any("/toolchain/" in n for n in ms), "no Windows toolchain/ in the Linux download")
    modes = {n[len(top) + 1:]: m.mode for n, m in ms.items() if m.isfile()}
    check(modes["qwarks-awakening"] == 0o755 and modes["install.sh"] == 0o755 and modes["port/rc/tests/run.sh"] == 0o755
          and modes["README.txt"] == 0o644 and modes["licenses/SDL3-LICENSE.txt"] == 0o644,
          "modes: the program, install.sh and source executables 0755, the rest 0644")
    check(all(m.uid == 0 and m.gid == 0 and m.uname == "" and m.mtime == pack_linux.FIXED_MTIME for m in ms.values())
          and all(ms[n].isdir() for n in ms if not ms[n].isfile()), "owner 0/0 without names, fixed dates, directories listed")
    with tarfile.open(tar) as t:
        rd = t.extractfile(f"{top}/README.txt").read().decode()
    check("install.sh" in rd and "--uninstall" in rd and "Python 3.10" in rd and "glibc" in rd and names.NAME_NOTE in rd,
          "README.txt: install, removal, Python and glibc requirements, the name note")
    s1 = pack_zip.sha256_file(tar)
    rc2, _ = pack(r, tk, b, out, "--replace")
    check(rc2 == 0 and pack_zip.sha256_file(tar) == s1 and s1 in txt, "same inputs, same bytes; the printed SHA-256 matches")
    rc3, t3 = pack(r, tk, b, out)
    check(rc3 == 1 and "exists" in t3, "an existing tarball needs --replace")
    # refusals write nothing
    out2 = os.path.join(tmp, "out2")
    wr(os.path.join(r, "port", "rc", "EBOOT.elf"), b"\x7fELF")
    rc4, t4 = pack(r, tk, b, out2)
    check(rc4 == 1 and not os.path.exists(out2) and "game file" in t4, "a game file in the build files refused", t4)
    os.remove(os.path.join(r, "port", "rc", "EBOOT.elf"))
    lic = os.path.join(b, "licenses", "SDL3-hidapi-LICENSE.txt")
    keep = open(lic, "rb").read()
    wr(lic, "some other licence")
    rc5, t5 = pack(r, tk, b, out2)
    check(rc5 == 1 and not os.path.exists(out2), "a licence text that is not HIDAPI's refused", t5)
    os.remove(lic)
    rc5b, t5b = pack(r, tk, b, out2)
    check(rc5b == 1 and not os.path.exists(out2) and "SDL3-hidapi-LICENSE.txt" in t5b, "a missing SDL part licence refused", t5b)
    wr(lic, keep)
    prog = os.path.join(b, "qwarks-awakening")
    keep = open(prog, "rb").read()
    wr(prog, elf(needed=("libc.so.6", "libSDL3.so.0")))
    rc6, t6 = pack(r, tk, b, out2)
    check(rc6 == 1 and not os.path.exists(out2) and "libSDL3.so.0" in t6, "a program with a shared SDL3 refused", t6)
    wr(prog, keep, 0o755)
    rc7, t7 = pack(r, tk, b, out2, version="1.2")
    check(rc7 == 2, "a bad --version refused")
    s = io.StringIO()
    rc8 = pack_linux.main(["--program", prog, "--sdl3-licenses", os.path.dirname(lic), "--toolkit", tk, "--version", "1.2.3", "--root", r,
                           "--list"], out=s)
    check(rc8 == 0 and "install.sh" in s.getvalue() and not os.path.exists(out2), "--list prints the list, writes nothing")
    return tar


def run_sh(script, home, data, *args, path=None):
    env = {"HOME": home, "XDG_DATA_HOME": data, "XDG_CONFIG_HOME": os.path.join(home, "cfg"),
           "PATH": path or os.environ.get("PATH", "/usr/bin:/bin")}
    return subprocess.run(["bash", script, *args], env=env, capture_output=True, text=True)


def test_install(tmp, tar):
    if os.name == "nt" or shutil.which("bash") is None or shutil.which("python3") is None:
        print("skip install.sh (needs bash and python3: Linux)")
        return
    unp = os.path.join(tmp, "unpacked")
    with tarfile.open(tar) as t:
        t.extractall(unp, filter="data") if hasattr(tarfile, "data_filter") else t.extractall(unp)
    src = os.path.join(unp, names.linux_tar_top("1.2.3"))
    home = os.path.join(tmp, "home dir")          # a space: the desktop entry's Exec must quote it
    data = os.path.join(home, "my data")
    app = os.path.join(data, "qwarks-awakening", "launcher")
    link = os.path.join(home, ".local", "bin", names.LINUX_PROGRAM)
    desk = os.path.join(data, "applications", "qwarks-awakening.desktop")
    icon = os.path.join(data, "icons", "hicolor", "256x256", "apps", "qwarks-awakening.png")
    wr(os.path.join(data, "qwarks-awakening", "library.json"), "{}")       # the player's data, never touched
    p = run_sh(os.path.join(src, "install.sh"), home, data)
    check(p.returncode == 0, "install.sh installs", p.stdout + p.stderr)
    check(os.path.isfile(os.path.join(app, names.LINUX_PROGRAM)) and os.access(os.path.join(app, names.LINUX_PROGRAM), os.X_OK)
          and os.path.isfile(os.path.join(app, "tools", "release", "catalog.py"))
          and os.path.isfile(os.path.join(app, "ps3recomp", "CMakeLists.txt")),
          "the launcher, its engine and the build files in $XDG_DATA_HOME/qwarks-awakening/launcher")
    check(os.path.islink(link) and os.readlink(link) == os.path.join(app, names.LINUX_PROGRAM), "~/.local/bin link to it")
    d = open(desk, encoding="utf-8").read() if os.path.isfile(desk) else ""
    check(f'Exec="{os.path.join(app, names.LINUX_PROGRAM)}"' in d and "Icon=qwarks-awakening" in d
          and "Exec=launcher" not in d and d.count("Exec=") == 1 and "Name=Qwark's Awakening" in d,
          "desktop entry: Exec quoted (a space in the path), the icon by name", d)
    check(os.path.isfile(icon) and open(icon, "rb").read()[:4] == b"\x89PNG", "icon in hicolor/256x256/apps")
    check("1.2.3" in open(os.path.join(app, ".qwarks-awakening-install"), encoding="utf-8").read(), "install marker with the version")
    wr(os.path.join(app, "build-leftover"), "x")
    p = run_sh(os.path.join(src, "install.sh"), home, data)
    check(p.returncode == 0 and not os.path.exists(os.path.join(app, "build-leftover"))
          and not [n for n in os.listdir(os.path.dirname(app)) if n.startswith("launcher.")],
          "a second install replaces the folder (no .new/.old left)", p.stdout + p.stderr)
    p = run_sh(os.path.join(app, "install.sh"), home, data)
    check(p.returncode != 0 and "installed copy" in p.stderr, "the installed copy refuses to install over itself", p.stderr)
    fake = os.path.join(tmp, "oldpy")
    wr(os.path.join(fake, "python3"), "#!/bin/sh\necho Python 3.8.10\nexit 1\n", 0o755)
    p = run_sh(os.path.join(src, "install.sh"), os.path.join(tmp, "h2"), os.path.join(tmp, "h2", "d"),
               path=fake + os.pathsep + os.environ.get("PATH", ""))
    check(p.returncode != 0 and "3.10" in p.stderr and not os.path.exists(os.path.join(tmp, "h2", "d", "qwarks-awakening")),
          "a Python older than 3.10 refused before anything is written", p.stderr)
    h3 = os.path.join(tmp, "h3")
    wr(os.path.join(h3, "d", "qwarks-awakening", "launcher", "mine.txt"), "x")
    p = run_sh(os.path.join(src, "install.sh"), h3, os.path.join(h3, "d"))
    check(p.returncode != 0 and os.path.isfile(os.path.join(h3, "d", "qwarks-awakening", "launcher", "mine.txt")),
          "a launcher folder this script did not make is left alone", p.stderr)
    p = run_sh(os.path.join(app, "install.sh"), home, data, "--uninstall")
    check(p.returncode == 0 and not os.path.exists(app) and not os.path.lexists(link) and not os.path.exists(desk)
          and not os.path.exists(icon), "--uninstall (from the installed copy) removes the launcher, link, entry, icon",
          p.stdout + p.stderr)
    check(os.path.isfile(os.path.join(data, "qwarks-awakening", "library.json")) and "Kept" in p.stdout,
          "--uninstall keeps the player's data and says where it is")
    p = run_sh(os.path.join(src, "install.sh"), home, data, "--uninstall")
    check(p.returncode == 0 and "not installed" in p.stdout, "--uninstall with nothing installed changes nothing")
    if os.path.lexists(link):
        os.remove(link)
    wr(link, "#!/bin/sh\n", 0o755)                    # someone else's ~/.local/bin/qwarks-awakening
    p = run_sh(os.path.join(src, "install.sh"), home, data)
    check(p.returncode == 0 and not os.path.islink(link) and "Left" in p.stdout, "an existing file in ~/.local/bin is left alone")
    p = run_sh(os.path.join(app, "install.sh"), home, data, "--uninstall")
    check(p.returncode == 0 and os.path.isfile(link), "--uninstall leaves a ~/.local/bin entry that is not its link")
    p = run_sh(os.path.join(src, "install.sh"), home, data, "--help")
    check(p.returncode == 0 and "--uninstall" in p.stdout and "set -euo" not in p.stdout, "--help prints the header")


def main():
    tmp = tempfile.mkdtemp(prefix="rc-pack-linux-test-")
    try:
        test_elf(tmp)
        tar = test_pack(tmp)
        if tar:
            test_install(tmp, tar)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"\ntest_pack_linux: {N - len(FAILS)}/{N} checks passed", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    with contextlib.suppress(BrokenPipeError):
        sys.exit(main())
