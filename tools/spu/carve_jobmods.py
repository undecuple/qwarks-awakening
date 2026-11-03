#!/usr/bin/env python3
"""Cut ToD's SPU job modules and the WWS policy module out of a decrypted EBOOT.elf into files for lift_jobmods.py.

    python3 -I carve_jobmods.py ELF tods_spu_jobs.json OUT_DIR

Writes OUT_DIR/jobmods/jobmod_<sha256[:12]>_e<ENTRY>.bin for every image (the name pattern lift_jobmods.py splits on '_':
field 1 = hash, field 2 = 'e' + entry offset in %04X) and OUT_DIR/policy/pm_<ea>.bin (the policy module, size rounded to
128 as the title passes it). Module size = the size the title registers (stored bytes counted from the header), else
min(footprint at header +0x14, distance to the next header). Prints a table of sizes; never write OUT_DIR inside git.
Run only inside tools/sandbox.sh (OUT_DIR under work/).
"""
import hashlib, json, os, struct, sys


def main():
    elf, js, out = sys.argv[1:4]
    d = open(elf, "rb").read()
    j = json.load(open(js))
    os.makedirs(os.path.join(out, "jobmods"), exist_ok=True)
    os.makedirs(os.path.join(out, "policy"), exist_ok=True)
    offs = [int(i["file_offset_of_header"], 16) for i in j["images"]]
    rows = []
    for k, im in enumerate(j["images"]):
        b = offs[k]
        foot = struct.unpack(">I", d[b + 0x14:b + 0x18])[0]
        ent = struct.unpack(">I", d[b + 0x10:b + 0x14])[0]
        gap = (offs[k + 1] - b) if k + 1 < len(offs) else None
        reg = im.get("size_argument_at_registration")
        if reg:
            size, how = reg, "registered"
        else:
            size, how = min(foot, gap) if gap else foot, "min(footprint, gap)"
        raw = d[b:b + size]
        name = "jobmod_%s_e%04X.bin" % (hashlib.sha256(raw).hexdigest()[:12], ent)
        open(os.path.join(out, "jobmods", name), "wb").write(raw)
        rows.append((k, name, size, how, foot, gap))
    pm_off = int(j["workload_creation_wrapper"]["policy_module"]["file_offset"], 16)
    pm_sz = (int(j["workload_creation_wrapper"]["policy_module"]["size"], 16) + 0x7F) & ~0x7F
    open(os.path.join(out, "policy", "pm_%s.bin" % j["workload_creation_wrapper"]["policy_module"]["ea"]), "wb").write(d[pm_off:pm_off + pm_sz])
    for r in rows:
        print("%2d %s size=%d (%s) footprint=%#x gap=%s" % r)
    print("policy module bytes:", pm_sz)


if __name__ == "__main__":
    main()
