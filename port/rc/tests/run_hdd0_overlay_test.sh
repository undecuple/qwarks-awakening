#!/usr/bin/env bash
# Builds and runs test_hdd0_overlay.c, a unity build of runtime/syscalls/sys_fs.c from the patched tree (RC_P3R_TREE,
# default work/ps3recomp-patched of the main checkout) with patch 7014 applied to a scratch copy when the tree lacks it.
# Runs the overlay case, the "nolower" case (no PS3_HDD0_LOWER: the old mapping) and the cellFs HLE case
# (test_hdd0_overlay_cellfs.cpp, unity build of ppu_fs.cpp). gcc / g++ (CC, CXX).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"
tmp="${TMPDIR:-/tmp}/hdd0ov_$$"; mkdir -p "$tmp/t"; trap 'rm -rf "$tmp"' EXIT
cp -r "$tree/include" "$tmp/t/"; mkdir -p "$tmp/t/runtime" "$tmp/t/libs/filesystem"
for d in syscalls ppu memory platform; do mkdir -p "$tmp/t/runtime/$d"; cp "$tree/runtime/$d/"*.h "$tmp/t/runtime/$d/" 2>/dev/null || true; done
cp "$tree/runtime/syscalls/sys_fs.c" "$tmp/t/runtime/syscalls/"; cp "$tree/runtime/ppu/ppu_fs.cpp" "$tmp/t/runtime/ppu/"
cp "$tree/libs/filesystem/edat.h" "$tmp/t/libs/filesystem/"
[[ -f "$tmp/t/runtime/syscalls/hdd0_overlay.h" ]] || (cd "$tmp/t" && patch -p1 -s < "$repo/port/patches/7014-hdd0-two-layer-overlay.patch")
cc="${CC:-gcc}"
"$cc" -std=gnu11 -O1 -w -I"$tmp/t/include" -I"$tmp/t" -DSYS_FS_SRC="\"$tmp/t/runtime/syscalls/sys_fs.c\"" -o "$tmp/test" "$here/test_hdd0_overlay.c"
mkdir -p "$tmp/run"
echo "== overlay ($cc)"; TMPDIR="$tmp/run" "$tmp/test"
echo "== nolower ($cc)"; TMPDIR="$tmp/run" "$tmp/test" nolower
rec="${RC_RECOMP:-$main/work/recomp/tods}"
if [[ -f "$rec/ppu_recomp.h" ]]; then   # the cellFs HLE path (C++ unity build of ppu_fs.cpp; needs the lift's ppu_recomp.h)
  cxx="${CXX:-g++}"
  "$cxx" -std=gnu++20 -O0 -w -I"$tmp/t/runtime/ppu" -I"$rec" -I"$tmp/t/include" -I"$tmp/t" -I"$tmp/t/runtime" \
     -DFS_SRC="\"$tmp/t/runtime/ppu/ppu_fs.cpp\"" -o "$tmp/test_cellfs" "$here/test_hdd0_overlay_cellfs.cpp"
  echo "== cellFs ($cxx)"; TMPDIR="$tmp/run" "$tmp/test_cellfs"
else echo "SKIP cellFs part: no lift at $rec"; fi
