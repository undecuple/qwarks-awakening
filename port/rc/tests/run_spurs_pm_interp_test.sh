#!/usr/bin/env bash
# Host test of patch 7034 (cellSpurs: an unlifted policy module runs in the SPU interpreter; cellSpursGetInfo's SPU thread
# ids and their SNR writes): a unity build of the patched libs/spurs/cellSpurs.c + runtime/spu/spurs_policy.c with
# test_spurs_pm_interp.c, run twice (default, and RC_SPURS_PM_INTERP=0). RC_P3R_TREE = a tree made by
# tools/ps3recomp-patched.sh (default work/rt-7034 of this checkout). gcc (CC). Exit 0 = PASS.
#   nix develop --command tools/sandbox.sh -- env RC_P3R_TREE=work/rt-7034 port/rc/tests/run_spurs_pm_interp_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${RC_P3R_TREE:-$repo/work/rt-7034}"
[[ -f "$tree/libs/spurs/cellSpurs.c" ]] || { echo "SKIP: no runtime tree at $tree"; exit 0; }
grep -q "R&C patch 7034" "$tree/libs/spurs/cellSpurs.c" || { echo "SKIP: $tree has no patch 7034"; exit 0; }
tmp="${TMPDIR:-/tmp}/rcpmi_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${CC:-gcc}"
echo "== test_spurs_pm_interp.c"
"$cc" -std=gnu11 -O1 -w -pthread -I"$tree/include" -I"$tree" -I"$tree/runtime/spu" -I"$here" ${RC_TEST_CFLAGS:-} \
   -Wl,--unresolved-symbols=ignore-all -o "$tmp/t" "$here/test_spurs_pm_interp.c"
"$tmp/t"
RC_SPURS_PM_INTERP=0 "$tmp/t" off
echo "PASS run_spurs_pm_interp_test"
