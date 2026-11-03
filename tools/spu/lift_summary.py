#!/usr/bin/env python3
"""Per-image report of a title's SPU lift: functions lifted, unsupported words, indirect branch sites, source lines. Output under work/ only.

    python3 -I tools/spu/lift_summary.py LIFT_DIR OUT.json       (LIFT_DIR = work/spu/<game>/lift, OUT = work/spu/<game>/lift_summary.json)

Counts every <LIFT_DIR>/*/spu_recomp.c: `SPU_ABI void <prefix>spu_func_XXXXXXXX(` definitions (functions), `spu_unsupported(0x` calls (words the
lifter could not decode: a failed instruction, with its address in the first, last and list below), `spu_indirect_branch(` sites. Also lists
the registration TUs and the raw SPU programs of rawspu_manifest.json. Numbers only; the lifted code itself is never copied.
"""
import glob, json, os, re, sys


def main():
    lift, out = sys.argv[1:3]
    rep = {"images": {}, "totals": {}, "registration_tus": sorted(os.path.basename(p) for p in glob.glob(os.path.join(lift, "*.c")))}
    tf = tu = 0
    for c in sorted(glob.glob(os.path.join(lift, "*", "spu_recomp.c"))):
        name = os.path.basename(os.path.dirname(c))
        s = open(c).read()
        funcs = len(re.findall(r"^SPU_ABI void \w+_spu_func_[0-9A-F]{8}\(", s, flags=re.M))
        uns = re.findall(r"spu_unsupported\(0x([0-9A-Fa-f]+)", s)
        rep["images"][name] = {"functions": funcs, "unsupported_words": len(uns), "unsupported_addresses": ["0x" + a for a in uns[:8]],
                               "indirect_branch_sites": s.count("spu_indirect_branch("), "c_lines": s.count("\n")}
        tf += funcs
        tu += len(uns)
    rep["totals"] = {"images": len(rep["images"]), "functions": tf, "unsupported_words": tu}
    man = os.path.join(lift, "rawspu_manifest.json")
    rep["raw_spu_programs"] = len(json.load(open(man))["programs"]) if os.path.exists(man) else 0
    json.dump(rep, open(out, "w"), indent=1)
    print(json.dumps(rep["totals"]), "raw programs:", rep["raw_spu_programs"])


if __name__ == "__main__":
    main()
