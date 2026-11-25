#!/usr/bin/env python3
"""First-run checks of the build pipeline (launcher-plan.md phase 2): can this game be built from what the player gave?

Adapted from the Killzone 3 port's tools/release/firstrun_check.py (Project Helghast, MIT, same author; 2026-11-10):
the report shape (OK / WARN / FAIL lines, --json {ok, checks, facts}), the CMake path-character rule and the OneDrive
placeholder rule are KZ3's; the game table, the EBOOT check (catalog.py check_eboot) and the per-game build profile
are this port's. Standard library only, Python 3.8+, Windows and Linux. Reads nothing of the game beyond PARAM.SFO and
the EBOOT's header and hash.

    firstrun_check.py --game ID [--eboot FILE] [--disc DIR|ISO] [--root DIR] [--build DIR] [--json]
    firstrun_check.py --kit-inputs [--root DIR]     every file a released game's build reads (tools/export-public.sh)

  --game   a catalogue id (tods, acit, qfb, a4o, ffa, nexus, rc1, rc2, rc3, dlhd) or a build profile id or alias
           (acit120, nexus101, collection_rc1, ffa105, ...): data/titles/build_profiles.json
  --eboot  the player's own decrypted program (EBOOT.elf; RC<n>.ppu.elf for the Collection's games)
  --disc   the game files: the disc as a folder (holds PS3_GAME), the installed PSN game folder, or a decrypted disc
           ISO. Optional for the build (the build needs only the EBOOT); Play needs it.
  --root   the source tree or install (default: two folders above this script); --build the build folder

Checks (code, what fails):
  G1 the game is in the catalogue and has a build profile; G2 this port has the game's glue (port/rc/<profile>/);
  G3 the tables the lift steps open are there (profile_build_inputs' required list; the paths go to the log);
  K1 the patched toolkit is there (release: <root>/ps3recomp; source tree: work/ps3recomp-patched or the
     submodule plus port/patches to make it);
  E1 the EBOOT: catalog.py check_eboot (encrypted, not a PS3 program, another game, wrong version = FAIL; unknown
     build or other region = WARN); E0 no EBOOT given;
  D1 the game files (when given): readable, PARAM.SFO title ID is one of the game's; an encrypted ISO is a FAIL;
  P1 a CMake list character ([ ] ;) in the install or build path; O1 the build folder inside OneDrive;
  S1 free disk space on the build drive (the game's estimate, else 2.5 GiB, plus 1 GiB; plus the ISO's size when
     its files are copied out).
Player messages name no path (the log has them) and say nothing about how to decrypt beyond pointing to RPCS3's
documentation (catalog.DOCS_HINT).

One line per check: OK / WARN / FAIL <code>: <message>. Exit 0 = no FAIL, 1 = at least one FAIL, 2 = usage.
"""
import argparse
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # the embeddable Python keeps the script folder off sys.path
import catalog  # noqa: E402  (stdlib only; the EBOOT check and the PARAM.SFO / ISO readers)

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
GIB = 1 << 30
CMAKE_BAD_CHARS = "[];"          # CMake reads these as list / bracket syntax inside paths (KZ3 pass 11)
DEFAULT_DISK_GB = 2.5            # ASSUMPTION: a game without a measured estimate (lift ~0.2 GB + objects ~1.2 GB + exe 0.6 GB)
DISK_MARGIN = 3 * GIB // 2      # the staged copy of the exe (0.6 GB with debug info) and slack
ONEDRIVE_VARS = ("OneDrive", "OneDriveConsumer", "OneDriveCommercial")


class Report:
    def __init__(self):
        self.checks, self.facts = [], {}

    def add(self, level, code, msg):
        self.checks.append({"code": code, "level": level, "message": msg})

    def ok(self, code, msg):
        self.add("OK", code, msg)

    def warn(self, code, msg):
        self.add("WARN", code, msg)

    def fail(self, code, msg):
        self.add("FAIL", code, msg)

    def passed(self):
        return not any(c["level"] == "FAIL" for c in self.checks)

    def reasons(self):
        return [c["message"] for c in self.checks if c["level"] == "FAIL"]


# ------------------------------------------------------------------------------------------------ game and profile
def load_profiles(root):
    with open(os.path.join(root, "data", "titles", "build_profiles.json"), encoding="utf-8") as f:
        return json.load(f)["profiles"]


def catalog_entries(cat):
    """{id: (entry, parent or None)} over the catalogue's games and children."""
    out = {}
    for g in cat["games"]:
        out[g["id"]] = (g, None)
        for c in g.get("children", []):
            out[c["id"]] = (c, g)
    return out


def resolve_game(root, game, cat=None):
    """-> {catalog_id, entry, parent, profile} or raises ValueError(player message). A catalogue id builds the
    catalogue's build_title (acit -> acit120); a profile id or alias names that profile (acit120, collection_rc1)."""
    cat = cat or catalog.load(os.path.join(root, "data", "titles", "catalog.json"))
    entries = catalog_entries(cat)
    profiles = load_profiles(root)

    def by_profile(key):
        for p in profiles:
            if key == p["id"] or key in p.get("aliases", []):
                return p
        return None

    if game in entries:
        entry, parent = entries[game]
        if entry.get("children"):
            kids = ", ".join(c["id"] for c in entry["children"])
            raise ValueError(f"{entry['title']} is built one game at a time: choose {kids}.")
        prof = by_profile(entry.get("build_title") or game)
        if prof is None:
            raise ValueError(f"{entry['title']} has no build recipe in this version of the port.")
        return {"catalog_id": game, "entry": entry, "parent": parent, "profile": prof}
    prof = by_profile(game)
    if prof is None or prof["catalog_id"] not in entries:
        raise ValueError(f"Unknown game '{game}'.")
    entry, parent = entries[prof["catalog_id"]]
    return {"catalog_id": prof["catalog_id"], "entry": entry, "parent": parent, "profile": prof}


def glue_dir(root, profile):
    return os.path.join(root, "port", "rc", profile["id"])


def has_glue(root, profile):
    return os.path.isfile(os.path.join(glue_dir(root, profile), f"hle_{profile['id']}.cpp"))


def released_profiles(root, cat=None):
    """Profile ids a player's launcher builds: every catalogue game or Collection game marked buildable (`supported` or
    `builds`, catalog.py's rule) without children, resolved the way --game resolves it (acit -> acit120, rc1 ->
    collection_rc1). An empty catalogue (a test tree) has none."""
    if cat is None:
        p = os.path.join(root, "data", "titles", "catalog.json")
        if not os.path.isfile(p):
            return []
        cat = catalog.load(p)
    if not cat.get("games"):
        return []
    out = []
    for gid, (entry, _parent) in catalog_entries(cat).items():
        if entry.get("children") or not (entry.get("supported") or entry.get("builds")):
            continue
        pid = resolve_game(root, gid, cat)["profile"]["id"]
        if pid not in out:
            out.append(pid)
    return out


def profile_build_inputs(root, pid):
    """-> (required, optional): the repo-relative paths (forward slashes) firstrun's lift steps and the game's CMake
    glue read for profile `pid`, besides the EBOOT, the toolkit and the shared tools.
      required: a build fails without them: port/rc/<pid>/hle_<pid>.cpp (G2), and once <pid>_spu_config.json is there
                (it turns the spu-lift step on) the other tables tools/spu/spu_lift.py and lift_raw_spu.py open: jobs,
                code, the config's lift table and tools/spu/<pid>_raw_spu.json (0.4.0: the last one was not
                exported for four games and their builds stopped at spu-lift).
      optional: read when present, and without them the build silently differs: the SPU config itself (none = no
                SPU lift), <pid>_igjobs.json, every file under port/rc/<pid>/ (ppu_overrides.txt and the glue).
    Only `optional` paths that exist under `root` are listed; `required` ones are listed either way."""
    t = lambda n: f"data/titles/{n}"
    req = [f"port/rc/{pid}/hle_{pid}.cpp"]
    cfg_rel = t(f"{pid}_spu_config.json")
    cfg_path = os.path.join(root, *cfg_rel.split("/"))
    if os.path.isfile(cfg_path):
        req += [cfg_rel, t(f"{pid}_spu_jobs.json"), t(f"{pid}_spu_code.json"), f"tools/spu/{pid}_raw_spu.json"]
        try:
            with open(cfg_path, encoding="utf-8") as f:
                table = (json.load(f) or {}).get("lift_table")
        except (OSError, ValueError, AttributeError):
            table = None
        if table:
            req.append(t(table))
    opt = [t(f"{pid}_igjobs.json")]
    glue = os.path.join(root, "port", "rc", pid)
    for d, dirs, files in os.walk(glue):
        dirs[:] = sorted(x for x in dirs if x != "__pycache__")
        opt += [os.path.relpath(os.path.join(d, n), root).replace(os.sep, "/") for n in sorted(files)]
    opt = [p for p in opt if p not in req and os.path.isfile(os.path.join(root, *p.split("/")))]
    return req, opt


def kit_inputs(root, cat=None):
    """{profile id: sorted required + present optional inputs} over released_profiles (export-public.sh's list)."""
    out = {}
    for pid in released_profiles(root, cat):
        req, opt = profile_build_inputs(root, pid)
        out[pid] = sorted(set(req) | set(opt))
    return out


def exe_name(profile):
    return profile["exe"] + ".exe"


# ------------------------------------------------------------------------------------------------ toolkit
def patchset_want(root):
    """The stamp tools/ps3recomp_patched.py writes: sha256(port/patches/*.patch)[:16] - the submodule's commit as
    `git rev-parse --short` prints it (7 or more hex digits). -> "<patch hash>-<full commit>" (stamp_matches compares
    the short form as a prefix). The commit comes from the gitlink (git ls-tree), so an uninitialised submodule still
    has one. None = no git."""
    import glob
    import hashlib
    import subprocess
    pats = sorted(glob.glob(os.path.join(glob.escape(os.path.join(root, "port", "patches")), "*.patch")))
    h = hashlib.sha256()
    for p in pats:
        with open(p, "rb") as f:
            h.update(f.read())   # raw bytes, as tools/build-rc.sh hashes them (cat | sha256sum)
    try:
        out = subprocess.run(["git", "-C", root, "ls-tree", "HEAD", "third_party/ps3recomp"], capture_output=True,
                             text=True, timeout=30).stdout.split()
        rev = out[2] if len(out) >= 3 else None
    except (OSError, subprocess.SubprocessError):
        rev = None
    return f"{h.hexdigest()[:16]}-{rev}" if rev else None


def stamp_matches(stamp, want):
    """A .rc-patchset stamp against patchset_want(): same patch hash, and the stamp's commit a prefix (>= 7 digits)
    of the submodule's commit."""
    if not stamp or not want or "-" not in stamp:
        return False
    sh, sc = stamp.split("-", 1)
    wh, wc = want.split("-", 1)
    return sh == wh and len(sc) >= 7 and wc.startswith(sc)


def find_toolkit(root, explicit=""):
    """-> (path, kind) of the patched ps3recomp: 'given' (--toolkit), 'release' (<root>/ps3recomp, shipped patched),
    'tree' (<root>/work/ps3recomp-patched, made by tools/ps3recomp_patched.py). The path may not exist yet."""
    if explicit:
        return os.path.abspath(explicit), "given"
    rel = os.path.join(root, "ps3recomp")
    if os.path.isfile(os.path.join(rel, "CMakeLists.txt")):
        return rel, "release"
    return os.path.join(root, "work", "ps3recomp-patched"), "tree"


def read_stamp(toolkit):
    try:
        with open(os.path.join(toolkit, ".rc-patchset"), encoding="utf-8") as f:
            return f.read().strip()
    except OSError:
        return ""


def can_make_toolkit(root):
    """The source tree can (re)make work/ps3recomp-patched with native Python and Git."""
    return (shutil.which("git") is not None
            and os.path.isfile(os.path.join(root, "third_party", "ps3recomp", "CMakeLists.txt"))
            and os.path.isfile(os.path.join(root, "tools", "ps3recomp_patched.py")))


def toolkit_state(root, explicit="", allow_stale=False):
    """-> {path, kind, stamp, want, state}: state ok | make (the toolkit step will make it) | missing | stale."""
    path, kind = find_toolkit(root, explicit)
    stamp = read_stamp(path)
    present = os.path.isfile(os.path.join(path, "CMakeLists.txt")) and os.path.isdir(os.path.join(path, "tools"))
    want = patchset_want(root) if kind == "tree" else None
    st = {"path": path, "kind": kind, "stamp": stamp, "want": want}
    if kind in ("release", "given"):
        st["state"] = "ok" if present else "missing"
    elif present and (allow_stale or not want or stamp_matches(stamp, want)):
        st["state"] = "ok"
    elif can_make_toolkit(root):
        st["state"] = "make"
    else:
        st["state"] = "stale" if present else "missing"
    return st


# ------------------------------------------------------------------------------------------------ the checks
def onedrive_root_of(path, env=None):
    env = os.environ if env is None else env
    p = os.path.normcase(os.path.abspath(path))
    for v in ONEDRIVE_VARS:
        d = env.get(v)
        if d:
            d = os.path.normcase(os.path.abspath(d))
            if p == d or p.startswith(d.rstrip("\\/") + os.sep):
                return d
    return None


def check_paths(r, root, build, env=None):
    for what, path in (("install", root), ("build", build)):
        bad = sorted({c for c in (path or "") if c in CMAKE_BAD_CHARS})
        if bad:
            r.fail("P1", f"The {what} folder's name contains {' '.join(bad)}, which the build tools cannot handle. "
                         "Choose a folder without these characters.")
    if build and onedrive_root_of(build, env):
        r.fail("O1", "The build folder is inside OneDrive. Choose a folder outside OneDrive for the build.")


def sfo_version(sfo):
    """-> {version, version_label}: PARAM.SFO APP_VER ("app version", the program's version) when set, else VERSION
    ("disc version", the disc's own number; the known-build table's disc_version, which E1 prints). The two differ on
    the same disc (ToD BCUS98127: APP_VER 01.00, VERSION 02.00), so each is printed with its own name."""
    if sfo.get("APP_VER"):
        return {"version": sfo["APP_VER"], "version_label": "app version"}
    if sfo.get("VERSION"):
        return {"version": sfo["VERSION"], "version_label": "disc version"}
    return {"version": None, "version_label": ""}


def disc_info(path):
    """-> {kind folder|iso, title_id, version, encrypted} or {error}."""
    if os.path.isdir(path):
        sfo = None
        for cand in (os.path.join(path, "PS3_GAME", "PARAM.SFO"), os.path.join(path, "PARAM.SFO")):
            if os.path.isfile(cand):
                with open(cand, "rb") as f:
                    try:
                        sfo = catalog.read_param_sfo(f.read())
                    except ValueError:
                        return {"error": "The game files' PARAM.SFO is damaged."}
                break
        if sfo is None:
            return {"error": "No PS3 game was found in the folder you chose (no PARAM.SFO)."}
        gap = catalog.folder_gap(os.path.dirname(cand))
        if gap:
            return {"error": f"The game folder is incomplete: {gap} is missing. Copy the whole game folder "
                             "(not only PARAM.SFO)."}
        return {"kind": "folder", "title_id": sfo.get("TITLE_ID"), **sfo_version(sfo), "encrypted": False}
    if os.path.isfile(path):
        sfo, enc = catalog.sfo_from_iso(path)
        if enc:
            return {"kind": "iso", "encrypted": True, "title_id": None, "version": None}
        if sfo is None:
            return {"error": "The file you chose is not a readable game disc image."}
        return {"kind": "iso", "title_id": sfo.get("TITLE_ID"), **sfo_version(sfo), "encrypted": False}
    return {"error": "The game files you chose are not there any more."}


def game_ids(entry, parent):
    ids = set(catalog._flat_ids(entry))
    if parent is not None:
        ids |= set(catalog._flat_ids(parent))
    return ids


def disk_need(entry, iso_bytes=0):
    est = entry.get("estimate") or {}
    gb = est.get("disk_gb") or DEFAULT_DISK_GB
    return int(gb * GIB) + DISK_MARGIN + iso_bytes


def free_bytes(path):
    p = os.path.abspath(path)
    while p and not os.path.exists(p):
        np = os.path.dirname(p)
        if np == p:
            break
        p = np
    try:
        return shutil.disk_usage(p).free
    except OSError:
        return None


def run_checks(root, game, eboot="", disc="", build="", toolkit="", allow_stale=False, env=None, cat=None,
               free=free_bytes, extracted=False, update=""):
    r = Report()
    root = os.path.abspath(root)
    try:
        g = resolve_game(root, game, cat)
    except (ValueError, OSError, KeyError) as e:
        r.fail("G1", str(e))
        return r
    entry, parent, prof = g["entry"], g["parent"], g["profile"]
    title = entry["title"]
    r.facts.update(game=g["catalog_id"], profile=prof["id"], title=title, exe=exe_name(prof),
                   elf_name=prof.get("elf_name") or "EBOOT.elf")
    r.ok("G1", f"{title}: build recipe {prof['id']}.")
    if has_glue(root, prof):
        r.ok("G2", "This version of the port can build this game.")
    else:
        r.fail("G2", f"{title} cannot be built by this version of the port yet.")
    if has_glue(root, prof):
        # G3: the tables the lift steps open (0.4.0: a missing tools/spu/<profile>_raw_spu.json stopped four games
        # at spu-lift with a traceback); the paths go to the log, the player gets one plain line
        miss = [p for p in profile_build_inputs(root, prof["id"])[0] if not os.path.isfile(os.path.join(root, *p.split("/")))]
        if miss:
            r.fail("G3", f"This install is missing {len(miss)} file(s) the build of {title} needs. "
                         "Installing the port again can help; if it does not, please report it.")
            r.facts["missing_inputs"] = miss
    tk =toolkit_state(root, toolkit, allow_stale)
    r.facts["toolkit"] = tk
    if tk["state"] == "ok":
        r.ok("K1", "The port's recompiler is ready.")
    elif tk["state"] == "make":
        r.ok("K1", "The port's recompiler will be prepared first.")
    elif tk["state"] == "stale":
        r.fail("K1", "The port's recompiler is older than its patches and cannot be refreshed here. "
                     "Install the port again.")
    else:
        r.fail("K1", "The port's recompiler is missing from this install. Install the port again.")
    if build:
        check_paths(r, root, build, env)
    # EBOOT
    if not eboot:
        r.fail("E0", f"{title} needs the decrypted {r.facts['elf_name']} made from your own copy. "
                     f"Choose it in Game files; {catalog.DOCS_HINT}")
    else:
        disc_tid = None
        if disc:
            di = disc_info(disc)
            disc_tid = di.get("title_id")
        known = catalog.eboot_index(cat or catalog.load(os.path.join(root, "data", "titles", "catalog.json")))
        chk = catalog.check_eboot(entry, eboot, known, disc_tid)
        r.facts["eboot"] = {"path": os.path.abspath(eboot), **{k: chk.get(k) for k in ("sha256", "size", "tag", "level")}}
        if chk.get("match"):
            r.facts["eboot"]["match"] = chk["match"]
        lvl = {"ok": "OK", "warn": "WARN", "fail": "FAIL"}[chk["level"]]
        r.add(lvl, "E1", chk["message"])
    # game files (optional for the build)
    iso_bytes = 0
    if disc:
        di = disc_info(disc)
        r.facts["disc"] = {"path": os.path.abspath(disc), **di}
        if di.get("error"):
            r.fail("D1", di["error"])
        elif di.get("encrypted"):
            r.fail("D1", "This disc image is encrypted. The port needs a decrypted disc image or the disc as a folder; "
                         + catalog.DOCS_HINT)
        elif catalog.untested_reason(entry, di.get("title_id")) or (parent and catalog.untested_reason(parent, di.get("title_id"))):
            r.fail("D1", catalog.untested_reason(entry, di.get("title_id")) or catalog.untested_reason(parent, di.get("title_id")))
        elif catalog.digital_reason(entry, di.get("title_id")):      # stage 0: no record for this digital copy yet
            r.fail("D1", catalog.digital_reason(entry, di.get("title_id")))
        elif di.get("title_id") not in game_ids(entry, parent):
            r.fail("D1", f"These game files are {di.get('title_id') or 'not a known game'}, not {title}.")
        else:
            r.ok("D1", f"Game files: {di['title_id']}"
                 + (f", {di.get('version_label') or 'version'} {di['version']}" if di.get("version") else "") + ".")
            if di["kind"] == "iso" and not extracted:
                try:
                    iso_bytes = os.path.getsize(disc)
                except OSError:
                    iso_bytes = 0
    else:
        r.warn("D0", "No game files chosen: the game can be built, but Play needs them.")
    if update:                  # a PKG, an incomplete or a misnamed update would run the game half-updated
        up = catalog.update_problem(update)
        r.facts["update"] = {"path": os.path.abspath(update)}
        if up:
            r.fail("U1", up)
        else:
            r.ok("U1", "Update: the installed update folder is complete.")
    need = disk_need(entry, iso_bytes)
    have = free(build or root)
    r.facts["disk"] = {"need": need, "free": have}
    if have is not None and have < need:
        r.fail("S1", f"Not enough free disk space for the build: {need / GIB:.1f} GB needed, {have / GIB:.1f} GB free. "
                     "Free some space or choose another build folder.")
    elif have is not None:
        r.ok("S1", f"Free disk space: {have / GIB:.1f} GB ({need / GIB:.1f} GB needed).")
    return r


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game")
    ap.add_argument("--kit-inputs", action="store_true",
                    help="print every file a released game's build reads (profile_build_inputs), one per line, and stop")
    ap.add_argument("--eboot", default="")
    ap.add_argument("--disc", default="")
    ap.add_argument("--update", default="")
    ap.add_argument("--root", default=DEFAULT_ROOT)
    ap.add_argument("--build", default="")
    ap.add_argument("--toolkit", default="")
    ap.add_argument("--allow-stale-toolkit", action="store_true")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    if a.kit_inputs:
        for p in sorted({p for v in kit_inputs(os.path.abspath(a.root)).values() for p in v}):
            print(p)
        return 0
    if not a.game:
        ap.error("--game is required")
    r = run_checks(a.root, a.game, a.eboot, a.disc, a.build, a.toolkit, a.allow_stale_toolkit,
                   update=a.update)
    if a.json:
        print(json.dumps({"ok": r.passed(), "checks": r.checks, "facts": r.facts}, indent=1))
    else:
        for c in r.checks:
            print(f"{c['level']} {c['code']}: {c['message']}")
    return 0 if r.passed() else 1


if __name__ == "__main__":
    sys.exit(main())
