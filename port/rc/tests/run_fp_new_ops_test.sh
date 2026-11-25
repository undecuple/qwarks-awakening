#!/usr/bin/env bash
# Host test for R&C patch 7067 (FP decompiler STR/SFL/DST/LIT/DP2A/RFL). Usage: port/rc/tests/run_fp_new_ops_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_fp_new_ops_$$"
cc -std=c11 -O1 -w -I"$PT/libs/video" -I"$PT/include" "$here/test_fp_new_ops.c" "$PT/libs/video/rsx_fp_decompiler.c" -o "$bin" -lm
"$bin"
rm -f "$bin"
