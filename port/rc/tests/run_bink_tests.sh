#!/usr/bin/env bash
# Builds and runs the Bink lifted-code host tests with g++ and clang++ against the lifted functions in work/recomp/tods
# (derived from the game binary: read at run time, never committed). No game run, no GPU, no game data beyond the lifted C.
# usage: port/rc/tests/run_bink_tests.sh [read_dcs|idct|all]   (env: RC_RECOMP=dir with ppu_recomp*.cpp, RC_P3R=ps3recomp runtime/ppu dir)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
rec="${RC_RECOMP:-$main/work/recomp/tods}"; p3r="${RC_P3R:-$main/work/ps3recomp-patched/runtime/ppu}"
[[ -f "$rec/ppu_recomp_060.cpp" ]] || { echo "SKIP: no lifted code at $rec (run tools/lift.sh tods)"; exit 0; }
tmp="${TMPDIR:-/tmp}/bink_tests_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
first=$(awk '/^void func_/{print NR; exit}' "$rec/ppu_recomp_060.cpp")
extract() {   # extract NAME: print the lifted function NAME (any ppu_recomp_*.cpp) up to its closing brace
  local f; f=$(grep -l "^void $1(ppu_context\* ctx) {" "$rec"/ppu_recomp_0*.cpp | head -n 1)
  awk -v n="$1" 'index($0, "void " n "(")==1{p=1} p{print} p&&/^}/{exit}' "$f"
}
build_run() {   # build_run TEST FUNC...
  local t="$1"; shift
  { head -n $((first - 1)) "$rec/ppu_recomp_060.cpp"; for fn in "$@"; do extract "$fn"; done; } > "$tmp/subset_$t.cpp"
  for fn in "$@"; do grep -q "^void $fn(" "$tmp/subset_$t.cpp" || { echo "missing $fn in the lifted code"; return 1; }; done
  local rc=0
  for cxx in g++ clang++; do
    command -v "$cxx" >/dev/null || { echo "no $cxx"; continue; }
    "$cxx" -std=gnu++20 -O1 -w -I"$rec" -I"$p3r" -DBINK_LIFTED_SUBSET="\"$tmp/subset_$t.cpp\"" -o "$tmp/${t}_$cxx" "$here/test_bink_$t.cpp" \
      && echo "== $t $cxx" && "$tmp/${t}_$cxx" || rc=1
  done
  return $rc
}
which="${1:-all}"; rc=0
[[ "$which" == read_dcs || "$which" == all ]] && { build_run read_dcs func_007FCCD8__lifted func_0080FC48__lifted || rc=1; }
[[ "$which" == idct || "$which" == all ]] && { build_run idct func_0080D5B8 func_0080D988 func_0080DDD8 || rc=1; }
exit $rc
