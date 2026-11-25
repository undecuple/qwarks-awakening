#!/usr/bin/env bash
# Host test for R&C patch 7109 (cellSaveData dirList follows CellSaveDataSetList sortType / sortOrder): runs the patched
# tree's tools/test_savedata_sort.py (libs/system/tests/test_savedata_sort.c: five saves whose name, time and SUB_TITLE
# orders differ, the four sort modes, the dirListMax cut after the sort, the ListLoad2 fallback = the newest save, the
# PARAM error for values above 1), then the negative control: the same checks against the tree's cellSaveData.c with
# the 7109 hunk reversed must FAIL. Exits 0 only when the test passes and the control fails. No game data.
# Usage: port/rc/tests/run_savedata_sort_test.sh [patched tree]   (or RC_P3R_TREE; default work/ps3recomp-patched)
#   nix develop .#ci --command port/rc/tests/run_savedata_sort_test.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
PT=$(cd "${1:-${RC_P3R_TREE:-"$repo/work/ps3recomp-patched"}}" && pwd)
patch="$repo/port/patches/7109-savedata-sort.patch"
grep -q "RC patch 7109" "$PT/libs/system/cellSaveData.c" || { echo "FAIL: $PT has no patch 7109" >&2; exit 1; }

echo "== test_savedata_sort ($PT)"
python3 "$PT/tools/test_savedata_sort.py"

tmp="${TMPDIR:-/tmp}/rc_savedata_sort_$$"; mkdir -p "$tmp/neg/libs/system"; trap 'rm -rf "$tmp"' EXIT
cp "$PT"/libs/system/{cellSaveData.c,cellSaveData.h,ps3_psf.h} "$tmp/neg/libs/system/"
ln -s "$PT/include" "$tmp/neg/include"
ln -s "$PT/runtime" "$tmp/neg/runtime"
(cd "$tmp/neg" && git -c core.autocrlf=false apply -R --include=libs/system/cellSaveData.c "$patch")
if python3 "$PT/tools/test_savedata_sort.py" --against "$tmp/neg" > "$tmp/neg.out"; then
  cat "$tmp/neg.out"
  echo "FAIL: negative control (7109 reversed) passed: the test does not detect the bug" >&2
  exit 1
fi
echo "negative control (cellSaveData.c without 7109) fails as expected:"
sed 's/^/  | /' "$tmp/neg.out"
echo "PASS run_savedata_sort_test"
