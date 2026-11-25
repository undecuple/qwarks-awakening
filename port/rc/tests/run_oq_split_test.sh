#!/usr/bin/env bash
# Host test for R&C patch 2173 (chunk submits split an open ZPASS query). Usage: port/rc/tests/run_oq_split_test.sh [patched tree]
# Negative control: red on a tree without 2173 (the old refusal line is still in rsx_live_draw.c, or rsx_oq_split.h is
# missing). Mutation check: the test must fail when the header refuses under an open query (OQS_MUTANT_REFUSE) and when
# the index-reuse guard tests the last host segment (OQS_MUTANT_LAST).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
src="$PT/libs/video/rsx_live_draw.c"
[ -f "$src" ] || { echo "FAIL no $src"; exit 1; }
if grep -qF 'if (!g.ready || !g_ld_unsubmitted || g_oq.open) return;' "$src"; then
    echo "FAIL tree still refuses a chunk under an open query (patch 2173 not applied): $src"; exit 1
fi
[ -f "$PT/libs/video/rsx_oq_split.h" ] || { echo "FAIL rsx_oq_split.h missing (patch 2173 not applied)"; exit 1; }
grep -qF 'oqs_gate(g.ready, g_ld_unsubmitted, g_oq.open, g_oq.split)' "$src" ||
    { echo "FAIL ld_submit_nowait does not use the 2173 gate"; exit 1; }
bin="${TMPDIR:-/tmp}/test_oq_split_$$"
trap 'rm -f "$bin" "$bin.m1" "$bin.m2" "$bin.out"' EXIT
cc -std=c11 -O1 -Wall -Wextra -I"$PT/libs/video" "$here/test_oq_split.c" -o "$bin"
"$bin"
for m in OQS_MUTANT_REFUSE OQS_MUTANT_LAST; do
    cc -std=c11 -O1 -Wall -Wextra -D$m -I"$PT/libs/video" "$here/test_oq_split.c" -o "$bin.m1"
    if "$bin.m1" > "$bin.out" 2>&1; then echo "FAIL mutant $m passed (the test does not see it)"; exit 1; fi
    echo "mutant $m: caught ($(grep -c '^FAIL ' "$bin.out") failing checks)"
done
echo "PASS run_oq_split_test"
