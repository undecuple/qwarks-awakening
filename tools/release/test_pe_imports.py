#!/usr/bin/env python3
"""Host test for tools/release/pe_imports.py, and the guard that the Windows programs we ship import only DLLs every
supported Windows (10 22H2, 11) ships (plus SDL3.dll, which ships beside them).

    python3 tools/release/test_pe_imports.py [EXE_OR_DLL ...]

From the Killzone 3 port's test_pe_imports.py (same author, MIT; its exe once imported XINPUT1_3.dll and did not start
on a fresh Windows 10 22H2). A self-test on synthetic PE32+ images always runs: the parser, the allow list, the
toolchain-DLL traps (never allowed, even with --allow) and the CLI. Without arguments it also checks the built
launcher (build/launcher-release/launcher.exe, SDL3.dll allowed) and the staged game exe (work/stage/tods/tods.exe, system
DLLs only) when they exist,
SKIP otherwise (public CI builds neither); a file named as an argument must exist. Exit 1 on any failure.
"""
import io
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pe_imports as pi  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))


def _synthetic_pe(dlls):
    """A minimal PE32+ image with one .idata section importing DLLS (names only; enough for pe_imports)."""
    sect_va, sect_raw = 0x1000, 0x200
    names, desc = b"", b""
    name_base = 20 * (len(dlls) + 1)
    for d in dlls:
        desc += struct.pack("<IIIII", 0, 0, 0, sect_va + name_base + len(names), 0)
        names += d.encode() + b"\0"
    body = desc + b"\0" * 20 + names
    pe = 0x40
    hdr = bytearray(sect_raw)
    hdr[0:2] = b"MZ"
    struct.pack_into("<I", hdr, 0x3C, pe)
    hdr[pe:pe + 4] = b"PE\0\0"
    optsize = 112 + 16 * 8
    struct.pack_into("<HHIIIHH", hdr, pe + 4, 0x8664, 1, 0, 0, 0, optsize, 0x22)
    opt = pe + 24
    struct.pack_into("<H", hdr, opt, 0x20B)
    struct.pack_into("<Q", hdr, opt + 24, 0x140000000)
    struct.pack_into("<I", hdr, opt + 108, 16)
    struct.pack_into("<II", hdr, opt + 112 + 8, sect_va, len(body))
    s = opt + optsize
    hdr[s:s + 8] = b".idata\0\0"
    struct.pack_into("<IIII", hdr, s + 8, len(body), sect_va, len(body), sect_raw)
    return bytes(hdr) + body


def cli(*args):
    buf = io.StringIO()
    return pi.main(list(args), out=buf), buf.getvalue()


def self_test():
    img = _synthetic_pe(["KERNEL32.dll", "XINPUT1_3.dll", "api-ms-win-core-synch-l1-2-0.dll"])
    normal, delay = pi.pe_imports(img)
    assert normal == ["KERNEL32.dll", "XINPUT1_3.dll", "api-ms-win-core-synch-l1-2-0.dll"], normal
    assert delay == [], delay
    bad = pi.bad_imports(normal)
    assert len(bad) == 1 and bad[0].startswith("XINPUT1_3.dll: DirectX"), bad
    assert pi.bad_imports(["d3d12.dll", "D3DCOMPILER_47.dll", "msvcrt.dll"]) == []
    assert len(pi.bad_imports(["d3dx9_43.dll", "MSVCP140.dll"])) == 2
    assert pi.bad_imports(["SDL3.dll"], extra=["SDL3.dll"]) == []
    for trap in ("libc++.dll", "libunwind.dll", "libwinpthread-1.dll", "libstdc++-6.dll"):
        assert len(pi.bad_imports([trap], extra=[trap])) == 1, trap        # a toolchain DLL is never allowed
    with tempfile.TemporaryDirectory() as d:
        good = os.path.join(d, "good.exe")
        with open(good, "wb") as f:
            f.write(_synthetic_pe(["KERNEL32.dll", "SDL3.dll", "api-ms-win-crt-runtime-l1-1-0.dll"]))
        cxx = os.path.join(d, "cxx.exe")
        with open(cxx, "wb") as f:
            f.write(_synthetic_pe(["KERNEL32.dll", "SDL3.dll", "libc++.dll"]))
        junk = os.path.join(d, "junk.exe")
        with open(junk, "wb") as f:
            f.write(b"not a PE")
        rc, out = cli("--allow", "SDL3.dll", good)
        assert rc == 0 and out.startswith("ok"), out
        rc, out = cli(good)
        assert rc == 1 and "SDL3.dll" in out, out
        rc, out = cli("--allow", "SDL3.dll", "--allow", "libc++.dll", cxx)
        assert rc == 1 and "libc++.dll: llvm-mingw runtime DLL" in out, out
        rc, out = cli(junk)
        assert rc == 1 and "no MZ header" in out, out
        rc, out = cli("--allow", "SDL3.dll", os.path.join(d, "missing.exe"))
        assert rc == 1, out
    print("ok   self-test (synthetic PE32+: parser, allow list, toolchain-DLL traps, CLI)")


def main(argv):
    self_test()
    defaults = [(os.path.join(ROOT, "build", "launcher-release", "launcher.exe"), ["SDL3.dll"]),
                (os.path.join(ROOT, "work", "stage", "tods", "tods.exe"), [])]   # the game loads SDL3 itself
    targets = [(p, []) for p in argv] or defaults        # named files: system DLLs only (pe_imports.py --allow for more)
    fails = 0
    for p, extra in targets:
        if not os.path.isfile(p):
            if argv:
                fails += 1
                print(f"FAIL {p}: no such file")
            else:
                print(f"SKIP {os.path.relpath(p, ROOT)}: not built")
            continue
        fails += pi.check([p], extra)
    print("test_pe_imports:", "FAIL" if fails else "PASS")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
