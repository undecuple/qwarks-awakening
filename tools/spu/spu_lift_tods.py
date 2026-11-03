#!/usr/bin/env python3
"""Produce ToD's lifted SPU images and registration TUs in the layout port/rc/CMakeLists.txt expects. Output under work/ only.

    nix develop .#re --command tools/sandbox.sh -- python3 -I tools/spu/spu_lift_tods.py \\
        ELF PS3RECOMP_DIR OUT_DIR       (ELF = work/bin/tods/EBOOT.elf, PS3RECOMP_DIR = work/ps3recomp-patched, OUT_DIR = work/spu/tods)

Writes (all derived from game code, never committed):
  OUT_DIR/jobmods/                  the 21 carved job modules (carve_jobmods.py) and policy/pm_*.bin
  OUT_DIR/lift/jobmod_*/            21 job modules lifted with the code/data table data/titles/tods_spu_lift_table.tsv (lift_jobmods.py --table)
  OUT_DIR/lift/elf_0008ea200/       the EDGE zlib inflate ELF (spu_lifter --auto-functions, --code-end at .rodata)
  OUT_DIR/lift/pm_10053d00/         the WWS policy module (wrapped at base 0xA00, seed 0xA2C)
  OUT_DIR/lift/jobmods_register.c   lift_jobmods.py's own generator (image ids 200..220, 16-byte header signatures)
  OUT_DIR/lift/spu_workloads.c      build_spu_workloads.write_registry (ps3recomp's generator) for the EDGE ELF (image 30) and the policy
                                    module (image 2, fingerprint over the 0x2b80 bytes the title passes, as cellSpurs.c spurs_resolve_pm computes)
  OUT_DIR/lift/spu_lift_tods.json   what was done
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
    elf, ps3, out = sys.argv[1:4]
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", ".."))
    tools = os.path.join(ps3, "tools")
    sys.path.insert(0, tools)
    bsw = load(os.path.join(tools, "build_spu_workloads.py"), "bsw")
    from wrap_spu_elf import wrap
    jobs_json = os.path.join(repo, "data/titles/tods_spu_jobs.json")
    lift = os.path.join(out, "lift")
    shutil.rmtree(lift, ignore_errors=True)
    os.makedirs(lift)
    d = open(elf, "rb").read()
    summary = {}
    # 1. carve
    run([sys.executable, os.path.join(here, "carve_jobmods.py"), elf, jobs_json, out])
    # 2. job modules with the table (patch 6001 gives lift_jobmods.py --table: code ends, data ranges, seed skip)
    run([sys.executable, os.path.join(tools, "lift_jobmods.py"), "--jobmods", os.path.join(out, "jobmods"), "--out", lift, "--symbol-prefix", "tods",
         "--table", os.path.join(repo, "data/titles/tods_spu_lift_table.tsv")], os.path.join(lift, "jobmods.log"))
    # 3. EDGE ELF
    code = json.load(open(os.path.join(repo, "data/titles/tods_spu_code.json")))["edge_zlib_inflate_elf"]
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
    ename = "elf_%08x" % eoff
    edir = os.path.join(lift, ename)
    cmd = [sys.executable, os.path.join(tools, "spu_lifter.py"), epath, "--auto-functions", epath, "--symbol-prefix", ename + "_", "-o", edir]
    if ce:
        cmd += ["--code-end", hex(ce)]
    run(cmd, os.path.join(lift, ename + ".log"))
    summary["edge"] = {"file_offset": hex(eoff), "size": esz, "code_end": hex(ce), "entry": hex(bsw.be32(e, 0x18))}
    # 4. policy module
    pmfile = glob.glob(os.path.join(out, "policy", "pm_*.bin"))[0]
    pm = open(pmfile, "rb").read()
    pname = "pm_10053d00"
    ppath = os.path.join(out, "images", pname + ".elf")
    open(ppath, "wb").write(wrap(pm, base=0xA00, entry=0xA00))
    pdir = os.path.join(lift, pname)
    run([sys.executable, os.path.join(tools, "spu_lifter.py"), "--auto-functions", ppath, "--base", "0xa00", "--symbol-prefix", pname + "_",
         "--extra-funcs", "0xa2c", "-o", pdir], os.path.join(lift, pname + ".log"))
    summary["policy_module"] = {"size": len(pm), "fingerprint": "%016X" % bsw.fnv1a64(pm), "image_id": 2}
    # 5. include rewrite for every lifted tree
    for h in glob.glob(os.path.join(lift, "*", "spu_recomp.[ch]")):
        bsw.debase_includes(h)
    # 6. registration: ps3recomp's write_registry for the two non-jobmod images
    imgs = [(ename, ename + "_", bsw.fnv1a64(e[:bsw.img_size(e)]), bsw.be32(e, 0x18)),
            (pname, pname + "_", bsw.fnv1a64(pm), 0xA00)]
    text_fps = {ename: bsw.text_fp(e), pname: None}
    ids = {ename: 30, pname: 2}
    class A: pass
    a = A()
    a.out, a.title, a.register_fn, a.constructor = os.path.join(lift, "spu_workloads.c"), "Tools of Destruction", "rc_spu_register_all", True
    a.wws_code_slots = {pname: 0x1330}   # patch 6501: ToD's policy module keeps the job code-buffer address at LS 0x1330 (@LS 0x3024); the runtime default is 0x1320
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
    json.dump(summary, open(os.path.join(lift, "spu_lift_tods.json"), "w"), indent=1)
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
