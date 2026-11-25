#!/usr/bin/env bash
# Host test for R&C patch 7110 (cellSysCacheMount: OK_RELAYED keeps the cache for the same non-empty id after a session
# that ended; another id, an empty id, no cache or a killed session empties PS3_HDD1_ROOT/cache and answers OK_CLEARED;
# ids outside A-Z 0-9 - _ are CELL_SYSCACHE_ERROR_PARAM). Runs the patch's libs/system/tests/test_syscache_mount.c
# (sessions = forked children; _exit = a killed run) linked with the tree's cellSysutil.c.
# Negative control: RC_SYSCACHE_CONTROL_TREE=<a tree without 7110> links the same test with that tree's cellSysutil.c
# and must FAIL (or the test cannot see the bug).
# Usage: port/rc/tests/run_syscache_mount_test.sh [patched tree]   (or RC_P3R_TREE; default work/ps3recomp-patched)
#   nix develop .#ci --command port/rc/tests/run_syscache_mount_test.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-${RC_P3R_TREE:-"$here/../../../work/ps3recomp-patched"}}
grep -q "RC patch 7110" "$PT/libs/system/cellSysutil.c" || { echo "FAIL: $PT has no patch 7110" >&2; exit 1; }
tmp="${TMPDIR:-/tmp}/rc_syscache_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${CC:-cc}"
build() {   # build OUT SYSUTIL_C
  "$cc" -std=gnu17 -O1 -UNDEBUG -ffunction-sections -fdata-sections -I"$PT/include" \
    "$PT/libs/system/tests/test_syscache_mount.c" "$2" -pthread -Wl,--gc-sections -o "$1"
}
build "$tmp/t" "$PT/libs/system/cellSysutil.c"
env -u PS3_HDD1_ROOT TMPDIR="$tmp" "$tmp/t" | grep -v '^\[cellSysutil\]'
if [[ -n "${RC_SYSCACHE_CONTROL_TREE:-}" ]]; then
  ctl="$RC_SYSCACHE_CONTROL_TREE/libs/system/cellSysutil.c"
  grep -q "RC patch 7110" "$ctl" && { echo "FAIL: the control tree $RC_SYSCACHE_CONTROL_TREE has patch 7110" >&2; exit 1; }
  build "$tmp/ctl" "$ctl"
  if env -u PS3_HDD1_ROOT TMPDIR="$tmp" "$tmp/ctl" > "$tmp/ctl.out"; then
    grep -v '^\[cellSysutil\]' "$tmp/ctl.out"
    echo "FAIL: negative control (cellSysutil.c without 7110) passed: the test does not detect the bug" >&2
    exit 1
  fi
  echo "negative control (cellSysutil.c without 7110) fails as expected:"
  grep -E '^\[FAIL\]|^FAIL' "$tmp/ctl.out" | sed 's/^/  | /'
fi
echo "PASS run_syscache_mount_test"
