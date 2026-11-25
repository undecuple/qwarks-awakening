#!/usr/bin/env bash
# Host test of patch 7111 (cellSpursEventFlagWait: a second PPU waiter on a flag gets CELL_SPURS_TASK_ERROR_BUSY): a unity
# build of the patched libs/spurs/cellSpurs.c with test_spurs_eventflag_busy.c (two PPU waiter threads). RC_P3R_TREE = a
# tree made by tools/ps3recomp-patched.sh (default work/ps3recomp-patched of this checkout). gcc (CC). Exit 0 = PASS.
# A tree without patch 7111 FAILs (the second waiter blocks), it does not SKIP.
#   nix develop .#ci --command port/rc/tests/run_spurs_eventflag_busy_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${RC_P3R_TREE:-$repo/work/ps3recomp-patched}"
[[ -f "$tree/libs/spurs/cellSpurs.c" ]] || { echo "FAIL: no runtime tree at $tree"; exit 1; }
tmp="${TMPDIR:-/tmp}/rcefbusy_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${CC:-gcc}"
echo "== test_spurs_eventflag_busy.c"
"$cc" -std=gnu11 -O1 -w -pthread -I"$tree/include" -I"$tree" -I"$tree/runtime/spu" -I"$here" ${RC_TEST_CFLAGS:-} \
   -ffunction-sections -fdata-sections -Wl,--gc-sections \
   -o "$tmp/t" "$here/test_spurs_eventflag_busy.c" "$here/rc_spurs_host_support.c"
"$tmp/t"
echo "PASS run_spurs_eventflag_busy_test"
