#!/usr/bin/env python3
"""Lift a title's raw SPU programs (streamed into local store by proxy DMA) and write their registration TU.

    nix develop .#re --command tools/sandbox.sh -- python3 -I tools/spu/lift_raw_spu.py \\
        ELF PS3RECOMP_DIR OUT_DIR [TABLE]
    (ELF = work/bin/tods/EBOOT.elf, PS3RECOMP_DIR = work/ps3recomp-patched, OUT_DIR = work/spu/tods/lift,
     TABLE = tools/spu/tods_raw_spu.json)

ToD runs two programs on raw SPUs (sys_raw_spu_create), not through SPURS: Insomniac's collision programs
igcollspu.elf and igCollBroadSpu.elf, embedded as flat local-store images in the EBOOT's data segment. Each image
starts with a 0x30-byte block whose words +0x10.. are read by the PPU loader:
    +0x10 entry LS address (NPC)      +0x14 image end in LS      +0x18 bss end
    +0x1C LS address of image+0x30    +0x20 parameter block offset - 0x30 (-1: none)   +0x24 parameter block size
The loader GETs the image to LS (+0x1C) - 0x30 and, before that, the caller writes a parameter block INTO THE IMAGE in
main memory. So the bytes the runtime fingerprints (patch 6502, spu_raw.c raw_spu_start) are the image with that block
filled in. A program with a parameter block is registered under the fingerprint of the LS window that ends where the
block starts (spu_raw_add_fp_window); one without is registered under the fingerprint of the whole image.

Writes (all derived from game code, never committed):
  OUT_DIR/<name>/spu_recomp.[ch]   the lifted program (spu_lifter --auto-functions on a wrapped ELF at the load base)
  OUT_DIR/<name>.log               the lifter's output
  OUT_DIR/raw/<name>.bin           the image bytes (input of tools/spu/rawspu_fp_check.c)
  OUT_DIR/rawspu_register.c        registration TU: spu_workload_register_img + spu_raw_add_fp_window, constructor
  OUT_DIR/rawspu_manifest.json     what was done
Never deletes anything else in OUT_DIR, so it runs after tools/spu/spu_lift_tods.py (which recreates OUT_DIR).
"""
import importlib.util, json, os, struct, subprocess, sys


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def segments(d):
    """PT_LOAD segments of a big-endian ELF64: (vaddr, offset, filesz)."""
    assert d[:4] == b"\x7fELF" and d[4] == 2 and d[5] == 2, "not a big-endian ELF64"
    phoff, = struct.unpack(">Q", d[0x20:0x28])
    phentsize, phnum = struct.unpack(">HH", d[0x36:0x3A])
    out = []
    for i in range(phnum):
        p = d[phoff + i * phentsize: phoff + (i + 1) * phentsize]
        typ, = struct.unpack(">I", p[0:4])
        off, va, _pa, filesz = struct.unpack(">QQQQ", p[8:40])
        if typ == 1:
            out.append((va, off, filesz))
    return out


def ea_bytes(d, segs, ea, n):
    for va, off, fsz in segs:
        if va <= ea and ea + n <= va + fsz:
            return d[off + ea - va: off + ea - va + n]
    raise SystemExit("EA 0x%x+0x%x is not inside a PT_LOAD's file bytes" % (ea, n))


def cstr(d, segs, ea):
    for va, off, fsz in segs:
        if va <= ea < va + fsz:
            s = d[off + ea - va: off + fsz]
            return s[:s.index(b"\0")].decode("ascii", "replace")
    return None


def be32(b, o):
    return struct.unpack(">I", b[o:o + 4])[0]


def program(d, segs, p):
    ea, size = int(p["ea"], 16), int(p["size"], 16)
    img = ea_bytes(d, segs, ea, size)
    entry, end, bss, hdr_ls, poff, psize = (be32(img, o) for o in range(0x10, 0x28, 4))
    # @0x005AB460: LSA = desc[+0x1C] - 0x30, or - 0x20 when desc[+4] == 0xDEADDEAD (not the case for ToD's two)
    base = (hdr_ls - (0x20 if be32(img, 4) == 0xDEADDEAD else 0x30)) & 0xFFFFFFFF
    if not (base < entry < base + size and base + size <= end <= bss <= 0x40000):
        raise SystemExit("%s: header does not describe a local-store image (base 0x%x entry 0x%x end 0x%x bss 0x%x)"
                         % (p["name"], base, entry, end, bss))
    win = None
    if poff != 0xFFFFFFFF and psize:
        pls = (hdr_ls + poff) & 0xFFFFFFFF                 # @0x005AAF08: LS address of the parameter block
        if base < pls < base + size:
            win = (base, pls - base)
    name = cstr(d, segs, int(p["elf_name_ea"], 16)) if p.get("elf_name_ea") else None
    if name is not None and os.path.splitext(name.replace("\\", "/").split("/")[-1])[0].lower() != p["name"]:
        raise SystemExit("%s: the name string at %s is %r" % (p["name"], p["elf_name_ea"], name))
    return img, dict(base=base, entry=entry, image_end=end, bss_end=bss, param_ls=(hdr_ls + poff) & 0xFFFFFFFF,
                     param_size=psize, window=win, elf_name=name)


def main():
    elf, ps3, out = sys.argv[1:4]
    here = os.path.dirname(os.path.abspath(__file__))
    table = json.load(open(sys.argv[4] if len(sys.argv) > 4 else os.path.join(here, "tods_raw_spu.json")))
    if not table.get("programs"):
        # a title without raw SPU programs (ACiT, Nexus: no sys_raw_spu_create call, no program names): nothing to lift or register
        os.makedirs(out, exist_ok=True)
        json.dump({"title": table["title"], "programs": []}, open(os.path.join(out, "rawspu_manifest.json"), "w"), indent=1)
        print("no raw SPU programs in the table for %s" % table["title"])
        return
    tools = os.path.join(ps3, "tools")
    sys.path.insert(0, tools)
    bsw = load(os.path.join(tools, "build_spu_workloads.py"), "bsw")
    from wrap_spu_elf import wrap
    d = open(elf, "rb").read()
    segs = segments(d)
    os.makedirs(os.path.join(out, "raw"), exist_ok=True)
    rows, man = [], {"title": table["title"], "programs": []}
    for p in table["programs"]:
        img, h = program(d, segs, p)
        name, iid = p["name"], int(p["image_id"])
        open(os.path.join(out, "raw", name + ".bin"), "wb").write(img)
        epath = os.path.join(out, "raw", name + ".elf")
        open(epath, "wb").write(wrap(img, base=h["base"], entry=h["entry"]))
        cmd = [sys.executable, os.path.join(tools, "spu_lifter.py"), "--auto-functions", epath, "--base", hex(h["base"]),
               "--symbol-prefix", name + "_", "-o", os.path.join(out, name),
               "--report", os.path.join(out, name + ".report.json")]
        if p.get("extra_funcs"):
            cmd += ["--extra-funcs", ",".join(p["extra_funcs"])]
        for r in p.get("data_ranges", []):
            cmd += ["--data-range", r]
        if p.get("code_end"):
            cmd += ["--code-end", p["code_end"]]
        r = subprocess.run(cmd, capture_output=True, text=True)
        open(os.path.join(out, name + ".log"), "w").write(r.stdout + r.stderr)
        if r.returncode:
            raise SystemExit("lift of %s FAILED:\n%s" % (name, r.stderr[-800:]))
        for f in ("spu_recomp.c", "spu_recomp.h"):
            bsw.debase_includes(os.path.join(out, name, f))
        src = open(os.path.join(out, name, "spu_recomp.c")).read()
        efn = "%sspu_func_%08X" % (name + "_", h["entry"])
        if efn + "(" not in src:
            raise SystemExit("%s: the lift has no function at the entry 0x%x" % (name, h["entry"]))
        w = h["window"]
        fp = bsw.fnv1a64(img[:w[1]]) if w else bsw.fnv1a64(img)
        rows.append((name, iid, efn, fp, w, h, len(img)))
        man["programs"].append(dict(name=name, image_id=iid, ea=p["ea"], size=hex(len(img)), base=hex(h["base"]),
                                    entry=hex(h["entry"]), image_end=hex(h["image_end"]), bss_end=hex(h["bss_end"]),
                                    param_block=[hex(h["param_ls"]), hex(h["param_size"])] if w else None,
                                    fp_window=[hex(w[0]), hex(w[1])] if w else None,
                                    fingerprint="0x%016X" % fp, run_fingerprint_clean="0x%016X" % bsw.fnv1a64(img),
                                    elf_name=h["elf_name"], functions=src.count("\nSPU_ABI void %sspu_func_" % (name + "_"))))
    L = ["/* rawspu_register.c -- GENERATED by tools/spu/lift_raw_spu.py. Do not edit.",
         " * Registers the lifted raw SPU programs of \"%s\". A program streamed into local store by proxy" % table["title"],
         " * DMA is resolved in spu_raw.c (raw_spu_start) by the fingerprint of its GET run or, when the guest fills a",
         " * parameter block inside the image before streaming it, by the fingerprint of a declared LS window that ends",
         " * where the block starts (patch 6502, spu_raw_add_fp_window). Defining RAWSPU_TABLE_ONLY keeps only the table",
         " * (tools/spu/rawspu_fp_check.c). */",
         "#include <stdint.h>", "",
         "static const struct rawspu_entry { const char* name; int image_id; uint32_t ls_lo, ls_len, run_len;",
         "                                   uint64_t fp; int windowed; } k_rawspu[] __attribute__((unused)) = {"]
    for name, iid, efn, fp, w, h, n in rows:
        lo, ln = (w if w else (h["base"], n))
        L.append("    { \"%s\", %d, 0x%05X, 0x%05X, 0x%05X, 0x%016XULL, %d },   /* entry 0x%X */"
                 % (name, iid, lo, ln, n, fp, 1 if w else 0, h["entry"]))
    L += ["};", "#define RAWSPU_COUNT %d" % len(rows), "", "#ifndef RAWSPU_TABLE_ONLY",
          "#include \"spu_workload.h\"", "#include \"spu_raw.h\"", "", "extern void spu_begin_image(int image_id);"]
    for name, iid, efn, fp, w, h, n in rows:
        L += ["extern SPU_ABI void %s(spu_context*);" % efn, "extern void %sspu_recomp_register(void);" % (name + "_")]
    L += ["", "void rc_rawspu_register_all(void)", "{"]
    for name, iid, efn, fp, w, h, n in rows:
        L.append("    spu_begin_image(%d); %sspu_recomp_register();" % (iid, name + "_"))
        L.append("    spu_workload_register_img(0x%016XULL, %s, %d, \"%s\");" % (fp, efn, iid, name))
        if w:
            L.append("    spu_raw_add_fp_window(0x%X, 0x%X);   /* parameter block at LS 0x%X, 0x%X bytes */"
                     % (w[0], w[1], h["param_ls"], h["param_size"]))
    L += ["    spu_begin_image(0);", "}", "",
          "__attribute__((constructor)) static void rc_rawspu_register_all_ctor(void)", "{",
          "    rc_rawspu_register_all();", "}", "#endif", ""]
    open(os.path.join(out, "rawspu_register.c"), "w").write("\n".join(L))
    json.dump(man, open(os.path.join(out, "rawspu_manifest.json"), "w"), indent=1)
    print(json.dumps(man, indent=1))


if __name__ == "__main__":
    main()
