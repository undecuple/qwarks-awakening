#!/usr/bin/env python3
"""Host test: the llvm-mingw toolchain file makes clang read a Windows-host link response file by the rules it was
written with (issue #44: an install folder named "Qwark's Awakening" failed the link step on Windows, every two object
paths joined into one argument with the apostrophes gone). No game data, no Windows, no network. Exit 0 = PASS.

    python3 -I tools/release/test_rsp_quoting.py

Background (2026-11-23, work/coord/preplan-1123/link44/repro.md): on a Windows host CMake's Ninja generator writes the
link .rsp with Ninja's Win32 escaping (objects on one line, "..." only around a path with a space or '"'); a Linux host
writes POSIX-shell quoting ('..'\\''..'). The clang driver reads @files with GNU rules unless given
--rsp-quoting=windows, whatever the host. So cmake/llvm-mingw.cmake must add the flag on a Windows host and only there.

Cases:
  A (needs cmake; CI has it) cmake/llvm-mingw.cmake evaluated in CMake script mode with CMAKE_HOST_SYSTEM_NAME set to
    Windows, then Linux (add_link_options recorded by a stand-in): Windows adds --rsp-quoting=windows as a link option;
    Linux adds it nowhere; on neither host is it in a *_LINKER_FLAGS_INIT value (those only seed the cache at the first
    configure, so a build folder configured before the fix would keep failing).
  B (needs a clang driver that reads @files itself: $RC_TEST_CLANG, else clang on PATH; the Nix cc-wrapper expands
    @files on its own, so its wrapped clang is used instead; without one B is reported as not run, A still decides)
    `clang -###` on a response file written as a Windows-host CMake + Ninja writes it, holding object paths with an
    apostrophe, '&', non-ASCII letters and a library path with spaces: with the link options case A found for a
    Windows host every path reaches the linker whole; with no options (what 0.4.0 did) at least one does not, so the
    case can see the bug; and a Linux-host (POSIX-quoted) response file reads whole without the option.
"""
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
TOOLCHAIN = os.path.join(ROOT, "cmake", "llvm-mingw.cmake")
FLAG = "--rsp-quoting=windows"

FAILS = []
N = 0


def check(cond, what, detail=None):
    global N
    N += 1
    print(("ok   " if cond else "FAIL ") + what + ("" if cond or detail is None else f": {detail}"))
    if not cond:
        FAILS.append(what)


# CMake script that evaluates the toolchain file for one host: add_link_options is a directory command CMake does not
# run in script mode, so a function of the same name records its arguments instead.
PROBE = r"""
set(CMAKE_HOST_SYSTEM_NAME "${RC_HOST}")
set(LLVM_MINGW_ROOT "")
function(add_link_options)
  set_property(GLOBAL APPEND PROPERTY RC_LINK_OPTIONS ${ARGN})
endfunction()
include("${RC_TOOLCHAIN}")
get_property(opts GLOBAL PROPERTY RC_LINK_OPTIONS)
message("LINK_OPTIONS=${opts}")
foreach(k EXE SHARED MODULE)
  message("${k}_INIT=${CMAKE_${k}_LINKER_FLAGS_INIT}")
endforeach()
"""


def eval_toolchain(cmake, host, tmp):
    """-> {'LINK_OPTIONS': [...], 'EXE_INIT': str, ...} for the toolchain file read on `host`."""
    probe = os.path.join(tmp, "probe.cmake")
    with open(probe, "w", encoding="utf-8") as f:
        f.write(PROBE)
    r = subprocess.run([cmake, f"-DRC_HOST={host}", f"-DRC_TOOLCHAIN={TOOLCHAIN}", "-P", probe],
                       capture_output=True, text=True, check=False)
    out = {}
    for line in (r.stdout + r.stderr).splitlines():
        k, sep, v = line.partition("=")
        if sep and k in ("LINK_OPTIONS", "EXE_INIT", "SHARED_INIT", "MODULE_INIT"):
            out[k] = [x for x in v.split(";") if x] if k == "LINK_OPTIONS" else v
    out["rc"] = r.returncode
    return out


def ninja_win32_escape(s, force=False):
    """Ninja's GetWin32EscapedString (ninja v1.13.2 src/util.cc): what a Windows-host Ninja writes for $in in an rsp.
    Quoted only when the string holds ' ' or '"' (force: CMake's own quoting of a LINK_LIBRARIES path that needs it)."""
    if not force and " " not in s and '"' not in s:
        return s
    out, bs = ['"'], 0
    for ch in s:
        if ch == "\\":
            bs += 1
        elif ch == '"':
            out.append("\\" * (bs + 1))
            bs = 0
        else:
            bs = 0
        out.append(ch)
    out.append("\\" * bs + '"')
    return "".join(out)


def posix_escape(s):
    """Ninja's GetShellEscapedString on a POSIX host: '...' with each ' written as '\\''."""
    return "'" + s.replace("'", "'\\''") + "'"


def find_clang():
    """A clang driver that expands @files itself, or ''. The Nix cc-wrapper (a shell script) expands them before the
    real driver sees them, so the clang it wraps is taken from it."""
    c = os.environ.get("RC_TEST_CLANG") or shutil.which("clang") or ""
    if not c:
        return ""
    try:
        with open(c, "rb") as f:
            head = f.read(2)
            if head == b"#!":
                m = re.search(rb"^\s*compiler=(\S+)$", f.read(), re.MULTILINE)
                c = m.group(1).decode() if m else ""
    except OSError:
        return ""
    return c if c and os.access(c, os.X_OK) else ""


# What the player's build links (object names as CMake makes them for sources outside the source dir: ':' and ' '
# become '_', the rest is kept) plus a library path with spaces; "X" stands for a user name.
D = "CMakeFiles/rc_qfb.dir/C_/Users/X/Desktop"
PATHS = [
    f"{D}/Qwark's_Awakening/port/rc/main.cpp.obj",
    f"{D}/Qwark's_Awakening/port/rc/stubs.cpp.obj",
    f"{D}/Q&A_Ünïcødé_日本/port/rc/main.cpp.obj",
    f"{D}/Desktop_(2)/it's_Qwark's/port/rc/rc_settings.cpp.obj",
    "lib dir/Qwark's Awakening & Ünï 日本/libps3recomp_runtime.a",
]


def link_args(clang, opts, rsp_text, tmp):
    """-> (exit code, the linker's argument list or None, stderr) for `clang -### OPTS @rsp`."""
    rsp = os.path.join(tmp, "link.rsp")
    with open(rsp, "wb") as f:
        f.write(rsp_text.encode("utf-8"))
    r = subprocess.run([clang, "-###", "--target=x86_64-w64-mingw32", "-fuse-ld=lld", *opts, "@link.rsp",
                        "-o", "rc_qfb.exe"], cwd=tmp, capture_output=True, check=False)
    err = r.stderr.decode("utf-8", "replace")
    for line in err.splitlines():
        if "rc_qfb.exe" in line and '"-cc1' not in line and line.lstrip().startswith('"'):
            try:
                return r.returncode, shlex.split(line), err
            except ValueError:
                continue
    return r.returncode, None, err


def main():
    cmake = shutil.which("cmake")
    if not cmake:
        print("SKIP test_rsp_quoting: no cmake on PATH (run it in `nix develop .#ci`)")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        win = eval_toolchain(cmake, "Windows", tmp)
        lin = eval_toolchain(cmake, "Linux", tmp)
        for host, r in (("Windows", win), ("Linux", lin)):
            check(r["rc"] == 0 and "LINK_OPTIONS" in r, f"{host} host: the toolchain file evaluates in script mode",
                  r)
        check(FLAG in win.get("LINK_OPTIONS", []), f"Windows host: {FLAG} is a link option",
              win.get("LINK_OPTIONS"))
        check(FLAG not in lin.get("LINK_OPTIONS", []), f"Linux host: {FLAG} is not a link option (it breaks the "
              "POSIX-quoted response files a Linux-host CMake writes)", lin.get("LINK_OPTIONS"))
        for host, r in (("Windows", win), ("Linux", lin)):
            bad = [k for k in ("EXE_INIT", "SHARED_INIT", "MODULE_INIT") if FLAG in r.get(k, "")]
            check(not bad, f"{host} host: {FLAG} is not in a *_LINKER_FLAGS_INIT (cached at the first configure only)",
                  bad)

        clang = find_clang()
        if not clang:
            print("note: no clang driver found ($RC_TEST_CLANG or clang on PATH): case B not run; case A decides")
        else:
            work = os.path.join(tmp, "b")
            for p in PATHS:
                os.makedirs(os.path.join(work, os.path.dirname(p)), exist_ok=True)
                open(os.path.join(work, p), "wb").close()
            objs, lib = PATHS[:-1], PATHS[-1]
            win_rsp = " ".join(ninja_win32_escape(p) for p in objs) + " " + ninja_win32_escape(lib, force=True) + "\n"
            posix_rsp = "".join(posix_escape(p) + "\n" for p in PATHS)
            opts = [o for o in win.get("LINK_OPTIONS", []) if o.startswith("--rsp-quoting")]

            rc, args, err = link_args(clang, opts, win_rsp, work)
            missing = [p for p in PATHS if args is None or p not in args]
            check(rc == 0 and args is not None and not missing,
                  f"clang ({os.path.basename(clang)}) with the Windows-host options reads a Windows-host rsp whole "
                  "(apostrophe, &, non-ASCII, spaces)", missing or err.strip()[-400:])

            rc, args, err = link_args(clang, [], win_rsp, work)
            broken = rc != 0 or args is None or any(p not in args for p in PATHS)
            check(broken, "without the option the same rsp does not read whole (the case sees issue #44)",
                  err.strip()[-400:])

            rc, args, err = link_args(clang, [], posix_rsp, work)
            missing = [p for p in PATHS if args is None or p not in args]
            check(rc == 0 and args is not None and not missing,
                  "a Linux-host (POSIX-quoted) rsp reads whole with the default rules", missing or err.strip()[-400:])

    print(f"test_rsp_quoting: {N} checks,", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
