#!/usr/bin/env python3
"""Host tests for proton.py (the Linux launcher's Proton path). No Proton, Wine, umu, network or game is used: a fake
`proton` script records its argv and environment, a fake `umu-run` and `steam-run` stand in on PATH, a fake wineserver
records `-k`, the pinned runtime is a small tarball served from file:// with its hash patched in, and every folder is a
temporary one (HOME, XDG_DATA_HOME, XDG_CONFIG_HOME are pointed there). Linux only (skips elsewhere).

Checked: Z: path mapping; the choice file (defaults, select, add, umu mode, refusals); the scan of Steam's places; the
pinned download (progress, SHA-256 refusal of a flipped byte deletes it and installs nothing, Stop, re-use); the launch
plan in both modes (direct: STEAM_COMPAT_* and `proton waitforexitandrun`; umu: PROTONPATH/WINEPREFIX/GAMEID) and
NixOS's steam-run rule; play end to end (log, STARTED/EXITED, settings bridge, saves outside the prefix, run file
removed); Stop (stop file and `stop --game`): wineserver -k on the game's prefix and the process group ended; the
settings bridge never replaces existing settings; catalog.py forwards `proton` and Linux `play` to proton.py.
"""
import hashlib
import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from unittest.mock import patch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
FAILS = []


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:1200]))
    if not cond:
        FAILS.append(what)


def wr(path, text="", mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)
    os.chmod(path, mode)
    return path


FAKE_PROTON = """#!/bin/sh
# records how it was started; "sleep" in FAKE_PROTON_MODE keeps it running (Stop tests)
{ printf '%s\\n' "$@"; echo "--env"; env | grep -E '^(STEAM_COMPAT_|PS3_|WINEPREFIX|PROTONPATH|GAMEID)' | sort; } > "$FAKE_RECORD"
echo "proton: starting $3"
# like Wine: a process in its own session (out of the process group's reach) that keeps the output pipe open
if [ "$FAKE_PROTON_MODE" = sleep ]; then setsid sleep 61 & sleep 60 & wait; fi
exit 3
"""
FAKE_WINESERVER = """#!/bin/sh
echo "$WINEPREFIX $*" >> "$FAKE_WS_RECORD"
"""
FAKE_UMU = """#!/bin/sh
{ printf '%s\\n' "$@"; echo "--env"; env | grep -E '^(STEAM_COMPAT_|PS3_|WINEPREFIX|PROTONPATH|GAMEID|PROTON_VERB)' | sort; } > "$FAKE_RECORD"
exit 0
"""


def make_proton(d, version="GE-Proton11-7"):
    wr(os.path.join(d, "proton"), FAKE_PROTON, 0o755)
    wr(os.path.join(d, "version"), f"1789520217 {version}\n")
    wr(os.path.join(d, "files", "bin", "wineserver"), FAKE_WINESERVER, 0o755)
    return d


def make_tarball(path, top="GE-Proton11-7-x86_64"):
    src = tempfile.mkdtemp()
    make_proton(os.path.join(src, top))
    with tarfile.open(path, "w:gz") as t:
        t.add(os.path.join(src, top), arcname=top)
    shutil.rmtree(src)
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def run(tmp):
    import proton as pr
    import catalog
    home = os.path.join(tmp, "home")
    env = {"HOME": home, "XDG_DATA_HOME": os.path.join(home, ".local", "share"),
           "XDG_CONFIG_HOME": os.path.join(home, ".config"), "PATH": os.path.join(tmp, "bin") + ":" + os.path.join(tmp, "sys"),
           "FAKE_RECORD": os.path.join(tmp, "record.txt"), "FAKE_WS_RECORD": os.path.join(tmp, "ws.txt")}
    os.makedirs(os.path.join(tmp, "bin"))
    os.makedirs(os.path.join(tmp, "sys"))   # only the plain tools the fake scripts use: no real umu-run / steam-run
    for t in ("env", "grep", "sort", "sleep", "sh", "setsid"):
        os.symlink(shutil.which(t), os.path.join(tmp, "sys", t))
    for k in ("PATH", "HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME"):   # keep only the fake tools in reach
        os.environ.pop(k, None)
    os.environ.update(env)
    data = os.path.join(env["XDG_DATA_HOME"], "qwarks-awakening")
    lib = os.path.join(data, "library.json")
    games = os.path.join(tmp, "games", "tods")
    exe = wr(os.path.join(games, "tods.exe"), "MZ")
    eboot = wr(os.path.join(tmp, "my files", "EBOOT.elf"), "\x7fELF")
    disc = os.path.join(tmp, "my files", "disc")
    os.makedirs(disc)
    wr(lib, json.dumps({"schema": 1, "games": [{"id": "tods", "title": "ToD", "build": {"exe": exe},
                                                 "files": {"eboot": eboot, "disc": disc}}]}))

    pr.is_nixos = lambda: False   # this host may be NixOS; the NixOS rule is tested on its own below
    # paths
    check(pr.wine_path("/data/p/a b/x.elf") == "Z:\\data\\p\\a b\\x.elf", "Z: path: absolute Linux path, backslashes")
    for bad in ("rel/x", "/a\\b", "/a\nb", ""):
        try:
            pr.wine_path(bad)
            check(False, f"Z: path refuses {bad!r}")
        except pr.Error:
            check(True, f"Z: path refuses {bad!r}")

    # the choice: defaults, nothing ready yet
    st = pr.status(lib)
    check(st["selected"] == "pinned" and st["umu_mode"] == "auto" and not st["pinned"]["ready"] and not st["ready"]
          and "not downloaded" in st["error"] and st["pinned"]["version"] == "GE-Proton11-7",
          "status: pinned GE-Proton11-7 by default, not downloaded, not ready", st)
    check(not os.path.exists(pr.prefs_path(lib)), "status writes nothing")
    # an installed Proton in Steam's compatibility tools is found; a folder without a proton script is not
    steam_ge = make_proton(os.path.join(home, ".local", "share", "Steam", "compatibilitytools.d", "GE-Proton11-7"))
    os.makedirs(os.path.join(home, ".local", "share", "Steam", "compatibilitytools.d", "empty"))
    valve = make_proton(os.path.join(home, ".steam", "root", "steamapps", "common", "Proton 10.0"), "proton-10.0-3")
    os.makedirs(os.path.join(home, ".steam", "root", "steamapps", "common", "Half-Life"))
    vers = pr.status(lib)["versions"]
    ids = [v["id"] for v in vers]
    check(steam_ge in ids and valve in ids and len(vers) == 2 and
          {v["version"] for v in vers} == {"GE-Proton11-7", "proton-10.0-3"},
          "scan: compatibilitytools.d and steamapps/common/Proton* (depth 1), each with its version", vers)
    # select / add / umu
    try:
        pr.select(lib, os.path.join(tmp, "nowhere"))
        check(False, "select refuses a folder not found")
    except pr.Error:
        check(True, "select refuses a folder not found")
    st = pr.select(lib, steam_ge)
    check(st["selected"] == steam_ge and st["ready"], "select: an installed Proton is chosen and ready", st)
    own = make_proton(os.path.join(tmp, "my-protons", "custom"))
    try:
        pr.add(lib, os.path.join(tmp, "my-protons"))
        check(False, "add refuses a folder with no proton script")
    except pr.Error:
        check(True, "add refuses a folder with no proton script")
    st = pr.add(lib, own)
    check(st["selected"] == own and any(v["id"] == own and v["added"] for v in st["versions"]),
          "add: the folder is listed and chosen", st)
    check(pr.set_umu(lib, "off")["umu_mode"] == "off" and pr.read_prefs(lib)["umu"] == "off", "umu: mode off is kept")
    wr(pr.prefs_path(lib), '{"selected": 5}')
    try:
        pr.read_prefs(lib)
        check(False, "a broken proton.json is refused, not guessed")
    except pr.Error:
        check(True, "a broken proton.json is refused, not guessed")
    os.remove(pr.prefs_path(lib))

    # the pinned download: a local tarball with its hash patched in
    tb = os.path.join(tmp, "srv", "GE-Proton11-7-x86_64.tar.gz")
    os.makedirs(os.path.dirname(tb))
    good = make_tarball(tb)
    entry = dict(pr.pinned_entry(), sha256=good, bytes=os.path.getsize(tb))
    url = pathlib.Path(tb).as_uri()
    file_open = lambda u: open(tb, "rb")
    with patch.object(pr, "pinned_entry", lambda manifest=None: entry):
        out = io.StringIO()
        check(pr.fetch(lib, out, url="https://example.org/x.tar.gz") == 1 and "not the runtime's GitHub release"
              in out.getvalue(), "fetch: a download address off the GitHub release is refused")
        bad = dict(entry, sha256="0" * 64)
        out = io.StringIO()
        rc = pr.fetch(lib, out, url=url, opener=file_open, entry=bad)
        check(rc == 1 and "SHA-256 differs" in out.getvalue() and not os.path.exists(pr.pinned_dir(entry=entry))
              and not os.listdir(os.path.join(data, "downloads")), "fetch: a wrong SHA-256 deletes the download, installs nothing",
              out.getvalue())
        # a flipped byte in the served file is refused the same way
        flipped = os.path.join(tmp, "srv", "flipped.tar.gz")
        b = bytearray(open(tb, "rb").read())
        b[len(b) // 2] ^= 1
        open(flipped, "wb").write(bytes(b))
        out = io.StringIO()
        check(pr.fetch(lib, out, url=url, opener=lambda u: open(flipped, "rb")) == 1 and "SHA-256 differs" in out.getvalue(),
              "fetch: a flipped byte is refused")
        stopf = os.path.join(tmp, "stop")
        wr(stopf)
        out = io.StringIO()
        check(pr.fetch(lib, out, stop_file=stopf, url=url, opener=file_open) == 130 and "RESULT ok=0 step=cancelled"
              in out.getvalue() and not os.path.exists(pr.pinned_dir(entry=entry)), "fetch: Stop ends it, nothing installed")
        os.remove(stopf)
        out = io.StringIO()
        rc = pr.fetch(lib, out, url=url, opener=file_open)
        st = pr.status(lib)
        check(rc == 0 and "PROGRESS step=1/2 name=download" in out.getvalue() and "RESULT ok=1" in out.getvalue()
              and st["pinned"]["ready"] and st["selected"] == "pinned" and st["ready"]
              and os.path.isfile(os.path.join(data, "runtimes", "GE-Proton11-7", "proton")),
              "fetch: checked, unpacked to <data>/runtimes/<tag>/, selected, ready", out.getvalue())
        out = io.StringIO()
        check(pr.fetch(lib, out, url=url, opener=lambda u: (_ for _ in ()).throw(AssertionError("no download")))
              == 0 and "already downloaded" in out.getvalue(), "fetch: a runtime in place is re-used (no download)")
        with open(os.path.join(pr.pinned_dir(entry=entry), pr.RUNTIME_STAMP), "w") as f:
            json.dump({"sha256": "1" * 64}, f)
        check(not pr.pinned_ready(entry=entry), "a runtime stamped with another hash is not ready")
        with open(os.path.join(pr.pinned_dir(entry=entry), pr.RUNTIME_STAMP), "w") as f:
            json.dump({"sha256": good}, f)

        # launch plan, direct (umu off)
        pr.set_umu(lib, "off")
        with patch.object(pr, "is_nixos", lambda: False):
            L = pr.launch(lib, "tods")
        pin = pr.pinned_dir(entry=entry)
        check(L["mode"] == "direct" and L["argv"] == [os.path.join(pin, "proton"), "waitforexitandrun", exe,
                                                       pr.wine_path(eboot)]
              and L["env"]["STEAM_COMPAT_DATA_PATH"] == os.path.join(data, "prefixes", "tods")
              and L["env"]["STEAM_COMPAT_CLIENT_INSTALL_PATH"] == os.path.join(data, "steam")
              and L["env"]["SteamAppId"] == "0" and L["env"]["PROTONFIXES_DISABLE"] == "1"
              and L["env"]["PS3_VFS_ROOT"] == pr.wine_path(disc)
              and L["env"]["PS3_HDD0_ROOT"] == pr.wine_path(os.path.join(data, "saves", "tods", "dev_hdd0"))
              and L["cwd"] == games, "plan (direct): proton waitforexitandrun exe Z:eboot, one prefix per game, saves "
              "outside the prefix", L)
        # NixOS: steam-run in front, or a refusal that says what to install
        with patch.object(pr, "is_nixos", lambda: True):
            try:
                pr.launch(lib, "tods")
                check(False, "NixOS without steam-run or umu is refused")
            except pr.Error as ex:
                check("steam-run" in str(ex), "NixOS without steam-run or umu is refused with what to install", str(ex))
            sr = wr(os.path.join(tmp, "bin", "steam-run"), "#!/bin/sh\nexec \"$@\"\n", 0o755)
            L = pr.launch(lib, "tods")
            check(L["argv"][0] == sr and L["argv"][1] == os.path.join(pin, "proton"), "NixOS: steam-run in front", L["argv"])
            os.remove(sr)
        # umu when installed and allowed
        umu = wr(os.path.join(tmp, "bin", "umu-run"), FAKE_UMU, 0o755)
        with patch.object(pr, "is_nixos", lambda: False):
            check(pr.launch(lib, "tods")["mode"] == "direct", "umu off: an installed umu-run is not used")
        pr.set_umu(lib, "auto")
        L = pr.launch(lib, "tods")
        check(L["mode"] == "umu" and L["argv"] == [umu, exe, pr.wine_path(eboot)] and L["env"]["PROTONPATH"] == pin
              and L["env"]["WINEPREFIX"] == os.path.join(data, "prefixes", "tods") and L["env"]["GAMEID"] == "0"
              and "STEAM_COMPAT_DATA_PATH" not in L["env"], "plan (umu): umu-run exe Z:eboot, PROTONPATH, WINEPREFIX", L)
        out = io.StringIO()
        rc = pr.play(lib, "tods", out=out)
        rec = open(env["FAKE_RECORD"]).read()
        check(rc == 0 and "EXITED game=tods code=0" in out.getvalue() and f"PROTONPATH={pin}" in rec,
              "play (umu): umu-run starts the game with the chosen Proton", out.getvalue() + rec)
        os.remove(umu)

        # play end to end, direct
        with patch.object(pr, "is_nixos", lambda: False):
            out = io.StringIO()
            rc = pr.play(lib, "tods", out=out)
        o = out.getvalue()
        rec = open(env["FAKE_RECORD"]).read().split("\n")
        logf = os.path.join(data, "logs", "proton-tods.log")
        cfg = os.path.join(env["XDG_CONFIG_HOME"], "qwarks-awakening")
        check(rc == 0 and f"LOG path={logf}" in o and "STARTED game=tods pid=" in o and "EXITED game=tods code=3" in o
              and "proton: starting" in o and "proton: starting" in open(logf).read(),
              "play: LOG, STARTED, the output (also in the log), EXITED code=N", o)
        check(rec[:4] == ["waitforexitandrun", exe, pr.wine_path(eboot), "--env"]
              and f"STEAM_COMPAT_DATA_PATH={data}/prefixes/tods" in rec and f"PS3_VFS_ROOT={pr.wine_path(disc)}" in rec,
              "play: proton got the plan's argv and environment", rec)
        check(os.path.islink(os.path.join(games, "tods")) and os.path.realpath(os.path.join(games, "tods")) ==
              os.path.join(cfg, "tods") and os.path.islink(os.path.join(games, "shared"))
              and open(os.path.join(games, pr.MARKER), "rb").read() == pr.MARKER_TEXT,
              "play: the launcher's settings folders are linked into the game folder with the portable marker")
        check(not os.path.exists(pr.run_file("tods")), "play: the run file is removed when the game ends")
        out = io.StringIO()
        with patch.object(pr, "is_nixos", lambda: False):
            check(pr.play(lib, "tods", out=out) == 0, "play: a second start keeps the bridge (idempotent)", out.getvalue())

        # Stop through the stop file: wineserver -k on this prefix, the process group ends
        os.environ["FAKE_PROTON_MODE"] = "sleep"
        stopf = os.path.join(tmp, "play.stop")
        out = io.StringIO()
        import threading
        res = {}
        with patch.object(pr, "is_nixos", lambda: False):
            th = threading.Thread(target=lambda: res.update(rc=pr.play(lib, "tods", stop_file=stopf, out=out)))
            t0 = time.monotonic()
            th.start()
            while "STARTED" not in out.getvalue() and time.monotonic() - t0 < 10:
                time.sleep(0.05)
            st = json.load(open(pr.run_file("tods")))
            wr(stopf)
            th.join(20)
        o = out.getvalue()
        ws = open(env["FAKE_WS_RECORD"]).read() if os.path.exists(env["FAKE_WS_RECORD"]) else ""
        alive, t1 = True, time.monotonic()
        while alive and time.monotonic() - t1 < 3:   # an orphaned child may linger a moment as a zombie
            try:
                os.killpg(st["pid"], 0)
                time.sleep(0.1)
            except OSError:
                alive = False
        check(not th.is_alive() and res.get("rc") == 0 and "STOPPED game=tods" in o and "EXITED game=tods" in o
              and time.monotonic() - t0 < 15, "Stop (stop file): the game ends, STOPPED then EXITED", o)
        check(f"{data}/prefixes/tods/pfx -k" in ws, "Stop: wineserver -k on the game's own prefix", ws)
        check(not alive and not os.path.exists(stopf), "Stop: the whole process group is gone, the stop file removed")
        check(not [p for p in pr.prefix_pids(os.path.join(data, "prefixes", "tods")) if pr._alive(p)],
              "Stop: also the process in its own session (found by the prefix in its environment) is gone")
        # Stop from outside (stop --game)
        os.remove(env["FAKE_WS_RECORD"])
        out = io.StringIO()
        with patch.object(pr, "is_nixos", lambda: False):
            th = threading.Thread(target=lambda: res.update(rc=pr.play(lib, "tods", out=out)))
            th.start()
            t0 = time.monotonic()
            while "STARTED" not in out.getvalue() and time.monotonic() - t0 < 10:
                time.sleep(0.05)
            o2 = io.StringIO()
            rc = pr.stop("tods", out=o2)
            th.join(20)
        check(rc == 0 and "STOPPED game=tods" in o2.getvalue() and not th.is_alive() and "EXITED" in out.getvalue()
              and "-k" in open(env["FAKE_WS_RECORD"]).read(), "stop --game: ends a running game from outside")
        o2 = io.StringIO()
        check(pr.stop("tods", out=o2) == 1 and "not running" in o2.getvalue(), "stop --game: nothing running is said plainly")
        del os.environ["FAKE_PROTON_MODE"]

    # the bridge never replaces existing settings
    g2 = os.path.join(tmp, "g2")
    os.makedirs(os.path.join(g2, "tods"))
    try:
        pr.bridge_settings(g2, "tods", os.path.join(tmp, "cfg2"))
        check(False, "bridge: an existing settings folder is left as it is")
    except pr.Error as ex:
        check("left as they are" in str(ex) and not os.path.exists(os.path.join(g2, pr.MARKER)),
              "bridge: an existing settings folder is left as it is (nothing written)", str(ex))
    g3 = os.path.join(tmp, "g3")
    wr(os.path.join(g3, pr.MARKER), "[install]\nportable=0\n")
    try:
        pr.bridge_settings(g3, "tods", os.path.join(tmp, "cfg3"))
        check(False, "bridge: a marker that is not portable=1 is left as it is")
    except pr.Error:
        check(not os.path.lexists(os.path.join(g3, "tods")), "bridge: a marker that is not portable=1 is left as it is")

    # catalog.py forwards: `proton get` and Linux `play` (the plan's errors reach the launcher as REASON)
    r = subprocess.run([sys.executable, os.path.join(HERE, "catalog.py"), "proton", "get", "--library", lib],
                       capture_output=True, text=True, env=dict(os.environ))
    check(r.returncode == 0 and r.stdout.startswith("STATUS {"), "catalog.py proton get -> proton.py", r.stdout + r.stderr)
    pr.add(lib, own)
    shutil.rmtree(os.path.dirname(own))
    r = subprocess.run([sys.executable, os.path.join(HERE, "catalog.py"), "play", "--library", lib, "--game", "tods"],
                       capture_output=True, text=True, env=dict(os.environ))
    check(r.returncode == 1 and "REASON The chosen Proton folder is gone" in r.stdout,
          "catalog.py play on Linux goes through proton.py (a missing Proton is a REASON)", r.stdout + r.stderr)
    r = subprocess.run([sys.executable, os.path.join(HERE, "catalog.py"), "play", "--library", lib, "--game", "tods",
                        "--dry-run"], capture_output=True, text=True, env=dict(os.environ))
    check(r.returncode == 0 and json.loads(r.stdout)["argv"] == [exe, eboot], "catalog.py play --dry-run stays the host plan")
    # catalog.run_play (the Windows path) honours the stop file too: here a sleeping script stands in for the game
    wr(exe, "#!/bin/sh\necho running\nsleep 60 & wait\n", 0o755)
    stopf = os.path.join(tmp, "run_play.stop")
    out = io.StringIO()
    import threading
    th = threading.Thread(target=lambda: catalog.run_play(lib, "tods", out=out, stop_file=stopf))
    t0 = time.monotonic()
    th.start()
    while "STARTED" not in out.getvalue() and time.monotonic() - t0 < 10:
        time.sleep(0.05)
    wr(stopf)
    th.join(15)
    check(not th.is_alive() and "STOPPED game=tods" in out.getvalue() and "EXITED game=tods" in out.getvalue()
          and time.monotonic() - t0 < 12 and not os.path.exists(stopf), "catalog run_play: the stop file ends the game "
          "(process group), STOPPED then EXITED", out.getvalue())


def main():
    if not sys.platform.startswith("linux"):
        print("test_proton: SKIP (Linux only)")
        return 0
    tmp = tempfile.mkdtemp(prefix="test-proton-")
    saved = dict(os.environ)
    try:
        run(tmp)
    finally:
        os.environ.clear()
        os.environ.update(saved)
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_proton:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
