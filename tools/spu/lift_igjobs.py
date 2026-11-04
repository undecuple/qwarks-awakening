#!/usr/bin/env python3
"""Lift Insomniac's igPm job kernel and its job blobs, and write their registration TU (igjobs_register.c).

    nix develop .#re --command tools/sandbox.sh -- python3 -I tools/spu/lift_igjobs.py \\
        ELF PS3RECOMP_DIR OUT_DIR TABLE
    (ELF = work/bin/acit120/EBOOT.elf, PS3RECOMP_DIR = work/ps3recomp-patched, OUT_DIR = work/spu/acit120/lift,
     TABLE = data/titles/acit120_igjobs.json)

A Crack in Time, Nexus, A4O and FFA run a SPURS workload "igPm" whose 0x230-byte policy module (a loader; it stays
interpreted, patch 7034) GETs Insomniac's job kernel from the EBOOT to LS 0x3F580 and calls it; the kernel then GETs each
job's code from the job header's code EA to the header's LS destination and calls its entry (the RE notes sections 30-31).
Both come from fixed EBOOT addresses, so they are lifted at their load addresses and registered as verified streamed regions
(spu_overlay_register_verified_region): the GET from the registered EA to the registered LS address makes the span resident,
the first dispatch compares the code bytes, and the interpreted policy module hands over to the lifted code (patch 7039).
A job blob starts with four `ila $r2` words (a signature), then the job header's quad 0 at +0x10: w0 hi16 << 2 = LS
destination, w1 hi16 << 2 = DMA size, w1 lo16 << 2 = bss address, w2 hi16 << 2 = bss size, w2 lo16 << 2 = entry (the
kernel's decode at LS 0x3F838..0x3F860). Blobs whose header holds other words (a layout the PPU relocates) need "lsa",
"size" and "entry" in the table, from a run with RC_SPU_GETLOG (patch 7038).

TABLE: {"kernel": {"ea", "lsa", "get_size", "entry", "code_end", "span", "image_id", "data_ranges"?},
        "jobs": [{"ea", "image_id", "lsa"?, "size"?, "entry"?, "code_end"?, "data_ranges"?, "extra_funcs"?}]}
Writes (all derived from game code, never committed): OUT_DIR/<name>/spu_recomp.[ch], OUT_DIR/<name>.log,
OUT_DIR/igjobs_register.c (the lifted code bytes as verification segments, image registration, constructor),
OUT_DIR/igjobs_manifest.json. Never deletes anything else in OUT_DIR, so it runs after tools/spu/spu_lift.py.
"""
import importlib.util, json, os, struct, subprocess, sys


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def segments(d):
    phoff, = struct.unpack(">Q", d[0x20:0x28])
    phentsize, phnum = struct.unpack(">HH", d[0x36:0x3A])
    out = []
    for i in range(phnum):
        p = d[phoff + i * phentsize: phoff + (i + 1) * phentsize]
        if struct.unpack(">I", p[0:4])[0] == 1:
            off, va, _pa, filesz = struct.unpack(">QQQQ", p[8:40])
            out.append((va, off, filesz))
    return out


def ea_bytes(d, segs, ea, n):
    for va, off, fsz in segs:
        if va <= ea and ea + n <= va + fsz:
            return d[off + ea - va: off + ea - va + n]
    raise SystemExit("EA 0x%x+0x%x is not inside a PT_LOAD's file bytes" % (ea, n))


def h(v):
    return int(v, 16) if isinstance(v, str) else int(v)


def ila_r2(w):
    return (w >> 25) == 0x21 and (w & 0x7F) == 2


def job_geometry(d, segs, j):
    """(lsa, size, entry) of a job blob: from the table when given, else from the header quad at +0x10."""
    ea = h(j["ea"])
    hdr = ea_bytes(d, segs, ea, 0x20)
    if not all(ila_r2(w) for w in struct.unpack(">4I", hdr[:16])):
        raise SystemExit("job 0x%x: no `ila $r2` x4 signature" % ea)
    w0, w1, w2 = struct.unpack(">3I", hdr[16:28])
    lsa = h(j["lsa"]) if "lsa" in j else (w0 >> 16) << 2
    size = h(j["size"]) if "size" in j else (w1 >> 16) << 2
    entry = h(j["entry"]) if "entry" in j else (w2 & 0xFFFF) << 2
    if not (size >= 0x20 and lsa + size <= 0x3F580 and lsa < entry < lsa + size):
        raise SystemExit("job 0x%x: header does not describe a job (lsa 0x%x size 0x%x entry 0x%x); give lsa/size/entry"
                         % (ea, lsa, size, entry))
    return lsa, size, entry


def lift(tools, wrap, bsw, out, name, img, base, entry, code_end, data_ranges, extra):
    epath = os.path.join(out, "raw", name + ".elf")
    open(epath, "wb").write(wrap(img, base=base, entry=entry))
    cmd = [sys.executable, os.path.join(tools, "spu_lifter.py"), "--auto-functions", epath, "--base", hex(base),
           "--symbol-prefix", name + "_", "-o", os.path.join(out, name), "--report", os.path.join(out, name + ".report.json"),
           "--code-end", hex(code_end)]
    for r in data_ranges:
        cmd += ["--data-range", r]
    if extra:
        cmd += ["--extra-funcs", ",".join(extra)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    open(os.path.join(out, name + ".log"), "w").write(r.stdout + r.stderr)
    if r.returncode:
        raise SystemExit("lift of %s FAILED:\n%s" % (name, r.stderr[-800:]))
    for f in ("spu_recomp.c", "spu_recomp.h"):
        bsw.debase_includes(os.path.join(out, name, f))
    src = open(os.path.join(out, name, "spu_recomp.c")).read()
    if "%sspu_func_%08X(" % (name + "_", entry) not in src:
        raise SystemExit("%s: the lift has no function at the entry 0x%x" % (name, entry))
    return src.count("\nSPU_ABI void %sspu_func_" % (name + "_"))


def main():
    elf, ps3, out, table_path = sys.argv[1:5]
    table = json.load(open(table_path))
    tools = os.path.join(ps3, "tools")
    sys.path.insert(0, tools)
    bsw = load(os.path.join(tools, "build_spu_workloads.py"), "bsw")
    from wrap_spu_elf import wrap
    d = open(elf, "rb").read()
    segs = segments(d)
    os.makedirs(os.path.join(out, "raw"), exist_ok=True)
    regs, man = [], {"title": table.get("title", ""), "regions": []}
    k = table["kernel"]
    kea, klsa, kget, kentry, kend, kspan, kid = (h(k[x]) for x in ("ea", "lsa", "get_size", "entry", "code_end", "span", "image_id"))
    kimg = ea_bytes(d, segs, kea, kget)
    name = "igkernel_%08x" % kea
    nf = lift(tools, wrap, bsw, out, name, kimg, klsa, kentry, kend, k.get("data_ranges", []), k.get("extra_funcs", []))
    regs.append((name, kid, kea, klsa, kspan, klsa, kimg[:kend - klsa]))
    man["regions"].append(dict(name=name, kind="kernel", ea=hex(kea), lsa=hex(klsa), span=hex(kspan), entry=hex(kentry),
                               verify=[hex(klsa), hex(kend - klsa)], image_id=kid, functions=nf))
    for j in table.get("jobs", []):
        ea, iid = h(j["ea"]), int(j["image_id"])
        lsa, size, entry = job_geometry(d, segs, j)
        img = ea_bytes(d, segs, ea, size)
        cend = h(j["code_end"]) if "code_end" in j else lsa + size
        name = "igjob_%08x" % ea
        nf = lift(tools, wrap, bsw, out, name, img, lsa, entry, cend, j.get("data_ranges", ["0x%x-0x%x" % (lsa, lsa + 0x20)]),
                  j.get("extra_funcs", []))
        # verify the code part only (the job may keep data after code_end that the PPU or the job itself rewrites)
        regs.append((name, iid, ea, lsa, size, lsa, img[:cend - lsa]))
        man["regions"].append(dict(name=name, kind="job", ea=hex(ea), lsa=hex(lsa), span=hex(size), entry=hex(entry),
                                   verify=[hex(lsa), hex(cend - lsa)], image_id=iid, functions=nf))
    L = ["/* igjobs_register.c -- GENERATED by tools/spu/lift_igjobs.py. Do not edit; derived from game code, never committed.",
         " * Insomniac's igPm job kernel and job blobs of \"%s\" as verified streamed regions (patch 7039): a GET from the" % man["title"],
         " * registered EA to the registered LS address makes the span resident; the first dispatch compares these bytes. */",
         "#include <stdint.h>", "#include <stdlib.h>", "#include \"spu_overlay_residency.h\"", "", "extern void spu_begin_image(int image_id);"]
    for name, iid, ea, lsa, span, vlsa, vb in regs:
        L.append("extern void %sspu_recomp_register(void);" % (name + "_"))
        L.append("static const uint8_t k_%s[%d] = {" % (name, len(vb)))
        for o in range(0, len(vb), 24):
            L.append("    " + ",".join("0x%02x" % x for x in vb[o:o + 24]) + ",")
        L.append("};")
    L += ["", "void rc_igjobs_register_all(void)", "{"]
    for name, iid, ea, lsa, span, vlsa, vb in regs:
        L.append("    spu_begin_image(%d); %sspu_recomp_register();" % (iid, name + "_"))
        L.append("    { const spu_overlay_code_segment s = { 0x%05X, %d, k_%s };" % (vlsa, len(vb), name))
        L.append("      spu_overlay_register_verified_region(0x%08X, 0x%05X, 0x%X, %d, &s, 1); }" % (ea, lsa, span, iid))
    L += ["    spu_begin_image(0);", "}", "",
          "/* RC_IGJOBS=0: register nothing (the kernel and every job run interpreted, as before; an A/B switch on one exe). */",
          "__attribute__((constructor)) static void rc_igjobs_register_ctor(void)", "{",
          "    const char* e = getenv(\"RC_IGJOBS\");", "    if (e && *e == '0') return;",
          "    rc_igjobs_register_all();", "}", ""]
    open(os.path.join(out, "igjobs_register.c"), "w").write("\n".join(L))
    json.dump(man, open(os.path.join(out, "igjobs_manifest.json"), "w"), indent=1)
    print(json.dumps(man, indent=1))


if __name__ == "__main__":
    main()
