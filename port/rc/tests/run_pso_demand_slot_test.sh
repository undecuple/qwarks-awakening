#!/usr/bin/env bash
# Host test for R&C patch 7082 (a PSO miss waits for its own slot). Usage: port/rc/tests/run_pso_demand_slot_test.sh [patched tree]
# 1. the model test against the shipped rsx_pso_slot.h must pass;
# 2. the same cases against a copy of the pre-7082 tail (-DRC_PSO_TAIL_PRE7082) must fail (the test catches the bug);
# 3. the patched get_pso must hand the slot it filled to rsx_pso_demand_tail and no longer await g.n_psos - 1u.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_pso_demand_slot_$$"
trap 'rm -f "$bin" "$bin.pre"' EXIT
cc -std=c11 -O1 -Wall -Wextra -Werror -I"$PT/libs/video" "$here/test_pso_demand_slot.c" -o "$bin"
"$bin"
cc -std=c11 -O1 -Wall -Wextra -Werror -DRC_PSO_TAIL_PRE7082 -I"$PT/libs/video" "$here/test_pso_demand_slot.c" -o "$bin.pre"
if "$bin.pre" 2>/dev/null; then echo "FAIL: the pre-7082 tail passes the cases (the test would not catch the bug)" >&2; exit 1; fi
echo "negative control (pre-7082 tail): fails as expected"
ld="$PT/libs/video/rsx_live_draw.c"
if grep -q 'ld_apso_await(g\.n_psos - 1u)' "$ld"; then echo "FAIL: $ld still awaits g.n_psos - 1u" >&2; exit 1; fi
grep -q 'const u32 slot = g\.n_psos;' "$ld" || { echo "FAIL: get_pso does not take its slot before the store" >&2; exit 1; }
grep -q 'rsx_pso_demand_tail(slot, g_ld_ahead_active, ld_ahead_scan, ld_apso_await_demand)' "$ld" \
  || { echo "FAIL: get_pso does not call rsx_pso_demand_tail with its slot" >&2; exit 1; }
echo "get_pso wiring: ok"
