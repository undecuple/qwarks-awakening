#!/usr/bin/env bash
# Builds and runs the Lua setjmp/longjmp host test (test_lua_ljmp.cpp) with g++ and clang++ against the lifted functions in work/recomp/tods
# (derived from the game binary: cut out at run time, never committed) and port/rc/tods/tods_lua_ljmp.inc. Runs it twice: "on" must PASS,
# "off" (TODS_LUA_LJMP=0) must reproduce the port's bug (NULL panic call + exit). No game run, no GPU, no game data beyond the lifted C.
# usage: port/rc/tests/run_lua_ljmp_test.sh   (env: RC_RECOMP=dir with ppu_recomp*.cpp, RC_P3R=ps3recomp runtime/ppu dir)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
rec="${RC_RECOMP:-$main/work/recomp/tods}"; p3r="${RC_P3R:-$main/work/ps3recomp-patched/runtime/ppu}"
[[ -f "$rec/ppu_recomp_051.cpp" ]] || { echo "SKIP: no lifted code at $rec (run tools/lift.sh tods)"; exit 0; }
tmp="${TMPDIR:-/tmp}/lua_ljmp_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
first=$(awk '/^void func_/{print NR; exit}' "$rec/ppu_recomp_051.cpp")
extract() {   # extract NAME [AS]: print the lifted function NAME (or NAME__lifted after a re-lift with the overrides) renamed to AS
  local n="$1" as="${2:-$1}" f
  f=$(grep -l -E "^void $n(__lifted)?\(ppu_context\* ctx\) \{" "$rec"/ppu_recomp_0*.cpp | head -n 1)
  [[ -n "$f" ]] || { echo "missing $n in the lifted code" >&2; return 1; }
  awk -v n="$n" '(index($0, "void " n "(")==1 || index($0, "void " n "__lifted(")==1){p=1} p{print} p&&/^}/{exit}' "$f" \
    | sed -E "1s/^void $n(__lifted)?\(/void $as(/"
}
{ head -n $((first - 1)) "$rec/ppu_recomp_051.cpp"
  extract func_00671CA8 func_00671CA8__lifted; extract func_00671D38; extract func_00671D9C
  extract func_0059E630; extract func_0059DE10; extract func_00826A10; extract func_00826B34 func_00826B34__lifted; } > "$tmp/subset.cpp"
rc=0
for cxx in g++ clang++; do
  command -v "$cxx" >/dev/null || { echo "no $cxx"; continue; }
  "$cxx" -std=gnu++20 -O1 -w -I"$rec" -I"$p3r" -DLUA_LIFTED_SUBSET="\"$tmp/subset.cpp\"" -o "$tmp/t_$cxx" "$here/test_lua_ljmp.cpp" || { rc=1; continue; }
  echo "== $cxx on";  "$tmp/t_$cxx"     || rc=1
  echo "== $cxx off"; "$tmp/t_$cxx" off || rc=1
done
exit $rc
