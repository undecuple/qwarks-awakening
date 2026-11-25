#!/usr/bin/env python3
"""Post-configure check of RSX_TRANSLATOR_ID (patch 2141, port/rc/rsx_translator_id.cmake) in a CMake/Ninja build dir;
no build, no game data.
  python3 -I tools/check_translator_define.py build/rc-<t>-win [--expect-tree work/ps3recomp-patched]
Fails (exit 1) when rsx_live_draw.c is compiled without -DRSX_TRANSLATOR_ID=0x<16 hex>ull, when the value is 0, when it
differs from the id recomputed here from the tree's libs/video/*.c *.h (names:sha256; joined, sha256, 16 hex), or when a
file of libs/video is not a regeneration dependency of build.ninja (a change there would not re-run configure).
Ninja/MinGW `-D` syntax assumed (what build.ninja shows); an MSVC `/D` build would need the regex widened."""
import glob
import hashlib
import os
import re
import sys


def tree_id(tree):
    """-> (16 hex digits, sorted file names): the rule of port/rc/rsx_translator_id.cmake."""
    v = os.path.join(tree, "libs", "video")
    names = sorted(os.path.basename(p) for p in glob.glob(os.path.join(v, "*.c")) + glob.glob(os.path.join(v, "*.h"))
                   if os.path.isfile(p))
    cat = ""
    for n in names:
        with open(os.path.join(v, n), "rb") as f:
            cat += f"{n}:{hashlib.sha256(f.read()).hexdigest()};"
    return hashlib.sha256(cat.encode()).hexdigest()[:16], names


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    bn = os.path.join(argv[1], "build.ninja")
    with open(bn, encoding="utf-8", errors="replace") as f:
        text = f.read().replace("$ ", " ").replace("$:", ":")
    m = re.search(r"^build [^\n]*libs/video/rsx_live_draw\.c\.obj: \S+ (\S.*?libs/video/rsx_live_draw\.c)\b[^\n]*\n"
                  r"(?:  [^\n]*\n)*?  DEFINES = ([^\n]*)\n", text, re.M)
    if not m:
        print("FAIL: no rsx_live_draw.c.obj rule in " + bn)
        return 1
    src, defines = m.group(1), m.group(2)
    d = re.search(r"-DRSX_TRANSLATOR_ID=0x([0-9a-fA-F]{16})ull\b", defines)
    if not d:
        print("FAIL: rsx_live_draw.c is compiled without RSX_TRANSLATOR_ID (the id is 0): " + defines[:200])
        return 1
    got = d.group(1).lower()
    if int(got, 16) == 0:
        print("FAIL: RSX_TRANSLATOR_ID is 0")
        return 1
    tree = os.path.dirname(os.path.dirname(os.path.dirname(src)))
    if "--expect-tree" in argv:
        tree = argv[argv.index("--expect-tree") + 1]
    want, names = tree_id(tree)
    if got != want:
        print(f"FAIL: built id {got}, tree {tree} gives {want}")
        return 1
    rerun = re.search(r"^build build\.ninja[^\n]*?: RERUN_CMAKE ([^\n]*)", text, re.M)
    deps = rerun.group(1) if rerun else ""
    missing = [n for n in names if "libs/video/" + n not in deps]
    if missing:
        print(f"FAIL: {len(missing)} libs/video files are not configure dependencies (first: {missing[0]})")
        return 1
    print(f"ok: RSX_TRANSLATOR_ID=0x{got}ull = id of {len(names)} files in {tree}/libs/video, all are configure dependencies")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
