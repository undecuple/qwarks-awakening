#!/usr/bin/env bash
# Builds and runs test_fs_stat_times.cpp, a unity build of runtime/ppu/ppu_fs.cpp from the patched tree (RC_P3R_TREE,
# default work/ps3recomp-patched of the main checkout) with patch 7011 applied to a scratch copy when the tree lacks it.
# usage: port/rc/tests/run_fs_stat_test.sh [base|patched]   (one compiler: CXX, default g++). Exit 0 = PASS.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"; rec="${RC_RECOMP:-$main/work/recomp/tods}"; mode="${1:-patched}"
[[ -f "$tree/runtime/ppu/ppu_fs.cpp" && -f "$rec/ppu_recomp.h" ]] || { echo "SKIP: no tree or lift"; exit 0; }
tmp="${TMPDIR:-/tmp}/fsstat_$$"; mkdir -p "$tmp/t/runtime/ppu"; trap 'rm -rf "$tmp"' EXIT
cp "$tree/runtime/ppu/ppu_fs.cpp" "$tmp/t/runtime/ppu/"
has=0; grep -q "R&C patch 7011" "$tmp/t/runtime/ppu/ppu_fs.cpp" && has=1
if [[ "$mode" == patched && $has == 0 ]]; then (cd "$tmp/t" && patch -p1 -s < "$repo/port/patches/7011-cellfs-stat-file-times.patch"); fi
if [[ "$mode" == base && $has == 1 ]]; then echo "tree already carries 7011"; exit 2; fi
cxx="${CXX:-g++}"
"$cxx" -std=gnu++20 -O0 -w -I"$tree/runtime/ppu" -I"$rec" -I"$tree/include" -I"$tree" -I"$tree/runtime" \
   -DFS_SRC="\"$tmp/t/runtime/ppu/ppu_fs.cpp\"" -o "$tmp/test" "$here/test_fs_stat_times.cpp"
echo "== $mode ($cxx)"; "$tmp/test"
