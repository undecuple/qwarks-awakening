#!/usr/bin/env python3
"""Produce a title's lifted SPU images and registration TUs in the layout port/rc/CMakeLists.txt expects. Output under work/ only.

    nix develop .#re --command tools/sandbox.sh -- python3 -I tools/spu/spu_lift.py \\
        GAME ELF PS3RECOMP_DIR OUT_DIR       (GAME = tods|acit|nexus, ELF = work/bin/GAME/EBOOT.elf, PS3RECOMP_DIR = work/ps3recomp-patched,
                                              OUT_DIR = work/spu/GAME)

Per-title inputs (numbers, offsets and names recovered from the EBOOT; no game bytes): data/titles/GAME_spu_config.json (title string, symbol
prefix, image ids, policy-module slot and seeds, the optional lift table), GAME_spu_jobs.json (job-module header offsets, policy module EA, size
and file offset: read by carve_jobmods.py), GAME_spu_code.json (the EDGE inflate ELF's file offset and size), GAME_spu_lift_table.tsv (optional:
code ends and data islands, tools/survey/lift_table.py).

Writes (all derived from game code, never committed), shown for tods:
  OUT_DIR/jobmods/                  the carved job modules (carve_jobmods.py) and policy/pm_*.bin
  OUT_DIR/lift/jobmod_*/            the job modules lifted (lift_jobmods.py, with --table when the config names one)
  OUT_DIR/lift/elf_0008ea200/       the EDGE zlib inflate ELF (spu_lifter --auto-functions, --code-end at .rodata when it has section headers)
  OUT_DIR/lift/pm_10053d00/         the WWS policy module (wrapped at base 0xA00, seed from the config)
  OUT_DIR/lift/jobmods_register.c   lift_jobmods.py's own generator (image ids 200.., 16-byte header signatures)
  OUT_DIR/lift/spu_workloads.c      build_spu_workloads.write_registry (ps3recomp's generator) for the EDGE ELF and the policy
                                    module (fingerprint over the bytes the title passes, as cellSpurs.c spurs_resolve_pm computes)
  OUT_DIR/lift/spu_lift_<game>.json what was done
Every lifted header/source gets its relative runtime includes rewritten to bare ones (build_spu_workloads.debase_includes) because the
CMake build resolves them through -I runtime/spu. taskset_policy_bytes.c is NOT produced: those bytes come from the firmware's libsre.
"""
import glob, importlib.util, json, os, re, shutil, struct, subprocess, sys


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def run(cmd, log=None):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if log:
        os.makedirs(os.path.dirname(log), exist_ok=True)
        open(log, "w").write(r.stdout + r.stderr)
    if r.returncode:
        raise SystemExit("FAILED: %s\n%s" % (" ".join(cmd[:4]), r.stderr[-600:]))
    return r.stdout


def main():
    game, elf, ps3, out = sys.argv[1:5]
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", ".."))
    titles = os.path.join(repo, "data/titles")
    cfg = json.load(open(os.path.join(titles, "%s_spu_config.json" % game)))
    tools = os.path.join(ps3, "tools")
    sys.path.insert(0, tools)
    bsw = load(os.path.join(tools, "build_spu_workloads.py"), "bsw")
    from wrap_spu_elf import wrap
    jobs_json = os.path.join(titles, "%s_spu_jobs.json" % game)
    lift = os.path.join(out, "lift")
    shutil.rmtree(lift, ignore_errors=True)
    os.makedirs(lift)
    d = open(elf, "rb").read()
    summary = {}
    # 1. carve
    run([sys.executable, os.path.join(here, "carve_jobmods.py"), elf, jobs_json, out])
    # 2. job modules (patch 6001 gives lift_jobmods.py --table: code ends, data ranges, seed skip)
    cmd = [sys.executable, os.path.join(tools, "lift_jobmods.py"), "--jobmods", os.path.join(out, "jobmods"), "--out", lift,
           "--symbol-prefix", cfg["symbol_prefix"]]
    if cfg.get("lift_table"):
        cmd += ["--table", os.path.join(titles, cfg["lift_table"])]
    run(cmd, os.path.join(lift, "jobmods.log"))
    imgs, text_fps, ids = [], {}, {}
    # 3. EDGE ELF (optional: a title without one has no "edge_zlib_inflate_elf" in its code table)
    code = json.load(open(os.path.join(titles, "%s_spu_code.json" % game))).get("edge_zlib_inflate_elf")
    if code:
        eoff, esz = int(code["file_offset"], 16), code["size"]
        e = d[eoff:eoff + esz]
        assert bsw.is_spu_elf(e)
        os.makedirs(os.path.join(out, "images"), exist_ok=True)
        epath = os.path.join(out, "images", "elf_%08x.elf" % eoff)
        open(epath, "wb").write(e)
        from find_spu_functions import parse_elf
        ce = 0
        for sh in parse_elf(e)["shs"]:
            if sh["name"] == ".rodata" and sh["addr"]:
                ce = sh["addr"]
        if not ce and code.get("code_end"):
            ce = int(code["code_end"], 16)       # an ELF without section headers: the code end the table recovered
        ename = "elf_%08x" % eoff
        edir = os.path.join(lift, ename)
        cmd = [sys.executable, os.path.join(tools, "spu_lifter.py"), epath, "--auto-functions", epath, "--symbol-prefix", ename + "_", "-o", edir]
        if ce:
            cmd += ["--code-end", hex(ce)]
        run(cmd, os.path.join(lift, ename + ".log"))
        summary["edge"] = {"file_offset": hex(eoff), "size": esz, "code_end": hex(ce), "entry": hex(bsw.be32(e, 0x18))}
        imgs.append((ename, ename + "_", bsw.fnv1a64(e[:bsw.img_size(e)]), bsw.be32(e, 0x18)))
        text_fps[ename] = bsw.text_fp(e)
        ids[ename] = cfg["edge_image_id"]
    # 4. policy module
    pmfile = glob.glob(os.path.join(out, "policy", "pm_*.bin"))[0]
    pm = open(pmfile, "rb").read()
    pname = "pm_%x" % int(json.load(open(jobs_json))["workload_creation_wrapper"]["policy_module"]["ea"], 16)
    os.makedirs(os.path.join(out, "images"), exist_ok=True)
    ppath = os.path.join(out, "images", pname + ".elf")
    open(ppath, "wb").write(wrap(pm, base=0xA00, entry=0xA00))
    pdir = os.path.join(lift, pname)
    run([sys.executable, os.path.join(tools, "spu_lifter.py"), "--auto-functions", ppath, "--base", "0xa00", "--symbol-prefix", pname + "_",
         "--extra-funcs", cfg["pm_seed"], "-o", pdir], os.path.join(lift, pname + ".log"))
    summary["policy_module"] = {"size": len(pm), "fingerprint": "%016X" % bsw.fnv1a64(pm), "image_id": cfg["pm_image_id"]}
    # 5. include rewrite for every lifted tree
    for h in glob.glob(os.path.join(lift, "*", "spu_recomp.[ch]")):
        bsw.debase_includes(h)
    # 6. registration: ps3recomp's write_registry for the two non-jobmod images
    imgs.append((pname, pname + "_", bsw.fnv1a64(pm), 0xA00))
    text_fps[pname] = None
    ids[pname] = cfg["pm_image_id"]
    class A: pass
    a = A()
    a.out, a.title, a.register_fn, a.constructor = os.path.join(lift, "spu_workloads.c"), cfg["title"], "rc_spu_register_all", True
    # patch 6501: ToD's policy module keeps the job code-buffer address at LS 0x1330 (@LS 0x3024); the runtime default is 0x1320
    a.wws_code_slots = {pname: int(cfg["wws_code_slot"], 16)} if cfg.get("wws_code_slot") else {}
    bsw.write_registry(a, imgs, text_fps, ids)
    # write_registry declares the entry functions without SPU_ABI, which is sysv_abi on Win64; the lifted definitions carry it, so
    # spu_workload_register_img(...) fails to compile ("incompatible pointer type"). Fix the declarations here (an upstream generator bug).
    wp = os.path.join(lift, "spu_workloads.c")
    t = open(wp).read()
    t = re.sub(r"^extern void (\w+_spu_func_[0-9A-F]{8}\(spu_context\*\);)", r"extern SPU_ABI void \1", t, flags=re.M)
    open(wp, "w").write(t)
    # 7. summary
    summary["jobmods"] = len(glob.glob(os.path.join(lift, "jobmod_*")))
    summary["dirs_with_spu_recomp_c"] = len(glob.glob(os.path.join(lift, "*", "spu_recomp.c")))
    summary["generated"] = sorted(os.path.basename(p) for p in glob.glob(os.path.join(lift, "*.c")))
    json.dump(summary, open(os.path.join(lift, "spu_lift_%s.json" % game), "w"), indent=1)
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
