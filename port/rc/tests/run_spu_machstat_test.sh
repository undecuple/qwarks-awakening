#!/usr/bin/env bash
# Host test for R&C patch 7105 (SPU_RdMachStat bit 31 = the tracked interrupt-enable state), two parts, each with a
# negative control that must FAIL (or the test cannot see the bug); the runner exits 0 only when both pass and both
# controls fail:
#  1. the REAL runtime: port/rc/tests/test_spu_machstat_channel.c linked with the patched tree's spu_channels.c,
#     spu_interp.c, spu_drain.c (as upstream's tools/test_spu_machine_status.py links its fixture): spu_rdch, the
#     interpreter's rdch + bi-family E/D bits, interrupt entry, the WWS job-API assert and the AddJob save/restore.
#     Control: the same binary with KZ3_SPU_MACHSTAT_IE=0 (the old "1 while running").
#  2. the pure model: runtime/spu/tests/test_spu_machstat.c (spu_machstat.h). Control: -DNO_7105.
# Usage: port/rc/tests/run_spu_machstat_test.sh [patched tree]   (or RC_P3R_TREE; default work/ps3recomp-patched)
#   nix develop .#ci --command port/rc/tests/run_spu_machstat_test.sh work/p7105/pt
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-${RC_P3R_TREE:-"$here/../../../work/ps3recomp-patched"}}
spu="$PT/runtime/spu"
grep -q "R&C patch 7105" "$spu/spu_channels.c" || { echo "FAIL: $PT has no patch 7105" >&2; exit 1; }
tmp="${TMPDIR:-/tmp}/rc_machstat_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${CC:-cc}"

echo "== 1. real runtime (test_spu_machstat_channel.c)"
"$cc" -std=gnu17 -O1 -UNDEBUG -w -I"$PT/include" -I"$spu" -I"$PT/runtime/platform" -I"$PT/runtime/diag" \
  "$here/test_spu_machstat_channel.c" \
  "$spu"/{spu_channels.c,spu_drain.c,spu_lockstep.c,spu_coherency.c,spu_workload.c,spurs_policy.c,spurs_job.c} \
  "$spu"/{spu_tsp_weak.c,spu_vm_pagemap.c,spurs_policy_blob_weak.c,spu_interp.c} \
  "$PT/runtime/platform/win32_compat.c" "$PT/runtime/diag/kz3_wait_acct.c" -pthread -lm -rdynamic -o "$tmp/chan"
clean_env=(env -u KZ3_SPU_MACHSTAT_IE -u KZ3_SPU_MACHSTAT_TRACE)
"${clean_env[@]}" "$tmp/chan"
if "${clean_env[@]}" KZ3_SPU_MACHSTAT_IE=0 "$tmp/chan" > "$tmp/chan.neg"; then
  cat "$tmp/chan.neg"
  echo "FAIL: negative control (KZ3_SPU_MACHSTAT_IE=0) passed: the real-runtime test does not detect the bug" >&2
  exit 1
fi
echo "negative control 1 (KZ3_SPU_MACHSTAT_IE=0) fails as expected:"
sed 's/^/  | /' "$tmp/chan.neg"

echo "== 2. model (runtime/spu/tests/test_spu_machstat.c)"
"$cc" -std=gnu17 -O1 -Wall -Wextra -Werror -I"$spu" "$spu/tests/test_spu_machstat.c" -o "$tmp/model"
"$tmp/model"
"$cc" -std=gnu17 -O1 -Wall -Wextra -Werror -DNO_7105 -I"$spu" "$spu/tests/test_spu_machstat.c" -o "$tmp/model.neg"
if "$tmp/model.neg" >/dev/null; then
  echo "FAIL: the negative control (-DNO_7105, the old '1 while running') passed: the model test does not detect the bug" >&2
  exit 1
fi
echo "negative control 2 (-DNO_7105) fails as expected"
echo "PASS run_spu_machstat_test"
