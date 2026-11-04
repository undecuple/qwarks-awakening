#!/usr/bin/env bash
# Host test for Quest for Booty's Lua 5.0.2 error path (test_qfb_lua_ljmp.cpp): the REAL lifted luaD_rawrunprotected / luaD_throw,
# the TOC stubs to libc setjmp / longjmp / exit and libc setjmp / longjmp of work/bin/qfb/EBOOT.elf lifted with
# port/rc/qfb/ppu_overrides.txt (the `setjmp` / `longjmp` rows, toolkit patch 1116), against the toolkit's runtime half
# runtime/ppu/ppu_setjmp_rt.inc. Lifted C is derived from the game binary: cut out at run time into a temp dir, never committed.
# No game run, no GPU. Same shape as run_acit_lua_ljmp_test.sh.
#   nix develop .#re --command tools/sandbox.sh -- port/rc/tests/run_qfb_lua_ljmp_test.sh
# Lifted code: RC_RECOMP (default: this checkout's work/recomp/qfb, else the main checkout's) when it was lifted with the rows (it
# has PPU_SJ_CALL); otherwise a seed lift of the 7 functions with this checkout's overrides file into work/tests/qfb-ljmp-lift
# (needs the decrypted ELF, its analysis and work/ps3recomp-patched; seconds, reused while the overrides file and lifter are unchanged).
# Runs "on" (must PASS) and "off" (KZ3_LJMP=0, must reproduce the no-handler exit: null reads 0x11/0x28, NULL call, exit(1)).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; root="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
p3r="${RC_P3R:-$root/work/ps3recomp-patched/runtime/ppu}"; [[ -f "$p3r/ppu_setjmp_rt.inc" ]] || p3r="$main/work/ps3recomp-patched/runtime/ppu"
[[ -f "$p3r/ppu_setjmp_rt.inc" ]] || { echo "SKIP: no patched toolkit (tools/ps3recomp-patched.sh)"; exit 0; }
ovr="$root/port/rc/qfb/ppu_overrides.txt"
rec="${RC_RECOMP:-$root/work/recomp/qfb}"; [[ -d "$rec" ]] || rec="$main/work/recomp/qfb"
if ! grep -qs 'PPU_SJ_CALL(ctx, func_00725328' "$rec"/ppu_recomp_0*.cpp; then
  elf="$main/work/bin/qfb/EBOOT.elf"; an="$main/work/analysis/qfb"; pt="$(dirname "$(dirname "$p3r")")"
  [[ -f "$elf" && -f "$an/EBOOT.imports.json" ]] || { echo "SKIP: $rec has no setjmp rows and no ELF/analysis to lift"; exit 0; }
  rec="$root/work/tests/qfb-ljmp-lift"
  stamp="$(cat "$ovr" "$pt/tools/ppu_lifter.py" | sha256sum | cut -c1-16)"
  if [[ "$(cat "$rec/.stamp" 2>/dev/null)" != "$stamp" ]]; then
    mkdir -p "$rec"
    # luaD_rawrunprotected, luaD_throw, setjmp/longjmp/exit stubs, libc setjmp, libc longjmp (bounds of work/analysis/qfb/functions.json)
    echo '[{"start":"0x005601A8","end":"0x00560238"},{"start":"0x00560238","end":"0x005602A0"},{"start":"0x00725328","end":"0x00725344"},{"start":"0x00725344","end":"0x00725360"},{"start":"0x00724FFC","end":"0x00725018"},{"start":"0x006FF740","end":"0x006FF864"},{"start":"0x006FF864","end":"0x006FF9AC"}]' > "$rec/functions.json"
    echo "lifting (seeds luaD_rawrunprotected, luaD_throw, 3 stubs, setjmp, longjmp) into $rec ..."
    python3 "$pt/tools/ppu_lifter.py" "$elf" --functions "$rec/functions.json" --hle-stubs "$an/EBOOT.imports.json" \
      --toc 0x8400e8 --code-end 0x77af2c --overrides "$ovr" -j 4 --output "$rec" > "$rec/lift.log" 2>&1 || { tail "$rec/lift.log"; exit 1; }
    echo "$stamp" > "$rec/.stamp"
  fi
fi
tmp="${TMPDIR:-/tmp}/qfb_ljmp_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
f0="$(grep -l -E '^void func_005601A8\(' "$rec"/ppu_recomp_0*.cpp | head -n 1)"
first=$(awk '/^void func_/{print NR; exit}' "$f0")
extract() {   # extract NAME: print the lifted function NAME (static forward declarations stay with the __ljbody)
  local n="$1" f
  f=$(grep -l -E "^(static )?void $n\(ppu_context\* ctx\) \{" "$rec"/ppu_recomp_0*.cpp | head -n 1)
  [[ -n "$f" ]] || { echo "missing $n in the lifted code ($rec)" >&2; return 1; }
  awk -v n="$n" '(index($0, "void " n "(")==1 || index($0, "static void " n "(")==1) && /\{$/{p=1} p{print} p&&/^}/{exit}' "$f"
}
{ head -n $((first - 1)) "$f0" | grep -v 'ppu_setjmp.h'
  echo '#include "ppu_setjmp.h"'
  echo 'static void func_006FF864__ljbody(ppu_context* ctx);'
  echo 'void func_00560294(ppu_context* ctx); void func_00725344(ppu_context* ctx); void func_005602A0(ppu_context* ctx);'
  echo 'void func_00725018(ppu_context* ctx); void func_00725360(ppu_context* ctx); void func_006FF864(ppu_context* ctx);'
  extract func_005601A8; extract func_00560238
  # luaD_throw's exit(1) tail: its own fragment in the full lift (a trampoline target), inline in a seed lift
  if grep -qs -E '^void func_00560294\(' "$rec"/ppu_recomp_0*.cpp; then extract func_00560294; fi
  extract func_00725328; extract func_00725344; extract func_00724FFC
  extract func_006FF740; extract func_006FF864__ljbody; extract func_006FF864; } > "$tmp/subset.cpp"
grep -qE '^void func_00560294\(ppu_context\* ctx\) \{' "$tmp/subset.cpp" || echo 'void func_00560294(ppu_context*) { fprintf(stderr, "unexpected func_00560294\n"); exit(2); }' >> "$tmp/subset.cpp"
grep -q 'PPU_SJ_CALL(ctx, func_00725328, 0x005601E0u, 544)' "$tmp/subset.cpp" || { echo "FAIL: the setjmp stub call @0x005601E0 is not a PPU_SJ_CALL with status 0x220"; exit 1; }
grep -q 'ppu_sj_longjmp(ctx, _sj_jb)' "$tmp/subset.cpp" || { echo "FAIL: longjmp 0x006FF864 has no host-jump wrapper"; exit 1; }
rc=0
for cxx in g++ clang++; do
  command -v "$cxx" >/dev/null || { echo "no $cxx"; continue; }
  "$cxx" -std=gnu++20 -O1 -w -I"$rec" -I"$p3r" -DLUA_LIFTED_SUBSET="\"$tmp/subset.cpp\"" -DPPU_SJ_RT="\"$p3r/ppu_setjmp_rt.inc\"" \
    -o "$tmp/t_$cxx" "$here/test_qfb_lua_ljmp.cpp" || { rc=1; continue; }
  echo "== $cxx on";  "$tmp/t_$cxx" 2>"$tmp/err_on" || rc=1; echo "   stderr: $(grep -c "^\[ljmp\] caught" "$tmp/err_on") [ljmp] caught line(s), first: $(head -n 1 "$tmp/err_on")"
  echo "== $cxx off"; "$tmp/t_$cxx" off || rc=1
done
[[ -n "${RC_KEEP:-}" ]] && cp "$tmp"/t_* "$tmp/subset.cpp" "$RC_KEEP"/
exit $rc
