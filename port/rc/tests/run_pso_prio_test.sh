#!/usr/bin/env bash
# Host test for R&C patch 7052 (demand-first PSO build queue). Usage: port/rc/tests/run_pso_prio_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_pso_prio_$$"
cc -std=c11 -O1 -Wall -Wextra -I"$PT/libs/video" "$here/test_pso_prio.c" -o "$bin"
"$bin"
rm -f "$bin"
