#!/usr/bin/env python3
"""Host test: every file a released game's build reads is in the build kit (0.4.0: four games' raw SPU tables,
tools/spu/<profile>_raw_spu.json, were left out of the public tree, so the kit packed from it lacked them and those
builds stopped at spu-lift with FileNotFoundError). No game data, no compiler, no network. Exit 0 = PASS.

    python3 -I tools/release/test_kit_inputs.py [LIST]

The file list: LIST (one repo-relative path per line), else, in the private source tree, what the public export takes
(`export-public.sh --list`, the script beside tools/release in tools/), else (the public tree, which is the export)
the files of this tree. The kit = that list's paths under pack_zip.KIT_TREE plus KIT_FILES (what pack_zip.py packs
from the public tree).

Cases, per released profile (firstrun_check.released_profiles: the catalogue's buildable games, resolved as
`firstrun.py --game` resolves them):
  - its required inputs (firstrun_check.profile_build_inputs: glue, SPU config / jobs / code, the config's lift table,
    the raw SPU table) are in the kit; from the private tree also its optional ones that exist there (igjobs,
    ppu_overrides.txt, every file under port/rc/<profile>/), which a build silently lacks otherwise;
  - every table file firstrun.py's own lift commands name (cmd_spu_lift, cmd_ppu_lift) and every per-title table the
    SPU lift tools open by pattern ("%s_<name>.json" in tools/spu/spu_lift.py and the other lift tools) is one of
    those inputs, so profile_build_inputs cannot drift from the steps it describes;
  - the tracked files the launcher zip packs at their own path (pack_zip.REPO_FILES: catalog.py, kit.py and the data
    they read) are in the list (0.4.1's first export left out data/titles/unofficial.json: public CI and the zip broke).
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import firstrun  # noqa: E402
import firstrun_check as fc  # noqa: E402
import pack_zip  # noqa: E402  (KIT_TREE, KIT_FILES)

FAILS = []
LIFT_TOOLS = ("spu_lift.py", "lift_raw_spu.py", "lift_igjobs.py", "carve_jobmods.py", "lift_summary.py")


def check(cond, what, detail=""):
    print(("ok   " if cond else "FAIL ") + what + ("" if cond else f"  {detail}"[:1200]))
    if not cond:
        FAILS.append(what)


def file_list(argv):
    """-> (mode, [paths])."""
    if argv:
        with open(argv[0], encoding="utf-8") as f:
            return f"list {argv[0]}", [l.strip() for l in f if l.strip()]
    script = os.path.join(ROOT, "tools", "export-public.sh")
    if os.path.isfile(script):
        r = subprocess.run(["bash", script, "--list"], cwd=ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            check(False, "the export's --list runs", r.stderr)
            return "export", []
        return "export", r.stdout.split("\n")
    out = []
    for top in pack_zip.KIT_TREE + tuple(os.path.dirname(f) for f in pack_zip.KIT_FILES):
        for d, dirs, files in os.walk(os.path.join(ROOT, *top.split("/"))):
            dirs[:] = [x for x in dirs if x not in pack_zip.SKIP_DIRS]
            out += [os.path.relpath(os.path.join(d, n), ROOT).replace(os.sep, "/") for n in files]
    return "tree", out


def in_kit(path):
    return path in pack_zip.KIT_FILES or any(path.startswith(t + "/") for t in pack_zip.KIT_TREE)


def named_by_firstrun(pid, prof, tmp):
    """The repo files firstrun's spu-lift and ppu-lift commands name for this profile (argv entries under ROOT)."""
    c = firstrun.Ctx(root=ROOT, build=tmp, stage=tmp, game=pid, toolkit=os.path.join(tmp, "toolkit"), profile=prof)
    argv = [a for cmd in firstrun.cmd_spu_lift(c) + firstrun.cmd_ppu_lift(c) for a in cmd if isinstance(a, str)]
    rels = []
    for a in argv:
        a = os.path.abspath(a)
        if a.startswith(ROOT + os.sep) and re.search(r"\.(json|tsv|txt)$", a):
            rels.append(os.path.relpath(a, ROOT).replace(os.sep, "/"))
    return rels


def lift_tool_patterns():
    """{suffix} of the per-title files the SPU lift tools open as "%s_<suffix>" under data/titles."""
    pats = set()
    for n in LIFT_TOOLS:
        p = os.path.join(ROOT, "tools", "spu", n)
        if os.path.isfile(p):
            with open(p, encoding="utf-8") as f:
                pats |= set(re.findall(r"\"%s_([A-Za-z0-9_]+\.(?:json|tsv))\"", f.read()))
    return pats


def main(argv):
    mode, files = file_list(argv)
    kit = {p for p in files if p and in_kit(p)}
    names = set(files) if mode != "tree" else {p for p in pack_zip.REPO_FILES if os.path.isfile(os.path.join(ROOT, *p.split("/")))}
    miss = sorted(set(pack_zip.REPO_FILES) - names)
    check(not miss, f"the launcher zip's repo files are in the tree ({mode}: {len(pack_zip.REPO_FILES)} files)", miss)
    pids = fc.released_profiles(ROOT)
    check(len(pids) > 0, f"released profiles found ({', '.join(pids)})")
    profiles = {p["id"]: p for p in fc.load_profiles(ROOT)}
    pats = lift_tool_patterns()
    check({"spu_config.json", "spu_jobs.json", "spu_code.json"} <= pats,
          "the SPU lift tools' per-title table names are found", sorted(pats))
    with tempfile.TemporaryDirectory() as tmp:
        for pid in pids:
            req, opt = fc.profile_build_inputs(ROOT, pid)
            want = set(req) | (set(opt) if mode != "tree" else set())
            miss = sorted(want - kit)
            check(not miss, f"{pid}: every build input is in the kit ({mode}: {len(want)} files)", miss)
            if f"data/titles/{pid}_spu_config.json" in req:
                named = set(named_by_firstrun(pid, profiles[pid], tmp)) | {f"data/titles/{pid}_{s}" for s in pats}
                named = {n for n in named if os.path.isfile(os.path.join(ROOT, *n.split("/")))}
                extra = sorted(named - set(req) - set(opt))
                check(not extra, f"{pid}: profile_build_inputs covers every table firstrun's lift steps open", extra)
    print("test_kit_inputs:", "FAIL" if FAILS else "PASS")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
