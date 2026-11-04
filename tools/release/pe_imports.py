#!/usr/bin/env python3
"""Check that a Windows exe or DLL imports only DLLs every supported Windows (10 22H2, 11) ships, plus the ones named
with --allow (shipped beside it, e.g. SDL3.dll). Standard library only; the PE import table (normal and delay-load) is
read here, so no objdump is needed.

    pe_imports.py [--allow DLL]... EXE_OR_DLL...

The release workflow runs it on the built launcher (`--allow SDL3.dll`): a toolchain runtime DLL (libc++.dll,
libunwind.dll, libwinpthread-1.dll, libstdc++-6.dll, libgcc_s_seh-1.dll) is never allowed, even with --allow, because
the zip does not carry them. From the Killzone 3 port's test_pe_imports.py (same author, MIT): on 2026-10-22 its exe
imported XINPUT1_3.dll, which only the DirectX June 2010 redistributable installs, and did not start on a fresh
Windows 10 22H2 (0xC0000135). Exit 0 = every file ok, 1 = a bad import or an unreadable file, 2 = usage.
"""
import argparse
import os
import struct
import sys

# DLLs present in System32 of every stock Windows 10 22H2 and Windows 11 install, with why we accept each.
ALLOWED = {
    "kernel32.dll": "core Win32 (every Windows)",
    "user32.dll": "windows, messages, input (every Windows)",
    "gdi32.dll": "GDI text and bitmaps for the port's notices (every Windows)",
    "advapi32.dll": "registry, security (every Windows)",
    "shell32.dll": "shell folders (every Windows)",
    "shlwapi.dll": "path helpers (every Windows)",
    "ole32.dll": "COM, WASAPI audio (every Windows)",
    "oleaut32.dll": "COM automation types (every Windows; SDL3 uses it)",
    "ws2_32.dll": "Winsock (every Windows)",
    "bcrypt.dll": "BCryptGenRandom (CNG, Windows Vista and later)",
    "winmm.dll": "timeBeginPeriod, timers (every Windows)",
    "ntdll.dll": "native API (every Windows)",
    "msvcrt.dll": "the system C runtime MinGW-w64 targets (every Windows; not the VC++ redistributable)",
    "psapi.dll": "process memory info (every Windows)",
    "dbghelp.dll": "stack traces in the crash handler (System32 since Windows XP)",
    "version.dll": "file version info (every Windows; SDL3)",
    "setupapi.dll": "device enumeration (every Windows; SDL3 HIDAPI)",
    "cfgmgr32.dll": "device configuration (every Windows; SDL3)",
    "hid.dll": "HID devices (every Windows; SDL3 HIDAPI)",
    "imm32.dll": "input method editor (every Windows; SDL3)",
    "comctl32.dll": "common controls (every Windows; launcher)",
    "comdlg32.dll": "file dialogs (every Windows; launcher)",
    "uxtheme.dll": "themes (every Windows; launcher)",
    "dwmapi.dll": "desktop window manager (Windows Vista and later)",
    "d3d12.dll": "Direct3D 12 (Windows 10 and later)",
    "dxgi.dll": "DXGI (Windows 7 and later)",
    "d3dcompiler_47.dll": "HLSL compiler, in System32 since Windows 8.1 (not the DirectX redistributable's _43)",
}
# API-set contracts: resolved by the loader on Windows 8 and later. Only these two families, which Windows 10 has.
ALLOWED_PREFIXES = ("api-ms-win-core-", "api-ms-win-crt-")

# Known traps, named in the failure message: they need a redistributable or are toolchain runtime DLLs.
TRAPS = {
    "xinput1_3.dll": "DirectX June 2010 redistributable only: load XInput at run time (patch 4019)",
    "xinput1_4.dll": "load at run time like patch 4019 (keeps one code path with 1_3 and 9_1_0)",
    "xinput9_1_0.dll": "load at run time like patch 4019",
    "libgcc_s_seh-1.dll": "MinGW runtime DLL: link -static-libgcc",
    "libstdc++-6.dll": "MinGW runtime DLL: link -static-libstdc++",
    "libwinpthread-1.dll": "MinGW runtime DLL: link winpthread statically",
    "libc++.dll": "llvm-mingw runtime DLL: link -static-libstdc++",
    "libunwind.dll": "llvm-mingw runtime DLL: link -static-libgcc",
}


def _rva_to_off(sections, rva):
    for va, vsize, raw, rawsize in sections:
        if va <= rva < va + max(vsize, rawsize):
            off = rva - va
            if off >= rawsize:
                raise ValueError(f"RVA {rva:#x} in a section's uninitialised tail")
            return raw + off
    raise ValueError(f"RVA {rva:#x} outside every section")


def _cstr(data, off):
    end = data.find(b"\0", off, off + 512)
    if end < 0:
        raise ValueError(f"unterminated name at {off:#x}")
    return data[off:end].decode("ascii")


def pe_imports(data):
    """Return (normal, delay) lists of imported DLL names from a PE image's bytes."""
    if data[:2] != b"MZ":
        raise ValueError("no MZ header")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("no PE signature")
    nsec, = struct.unpack_from("<H", data, pe + 6)
    optsize, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    magic, = struct.unpack_from("<H", data, opt)
    if magic == 0x20B:
        dd, image_base = opt + 112, struct.unpack_from("<Q", data, opt + 24)[0]
    elif magic == 0x10B:
        dd, image_base = opt + 96, struct.unpack_from("<I", data, opt + 28)[0]
    else:
        raise ValueError(f"unknown optional header magic {magic:#x}")
    ndirs, = struct.unpack_from("<I", data, dd - 4)
    sections = []
    for i in range(nsec):
        s = opt + optsize + 40 * i
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, s + 8)
        sections.append((va, vsize, raw, rawsize))

    def directory(idx):
        if idx >= ndirs:
            return 0, 0
        return struct.unpack_from("<II", data, dd + 8 * idx)

    normal = []
    rva, size = directory(1)
    if rva:
        off = _rva_to_off(sections, rva)
        while True:
            desc = struct.unpack_from("<IIIII", data, off)
            if desc == (0, 0, 0, 0, 0):
                break
            normal.append(_cstr(data, _rva_to_off(sections, desc[3])))
            off += 20
    delay = []
    rva, size = directory(13)
    if rva:
        off = _rva_to_off(sections, rva)
        while True:
            attrs, name = struct.unpack_from("<II", data, off)
            if attrs == 0 and name == 0:
                break
            # attrs bit 0 set = RVAs (every modern linker); clear = old VC6 style virtual addresses.
            delay.append(_cstr(data, _rva_to_off(sections, name if attrs & 1 else name - image_base)))
            off += 32
    return normal, delay


def bad_imports(names, extra=()):
    """Problem lines for imports outside ALLOWED / ALLOWED_PREFIXES and `extra` (DLLs shipped beside the exe)."""
    extra = {e.lower() for e in extra}
    out = []
    for n in names:
        low = n.lower()
        if low in ALLOWED or low.startswith(ALLOWED_PREFIXES) or (low in extra and low not in TRAPS):
            continue
        out.append(f"{n}: {TRAPS.get(low, 'not shipped by every Windows 10 22H2/11 (add to ALLOWED only with proof)')}")
    return out


def check(paths, extra=(), out=sys.stdout):
    """-> number of failed files; prints one line per file (and one per bad import)."""
    fails = 0
    for p in paths:
        try:
            with open(p, "rb") as f:
                normal, delay = pe_imports(f.read())
        except (OSError, ValueError, struct.error) as e:
            fails += 1
            print(f"FAIL {p}: {e}", file=out)
            continue
        bad = bad_imports(normal, extra) + [f"(delay-load) {b}" for b in bad_imports(delay, extra)]
        if bad:
            fails += 1
            print(f"FAIL {p}: {len(bad)} import(s) a stock Windows 10/11 lacks:", file=out)
            for b in bad:
                print(f"       {b}", file=out)
        else:
            names = ", ".join(sorted(set(normal + delay), key=str.lower))
            print(f"ok   {p}: {len(normal)} imports, {len(delay)} delay-load, all allowed ({names})", file=out)
    return fails


def main(argv=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--allow", action="append", default=[], metavar="DLL", help="a DLL shipped beside the file")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args(argv)
    return 1 if check(a.files, a.allow, out) else 0


if __name__ == "__main__":
    sys.exit(main())
