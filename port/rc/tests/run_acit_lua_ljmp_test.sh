#!/usr/bin/env bash
# Host test for ACiT 1.20's Lua error path (test_acit_lua_ljmp.cpp): the REAL lifted luaD_rawrunprotected / luaD_throw / libc
# setjmp / longjmp of work/bin/acit120/EBOOT.elf lifted with port/rc/acit120/ppu_overrides.txt (the `setjmp` / `longjmp` rows,
# toolkit patch 1116), against the toolkit's runtime half runtime/ppu/ppu_setjmp_rt.inc. Lifted C is derived from the game binary:
# cut out at run time into a temp dir, never committed. No game run, no GPU.
#   nix develop .#re --command tools/sandbox.sh -- port/rc/tests/run_acit_lua_ljmp_test.sh
# Lifted code: RC_RECOMP (default: the main checkout's work/recomp/acit120) when it was lifted with the rows (it has PPU_SJ_CALL);
# otherwise a fresh lift with this checkout's overrides file into work/tests/acit-ljmp-lift (needs the decrypted ELF and
# work/ps3recomp-patched; several minutes, reused while the overrides file and the toolkit are unchanged).
# Runs "on" (must PASS) and "off" (KZ3_LJMP=0, must reproduce the acit-boot3 exit: null reads 0x11/0x58 by luaD_throw, exit(1)).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; root="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
p3r="${RC_P3R:-$root/work/ps3recomp-patched/runtime/ppu}"; [[ -f "$p3r/ppu_setjmp_rt.inc" ]] || p3r="$main/work/ps3recomp-patched/runtime/ppu"
[[ -f "$p3r/ppu_setjmp_rt.inc" ]] || { echo "SKIP: no patched toolkit (tools/ps3recomp-patched.sh)"; exit 0; }
ovr="$root/port/rc/acit120/ppu_overrides.txt"
rec="${RC_RECOMP:-$main/work/recomp/acit120}"
if ! grep -qs 'PPU_SJ_CALL(ctx, func_0090AF28' "$rec"/ppu_recomp_0*.cpp; then
  elf="$main/work/bin/acit120/EBOOT.elf"; an="$main/work/analysis/acit120"; pt="$(dirname "$(dirname "$p3r")")"
  [[ -f "$elf" && -f "$an/EBOOT.imports.json" ]] || { echo "SKIP: $rec has no setjmp rows and no ELF/analysis to lift"; exit 0; }
  rec="$root/work/tests/acit-ljmp-lift"
  stamp="$(cat "$ovr" "$pt/tools/ppu_lifter.py" | sha256sum | cut -c1-16)"
  if [[ "$(cat "$rec/.stamp" 2>/dev/null)" != "$stamp" ]]; then
    mkdir -p "$rec"
    echo '[{"start":"0x0050AF5C","end":"0x0050AFEC"},{"start":"0x0050B0DC","end":"0x0050B1F8"},{"start":"0x0090AF28","end":"0x0090B04C"},{"start":"0x0090B04C","end":"0x0090B194"}]' > "$rec/functions.json"
    echo "lifting (seeds luaD_rawrunprotected, luaD_throw, setjmp, longjmp) into $rec ..."
    python3 "$pt/tools/ppu_lifter.py" "$elf" --functions "$rec/functions.json" --hle-stubs "$an/EBOOT.imports.json" \
      --toc 0xd347b0 --code-end 0xa4ab94 --overrides "$ovr" -j 4 --output "$rec" > "$rec/lift.log" 2>&1 || { tail "$rec/lift.log"; exit 1; }
    echo "$stamp" > "$rec/.stamp"
  fi
fi
tmp="${TMPDIR:-/tmp}/acit_ljmp_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
f0="$(grep -l -E '^void func_0050AF5C\(' "$rec"/ppu_recomp_0*.cpp | head -n 1)"
first=$(awk '/^void func_/{print NR; exit}' "$f0")
extract() {   # extract NAME: print the lifted function NAME (static forward declarations stay with the __ljbody)
  local n="$1" f
  f=$(grep -l -E "^(static )?void $n\(ppu_context\* ctx\) \{" "$rec"/ppu_recomp_0*.cpp | head -n 1)
  [[ -n "$f" ]] || { echo "missing $n in the lifted code ($rec)" >&2; return 1; }
  awk -v n="$n" '(index($0, "void " n "(")==1 || index($0, "static void " n "(")==1) && /\{$/{p=1} p{print} p&&/^}/{exit}' "$f"
}
{ head -n $((first - 1)) "$f0" | grep -v 'ppu_setjmp.h'
  echo '#include "ppu_setjmp.h"'
  echo 'static void func_0090B04C__ljbody(ppu_context* ctx);'
  extract func_0050AF5C; extract func_0050B0DC; extract func_0050B1EC
  extract func_0090AF28; extract func_0090B04C__ljbody; extract func_0090B04C; } > "$tmp/subset.cpp"
grep -q 'PPU_SJ_CALL(ctx, func_0090AF28, 0x0050AF94u, 544)' "$tmp/subset.cpp" || { echo "FAIL: the setjmp call site @0x0050AF94 is not a PPU_SJ_CALL with status 0x220"; exit 1; }
grep -q 'ppu_sj_longjmp(ctx, _sj_jb)' "$tmp/subset.cpp" || { echo "FAIL: longjmp 0x0090B04C has no host-jump wrapper"; exit 1; }
rc=0
for cxx in g++ clang++; do
  command -v "$cxx" >/dev/null || { echo "no $cxx"; continue; }
  "$cxx" -std=gnu++20 -O1 -w -I"$rec" -I"$p3r" -DLUA_LIFTED_SUBSET="\"$tmp/subset.cpp\"" -DPPU_SJ_RT="\"$p3r/ppu_setjmp_rt.inc\"" \
    -o "$tmp/t_$cxx" "$here/test_acit_lua_ljmp.cpp" || { rc=1; continue; }
  echo "== $cxx on";  "$tmp/t_$cxx" 2>"$tmp/err_on" || rc=1; echo "   stderr: $(grep -c "^\[ljmp\] caught" "$tmp/err_on") [ljmp] caught line(s), first: $(head -n 1 "$tmp/err_on")"
  echo "== $cxx off"; "$tmp/t_$cxx" off || rc=1
done
[[ -n "${RC_KEEP:-}" ]] && cp "$tmp"/t_* "$tmp/subset.cpp" "$RC_KEEP"/
exit $rc
