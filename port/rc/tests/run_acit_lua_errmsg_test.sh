#!/usr/bin/env bash
# Host test for ACiT 1.20's RC_LUA_ERRMSG diagnostic (port/rc/acit120/acit120_lua_errmsg.inc, test_acit_lua_errmsg.cpp) through the
# landing hook of toolkit patch 7113 (the twin of run_ffa_lua_errmsg_test.sh), against the toolkit's runtime half
# runtime/ppu/ppu_setjmp_rt.inc. Synthetic guest memory only: no game data, no lifted code, no GPU.
#   nix develop .#re --command tools/sandbox.sh -- port/rc/tests/run_acit_lua_errmsg_test.sh
# The patched toolkit (work/ps3recomp-patched, this checkout's or the main checkout's) is copied to a temp dir and 7113 is
# applied there when the tree does not carry it yet. Runs "on" (must PASS) and "off" (RC_LUA_ERRMSG unset: no [lua-err] line).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; root="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
pt="${RC_P3R_TREE:-$root/work/ps3recomp-patched}"; [[ -f "$pt/runtime/ppu/ppu_setjmp_rt.inc" ]] || pt="$main/work/ps3recomp-patched"
[[ -f "$pt/runtime/ppu/ppu_setjmp_rt.inc" ]] || { echo "SKIP: no patched toolkit (tools/ps3recomp-patched.sh)"; exit 0; }
tmp="${TMPDIR:-/tmp}/acit_luaerr_$$"; mkdir -p "$tmp/runtime/ppu" "$tmp/runtime/platform"; trap 'rm -rf "$tmp"' EXIT
cp "$pt/runtime/ppu/ppu_setjmp_rt.inc" "$pt/runtime/ppu/ppu_setjmp.h" "$pt/runtime/ppu/ppu_context.h" "$tmp/runtime/ppu/"
mkdir -p "$tmp/include"; cp -r "$pt/include/ps3emu" "$tmp/include/"
if ! grep -q ppu_sj_landed_fn "$tmp/runtime/ppu/ppu_setjmp_rt.inc"; then
  (cd "$tmp" && patch -s -p1 < "$root/port/patches/7113-ppu-setjmp-landing-hook.patch") || { echo "FAIL: 7113 does not apply"; exit 1; }
fi
grep -q 'if (hook) hook(ctx, r->site, st);' "$tmp/runtime/ppu/ppu_setjmp_rt.inc" || { echo "FAIL: no landing hook call in the runtime half"; exit 1; }
rc=0
# RC_TEST_CC (as run_log_test.sh): only that compiler family (gcc -> g++, clang -> clang++). Public CI sets gcc: the runner's
# own clang++ is on PATH inside nix develop and links against the shell's glibc, so its program does not start.
cxxs=(g++ clang++)
case "${RC_TEST_CC:-}" in "") ;; *gcc) cxxs=("${RC_TEST_CC%gcc}g++") ;; *clang) cxxs=("${RC_TEST_CC}++") ;; *) cxxs=("$RC_TEST_CC") ;; esac
for cxx in "${cxxs[@]}"; do
  command -v "$cxx" >/dev/null || { echo "no $cxx"; continue; }
  "$cxx" -std=gnu++20 -O1 -w -I"$tmp/runtime/ppu" \
    -DPPU_SJ_RT="\"$tmp/runtime/ppu/ppu_setjmp_rt.inc\"" -DACIT_LUA_ERRMSG_INC="\"$root/port/rc/acit120/acit120_lua_errmsg.inc\"" \
    -o "$tmp/t_${cxx##*/}" "$here/test_acit_lua_errmsg.cpp" || { rc=1; continue; }
  echo "== $cxx on";  "$tmp/t_${cxx##*/}" 2>"$tmp/err_on" || rc=1
  echo "   stderr: $(head -n 1 "$tmp/err_on")"
  echo "== $cxx off"; "$tmp/t_${cxx##*/}" off 2>/dev/null || rc=1
done
exit $rc
