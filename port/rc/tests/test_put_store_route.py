#!/usr/bin/env python3
"""Host test for R&C patch 7116, lifter half: the ppu_recomp.h template (tools/ppu_lifter.py HEADER_PREAMBLE) and the
per-function store macros (SOURCE_PREAMBLE's PPU_MM_SNAP block) send the guest's store to the gcm put word
(ppu_hle_inject_base + 0x2000) out of line, where libgcm's hook sees it, and nothing else.

Checked by compiling the template text from the given tree with a C driver whose out-of-line vm_write*/vm_read* record
every call:
- vm_write32 (both forms: vm_write32_inl, and vm_write32 inside a PPU_MM_SNAP function): out of line only at put
  itself, over put - 16 .. put + 16 byte by byte and at far addresses;
- vm_write64: out of line exactly when [ea, ea + 8) overlaps [put, put + 4);
- vm_write8 / vm_write16 and every load except the ref-poll word: inline, unchanged;
- the inline stores land in guest memory big-endian;
- the put word follows ppu_hle_inject_base (0x7F000000, the port's default, and 0x20000000);
- with the store diagnostics armed (vm_inline_st_ok = 0) every store goes out of line, as before.
Negative control: against a tree without patch 7116 it fails (the put store stays inline).

Usage: python3 -I port/rc/tests/test_put_store_route.py [patched tree]   (needs cc on PATH: nix develop .#ci)"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
TREE = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else HERE / "../../../work/ps3recomp-patched").resolve()


def template_parts(tree):
    """HEADER_PREAMBLE and the PPU_MM_SNAP block of SOURCE_PREAMBLE, read from the lifter as text (not imported: the
    lifter is not run, only its template strings are compiled)."""
    src = (tree / "tools" / "ppu_lifter.py").read_text(encoding="utf-8")
    ns = {}
    # The two constants are plain triple-quoted strings joined with FENCE_PREAMBLE; evaluate just those assignments.
    for name in ("FENCE_PREAMBLE", "HEADER_PREAMBLE", "SOURCE_PREAMBLE"):
        m = re.search(r'^' + name + r' = (.*?^""")\n', src, re.M | re.S)
        if not m:
            raise SystemExit(f"FAIL: {name} not found in {tree}/tools/ppu_lifter.py (anchor changed; review the test)")
        exec(compile(name + " = " + m.group(1), name, "exec"), {}, ns)
    snap = re.search(r'^#define PPU_MM_SNAP .*?^#define vm_write64\(a, v\) vm_write64_mm\(&_mm, a, v\)\n#endif\n',
                     ns["SOURCE_PREAMBLE"], re.M | re.S)
    if not snap:
        raise SystemExit("FAIL: PPU_MM_SNAP store-macro block not found in SOURCE_PREAMBLE (anchor changed)")
    return ns["HEADER_PREAMBLE"], snap.group(0)


DRIVER = r"""
#include "ppu_recomp.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
uint8_t* vm_base;
int vm_inline_ok = 1, vm_inline_st_ok = 1;
uint32_t ppu_vm_size = 0;
uint32_t ppu_hle_inject_base = 0x7F000000u;
uint32_t g_barrier_sync_watch = 0, g_null_sweep_hi = 0;
const unsigned char* g_spu_coh_bits = 0;
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void ppu_st_coh(uint32_t ea, const void* src, uint32_t n) { (void)ea; (void)src; (void)n; }
/* Out of line: record. The parentheses keep the header's function-like macros off the definitions. */
static int ool_n; static uint32_t ool_ea; static int ool_w;
static void ool(uint64_t a, int w) { ool_n++; ool_ea = (uint32_t)a; ool_w = w; }
uint8_t  (vm_read8) (uint64_t a) { ool(a, -1); return 0; }
uint16_t (vm_read16)(uint64_t a) { ool(a, -2); return 0; }
uint32_t (vm_read32)(uint64_t a) { ool(a, -4); return 0; }
uint64_t (vm_read64)(uint64_t a) { ool(a, -8); return 0; }
void (vm_write8) (uint64_t a, uint8_t  v) { (void)v; ool(a, 1); }
void (vm_write16)(uint64_t a, uint16_t v) { (void)v; ool(a, 2); }
void (vm_write32)(uint64_t a, uint32_t v) { (void)v; ool(a, 4); }
void (vm_write64)(uint64_t a, uint64_t v) { (void)v; ool(a, 8); }
int ppu_stwcx32(uint64_t a, uint32_t e, uint32_t v) { (void)a; (void)e; (void)v; return 0; }
int ppu_stdcx64(uint64_t a, uint64_t e, uint64_t v) { (void)a; (void)e; (void)v; return 0; }

/* The per-function form, as a lifted function uses it. */
SNAP_BLOCK
static void lifted_st32(uint64_t a, uint32_t v) { PPU_MM_SNAP vm_write32(a, v); }
static void lifted_st64(uint64_t a, uint64_t v) { PPU_MM_SNAP vm_write64(a, v); }
static void lifted_st16(uint64_t a, uint16_t v) { PPU_MM_SNAP vm_write16(a, v); }
static void lifted_st8 (uint64_t a, uint8_t  v) { PPU_MM_SNAP vm_write8(a, v); }
static uint32_t lifted_ld32(uint64_t a) { PPU_MM_SNAP return vm_read32(a); }
#undef vm_read8
#undef vm_read16
#undef vm_read32
#undef vm_read64
#undef vm_write8
#undef vm_write16
#undef vm_write32
#undef vm_write64

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; if (fails <= 20) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static int went_out(void (*f)(void), uint32_t ea, int w)
{
    ool_n = 0; ool_ea = 0; ool_w = 0; f();
    return ool_n == 1 && ool_ea == ea && ool_w == w ? 1 : ool_n == 0 ? 0 : -1;
}
static uint64_t cur_a; static uint64_t cur_v;
static void s32i(void) { vm_write32_inl(cur_a, (uint32_t)cur_v); }
static void s32m(void) { lifted_st32(cur_a, (uint32_t)cur_v); }
static void s64i(void) { vm_write64_inl(cur_a, cur_v); }
static void s64m(void) { lifted_st64(cur_a, cur_v); }
static void s16m(void) { lifted_st16(cur_a, (uint16_t)cur_v); }
static void s8m (void) { lifted_st8 (cur_a, (uint8_t)cur_v); }
static void l32m(void) { (void)lifted_ld32(cur_a); }
static void l32i(void) { (void)vm_read32_inl(cur_a); }

static void sweep(uint32_t base)
{
    ppu_hle_inject_base = base;
    const uint32_t put = base + 0x2000u, poll = base + 0x2008u;
    for (int d = -16; d <= 16; d++) {
        const uint32_t ea = put + (uint32_t)d;
        cur_a = ea; cur_v = 0x11223344u;
        int r = went_out(s32i, ea, 4), m = went_out(s32m, ea, 4);
        CHECK(r == (d == 0) && m == (d == 0), "base %08X: 32-bit store at put%+d: inl=%d mm=%d, want %d", base, d, r, m, d == 0);
        const int ov = d >= -7 && d <= 3;
        cur_v = 0x0102030405060708ull;
        r = went_out(s64i, ea, 8); m = went_out(s64m, ea, 8);
        CHECK(r == ov && m == ov, "base %08X: 64-bit store at put%+d: inl=%d mm=%d, want %d", base, d, r, m, ov);
        r = went_out(s16m, ea, 2); m = went_out(s8m, ea, 1);
        CHECK(r == 0 && m == 0, "base %08X: 16/8-bit store at put%+d went out of line (%d/%d)", base, d, r, m);
        r = went_out(l32m, ea, -4); m = went_out(l32i, ea, -4);
        CHECK(r == (ea == poll) && m == (ea == poll), "base %08X: 32-bit load at put%+d: mm=%d inl=%d", base, d, r, m);
    }
    /* Far addresses: all inline. */
    const uint32_t far[] = { 0x10000u, 0x00100000u, 0x10000000u, base, base + 0x1000u, base + 0x3000u, put + 0x100000u };
    for (unsigned i = 0; i < sizeof far / sizeof far[0]; i++) {
        cur_a = far[i]; cur_v = 0xA1B2C3D4u;
        CHECK(went_out(s32m, far[i], 4) == 0 && went_out(s32i, far[i], 4) == 0, "32-bit store at %08X went out of line", far[i]);
        CHECK(vm_base[far[i]] == 0xA1 && vm_base[far[i] + 3] == 0xD4, "inline 32-bit store at %08X not big-endian in memory", far[i]);
        CHECK(went_out(s64m, far[i], 8) == 0, "64-bit store at %08X went out of line", far[i]);
    }
}

int main(void)
{
    vm_base = (uint8_t*)mmap(0, 0x80000000ull, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (vm_base == MAP_FAILED) { puts("FAIL: mmap"); return 1; }
    sweep(0x7F000000u);
    sweep(0x20000000u);
    /* Store diagnostics armed: everything out of line, as before the patch. */
    vm_inline_st_ok = 0; ppu_hle_inject_base = 0x7F000000u;
    cur_a = 0x00100000u; cur_v = 1;
    CHECK(went_out(s32m, 0x00100000u, 4) == 1 && went_out(s64m, 0x00100000u, 8) == 1, "vm_inline_st_ok=0: store stayed inline");
    printf("[put_store_route] %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
"""


def main():
    cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    if not cc:
        print("SKIP: no C compiler on PATH (run inside nix develop .#ci)")
        return 0
    header, snap = template_parts(TREE)
    with tempfile.TemporaryDirectory(prefix="rc-putroute-") as tmp:
        t = pathlib.Path(tmp)
        (t / "ppu_recomp.h").write_text(header, encoding="utf-8")
        (t / "drv.c").write_text(DRIVER.replace("SNAP_BLOCK", snap), encoding="utf-8")
        r = subprocess.run([cc, "-std=gnu17", "-O2", "-Wall", "-Wno-unused-function", "-I", tmp, str(t / "drv.c"),
                            "-o", str(t / "drv")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if r.returncode:
            print(r.stdout)
            print("FAIL: driver did not compile")
            return 1
        r = subprocess.run([str(t / "drv")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode:
            print(f"[test_put_store_route] FAIL ({TREE})")
            return 1
    print("[test_put_store_route] PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
