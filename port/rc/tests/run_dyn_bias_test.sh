#!/usr/bin/env bash
# Host test for R&C patch 7016 (dynamic depth bias, PSO key). Usage: port/rc/tests/run_dyn_bias_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_dyn_bias_$$"
cc -std=c11 -O1 -Wall -Wextra -I"$PT/libs/video" "$here/test_dyn_bias.c" -o "$bin"
"$bin"
