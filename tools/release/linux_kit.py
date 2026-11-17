#!/usr/bin/env python3
"""Linux build tools for the launcher's Build (linux-launcher-plan.md phase 3). Python 3.8+, standard library only.

A Linux PC builds the Windows game exe the same way the Windows build kit does: with llvm-mingw (clang for the
x86_64-w64-mingw32 target), CMake and Ninja, here their Linux-hosted releases, plus SDL3's MinGW package for the
SDL3.dll that goes beside the game. Every one is the toolchain.json entry with "linux": "build" (and sdl3), pinned by
version and SHA-256 and downloaded from its project's own release by fetch_toolchain.fetch_one, only when the player
confirms in the launcher. Nothing here is shipped by this project.

    linux_kit.py status [--dest DIR]                 STATUS {"ready", "path", "toolchain", "missing", "entries"}
    linux_kit.py fetch  [--dest DIR] [--stop-file F] download, check, unpack every entry not yet in place
    linux_kit.py toolchain [--dest DIR]              print the folder to give firstrun.py --toolchain (exit 1: not ready)
    linux_kit.py check [--dest DIR]                  start each downloaded tool once (--version); REASON + exit 1 when
                                                     one cannot start (NixOS without nix-ld or steam-run)

DIR defaults to $XDG_DATA_HOME/qwarks-awakening/build-tools (~/.local/share/...); the trees land in DIR/trees/<name>/,
which firstrun.resolve_toolchain reads (llvm-mingw-linux/, cmake-linux/, ninja-linux/, sdl3/). catalog.py build on Linux
passes it as --toolchain when it is ready. Lines: PROGRESS step=N/M name=<entry> ... pct=P, REASON, RESULT ok=0|1 step=S.
Exit 0 = ok, 1 = failed, 130 = stopped.
"""
import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fetch_toolchain as ft  # noqa: E402

APP = "qwarks-awakening"
EXTRA = ("sdl3",)            # platform-neutral entries the Linux build also needs (SDL3.dll for the game folder)


def default_dest(env=None):
    env = os.environ if env is None else env
    base = env.get("XDG_DATA_HOME", "")
    if not os.path.isabs(base):
        base = os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.path.join(base, APP, "build-tools")


def entries(manifest=None):
    m = ft.load(manifest or ft.MANIFEST)
    return [c for c in m["components"] if c.get("linux") == "build" or c["name"] in EXTRA]


def status(dest, manifest=None):
    have = ft.read_fetched(dest)
    ents = entries(manifest)
    missing = [c["name"] for c in ents
               if (have.get(c["name"]) or {}).get("sha256") != c["sha256"]
               or not os.path.isdir(os.path.join(dest, "trees", c["name"]))]
    return {"ready": not missing, "path": dest, "toolchain": os.path.join(dest, "trees"), "missing": missing,
            "entries": [{"name": c["name"], "version": c["version"]} for c in ents]}


def fetch(dest, out=sys.stdout, stop_file=None, manifest=None, opener=ft.open_url):
    t0 = time.monotonic()

    def result(ok, step):
        print(f"RESULT ok={1 if ok else 0} step={step} elapsed={int(time.monotonic() - t0)}", file=out, flush=True)

    st = status(dest, manifest)
    todo = [c for c in entries(manifest) if c["name"] in st["missing"]]
    for i, c in enumerate(todo):
        if stop_file and os.path.exists(stop_file):
            print("REASON Download stopped. What was already in place stays.", file=out, flush=True)
            result(False, "cancelled")
            return 130
        print(f"PROGRESS step={i + 1}/{len(todo)} name={c['name']} done={i} total={len(todo)} "
              f"elapsed={int(time.monotonic() - t0)} pct={100.0 * i / len(todo):.1f}", file=out, flush=True)
        buf = io.StringIO()
        ok = ft.fetch_one(c, dest, False, manifest or ft.MANIFEST, opener, buf)
        for line in buf.getvalue().splitlines():
            print(line, file=out, flush=True)
        shutil.rmtree(os.path.join(dest, "downloads", c["name"]), ignore_errors=True)   # the archive is not needed again
        if not ok:
            print(f"REASON {c['name']} {c['version']} could not be downloaded or did not match its pinned SHA-256.",
                  file=out, flush=True)
            result(False, c["name"])
            return 1
    print(f"PROGRESS step={len(todo)}/{len(todo)} name=done done={len(todo)} total={len(todo)} "
          f"elapsed={int(time.monotonic() - t0)} pct=100.0", file=out, flush=True)
    result(True, "-")
    return 0


# The downloaded tools are generic-Linux programs (they expect /lib64/ld-linux-x86-64.so.2 and the usual library
# folders). NixOS has neither unless nix-ld is on, so the kernel refuses them with "No such file or directory" (or the
# shell's 127). The pre-check starts each one once, so a build fails with this message instead of in the middle.
PROBES = (("llvm-mingw-linux", "x86_64-w64-mingw32-clang"), ("cmake-linux", "cmake"), ("ninja-linux", "ninja"))
NIXOS_HINT = ("On NixOS, programs made for other Linux distributions need nix-ld (programs.nix-ld.enable = true) or "
              "steam-run: enable nix-ld, or start the launcher with steam-run qwarks-awakening.")


def is_nixos():
    """/etc/NIXOS, or the system profile when /etc is not fully visible (proton.is_nixos: the same test)."""
    return os.path.exists("/etc/NIXOS") or os.path.isdir("/run/current-system/sw")


def _find(top, name):
    for d in (os.path.join(top, "bin"), top):
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return ""


def start_problem(trees, nixos=None, run=subprocess.run):
    """Start each downloaded tool (PROBES under `trees`) with --version -> None, or the player's message for the first
    one that cannot start. A tool not in place is left to firstrun.py's own checks."""
    nixos = is_nixos() if nixos is None else nixos
    for tree, name in PROBES:
        exe = _find(os.path.join(trees, tree), name)
        if not exe:
            continue
        why = ""
        try:
            r = run([exe, "--version"], capture_output=True, timeout=60)
            if r.returncode == 127 or (r.returncode != 0 and not (r.stdout or b"").strip()):
                why = f"exit code {r.returncode}"
        except FileNotFoundError:
            why = "the system cannot start it"
        except (OSError, subprocess.SubprocessError) as e:
            why = str(e) or e.__class__.__name__
        if why:
            msg = f"The Linux build tools are in place, but {name} could not be started ({why})."
            return msg + " " + (NIXOS_HINT if nixos else
                                "Download them again in Settings > Building > \"Linux build tools\".")
    return None


def main(argv=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=("status", "fetch", "toolchain", "check"))
    ap.add_argument("--dest", default="")
    ap.add_argument("--stop-file")
    a = ap.parse_args(argv)
    dest = os.path.abspath(a.dest) if a.dest else default_dest()
    if a.cmd == "status":
        print("STATUS " + json.dumps(status(dest)), file=out, flush=True)
        return 0
    if a.cmd == "toolchain":
        st = status(dest)
        if not st["ready"]:
            return 1
        print(st["toolchain"], file=out)
        return 0
    if a.cmd == "check":
        st = status(dest)
        why = start_problem(st["toolchain"]) if st["ready"] else None
        if why:
            print("REASON " + why, file=out, flush=True)
            return 1
        return 0
    return fetch(dest, out, a.stop_file)


if __name__ == "__main__":
    sys.exit(main())
