#!/usr/bin/env bash
# Host test for R&C patches 7116 (keep the word an idle RSX fetches when the guest moves put; its lifter half) and 7118.
# Usage: port/rc/tests/run_gcm_put_snap_test.sh [patched tree]   (default work/ps3recomp-patched)
# 1. test_gcm_put_snap.c: the real walker (cellGcmSys.c) playing FFA's frame-end inject, once per GCM_PUT_SNAP mode
#    (on: the kept JUMP-back is walked; log and off: the reused scratch is walked, the old behaviour) and once with
#    patch 7118's GCM_PUT_TRACE alone (hook armed, nothing kept, two [put-trace] lines). The guest's put stores run the
#    tree's own vm_write32 / vm_write64 hook lines, extracted by put_hook_lines.py (hook before the store, after the
#    OOB guard); mode on adds a std with put as its low word and both ring recycle paths dropping a pending keep.
# 2. test_put_store_route.py: the lifter's ppu_recomp.h template sends the 32-bit store to the gcm put word (and a
#    64-bit store overlapping it) out of line, and nothing else.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
[[ -f "$PT/libs/video/cellGcmSys.c" ]] || { echo "SKIP: no patched tree at $PT"; exit 0; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/rc-putsnap-XXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
cc=${CC:-cc}
# The real hook lines of vm_write32 / vm_write64 (runtime/ppu/ppu_loader.cpp), verbatim; fails on a tree without 7116.
python3 -I "$here/put_hook_lines.py" "$PT/runtime/ppu/ppu_loader.cpp" "$tmp/put_hook_lines.h"
"$cc" -std=gnu17 -O1 -w -I"$PT/include" -I"$PT/libs/video" -DRC_PUT_HOOK_LINES="\"$tmp/put_hook_lines.h\"" \
  "$here/test_gcm_put_snap.c" "$PT/runtime/platform/win32_compat.c" \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -pthread -o "$tmp/test_gcm_put_snap"
"$tmp/test_gcm_put_snap" on  > "$tmp/on.txt"  2>&1 || { cat "$tmp/on.txt";  echo "FAIL: mode on";  exit 1; }
GCM_PUT_SNAP=log "$tmp/test_gcm_put_snap" log > "$tmp/log.txt" 2>&1 || { cat "$tmp/log.txt"; echo "FAIL: mode log"; exit 1; }
GCM_PUT_SNAP=off "$tmp/test_gcm_put_snap" off > "$tmp/off.txt" 2>&1 || { cat "$tmp/off.txt"; echo "FAIL: mode off"; exit 1; }
# Patch 7118: the trace arms the hook with GCM_PUT_SNAP=0; both FFA-shaped stores print, the forward one (put G -> S)
# with the JUMP-back 0x20000008 at w@new, the restore (S -> G) with it at w@old.
GCM_PUT_SNAP=0 GCM_PUT_TRACE=on "$tmp/test_gcm_put_snap" trace > "$tmp/trace.txt" 2>&1 || { cat "$tmp/trace.txt"; echo "FAIL: mode trace"; exit 1; }
t1=$(grep -c '^\[put-trace\] #1 put 0x00000008 -> 0x00008008 get=0x00000008 .*w@new=0x20000008 ' "$tmp/trace.txt" || true)
t2=$(grep -c '^\[put-trace\] #2 put 0x00008008 -> 0x00000008 get=0x00008008 w@old=0x20000008 ' "$tmp/trace.txt" || true)
tn=$(grep -c '^\[put-trace\]' "$tmp/trace.txt" || true)
[[ "$t1" == 1 && "$t2" == 1 && "$tn" == 2 ]] || { cat "$tmp/trace.txt"; echo "FAIL: trace lines ($t1 $t2 $tn)"; exit 1; }
grep -h '^\[gcm_put_snap\] .* PASS\|^\[gcm-putsnap\] test:\|^\[put-trace\]' "$tmp/on.txt" "$tmp/log.txt" "$tmp/off.txt" "$tmp/trace.txt"
python3 -I "$here/test_put_store_route.py" "$PT"
echo "[run_gcm_put_snap_test] PASS"
