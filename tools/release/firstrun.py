#!/usr/bin/env python3
"""Build pipeline of the setup engine (launcher-plan.md phase 2): one game from the player's own files to games/<id>/<exe>.

Adapted from the Killzone 3 port's tools/release/firstrun.py (Project Helghast, MIT, same author; 2026-11-10): the step
table with weights and done-markers, the machine-readable LOG / PROGRESS / REASON / RESULT lines, the stop file, the
resume rule, Ninja's [x/y] progress and the memory-based job count are KZ3's; the steps themselves are this port's
tools/lift.sh, tools/spu/lift_spu.sh and tools/build-rc.sh, driven from data/titles/build_profiles.json so every game
with a profile and glue (port/rc/<profile>/) builds the same way. Standard library only, Python 3.10+, Windows and Linux.

    firstrun.py --game ID [--eboot FILE] [--disc DIR|ISO] [--library FILE] [--root DIR] [--build DIR] [--stage DIR]
                [--jobs N] [--toolchain DIR] [--toolkit DIR] [--config RelWithDebInfo|Release] [--log FILE]
                [--stop-file FILE] [--dry-run] [--allow-stale-toolkit]

  --game      a catalogue id (tods, acit, qfb, a4o, ffa, nexus, rc1, rc2, rc3, dlhd) or a build profile id / alias
              (acit120, nexus101, collection_rc1, ...); firstrun_check.resolve_game
  --eboot     the player's decrypted program (EBOOT.elf, RC<n>.ppu.elf); --disc the game files (folder or decrypted
              ISO; optional for the build, an ISO's PS3_GAME is copied out to <build>/disc for Play)
  --library   library.json: --eboot / --disc default to the files it records for the game
  --root      the source tree or install (default: two folders above this script); its port/, data/, tools/, cmake/
  --build     everything generated (default <root>/build/firstrun/<profile>); --stage where the exe goes (default
              <root>/games/<catalogue id>)
  --toolchain llvm-mingw for the compile: a folder holding bin/x86_64-w64-mingw32-clang[.exe], or a folder holding
              llvm-mingw/, cmake/, ninja/ (fetch_toolchain.py's trees/ layout, the install's toolchain/). Default: the
              install's <root>/toolchain when present, else (Linux) the MinGW-w64 GCC on PATH (cmake/mingw-w64.cmake,
              the same compiler tools/build-rc.sh uses, from `nix develop .#win`)
  --toolkit   the patched ps3recomp (default <root>/ps3recomp in an install, else <root>/work/ps3recomp-patched,
              made or refreshed by the toolkit step with tools/ps3recomp-patched.sh when its stamp is stale)
  --config    CMake build type: RelWithDebInfo (default: what tools/build-rc.sh builds and every test ran) or Release
              (what catalog.py build passes for players: RelWithDebInfo's -O2 without -g, no debug information)
  --split-debug  with RelWithDebInfo (the player's "debug information" setting): the stage step moves the exe's
              debug information to <exe>.debug beside it (objcopy --only-keep-debug / --strip-debug /
              --add-gnu-debuglink); build.json records debug_info none | split | embedded
  --jobs      compile jobs; default from free RAM: GCC (available - 8 GiB) / 2 GiB, 1..6 (build-rc.sh); clang
              (available - 3 GiB) / 1 GiB, 1..min(cores, 16) (KZ3's measured llvm-mingw rule)
  --stop-file the launcher's Cancel: when the file appears the running tool's process tree ends, its step leaves no
              marker, RESULT step=cancelled, exit 130; the next start resumes there
  --dry-run   print the plan (steps, weights, done or pending, commands); runs and writes nothing

Steps (name: what; marker keys):
  check      firstrun_check.run_checks in process (always)
  toolkit    tools/ps3recomp-patched.sh when the source tree's patched toolkit is missing or stale (always checked)
  extract    an ISO's PS3_GAME (and PS3_DISC.SFB) into <build>/disc (only for an ISO)
  load       ppu_loader.py + find_functions.py into <build>/analysis              (tools/lift.sh)
  ppu-lift   ppu_lifter.py --chunks 64 [--overrides port/rc/<p>/ppu_overrides.txt] into <build>/recomp; then the
             override announcement guard of build-rc.sh
  spu-lift   tools/spu/spu_lift.py, lift_raw_spu.py, [lift_igjobs.py], lift_summary.py into <build>/spu/lift
             (tools/spu/lift_spu.sh); skipped when the game has no SPU tables
  configure  cmake -S port/rc -B <build>/obj (RC_GAME, RC_EXE, PS3RECOMP_DIR, RECOMP_DIR, SPU_DIR; always, cheap)
  compile    cmake --build (Ninja; always: an up-to-date build is a no-op)
  stage      <stage>/<exe> + SDL3.dll and its licence + build.json (exe SHA-256, port version, inputs)

Machine-readable lines on stdout (the launcher and catalog.py build parse them; every other line is a tool's own):
  LOG path=<file>
  PROGRESS step=N/M name=<step> done=X total=Y elapsed=S pct=P
  REASON <text>                  one per reason before RESULT ok=0; plain words, no path
  STAGED exe=<file> sha256=<hex>
  RESULT ok=1|0 step=<failed step or -> elapsed=S
Exit 0 = built, 1 = a step failed, 2 = usage, 130 = cancelled.

Resume: each finished step writes <build>/.firstrun/<NN>-<name>.done (JSON: inputs fingerprint, seconds). A changed
EBOOT or game restarts from the load step; a changed lift input (toolkit stamp, override table, SPU tables, lift
tools) from the lift steps; configure/compile always run (Ninja rebuilds only what changed).
"""
import argparse
import datetime
import glob
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Callable

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # the embeddable Python keeps the script folder off sys.path
import catalog  # noqa: E402
import firstrun_check as fc  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MARKER_DIR = ".firstrun"
NINJA_RE = re.compile(r"^\[(\d+)/(\d+)\]")
LIFT_CHUNKS = 64          # tools/lift.sh RC_LIFT_CHUNKS default (patch 3005: stable address-keyed chunks)
LIFT_JOBS = 4             # tools/lift.sh -j 4
GIB = 1 << 30
CONFIGS = ("RelWithDebInfo", "Release")
STEP_REASONS = {          # the player's message when a step's tool fails (the log has the tool's own lines)
    "toolkit": "Preparing the port's recompiler failed.",
    "extract": "Copying the game files out of the disc image failed.",
    "load": "Reading the game's program failed. Check that the EBOOT is the decrypted one made from your own copy.",
    "ppu-lift": "Recompiling the game's main code failed.",
    "spu-lift": "Recompiling the game's SPU code failed.",
    "configure": "Setting up the compiler failed. The install may be incomplete; installing the port again can help.",
    "compile": "Compiling the game failed.",
    "stage": "Copying the built game to its folder failed.",
}
LOG_HINT = "The build log has the details."


# ------------------------------------------------------------------------------------------------------ context
@dataclass
class Ctx:
    root: str
    build: str
    stage: str
    game: str
    eboot: str = ""
    disc: str = ""
    jobs: int = 1
    config: str = "RelWithDebInfo"
    toolkit: str = ""                    # the patched ps3recomp folder (resolved by the check)
    toolkit_kind: str = ""
    toolkit_make: bool = False
    toolchain: str = ""                  # llvm-mingw folder, "" = MinGW-w64 GCC from PATH
    cmake: str = "cmake"
    ninja: str = ""
    python: str = sys.executable
    allow_stale: bool = False
    split_debug: bool = False            # RelWithDebInfo, then <exe>.debug beside the exe and none in it
    facts: dict = field(default_factory=dict)
    profile: dict = field(default_factory=dict)
    entry: dict = field(default_factory=dict)
    say: Callable | None = field(default=None, repr=False)
    progress: Callable | None = field(default=None, repr=False)   # (done, total) -> PROGRESS, set by the Runner

    def b(self, *rel):
        return os.path.join(self.build, *rel)

    def r(self, *rel):
        return os.path.join(self.root, *rel)

    @property
    def py(self):
        """UTF-8 mode: Windows Python's default text encoding is the ANSI code page (KZ3 pass 12: the lifter's
        comments came out as cp1252 there)."""
        return [self.python, "-X", "utf8"]

    @property
    def pid(self):
        return self.profile.get("id", self.game)

    @property
    def elf_name(self):
        return self.profile.get("elf_name") or "EBOOT.elf"

    @property
    def stem(self):
        return os.path.splitext(self.elf_name)[0]       # ppu_loader names its outputs after the ELF (EBOOT, RC1.ppu)

    @property
    def staged_elf(self):
        return self.b("analysis", self.elf_name)

    def env(self):
        e = dict(os.environ)
        extra = []
        if self.toolchain:
            extra.append(os.path.join(self.toolchain, "bin"))
        for p in (self.cmake, self.ninja):
            if p and os.path.isabs(p):
                extra.append(os.path.dirname(p))
        e["PATH"] = os.pathsep.join([d for d in extra if os.path.isdir(d)] + [e.get("PATH", "")])
        e["PYTHONIOENCODING"] = "utf-8"
        e["PYTHONUTF8"] = "1"
        return e

    def spu_tables(self):
        return os.path.isfile(self.r("data", "titles", f"{self.pid}_spu_config.json"))

    def overrides(self):
        p = self.r("port", "rc", self.pid, "ppu_overrides.txt")
        return p if os.path.isfile(p) else ""

    def parts(self):
        """Fingerprint parts: base = game, profile, EBOOT identity, game files; lift = what the lifts read besides it."""
        eb = self.facts.get("eboot") or {}
        key = {"game": self.game, "profile": self.profile, "eboot_sha256": eb.get("sha256"), "eboot_size": eb.get("size"),
               "disc": os.path.abspath(self.disc) if self.disc else ""}
        return {"base": _h16(json.dumps(key, sort_keys=True)), "lift": lift_inputs_hash(self)}

    def fingerprint(self, keys=()):
        p = self.parts()
        return p["base"] if not keys else _h16("|".join([p["base"]] + [p[k] for k in keys]))


def _h16(text):
    return hashlib.sha256(text.encode()).hexdigest()[:16]


def lift_inputs_hash(c):
    """SHA-256 (16 hex) over what the lifts read besides the EBOOT: the toolkit's patch-set stamp and lifter tools,
    the game's override table, SPU tables and our lift tools."""
    pid = c.pid
    items = [("toolkit", os.path.join(c.toolkit, ".rc-patchset")), ("toolkit", os.path.join(c.toolkit, "tools", "*.py")),
             ("root", c.r("port", "rc", pid, "ppu_overrides.txt")),
             ("root", c.r("data", "titles", f"{pid}_spu_*.json")), ("root", c.r("data", "titles", f"{pid}_spu_lift_table.tsv")),
             ("root", c.r("data", "titles", f"{pid}_igjobs.json")), ("root", c.r("tools", "spu", f"{pid}_raw_spu.json")),
             ("root", c.r("tools", "spu", "*.py"))]
    h = hashlib.sha256()
    for base, pat in items:
        hits = sorted(glob.glob(glob.escape(os.path.dirname(pat)) + os.sep + os.path.basename(pat)))
        h.update(f"{base}:{os.path.basename(pat)}:{len(hits)}\n".encode())
        for p in hits:
            h.update(os.path.basename(p).encode() + b"\0")
            with open(p, "rb") as f:
                h.update(hashlib.sha256(f.read()).digest())
    return h.hexdigest()[:16]


# ------------------------------------------------------------------------------------------------------ steps
@dataclass
class Step:
    name: str
    label: str
    weight: float
    commands: Callable
    always: bool = False
    ninja: bool = False
    clean: Callable = lambda c: []
    after: Callable | None = None
    keys: tuple = ()
    when: Callable = lambda c: True      # False = the step does not apply to this game / input (skipped, no marker)


class StepFailed(Exception):
    pass


def cmd_toolkit(c):
    return [["bash", c.r("tools", "ps3recomp-patched.sh")]] if c.toolkit_make else []


def after_toolkit(c):
    st = fc.toolkit_state(c.root, c.toolkit if c.toolkit_kind == "given" else "", c.allow_stale)
    if st["state"] != "ok":
        raise StepFailed("The port's recompiler could not be prepared.")
    c.say(f"toolkit {st['kind']} stamp {st['stamp'] or '(none)'}")


def iso_walk(iso, path=""):
    """-> [(relpath, extents)] of every file under path in an open catalog.Iso."""
    e = iso.lookup(path) if path else (True, [iso.root])
    if e is None or not e[0]:
        return []
    out = []
    for name, isdir, ext in iso._dir(*e[1][0]):
        rel = f"{path}/{name}" if path else name
        out += iso_walk(iso, rel) if isdir else [(rel, ext)]
    return out


def after_extract(c):
    """Copy PS3_DISC.SFB and PS3_GAME/ out of the decrypted ISO into <build>/disc (files with the same size kept)."""
    dest = c.b("disc")
    with catalog.Iso(c.facts["disc"]["path"]) as iso:
        files = [(r, e) for r, e in iso_walk(iso) if r.upper() == "PS3_DISC.SFB" or r.upper().startswith("PS3_GAME/")]
        total = sum(s for _, ext in files for _, s in ext) or 1
        done = 0
        for rel, ext in files:
            out = os.path.join(dest, *rel.split("/"))
            size = sum(s for _, s in ext)
            if not (os.path.isfile(out) and os.path.getsize(out) == size):
                os.makedirs(os.path.dirname(out), exist_ok=True)
                with open(out + ".part", "wb") as f:
                    for lba, s in ext:
                        off = 0
                        while off < s:
                            n = min(8 << 20, s - off)
                            f.write(iso._raw(lba * catalog.SECTOR + off, n))
                            off += n
                os.replace(out + ".part", out)
            done += size
            if c.progress:
                c.progress(done, total)
    c.facts["extracted"] = dest


def stage_elf(c):
    """The player's EBOOT under the profile's file name in <build>/analysis (a copy: the source may be on another
    drive; ppu_loader names its outputs after the file, and the lifts read <stem>.*.json)."""
    os.makedirs(c.b("analysis"), exist_ok=True)
    if os.path.isfile(c.eboot) and os.path.abspath(c.eboot) != os.path.abspath(c.staged_elf):
        shutil.copyfile(c.eboot, c.staged_elf)
    return c.staged_elf


def cmd_load(c):
    pt, an, elf = c.toolkit, c.b("analysis"), c.staged_elf
    return [[*c.py, os.path.join(pt, "tools", "ppu_loader.py"), elf, "-o", an + os.sep],
            [*c.py, os.path.join(pt, "tools", "find_functions.py"), elf,
             "--seed-json", os.path.join(an, f"{c.stem}.functions.json"), "--output", os.path.join(an, "functions.json")]]


def before_load(c):
    stage_elf(c)


def cmd_ppu_lift(c):
    an = c.b("analysis")
    ovr = ["--overrides", c.overrides()] if c.overrides() else []
    return [[*c.py, os.path.join(c.toolkit, "tools", "ppu_lifter.py"), c.staged_elf,
             "--functions", os.path.join(an, "functions.json"), "--hle-stubs", os.path.join(an, f"{c.stem}.imports.json"),
             "--toc", c.profile["toc"], "--code-end", c.profile["code_end"], *ovr,
             "-j", str(min(c.jobs, LIFT_JOBS)), "--chunks", str(LIFT_CHUNKS), "--output", c.b("recomp")]]


def override_guard(c):
    """build-rc.sh's guard: the lift announces exactly the addresses of the game's ppu_overrides.txt."""
    rec = c.b("recomp")
    ann = os.path.join(rec, "ppu_overrides.h")
    if not os.path.isfile(ann):
        ann = os.path.join(rec, "ppu_recomp.h")
    want = set()
    if c.overrides():
        with open(c.overrides(), encoding="utf-8") as f:
            for line in f:
                m = re.match(r"\s*([0-9A-Fa-f]{8})", line)
                if m:
                    want.add(m.group(1).upper())
    have = set()
    if os.path.isfile(ann):
        with open(ann, encoding="utf-8", errors="replace") as f:
            have = {m.upper() for m in re.findall(r"PPU_OVERRIDE_([0-9A-Fa-f]{8})", f.read())}
    if want != have:
        c.say(f"override guard: want {len(want)} announced {len(have)}; missing {sorted(want - have)[:5]} "
              f"extra {sorted(have - want)[:5]}")
        raise StepFailed("The recompiled code does not match the port's tables for this game. " + LOG_HINT)
    with open(os.path.join(rec, ".lift-stamp"), "w", encoding="utf-8") as f:
        f.write(f"game={c.pid} elf={(c.facts.get('eboot') or {}).get('sha256', '')[:16]} "
                f"overrides={_file16(c.overrides()) if c.overrides() else 'none'} "
                f"patchset={fc.read_stamp(c.toolkit)} chunks={LIFT_CHUNKS} date={_now()} by=firstrun\n")


def _file16(p):
    with open(p, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()[:16]


def _now():
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


def cmd_spu_lift(c):
    pid, pt, elf, out = c.pid, c.toolkit, c.staged_elf, c.b("spu")
    iso = [*c.py, "-I"]    # lift_spu.sh runs these isolated
    cmds = [[*iso, c.r("tools", "spu", "spu_lift.py"), pid, elf, pt, out],
            [*iso, c.r("tools", "spu", "lift_raw_spu.py"), elf, pt, os.path.join(out, "lift"),
             c.r("tools", "spu", f"{pid}_raw_spu.json")]]
    if os.path.isfile(c.r("data", "titles", f"{pid}_igjobs.json")):
        cmds.append([*iso, c.r("tools", "spu", "lift_igjobs.py"), elf, pt, os.path.join(out, "lift"),
                     c.r("data", "titles", f"{pid}_igjobs.json")])
    cmds.append([*iso, c.r("tools", "spu", "lift_summary.py"), os.path.join(out, "lift"),
                 os.path.join(out, "lift_summary.json")])
    return cmds


def spu_dir(c):
    lift = c.b("spu", "lift")
    if c.spu_tables() and os.path.isfile(os.path.join(lift, "spu_workloads.c")):
        return lift
    return c.r("port", "rc", c.pid, "spu")     # the CMake default (no lifted images)


def cm_path(p):
    """Forward slashes in CMake -D values on Windows (KZ3 pass 11); a no-op on Linux."""
    return p.replace("\\", "/") if os.sep == "\\" else p


def cmd_configure(c):
    a = [c.cmake, "-S", c.r("port", "rc"), "-B", c.b("obj"), "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={c.config}",
         f"-DPython3_EXECUTABLE={cm_path(c.python)}", f"-DRC_GAME={c.pid}", f"-DRC_EXE={c.profile['exe']}",
         f"-DPS3RECOMP_DIR={cm_path(c.toolkit)}", f"-DRECOMP_DIR={cm_path(c.b('recomp'))}",
         f"-DSPU_DIR={cm_path(spu_dir(c))}"]
    if c.config == "Release":
        # a player's build is RelWithDebInfo's code (-O2, the flags every test ran) without -g, so no debug
        # information is in the exe; CMake's own Release default (-O3) is not used
        a += [f"-DCMAKE_{lang}_FLAGS_RELEASE=-O2 -DNDEBUG" for lang in ("C", "CXX")]
    if c.ninja:
        a.append(f"-DCMAKE_MAKE_PROGRAM={cm_path(c.ninja)}")
    if c.toolchain:
        a += [f"-DCMAKE_TOOLCHAIN_FILE={cm_path(c.r('cmake', 'llvm-mingw.cmake'))}", f"-DLLVM_MINGW_ROOT={cm_path(c.toolchain)}"]
    else:
        a.append(f"-DCMAKE_TOOLCHAIN_FILE={cm_path(c.r('cmake', 'mingw-w64.cmake'))}")
    return [a]


def clean_configure(c):
    """A cache made for another toolkit, compiler or build type is dropped (CMake cannot switch compilers in place;
    ps3recomp globs its sources without CONFIGURE_DEPENDS, so a toolkit with new files needs a fresh configure)."""
    cache = c.b("obj", "CMakeCache.txt")
    stamp = c.b("obj", ".firstrun-config")
    want = json.dumps({"toolkit": fc.read_stamp(c.toolkit), "toolchain": c.toolchain, "config": c.config,
                       "root": c.root}, sort_keys=True)
    try:
        with open(stamp, encoding="utf-8") as f:
            same = f.read() == want
    except OSError:
        same = False
    if os.path.isfile(cache) and not same:
        return [c.b("obj")]
    return []


def after_configure(c):
    with open(c.b("obj", ".firstrun-config"), "w", encoding="utf-8") as f:
        f.write(json.dumps({"toolkit": fc.read_stamp(c.toolkit), "toolchain": c.toolchain, "config": c.config,
                            "root": c.root}, sort_keys=True))


def cmd_compile(c):
    return [[c.cmake, "--build", c.b("obj"), "-j", str(c.jobs)]]


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


def find_sdl3(c):
    """SDL3.dll for the game folder (patch 4010: native pads; the game falls back to XInput without it):
    <root>/SDL3.dll (beside the launcher in an install), the toolchain's SDL3 package, else stage_sdl3.py's search
    (the `nix develop .#win` shell). -> (dll, licence) or (None, None)."""
    cands = [(c.r("SDL3.dll"), c.r("licenses", "SDL3-LICENSE.txt"))]
    for top in filter(None, [os.path.dirname(c.toolchain) if c.toolchain else "", c.r("toolchain")]):
        for d in glob.glob(glob.escape(top) + os.sep + "sdl3*" + os.sep + "**" + os.sep + "SDL3.dll", recursive=True):
            if "x86_64" in d or "x64" in d:
                cands.append((d, ""))
    for dll, lic in cands:
        if os.path.isfile(dll):
            return dll, lic if lic and os.path.isfile(lic) else None
    return None, None


def objcopy_cmd(c):
    """The toolchain's objcopy for PE files: llvm-mingw's x86_64-w64-mingw32-objcopy (llvm-objcopy behind it), else
    MinGW-w64 binutils' on PATH (the `nix develop .#win` shell)."""
    exe = ".exe" if os.name == "nt" else ""
    if c.toolchain:
        for n in ("x86_64-w64-mingw32-objcopy", "llvm-objcopy"):
            p = os.path.join(c.toolchain, "bin", n + exe)
            if os.path.isfile(p):
                return p
    return shutil.which("x86_64-w64-mingw32-objcopy") or ""


def split_debug(c, exe):
    """(the player opted in): move the exe's debug information to <exe>.debug beside it and link it with
    .gnu_debuglink (objcopy --only-keep-debug, --strip-debug, --add-gnu-debuglink, run in the exe's folder so the
    link holds the bare file name). The .debug file is the player's own, from their game: never published.
    -> the debug file's name."""
    oc = objcopy_cmd(c)
    if not oc:
        raise StepFailed("The debug information could not be split: no objcopy came with the compiler. " + LOG_HINT)
    d, name = os.path.dirname(exe), os.path.basename(exe)
    dbg = name + ".debug"
    for argv in ([oc, "--only-keep-debug", name, dbg], [oc, "--strip-debug", name],
                 [oc, f"--add-gnu-debuglink={dbg}", name]):
        r = subprocess.run(argv, cwd=d, capture_output=True, text=True)
        c.say(" ".join(argv[1:]) + (f": {r.stderr.strip()}" if r.stderr.strip() else ""))
        if r.returncode != 0:
            raise StepFailed("The debug information could not be split. " + LOG_HINT)
    return dbg


def after_stage(c):
    exe = c.b("obj", c.facts["exe"])
    if not os.path.isfile(exe):
        raise StepFailed("The compiler finished but the game program is missing. " + LOG_HINT)
    os.makedirs(c.stage, exist_ok=True)
    dst = os.path.join(c.stage, c.facts["exe"])
    shutil.copyfile(exe, dst + ".part")
    os.replace(dst + ".part", dst)
    dll, lic = find_sdl3(c)
    sdl = None
    if dll:
        shutil.copyfile(dll, os.path.join(c.stage, "SDL3.dll"))
        if lic:
            shutil.copyfile(lic, os.path.join(c.stage, "SDL3-LICENSE.txt"))
        sdl = "copied"
    else:
        st = c.r("tools", "release", "stage_sdl3.py")
        if os.path.isfile(st):
            r = subprocess.run([*c.py, st, "--out", c.stage], capture_output=True, text=True)
            if r.returncode == 0:
                if os.path.isfile(os.path.join(c.stage, "LICENSE.txt")):
                    os.replace(os.path.join(c.stage, "LICENSE.txt"), os.path.join(c.stage, "SDL3-LICENSE.txt"))
                sdl = "stage_sdl3.py"
    if not sdl:
        c.say("SDL3.dll not staged: pads fall back to XInput")
    debug_info, debug_file = ("none" if c.config == "Release" else "embedded"), None
    if c.split_debug:
        debug_file = split_debug(c, dst)
        debug_info = "split"
    else:
        old = dst + ".debug"           # a debug file of an earlier build with debug information on
        if os.path.isfile(old):
            os.remove(old)
    sha = sha256_file(dst)
    cat = catalog.load(c.r("data", "titles", "catalog.json"))
    eb_sha = (c.facts.get("eboot") or {}).get("sha256")
    rec = {"schema": 1, "game": c.game, "profile": c.pid, "exe": c.facts["exe"], "exe_sha256": sha,
           "exe_size": os.path.getsize(dst), "port_version": cat.get("port_version"), "built_at": _now(),
           "config": c.config, "compiler": "llvm-mingw" if c.toolchain else "mingw-w64 gcc",
           "toolkit_stamp": fc.read_stamp(c.toolkit), "eboot_sha256": eb_sha,
           "eboot": os.path.abspath(c.eboot), "disc": c.facts.get("extracted") or (os.path.abspath(c.disc) if c.disc else ""),
           "sdl3": bool(sdl),
           # build record: the release, and what a later scan compares to offer "Update available: rebuild"
           "release": cat.get("port_version"), "inputs": catalog.build_inputs(c.root, c.pid, eb_sha),
           "debug_info": debug_info, "debug_file": debug_file}
    with open(os.path.join(c.stage, "build.json"), "w", encoding="utf-8") as f:
        json.dump(rec, f, indent=1)
        f.write("\n")
    c.say(f"STAGED exe={dst} sha256={sha}")


STEPS = [   # weights: ToD under Linux at -j6 (ASSUMPTION until measured through this file: lift ~1 min, compile ~10)
    Step("check", "Checking files", 1, lambda c: [], always=True),
    Step("toolkit", "Preparing the recompiler", 2, cmd_toolkit, always=True, after=after_toolkit),
    Step("extract", "Preparing game files", 10, lambda c: [], after=after_extract,
         when=lambda c: (c.facts.get("disc") or {}).get("kind") == "iso"),
    Step("load", "Reading the game program", 3, cmd_load, clean=lambda c: [c.b("analysis")]),
    Step("ppu-lift", "Recompiling main code", 6, cmd_ppu_lift, clean=lambda c: [c.b("recomp")], after=override_guard,
         keys=("lift",)),
    Step("spu-lift", "Recompiling SPU code", 8, cmd_spu_lift, clean=lambda c: [c.b("spu")], keys=("lift",),
         when=lambda c: c.spu_tables()),
    Step("configure", "Setting up the compiler", 2, cmd_configure, always=True, clean=clean_configure, after=after_configure),
    Step("compile", "Compiling", 75, cmd_compile, always=True, ninja=True),
    Step("stage", "Finishing", 1, lambda c: [], always=True, after=after_stage),
]
BEFORE = {"load": before_load}


# ------------------------------------------------------------------------------------------------------ helpers
def available_ram():
    try:
        if os.name == "nt":
            import ctypes

            class MS(ctypes.Structure):
                _fields_ = [("len", ctypes.c_ulong), ("load", ctypes.c_ulong), ("total", ctypes.c_ulonglong),
                            ("avail", ctypes.c_ulonglong), ("tpf", ctypes.c_ulonglong), ("apf", ctypes.c_ulonglong),
                            ("tv", ctypes.c_ulonglong), ("av", ctypes.c_ulonglong), ("ext", ctypes.c_ulonglong)]
            m = MS()
            m.len = ctypes.sizeof(MS)
            return m.avail if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m)) else None
        with open("/proc/meminfo") as f:
            for line in f:
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1]) * 1024
    except (OSError, ValueError, AttributeError):
        pass
    return None


def jobs_for(avail, clang=False, cores=None):
    """GCC: tools/build-rc.sh's rule, (available - 8 GiB) / 2 GiB, 1..6 (a 64-chunk cc1plus peaks at 0.7-1.3 GB,
    up to 5.3 GB). llvm-mingw clang: KZ3's measured rule, (available - 3 GiB) / 1 GiB, 1..min(cores, 16)."""
    if not avail:
        return 1
    if clang:
        return max(1, min(cores or 8, 16, int((avail - 3 * GIB) // GIB)))
    return max(1, min(6, int((avail - 8 * GIB) // (2 * GIB))))


def _find_exe(top, names):
    exe = ".exe" if os.name == "nt" else ""
    for n in names:
        for d in (top, os.path.join(top, "bin")):
            p = os.path.join(d, n + exe)
            if os.path.isfile(p):
                return p
    return ""


def resolve_toolchain(root, given):
    """-> (llvm_mingw, cmake, ninja). `given` (or <root>/toolchain) may be an llvm-mingw folder or a folder holding
    llvm-mingw/, cmake/, ninja/ (fetch_toolchain.py's trees/). Nothing found = ("", "cmake", "") = GCC from PATH."""
    tops = [given] if given else [os.path.join(root, "toolchain")]
    if given and not os.path.isdir(given):
        raise ValueError("The compiler folder you gave is not there.")
    for top in tops:
        if not top or not os.path.isdir(top):
            continue
        lm = top if _find_exe(top, ["x86_64-w64-mingw32-clang"]) else os.path.join(top, "llvm-mingw")
        if not _find_exe(lm, ["x86_64-w64-mingw32-clang"]):
            if given:
                raise ValueError("The compiler folder you gave holds no llvm-mingw.")
            continue
        cm = _find_exe(os.path.join(top, "cmake"), ["cmake"]) or "cmake"
        nj = _find_exe(os.path.join(top, "ninja"), ["ninja"])
        return lm, cm, nj
    return "", "cmake", ""


def library_files(path, game):
    """(eboot, disc) recorded for the game in library.json (overrides already applied by the engine)."""
    lib = catalog.read_library(path) if path else None
    if not lib:
        return "", ""
    g = catalog._find_game(lib, game)
    if not g:
        return "", ""
    f = g.get("files") or {}
    return f.get("eboot") or "", f.get("disc") or ""


# ------------------------------------------------------------------------------------------------------ runner
class Cancelled(Exception):
    pass


def marker_path(ctx, i, step):
    return ctx.b(MARKER_DIR, f"{i + 1:02d}-{step.name}.done")


def is_done(ctx, i, step):
    try:
        with open(marker_path(ctx, i, step), encoding="utf-8") as f:
            return json.load(f).get("inputs") == ctx.fingerprint(step.keys)
    except (OSError, ValueError):
        return False


def mark_done(ctx, i, step, secs):
    p = marker_path(ctx, i, step)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8") as f:
        json.dump({"inputs": ctx.fingerprint(step.keys), "seconds": round(secs, 1)}, f)


def safe_clean(ctx, d):
    """Remove a step's old outputs, only inside <build> (never the player's files)."""
    b = os.path.realpath(ctx.build)
    r = os.path.realpath(d)
    if r != b and r.startswith(b + os.sep) and os.path.isdir(r):
        shutil.rmtree(r)


class Log:
    def __init__(self, path):
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        self.path = path
        self.f = open(path, "w", encoding="utf-8", newline="\n")

    def write(self, line):
        self.f.write(line.rstrip("\r\n") + "\n")
        self.f.flush()

    def close(self):
        self.f.close()


class Runner:
    def __init__(self, ctx, steps, log, out=sys.stdout):
        self.ctx, self.steps, self.log, self.out = ctx, steps, log, out
        self.t0 = time.monotonic()
        tot = sum(s.weight for s in steps) or 1.0
        self.frac = [s.weight / tot for s in steps]
        self.proc = None
        self.stop_requested = False
        self.lock = threading.Lock()

    def watch_stop(self, path, poll=0.25):
        def loop():
            while not os.path.exists(path):
                time.sleep(poll)
            self.stop_requested = True
            self.stop_child()
        threading.Thread(target=loop, name="stop-file", daemon=True).start()

    def emit(self, line, to_log=True):
        with self.lock:
            print(line, file=self.out, flush=True)
            if to_log and self.log:
                self.log.write(line)

    def progress(self, i, done, total):
        part = (done / total) if total else 0.0
        pct = 100.0 * (sum(self.frac[:i]) + self.frac[i] * min(1.0, part))
        self.emit(f"PROGRESS step={i + 1}/{len(self.steps)} name={self.steps[i].name} done={done} total={total} "
                  f"elapsed={int(time.monotonic() - self.t0)} pct={pct:.1f}")

    def run_cmd(self, i, argv):
        step = self.steps[i]
        self.log.write(f"$ {subprocess.list2cmdline(argv)}")
        t = time.monotonic()
        kw = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == "nt" else {"start_new_session": True}
        tail = []
        try:
            self.proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                         encoding="utf-8", errors="replace", bufsize=1, env=self.ctx.env(), **kw)
        except OSError as e:
            self.emit(f"cannot start {os.path.basename(argv[0])}: {e}")
            return 127, tail
        last_pct = -1
        try:
            for line in self.proc.stdout:
                line = line.rstrip("\r\n")
                tail = (tail + [line])[-40:]
                m = NINJA_RE.match(line) if step.ninja else None
                if m:
                    self.log.write(line)
                    d, n = int(m.group(1)), int(m.group(2))
                    v = int(200 * d / n) if n else 0
                    if v != last_pct or d >= n:      # at most one PROGRESS per 0.5 %
                        last_pct = v
                        self.emit(line, to_log=False)
                        self.progress(i, d, n)
                    continue
                self.emit(line)
            rc = self.proc.wait()
            if self.stop_requested:
                raise Cancelled()
        except KeyboardInterrupt:
            self.stop_child()
            raise Cancelled()
        finally:
            self.proc = None
        self.log.write(f"# exit {rc} after {time.monotonic() - t:.1f} s")
        return rc, tail

    def stop_child(self):
        p = self.proc
        if p and p.poll() is None:
            if os.name == "nt":   # the whole tree: cmake -> ninja -> compilers outlive a terminate()
                subprocess.run(["taskkill", "/T", "/F", "/PID", str(p.pid)], capture_output=True,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            else:
                try:
                    os.killpg(p.pid, 15)
                except OSError:
                    p.terminate()
            try:
                p.wait(10)
            except subprocess.TimeoutExpired:
                p.kill()

    def run(self):
        ctx, n = self.ctx, len(self.steps)
        ctx.say = self.emit
        try:
            for i, step in enumerate(self.steps):
                if self.stop_requested:
                    raise Cancelled()
                if step.name == "check":
                    self.progress(i, 0, 1)
                    bad = self.check()
                    if bad:
                        return self.failed("check", *bad)
                    self.progress(i, 1, 1)
                    continue
                if not step.when(ctx):
                    self.log.write(f"## step {i + 1}/{n} {step.name}: not needed for this game")
                    self.progress(i, 1, 1)
                    continue
                if not step.always and is_done(ctx, i, step):
                    self.log.write(f"## step {i + 1}/{n} {step.name}: done earlier, skipped")
                    self.progress(i, 1, 1)
                    continue
                self.log.write(f"## step {i + 1}/{n} {step.name}: {step.label}")
                t = time.monotonic()
                for d in step.clean(ctx):
                    safe_clean(ctx, d)
                ctx.progress = lambda d, tot, i=i: self.progress(i, int(1000 * d / tot), 1000)
                try:
                    if step.name in BEFORE:
                        BEFORE[step.name](ctx)
                    cmds = step.commands(ctx)
                except (StepFailed, OSError) as e:
                    self.log.write(f"DETAIL {e}")
                    return self.failed(step.name, str(e) if isinstance(e, StepFailed) else STEP_REASONS.get(step.name, ""))
                self.progress(i, 0, len(cmds) or 1)
                for k, argv in enumerate(cmds):
                    rc, tail = self.run_cmd(i, argv)
                    if rc != 0:
                        self.emit(f"step {step.name} failed: exit {rc}")
                        last = next((ln for ln in reversed(tail) if re.search(r"\berror\b", ln, re.I)),
                                    tail[-1] if tail else "")
                        self.log.write(f"DETAIL {last}")
                        return self.failed(step.name, STEP_REASONS.get(step.name, "A build step failed.") + " " + LOG_HINT)
                    if not step.ninja:
                        self.progress(i, k + 1, len(cmds))
                if step.after:
                    try:
                        step.after(ctx)
                    except StepFailed as e:
                        return self.failed(step.name, str(e))
                    except OSError as e:
                        self.log.write(f"DETAIL {e}")
                        return self.failed(step.name, STEP_REASONS.get(step.name, "A build step failed.") + " " + LOG_HINT)
                self.progress(i, 1, 1)
                self.log.write(f"## step {step.name} done in {time.monotonic() - t:.1f} s")
                if not step.always:
                    mark_done(ctx, i, step, time.monotonic() - t)
            return self.result(True, "-")
        except (Cancelled, KeyboardInterrupt):
            self.stop_child()
            self.log.write("## cancelled: the running step left no marker, the next start resumes here")
            self.emit(f"RESULT ok=0 step=cancelled elapsed={int(time.monotonic() - self.t0)}")
            return 130

    def check(self):
        """The check step in process: firstrun_check.run_checks; sets the context's facts and toolkit."""
        c = self.ctx
        r = fc.run_checks(c.root, c.game, c.eboot, c.disc, c.build, c.toolkit if c.toolkit_kind == "given" else "",
                          c.allow_stale, extracted=os.path.isdir(c.b("disc", "PS3_GAME")))
        for chk in r.checks:
            self.emit(f"{chk['level']} {chk['code']}: {chk['message']}")
        self.log.write("# facts " + json.dumps(r.facts, sort_keys=True))
        if not r.passed():
            return r.reasons()
        c.facts = r.facts
        tk = r.facts["toolkit"]
        c.toolkit, c.toolkit_kind, c.toolkit_make = tk["path"], tk["kind"], tk["state"] == "make"
        g = fc.resolve_game(c.root, c.game)
        c.profile, c.entry = g["profile"], g["entry"]
        return []

    def failed(self, step, *reasons):
        rs = [" ".join(str(r).split()) for r in reasons if str(r).strip()] or ["The build stopped. " + LOG_HINT]
        for r in rs:
            self.log.write(r)
            self.emit(f"REASON {r}", to_log=False)
        return self.result(False, step)

    def result(self, ok, step):
        self.emit(f"RESULT ok={int(ok)} step={step} elapsed={int(time.monotonic() - self.t0)}")
        return 0 if ok else 1


def header(ctx, log_path):
    du = shutil.disk_usage(ctx.build if os.path.isdir(ctx.build) else ctx.root)
    ram = available_ram()
    return [f"# started {_now()}", f"# firstrun.py, Python {platform.python_version()}, {platform.platform()}",
            f"# cores {os.cpu_count()}, available RAM {ram / GIB:.1f} GiB" if ram else f"# cores {os.cpu_count()}",
            f"# free disk {du.free / GIB:.1f} GiB on the build drive, jobs {ctx.jobs}, config {ctx.config}",
            f"# game {ctx.game}", f"# root {ctx.root}", f"# build {ctx.build}", f"# stage {ctx.stage}",
            f"# compiler {ctx.toolchain or 'mingw-w64 gcc (cmake/mingw-w64.cmake)'}, cmake {ctx.cmake}, "
            f"ninja {ctx.ninja or '(PATH)'}, python {ctx.python}",
            f"# eboot {ctx.eboot or '(none)'}", f"# disc {ctx.disc or '(none)'}", f"# log {log_path}"]


def dry_run(ctx, steps, log_path, out=sys.stdout):
    r = fc.run_checks(ctx.root, ctx.game, ctx.eboot, ctx.disc, ctx.build, ctx.toolkit if ctx.toolkit_kind == "given" else "",
                      ctx.allow_stale)
    print(f"LOG path={log_path}", file=out)
    for chk in r.checks:
        print(f"{chk['level']} {chk['code']}: {chk['message']}", file=out)
    try:
        g = fc.resolve_game(ctx.root, ctx.game)
    except ValueError:
        return 1
    ctx.facts = r.facts
    ctx.profile, ctx.entry = g["profile"], g["entry"]
    tk = r.facts.get("toolkit") or fc.toolkit_state(ctx.root)
    ctx.toolkit, ctx.toolkit_kind, ctx.toolkit_make = tk["path"], tk["kind"], tk["state"] == "make"
    if not ctx.eboot:
        ctx.eboot = "<EBOOT.elf>"
    tot = sum(s.weight for s in steps) or 1.0
    for i, s in enumerate(steps):
        state = "n/a" if not s.when(ctx) else "always" if s.always else ("done" if is_done(ctx, i, s) else "pending")
        print(f"{i + 1}/{len(steps)} {s.name:10} {100 * s.weight / tot:5.1f}%  {state:7} {s.label}", file=out)
        for argv in s.commands(ctx):
            print("    " + subprocess.list2cmdline(argv), file=out)
    return 0 if r.passed() else 1


def _interrupt(*_):
    raise KeyboardInterrupt


def main(argv=None, steps=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", required=True)
    ap.add_argument("--eboot", default="")
    ap.add_argument("--disc", default="")
    ap.add_argument("--library", default="")
    ap.add_argument("--root", default=DEFAULT_ROOT)
    ap.add_argument("--build", default="")
    ap.add_argument("--stage", default="")
    ap.add_argument("--jobs", type=int, default=0)
    ap.add_argument("--toolchain", default="")
    ap.add_argument("--toolkit", default="")
    ap.add_argument("--config", default="RelWithDebInfo", choices=CONFIGS)
    ap.add_argument("--split-debug", action="store_true")
    ap.add_argument("--log", default="")
    ap.add_argument("--stop-file", default="")
    ap.add_argument("--allow-stale-toolkit", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args(argv)
    root = os.path.abspath(a.root)
    eboot, disc = a.eboot, a.disc
    if a.library and not (eboot and disc):
        le, ld = library_files(a.library, a.game)
        eboot, disc = eboot or le, disc or ld
    try:
        g = fc.resolve_game(root, a.game)
        pid, cid = g["profile"]["id"], g["catalog_id"]
    except (ValueError, OSError, KeyError) as e:
        print(f"REASON {e}", file=out)
        print("RESULT ok=0 step=check elapsed=0", file=out)
        return 1
    try:
        lm, cm, nj = resolve_toolchain(root, a.toolchain)
    except ValueError as e:
        print(f"REASON {e}", file=out)
        print("RESULT ok=0 step=check elapsed=0", file=out)
        return 2
    build = os.path.abspath(a.build or os.path.join(root, "build", "firstrun", pid))
    stage = os.path.abspath(a.stage or os.path.join(root, "games", cid))
    jobs = a.jobs or jobs_for(available_ram(), clang=bool(lm), cores=os.cpu_count())
    ctx = Ctx(root=root, build=build, stage=stage, game=a.game, eboot=os.path.abspath(eboot) if eboot else "",
              disc=os.path.abspath(disc) if disc else "", jobs=jobs, config=a.config, toolchain=lm, cmake=cm, ninja=nj,
              allow_stale=a.allow_stale_toolkit,
              split_debug=a.split_debug and a.config == "RelWithDebInfo")   # Release has no debug info to split
    if a.toolkit:
        ctx.toolkit, ctx.toolkit_kind = os.path.abspath(a.toolkit), "given"
    steps = list(STEPS if steps is None else steps)
    log_path = os.path.abspath(a.log or os.path.join(build, "firstrun.log"))
    if a.dry_run:
        return dry_run(ctx, steps, log_path, out)
    os.makedirs(build, exist_ok=True)
    log = Log(log_path)
    for line in header(ctx, log_path):
        log.write(line)
    run = Runner(ctx, steps, log, out)
    run.emit(f"LOG path={log_path}", to_log=False)
    if a.stop_file:
        try:
            os.remove(a.stop_file)       # a stale stop file from an earlier cancel must not stop this run
        except OSError:
            pass
        run.watch_stop(a.stop_file)
    try:
        import signal
        signal.signal(signal.SIGTERM, _interrupt)
    except (ValueError, AttributeError):
        pass
    try:
        return run.run()
    finally:
        log.close()


if __name__ == "__main__":
    sys.exit(main())
