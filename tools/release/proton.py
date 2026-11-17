#!/usr/bin/env python3
"""Linux: run the Windows games through Proton (linux-launcher-plan.md phase 1 and 2). Python 3.8+, standard library.

The launcher (and catalog.py play on Linux) runs this; it never runs on Windows. The Proton choice:

  pinned     GE-Proton at the exact release in toolchain.json (entry "ge-proton": tag, SHA-256 of the upstream tarball).
             Downloaded from its GitHub release by `fetch`, which the launcher runs only after the player confirms;
             unpacked to <data>/runtimes/<tag>/. Never re-hosted, never shipped by this project.
  <folder>   a Proton the player already has: a Steam compatibility tool (compatibilitytools.d/<name>), Valve's Proton
             (steamapps/common/Proton*), or any folder holding an executable `proton` script that the player added.

How a game starts (one Wine prefix per game, <data>/prefixes/<game id>/; nothing of the player's other prefixes is used):
  - umu-launcher, when `umu-run` is on PATH and the umu setting is "auto": umu-run <exe> <eboot> with PROTONPATH = the
    chosen Proton, WINEPREFIX = the game's prefix, GAMEID=0 (no protonfixes). umu runs Proton inside Steam's container
    (Steam Linux Runtime), which it downloads itself on first use. umu-launcher is a separate program the player
    installs (GPL-3); it is only started as a process, never linked or shipped.
  - otherwise directly, as Steam would: <proton> waitforexitandrun <exe> <eboot> with STEAM_COMPAT_DATA_PATH = the
    game's prefix and STEAM_COMPAT_CLIENT_INSTALL_PATH = <data>/steam (an empty folder: Proton only needs it to exist;
    Steam itself is not needed), app id 0 and PROTONFIXES_DISABLE=1 as umu sets them. On NixOS a direct run needs an FHS environment: `steam-run` is put in front when it is
    on PATH, else the start is refused with what to install.
  Paths the game reads from its environment (PS3_VFS_ROOT, PS3_HDD0_ROOT, PS3_HDD0_LOWER) and the EBOOT argument are
  given as Wine paths (Z: + the absolute Linux path). The game's settings: the launcher's settings folder (XDG config)
  is linked into the game's folder with the runtime's portable marker, so Settings in the launcher apply to the game
  (bridge_settings; existing files are never replaced).

Stop: `play --stop-file F` watches F (the launcher's Stop button writes it); `stop --game ID` does the same from outside
(a launcher that was closed). Both end the game the same way: `wineserver -k` for that game's prefix, then SIGTERM to
every process whose environment names that prefix (Wine's processes start their own sessions, and steam-run runs them
in its own PID namespace) and to the process group play started, SIGKILL to what is left after 5 s.

    proton.py get    --library FILE                           STATUS {json}: choice, pinned runtime, installed Protons, umu
    proton.py select --library FILE --id pinned|FOLDER        choose (an installed folder must be listed or added)
    proton.py add    --library FILE --path FOLDER             add a Proton folder the scan does not find
    proton.py umu    --library FILE --mode auto|off           use umu-launcher when it is installed, or never
    proton.py fetch  --library FILE [--stop-file F] [--allow-local URL]   download + check + unpack the pinned GE-Proton
    proton.py plan   --library FILE --game ID [--data DIR]    the command, environment and folder play would use (JSON)
    proton.py play   --library FILE --game ID [--data DIR] [--stop-file F]
    proton.py stop   --library FILE --game ID

Data folders: <data> = $XDG_DATA_HOME/qwarks-awakening (default ~/.local/share/qwarks-awakening); the choice is kept in
proton.json beside library.json (catalog.py never writes it). Lines on stdout: STATUS {json}, PROGRESS ... pct=P (fetch),
LOG path=F, STARTED game= pid=, STOPPED game=, EXITED game= code= seconds=, REASON <text>, RESULT ok=0|1 step=S.
Exit 0 = ok (play: the game ran and exited, whatever its code), 1 = refused or failed, 2 = usage, 130 = fetch stopped.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import catalog            # noqa: E402  (play_command: the same launch plan as on Windows)
import fetch_toolchain as ft  # noqa: E402  (the pinned manifest, safe tar unpacking)

APP = "qwarks-awakening"
PINNED = "pinned"
PREFS_FILE = "proton.json"
PATH_ENV = ("PS3_VFS_ROOT", "PS3_HDD0_ROOT", "PS3_HDD0_LOWER")
MARKER = "kz3-install.ini"           # the runtime's portable marker (rc_settings.h reads this name)
MARKER_TEXT = b"[install]\nportable=1\n"
MAX_LOG = 1 << 20
MAX_PATHS = 64
STOP_GRACE = 5.0
RUNTIME_STAMP = ".qwarks-runtime.json"


class Error(Exception):
    """A message for the player (REASON)."""


# ------------------------------------------------------------------------------------------------------- folders
def xdg(key, fallback, env=None):
    env = os.environ if env is None else env
    v = env.get(key, "")
    if v and os.path.isabs(v):
        return v
    return os.path.join(os.path.expanduser("~"), fallback)


def data_root(env=None):
    return os.path.join(xdg("XDG_DATA_HOME", os.path.join(".local", "share"), env), APP)


def config_root(env=None):
    return os.path.join(xdg("XDG_CONFIG_HOME", ".config", env), APP)


def real_dir(path):
    """Make the folder (and its parents); refuse a symlink or a file at the last component."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    try:
        os.mkdir(path, 0o700)
    except FileExistsError:
        pass
    if not stat.S_ISDIR(os.lstat(path).st_mode):
        raise Error("A file or link is in the way of the launcher's folder " + path + ".")
    return path


def game_id(v):
    if not isinstance(v, str) or not re.fullmatch(r"[a-z0-9_-]{1,31}", v) or v == "shared":
        raise Error("Not a game id: " + repr(v))
    return v


def host_path(v, what="Path"):
    if not isinstance(v, str) or not v or any(ord(c) < 32 for c in v):
        raise Error(what + " must be a path without control characters.")
    v = os.path.expanduser(v)
    if not os.path.isabs(v):
        raise Error(what + " must be an absolute path.")
    return os.path.normpath(v)


def wine_path(path):
    """A Linux path as Wine sees it through its Z: drive (Z:\\home\\...); a backslash in the name cannot be mapped."""
    path = host_path(path)
    if "\\" in path:
        raise Error("A path with a backslash in a name cannot be given to the game: " + path)
    return "Z:" + path.replace("/", "\\")


# ------------------------------------------------------------------------------------------------------- choice
def prefs_path(library):
    return os.path.join(os.path.dirname(os.path.abspath(library)), PREFS_FILE)


def read_prefs(library):
    p = {"format": 1, "selected": PINNED, "paths": [], "umu": "auto"}
    try:
        with open(prefs_path(library), encoding="utf-8") as f:
            got = json.load(f)
    except FileNotFoundError:
        return p
    except (OSError, ValueError):
        raise Error("proton.json beside the library cannot be read; delete it to start from the defaults.")
    if not isinstance(got, dict):
        raise Error("proton.json beside the library is not a settings file; delete it to start from the defaults.")
    sel, paths, umu = got.get("selected", PINNED), got.get("paths", []), got.get("umu", "auto")
    if not isinstance(sel, str) or not isinstance(paths, list) or not all(isinstance(x, str) for x in paths) \
            or umu not in ("auto", "off"):
        raise Error("proton.json beside the library has values the launcher does not know; delete it to reset.")
    if sel != PINNED:
        sel = host_path(sel, "The chosen Proton folder")
    p.update(selected=sel, paths=[host_path(x, "An added Proton folder") for x in paths][:MAX_PATHS], umu=umu)
    return p


def write_prefs(library, p):
    d = os.path.dirname(os.path.abspath(library))
    os.makedirs(d, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".proton-", suffix=".tmp", dir=d)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            json.dump(p, f, indent=1)
            f.write("\n")
        os.replace(tmp, prefs_path(library))
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def pinned_entry(manifest=None):
    m = ft.load(manifest) if manifest else ft.load()
    for c in m.get("components", []):
        if c.get("linux") == "runtime":
            return ft.expand(c)
    raise Error("This launcher has no pinned Proton (toolchain.json).")


def pinned_dir(env=None, entry=None):
    e = entry or pinned_entry()
    return os.path.join(data_root(env), "runtimes", e["version"])


def pinned_ready(env=None, entry=None):
    """The pinned runtime is unpacked and stamped with the SHA-256 it was checked against."""
    e = entry or pinned_entry()
    d = pinned_dir(env, e)
    try:
        with open(os.path.join(d, RUNTIME_STAMP), encoding="utf-8") as f:
            st = json.load(f)
    except (OSError, ValueError):
        return False
    return st.get("sha256") == e["sha256"] and is_proton(d)


def is_proton(d):
    s = os.path.join(d, "proton")
    return os.path.isdir(d) and os.path.isfile(s) and os.access(s, os.X_OK)


def proton_name(d):
    """The tool's own version line ('<build time> GE-Proton11-7') or the folder name."""
    try:
        with open(os.path.join(d, "version"), encoding="utf-8", errors="replace") as f:
            parts = f.read(256).split()
        if len(parts) >= 2:
            return parts[-1]
    except OSError:
        pass
    return os.path.basename(d.rstrip("/"))


def steam_roots(env=None):
    home = os.path.expanduser("~")
    roots = [os.path.join(xdg("XDG_DATA_HOME", os.path.join(".local", "share"), env), "Steam"),
             os.path.join(home, ".local", "share", "Steam"), os.path.join(home, ".steam", "root"),
             os.path.join(home, ".steam", "steam"),
             os.path.join(home, ".var", "app", "com.valvesoftware.Steam", "data", "Steam")]
    out = []
    for r in roots:
        if r not in out:
            out.append(r)
    return out


def installed(p, env=None):
    """Protons the player has: Steam's compatibility tools and Proton installs (fixed places, depth 1) and the folders
    the player added. Each folder once (by its real path)."""
    cands = []
    for root in steam_roots(env):
        for sub, prefix in (("compatibilitytools.d", ""), (os.path.join("steamapps", "common"), "Proton")):
            try:
                with os.scandir(os.path.join(root, sub)) as it:
                    cands += sorted(e.path for e in it if e.name.startswith(prefix) and e.is_dir())
            except OSError:
                pass
    cands += p["paths"]
    seen, out = set(), []
    for c in cands:
        if not is_proton(c):
            continue
        real = os.path.realpath(c)
        if real in seen:
            continue
        seen.add(real)
        out.append({"id": os.path.normpath(c), "name": os.path.basename(c.rstrip("/")), "version": proton_name(c),
                    "added": c in p["paths"]})
    return out


def umu_path():
    return shutil.which("umu-run") or ""


def is_nixos():
    """NixOS has no FHS library layout, so Proton needs steam-run there (or umu). /etc/NIXOS, or the system profile
    when /etc is not visible (a bubblewrap sandbox binds only parts of /etc: the first walk ran without steam-run)."""
    return os.path.exists("/etc/NIXOS") or os.path.isdir("/run/current-system/sw")


def status(library, env=None):
    p = read_prefs(library)
    e = pinned_entry()
    vers = installed(p, env)
    umu = umu_path()
    st = {"selected": p["selected"], "umu_mode": p["umu"], "umu": umu, "nixos": is_nixos(),
          "steam_run": shutil.which("steam-run") or "",
          "pinned": {"version": e["version"], "ready": pinned_ready(env, e), "path": pinned_dir(env, e),
                     "bytes": e.get("bytes", 0), "url": e["url"]},
          "versions": vers}
    try:
        choose(p, env)
        st["ready"] = True
    except Error as ex:
        st["ready"], st["error"] = False, str(ex)
    return st


def choose(p, env=None):
    """-> (proton folder, mode, wrapper argv, umu path) for a start, or Error with what the player can do."""
    if p["selected"] == PINNED:
        e = pinned_entry()
        if not pinned_ready(env, e):
            raise Error(f"{e['version']} is not downloaded yet. Download it in Settings, Proton, or choose a Proton "
                        "you already have.")
        d = pinned_dir(env, e)
    else:
        d = p["selected"]
        if not is_proton(d):
            raise Error("The chosen Proton folder is gone or holds no proton script. Choose another in Settings, Proton.")
    umu = umu_path() if p["umu"] == "auto" else ""
    if umu:
        return d, "umu", [], umu
    if is_nixos():
        sr = shutil.which("steam-run")
        if not sr:
            raise Error("On NixOS, Proton needs steam-run or umu-launcher to start. Install one of them "
                        "(programs.steam.enable gives steam-run).")
        return d, "direct", [sr], ""
    return d, "direct", [], ""


def select(library, choice, env=None):
    p = read_prefs(library)
    if choice != PINNED:
        choice = host_path(choice, "The Proton folder")
        if not any(os.path.realpath(v["id"]) == os.path.realpath(choice) for v in installed(p, env)):
            raise Error("That folder is not in the list of Protons found. Add it first.")
    p["selected"] = choice
    write_prefs(library, p)
    return status(library, env)


def add(library, path, env=None):
    path = host_path(path, "The Proton folder")
    if not is_proton(path):
        raise Error("That folder holds no executable proton script. Choose the Proton folder itself "
                    "(the one with the files 'proton' and 'version').")
    p = read_prefs(library)
    if path not in p["paths"]:
        if len(p["paths"]) >= MAX_PATHS:
            raise Error("Too many added Proton folders.")
        p["paths"].append(path)
    p["selected"] = path
    write_prefs(library, p)
    return status(library, env)


def set_umu(library, mode, env=None):
    p = read_prefs(library)
    p["umu"] = mode
    write_prefs(library, p)
    return status(library, env)


# ------------------------------------------------------------------------------------------------------- fetch
def url_ok(url, allow_local):
    u = urllib.parse.urlparse(url)
    if allow_local and u.scheme == "file":
        return True
    return u.scheme == "https" and u.hostname == "github.com" and "/releases/download/" in u.path


def open_url(url):
    req = urllib.request.Request(url, headers={"User-Agent": "qwarks-awakening-launcher/1"})
    return urllib.request.urlopen(req, timeout=120)    # noqa: S310 (url_ok checked the scheme and host)


def fetch(library, out=sys.stdout, stop_file=None, url=None, env=None, opener=open_url, entry=None):
    """Download the pinned GE-Proton, check its SHA-256 (a mismatch deletes it and changes nothing), unpack it
    into <data>/runtimes/<tag>/ and select it. Exit code as main()."""
    e = entry or pinned_entry()
    src = url or e["url"]
    t0 = time.monotonic()

    def result(ok, step):
        print(f"RESULT ok={1 if ok else 0} step={step} elapsed={int(time.monotonic() - t0)}", file=out, flush=True)

    if not url_ok(src, bool(url)):
        print("REASON The download address is not the runtime's GitHub release.", file=out, flush=True)
        result(False, "download")
        return 1
    dest = pinned_dir(env, e)
    if pinned_ready(env, e):
        print(f"REASON {e['version']} is already downloaded.", file=out, flush=True)
        p = read_prefs(library)
        p["selected"] = PINNED
        write_prefs(library, p)
        result(True, "-")
        return 0
    dl_dir = real_dir(os.path.join(data_root(env), "downloads"))
    part = os.path.join(dl_dir, e["version"] + ".tar.part")
    total = int(e.get("bytes") or 0)
    h, n, last = hashlib.sha256(), 0, -1.0
    try:
        with opener(src) as r, open(part, "wb") as f:
            hdr = getattr(r, "headers", None)
            total = int((hdr.get("Content-Length") if hdr else 0) or total or 0)
            while True:
                if stop_file and os.path.exists(stop_file):
                    raise KeyboardInterrupt
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                n += len(chunk)
                if n > ft.MAX_DOWNLOAD:
                    raise Error("The download is larger than any Proton release; it was stopped.")
                h.update(chunk)
                f.write(chunk)
                pct = 100.0 * n / total if total else 0.0
                if pct - last >= 1.0:
                    last = pct
                    print(f"PROGRESS step=1/2 name=download done={n} total={total} "
                          f"elapsed={int(time.monotonic() - t0)} pct={pct * 0.9:.1f}", file=out, flush=True)
    except KeyboardInterrupt:
        _rm(part)
        print("REASON Download stopped. Nothing was installed.", file=out, flush=True)
        result(False, "cancelled")
        return 130
    except (OSError, ValueError, Error) as ex:
        _rm(part)
        print(f"REASON The download failed ({ex}). Nothing was installed.", file=out, flush=True)
        result(False, "download")
        return 1
    got = h.hexdigest()
    if got != e["sha256"]:
        _rm(part)
        print(f"REASON The download is not the pinned {e['version']} (SHA-256 differs); it was deleted and nothing "
              "was installed.", file=out, flush=True)
        result(False, "verify")
        return 1
    print(f"PROGRESS step=2/2 name=unpack done=0 total=1 elapsed={int(time.monotonic() - t0)} pct=90.0",
          file=out, flush=True)
    try:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        ft.unpack_tar(part, dest, bool(e.get("strip_top")))
        if not is_proton(dest):
            raise ValueError("the archive holds no proton script at its top")
        with open(os.path.join(dest, RUNTIME_STAMP), "w", encoding="utf-8") as f:
            json.dump({"version": e["version"], "sha256": got, "url": src}, f)
            f.write("\n")
    except (OSError, ValueError) as ex:
        print(f"REASON Unpacking failed ({ex}).", file=out, flush=True)
        result(False, "unpack")
        return 1
    finally:
        _rm(part)
    p = read_prefs(library)
    p["selected"] = PINNED
    write_prefs(library, p)
    print(f"PROGRESS step=2/2 name=unpack done=1 total=1 elapsed={int(time.monotonic() - t0)} pct=100.0",
          file=out, flush=True)
    result(True, "-")
    return 0


def _rm(p):
    try:
        os.remove(p)
    except OSError:
        pass


# ------------------------------------------------------------------------------------------------------- settings
def _portable(text):
    """rc_settings.h's reading of the marker: [install] portable=1 (first key, comments and case as it reads them)."""
    section = False
    for raw in text.split("\0", 1)[0].split("\n"):
        line = re.split(r"[#;]", raw, maxsplit=1)[0].strip(" \t\r")
        if line.startswith("["):
            end = line.find("]")
            section = end >= 0 and line[1:end].lower() == "install"
        elif section and "=" in line:
            k, v = line.split("=", 1)
            if k.rstrip(" \t").lower() == "portable":
                return v.strip(" \t\r") == "1"
    return False


def bridge_settings(exe_dir, gid, data):
    """The game reads its settings from its own folder when the portable marker is there (rc_settings.h): link
    <exe dir>/<game id> and <exe dir>/shared to the launcher's settings folder and write the marker. Existing settings,
    folders or a marker that does not say portable=1 are never replaced; a half-made bridge is undone."""
    exe_dir, data = os.path.realpath(exe_dir), os.path.realpath(data)
    marker = os.path.join(exe_dir, MARKER)
    pairs = [(os.path.join(exe_dir, n), os.path.join(data, n)) for n in (gid, "shared")]
    if os.path.lexists(marker):
        if not stat.S_ISREG(os.lstat(marker).st_mode):
            raise Error("The game folder's settings marker is a link or folder; it was left as it is: " + marker)
        with open(marker, "rb") as f:
            if not _portable(f.read(65536).decode("utf-8", "replace")):
                raise Error("The game folder has its own settings marker; it was left as it is: " + marker)
        has_marker = True
    else:
        has_marker = False
    for link, target in pairs:
        if os.path.lexists(link) and not (os.path.islink(link) and os.path.realpath(link) == target):
            raise Error(f"The game folder already has settings at {link}; they were left as they are. Move them to "
                        f"{target} to use the launcher's settings.")
    made = []
    try:
        for _, target in pairs:
            real_dir(target)
        for link, target in pairs:
            if not os.path.lexists(link):
                os.symlink(target, link, target_is_directory=True)
                made.append(link)
        if not has_marker:
            with open(marker, "xb") as f:
                f.write(MARKER_TEXT)
            made.append(marker)
    except BaseException:
        for m in reversed(made):
            _rm(m)
        raise


# ------------------------------------------------------------------------------------------------------- plan / play
def launch(library, gid, data=None, env=None):
    """-> dict(argv, env (additions only), cwd, prefix, proton, mode, wrapper, warnings)."""
    gid = game_id(gid)
    env = os.environ if env is None else env
    lib = catalog.read_library(library)
    if lib is None:
        raise Error("The library has not been made yet; start the launcher once.")
    try:
        (exe, eboot), add_env, cwd = catalog.play_command(lib, gid)
    except ValueError as ex:
        raise Error(str(ex))
    warnings = []
    upd = ((catalog._find_game(lib, gid) or {}).get("files") or {}).get("update")
    if upd and "PS3_HDD0_LOWER" not in add_env:
        warnings.append("WARN The update's own files are not layered over the game (only an installed update folder "
                        "named game/<title ID> can be); the game runs with the update's EBOOT.")
    proton, mode, wrapper, umu = choose(read_prefs(library), env)
    root = data_root(env)
    prefix = real_dir(os.path.join(root, "prefixes", gid))
    e = {k: wine_path(v) for k, v in add_env.items() if k in PATH_ENV}
    if "PS3_HDD0_ROOT" not in e:   # saves outside the prefix, so a prefix can be deleted without losing them
        e["PS3_HDD0_ROOT"] = wine_path(os.path.join(root, "saves", gid, "dev_hdd0"))
    if mode == "umu":
        e.update(WINEPREFIX=prefix, PROTONPATH=proton, GAMEID="0", PROTONFIXES_DISABLE="1",
                 PROTON_VERB="waitforexitandrun", STORE="none")
        argv = [umu, exe, wine_path(eboot)]
    else:
        # app id 0 as umu sets it: GE-Proton's protonfixes otherwise reads a Steam app id from the prefix path and
        # stops when there is none (first walk, 2026-11-16); its game fixes are for Steam titles, so they stay off
        e.update(STEAM_COMPAT_DATA_PATH=prefix, STEAM_COMPAT_CLIENT_INSTALL_PATH=os.path.join(root, "steam"),
                 STEAM_COMPAT_APP_ID="0", SteamAppId="0", SteamGameId="0", PROTONFIXES_DISABLE="1")
        argv = wrapper + [os.path.join(proton, "proton"), "waitforexitandrun", exe, wine_path(eboot)]
    return {"argv": argv, "env": e, "cwd": cwd, "prefix": prefix, "proton": proton, "mode": mode,
            "wrapper": wrapper, "warnings": warnings, "settings": data or config_root(env), "exe_dir": cwd}


def run_file(gid, env=None):
    return os.path.join(data_root(env), "run", gid + ".json")


def _start_ticks(pid):
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii", errors="replace") as f:
            return f.read().rsplit(")", 1)[1].split()[19]
    except (OSError, IndexError):
        return ""


def prefix_pids(prefix):
    """This user's processes whose environment names the game's prefix (STEAM_COMPAT_DATA_PATH, or WINEPREFIX = the
    prefix or its pfx/). Proton and umu pass these to every Wine process they start, also to processes in steam-run's
    own PID namespace or in a new session, which the process group does not reach (third walk, 2026-11-16)."""
    want = {os.path.normpath(prefix), os.path.normpath(os.path.join(prefix, "pfx"))}
    me, out = os.getpid(), []
    try:
        names = os.listdir("/proc")
    except OSError:
        return out
    for d in names:
        if not d.isdigit() or int(d) == me:
            continue
        try:
            with open(f"/proc/{d}/environ", "rb") as f:
                env = f.read(1 << 20)
        except OSError:
            continue
        for kv in env.split(b"\0"):
            k, _, v = kv.partition(b"=")
            if k in (b"STEAM_COMPAT_DATA_PATH", b"WINEPREFIX") and v and \
                    os.path.normpath(v.decode("utf-8", "replace")) in want:
                out.append(int(d))
                break
    return out


def _signal_all(pids, sig):
    for pid in pids:
        try:
            os.kill(pid, sig)
        except OSError:
            pass


def kill_game(state, env=None, grace=STOP_GRACE):
    """End the game: `wineserver -k` on its prefix (Wine's own way), then SIGTERM to every process of that prefix and
    to the process group play started, SIGKILL to what is left after `grace` seconds."""
    proton, prefix = state.get("proton", ""), state.get("prefix", "")
    ws = os.path.join(proton, "files", "bin", "wineserver")
    if prefix and os.path.isfile(ws) and os.access(ws, os.X_OK):
        e = dict(os.environ if env is None else env)
        e["WINEPREFIX"] = os.path.join(prefix, "pfx")
        try:
            subprocess.run(list(state.get("wrapper") or []) + [ws, "-k"], env=e, timeout=10,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except (OSError, subprocess.TimeoutExpired):
            pass
    pid = int(state.get("pid") or 0)
    group = pid > 0 and (not state.get("ticks") or _start_ticks(pid) == state["ticks"])

    def left():
        pids = prefix_pids(prefix) if prefix else []
        return [p for p in pids if _alive(p)]
    _signal_all(left(), signal.SIGTERM)
    if group:
        try:
            os.killpg(pid, signal.SIGTERM)
        except OSError:
            group = False
    end = time.monotonic() + grace
    while time.monotonic() < end and left():
        time.sleep(0.1)
    _signal_all(left(), signal.SIGKILL)
    if group:
        try:
            os.killpg(pid, signal.SIGKILL)
        except OSError:
            pass


def _alive(pid):
    """Running (a zombie waiting for its parent counts as gone)."""
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii", errors="replace") as f:
            return f.read().rsplit(")", 1)[1].split()[0] != "Z"
    except (OSError, IndexError):
        return False


def play(library, gid, data=None, stop_file=None, out=sys.stdout, env=None, popen=subprocess.Popen):
    try:
        L = launch(library, gid, data, env)
    except Error as ex:
        print(f"REASON {ex}", file=out, flush=True)
        return 1
    for w in L["warnings"]:
        print(w, file=out, flush=True)
    try:
        bridge_settings(L["exe_dir"], gid, L["settings"])
    except (Error, OSError) as ex:
        print(f"REASON {ex}", file=out, flush=True)
        return 1
    root = data_root(env)
    logfile = os.path.join(real_dir(os.path.join(root, "logs")), "proton-" + gid + ".log")
    full = dict(os.environ if env is None else env)
    full.update(L["env"])
    print("LOG path=" + logfile, file=out, flush=True)
    if stop_file:
        _rm(stop_file)
    try:
        p = popen(L["argv"], cwd=L["cwd"], env=full, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                  start_new_session=True)
    except OSError as ex:
        print(f"REASON Proton could not be started ({ex}).", file=out, flush=True)
        return 1
    state = {"pid": p.pid, "ticks": _start_ticks(p.pid), "proton": L["proton"], "prefix": L["prefix"],
             "wrapper": L["wrapper"], "game": gid}
    rf = run_file(gid, env)
    real_dir(os.path.dirname(rf))
    with open(rf, "w", encoding="utf-8") as f:
        json.dump(state, f)
    t0 = time.monotonic()
    print(f"STARTED game={gid} pid={p.pid}", file=out, flush=True)
    stopped = threading.Event()
    done = threading.Event()

    def watch():
        while not done.wait(0.25):
            if stop_file and os.path.exists(stop_file):
                stopped.set()
                kill_game(state, env)
                return
    threading.Thread(target=watch, daemon=True).start()
    left = MAX_LOG
    try:
        with open(logfile, "wb") as log:
            for raw in iter(lambda: p.stdout.readline(), b""):
                if left > 0:
                    keep = raw[:left]
                    log.write(keep)
                    left -= len(keep)
                    print(keep.decode("utf-8", "replace").rstrip("\r\n"), file=out, flush=True)
                    if left <= 0:
                        log.write(b"\n[log capped at 1 MiB]\n")
        rc = p.wait()
    except KeyboardInterrupt:
        kill_game(state, env)
        rc = p.wait()
    finally:
        done.set()
        _rm(rf)
        if stop_file:
            _rm(stop_file)
    if stopped.is_set():
        print(f"STOPPED game={gid}", file=out, flush=True)
    print(f"EXITED game={gid} code={rc} seconds={int(time.monotonic() - t0)}", file=out, flush=True)
    return 0


def stop(gid, env=None, out=sys.stdout):
    gid = game_id(gid)
    try:
        with open(run_file(gid, env), encoding="utf-8") as f:
            state = json.load(f)
    except (OSError, ValueError):
        print("REASON That game is not running from this launcher.", file=out, flush=True)
        return 1
    kill_game(state, env)
    _rm(run_file(gid, env))
    print(f"STOPPED game={gid}", file=out, flush=True)
    return 0


# ------------------------------------------------------------------------------------------------------- CLI
def main(argv=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("get", "select", "add", "umu", "fetch", "plan", "play", "stop"):
        s = sub.add_parser(name)
        s.add_argument("--library", required=True)
        if name == "select":
            s.add_argument("--id", required=True)
        if name == "add":
            s.add_argument("--path", required=True)
        if name == "umu":
            s.add_argument("--mode", required=True, choices=("auto", "off"))
        if name in ("plan", "play", "stop"):
            s.add_argument("--game", required=True)
        if name in ("plan", "play"):
            s.add_argument("--data")
        if name in ("play", "fetch"):
            s.add_argument("--stop-file")
        if name == "fetch":
            s.add_argument("--allow-local", metavar="URL", help="tests: a file:// URL instead of the release")
    a = ap.parse_args(argv)
    if not sys.platform.startswith("linux") and os.environ.get("RC_PROTON_ANY_OS") != "1":
        print("REASON Proton is for Linux; on Windows the launcher starts the game directly.", file=out, flush=True)
        return 2
    lib = os.path.abspath(os.path.expanduser(a.library))
    try:
        if a.cmd == "fetch":
            return fetch(lib, out, a.stop_file, a.allow_local)
        if a.cmd == "play":
            return play(lib, a.game, a.data, a.stop_file, out)
        if a.cmd == "stop":
            return stop(a.game, out=out)
        if a.cmd == "plan":
            L = launch(lib, a.game, a.data)
            print(json.dumps({k: L[k] for k in ("argv", "env", "cwd", "mode", "proton", "prefix")}, indent=1), file=out)
            return 0
        res = {"get": lambda: status(lib), "select": lambda: select(lib, a.id), "add": lambda: add(lib, a.path),
               "umu": lambda: set_umu(lib, a.mode)}[a.cmd]()
        print("STATUS " + json.dumps(res, ensure_ascii=False), file=out, flush=True)
        return 0
    except (Error, OSError) as ex:
        if a.cmd == "get":   # the Settings page still shows something
            print("STATUS " + json.dumps({"selected": PINNED, "ready": False, "versions": [], "error": str(ex)}),
                  file=out, flush=True)
            return 0
        print(f"REASON {ex}", file=out, flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
