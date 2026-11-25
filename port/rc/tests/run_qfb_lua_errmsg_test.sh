#!/usr/bin/env bash
# Host test for Quest for Booty's RC_LUA_ERRMSG diagnostic (port/rc/qfb/qfb_lua_errmsg.inc, test_qfb_lua_errmsg.cpp) through the
# landing hook of toolkit patch 7113, with the REAL lifted luaD_rawrunprotected 0x005601A8 / luaD_throw 0x00560238, the TOC stubs
# and libc setjmp / longjmp of the QfB lift (cut out at run time into a temp dir, never committed; the same cut as
# run_qfb_lua_ljmp_test.sh), against the toolkit's runtime half runtime/ppu/ppu_setjmp_rt.inc. No game run, no GPU.
#   nix develop .#re --command tools/sandbox.sh -- port/rc/tests/run_qfb_lua_errmsg_test.sh
# From a worktree the main checkout is not visible in the sandbox: pass RC_RECOMP (a lift with the setjmp rows, e.g. the main
# checkout's work/recomp/qfb) and RC_P3R_TREE (the patched toolkit) and bind both with --ro.
# RC_ERRMSG_INC overrides the .inc (mutation checks on a copy). 7113 is applied to a temp copy of the runtime half when the
# tree does not carry it yet. Runs "on" (must PASS) and "off" (RC_LUA_ERRMSG unset: no [lua-err] line).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; root="$(cd "$here/../../.." && pwd)"
main="$root"; gcd="$(git -C "$here" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"; [[ -d "$gcd" ]] && main="$(cd "$gcd/.." && pwd)"
pt="${RC_P3R_TREE:-$root/work/ps3recomp-patched}"; [[ -f "$pt/runtime/ppu/ppu_setjmp_rt.inc" ]] || pt="$main/work/ps3recomp-patched"
[[ -f "$pt/runtime/ppu/ppu_setjmp_rt.inc" ]] || { echo "SKIP: no patched toolkit (tools/ps3recomp-patched.sh)"; exit 0; }
rec="${RC_RECOMP:-$root/work/recomp/qfb}"; [[ -d "$rec" ]] || rec="$main/work/recomp/qfb"
grep -qs 'PPU_SJ_CALL(ctx, func_00725328' "$rec"/ppu_recomp_0*.cpp || { echo "SKIP: no QfB lift with the setjmp rows at $rec (run_qfb_lua_ljmp_test.sh can seed one)"; exit 0; }
inc="${RC_ERRMSG_INC:-$root/port/rc/qfb/qfb_lua_errmsg.inc}"
tmp="${TMPDIR:-/tmp}/qfb_luaerr_$$"; mkdir -p "$tmp/rt"; trap 'rm -rf "$tmp"' EXIT
cp "$pt/runtime/ppu/"*.h "$pt/runtime/ppu/ppu_setjmp_rt.inc" "$tmp/rt/"
if ! grep -q ppu_sj_landed_fn "$tmp/rt/ppu_setjmp_rt.inc"; then
  (cd "$tmp/rt" && patch -s -p3 < "$root/port/patches/7113-ppu-setjmp-landing-hook.patch") || { echo "FAIL: 7113 does not apply"; exit 1; }
fi
grep -q 'if (hook) hook(ctx, r->site, st);' "$tmp/rt/ppu_setjmp_rt.inc" || { echo "FAIL: no landing hook call in the runtime half"; exit 1; }
f0="$(grep -l -E '^void func_005601A8\(' "$rec"/ppu_recomp_0*.cpp | head -n 1)"
first=$(awk '/^void func_/{print NR; exit}' "$f0")
extract() {   # extract NAME: print the lifted function NAME (as run_qfb_lua_ljmp_test.sh)
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
  if grep -qs -E '^void func_00560294\(' "$rec"/ppu_recomp_0*.cpp; then extract func_00560294; fi
  extract func_00725328; extract func_00725344; extract func_00724FFC
  extract func_006FF740; extract func_006FF864__ljbody; extract func_006FF864; } > "$tmp/subset.cpp"
grep -qE '^void func_00560294\(ppu_context\* ctx\) \{' "$tmp/subset.cpp" || echo 'void func_00560294(ppu_context*) { printf("FAIL: unexpected func_00560294\n"); exit(2); }' >> "$tmp/subset.cpp"
grep -q 'PPU_SJ_CALL(ctx, func_00725328, 0x005601E0u, 544)' "$tmp/subset.cpp" || { echo "FAIL: the setjmp stub call @0x005601E0 is not a PPU_SJ_CALL with status 0x220"; exit 1; }
rc=0
for cxx in g++ clang++; do
  command -v "$cxx" >/dev/null || { echo "no $cxx"; continue; }
  "$cxx" -std=gnu++20 -O1 -w -I"$tmp/rt" -I"$pt/runtime/ppu" -I"$rec" -I"$pt/include" -DLUA_LIFTED_SUBSET="\"$tmp/subset.cpp\"" \
    -DPPU_SJ_RT="\"$tmp/rt/ppu_setjmp_rt.inc\"" -DQFB_LUA_ERRMSG_INC="\"$inc\"" \
    -o "$tmp/t_$cxx" "$here/test_qfb_lua_errmsg.cpp" || { rc=1; continue; }
  echo "== $cxx on";  "$tmp/t_$cxx" 2>"$tmp/err_on" || rc=1
  echo "   stderr: $(head -n 1 "$tmp/err_on")"
  echo "== $cxx off"; "$tmp/t_$cxx" off 2>/dev/null || rc=1
done
exit $rc
