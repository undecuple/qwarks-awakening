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


def main(argv=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=("status", "fetch", "toolchain"))
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
    return fetch(dest, out, a.stop_file)


if __name__ == "__main__":
    sys.exit(main())
