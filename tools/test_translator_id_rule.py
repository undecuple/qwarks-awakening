#!/usr/bin/env python3
"""Host test of the RSX_TRANSLATOR_ID rule (tools/check_translator_define.tree_id, mirrored by port/rc/rsx_translator_id.cmake).
Scratch copy of <tree>/libs/video: id non-zero; a one-byte change, a rename and a new .c change it (positive controls);
a touch (mtime only), a .txt and a subdirectory file leave it unchanged (negative controls: content, not time, top level only).
  python3 -I tools/test_translator_id_rule.py [tree]   (tree default work/ps3recomp-patched)
-> 'PASS' / exit 1."""
import importlib.util
import os
import shutil
import sys
import tempfile
import time

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ctd", os.path.join(here, "check_translator_define.py"))
ctd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ctd)
tree = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "work", "ps3recomp-patched")
fails = 0


def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    fails += 0 if cond else 1


with tempfile.TemporaryDirectory() as d:
    v = os.path.join(d, "libs", "video")
    os.makedirs(v)
    src = os.path.join(tree, "libs", "video")
    for n in os.listdir(src):
        if os.path.isfile(os.path.join(src, n)) and n.endswith((".c", ".h")):
            shutil.copy(os.path.join(src, n), v)
    base, names = ctd.tree_id(d)
    check(f"id non-zero over {len(names)} files", int(base, 16) != 0 and len(names) > 0)
    check("same tree, same id", ctd.tree_id(d)[0] == base)
    p = os.path.join(v, "rsx_fp_decompiler.c")
    with open(p, "rb") as f:
        orig = f.read()
    with open(p, "ab") as f:
        f.write(b" ")
    check("one byte appended to rsx_fp_decompiler.c changes the id", ctd.tree_id(d)[0] != base)
    with open(p, "wb") as f:
        f.write(orig)
    check("restored file gives the old id back", ctd.tree_id(d)[0] == base)
    os.utime(p, (time.time() + 99999, time.time() + 99999))
    check("NEG: mtime only does not change the id", ctd.tree_id(d)[0] == base)
    with open(os.path.join(v, "notes.txt"), "w") as f:
        f.write("x")
    check("NEG: a .txt does not change the id", ctd.tree_id(d)[0] == base)
    os.makedirs(os.path.join(v, "sub"))
    with open(os.path.join(v, "sub", "a.h"), "w") as f:
        f.write("x")
    check("NEG: a subdirectory .h does not change the id (rule is top level only)", ctd.tree_id(d)[0] == base)
    os.rename(p, os.path.join(v, "rsx_fp_decompiler2.c"))
    check("a rename (same bytes) changes the id", ctd.tree_id(d)[0] != base)
    os.rename(os.path.join(v, "rsx_fp_decompiler2.c"), p)
    with open(os.path.join(v, "rsx_new.c"), "w") as f:
        f.write("x")
    check("a new .c changes the id", ctd.tree_id(d)[0] != base)
print("FAILED" if fails else "PASS")
sys.exit(1 if fails else 0)
