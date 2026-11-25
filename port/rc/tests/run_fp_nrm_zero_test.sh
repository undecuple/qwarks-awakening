#!/usr/bin/env bash
# Host test for R&C patch 7112 (FP decompiler NRM of a zero vector stays zero). Usage: port/rc/tests/run_fp_nrm_zero_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_fp_nrm_zero_$$"
cc -std=c11 -O1 -w -I"$PT/libs/video" -I"$PT/include" "$here/test_fp_nrm_zero.c" "$PT/libs/video/rsx_fp_decompiler.c" -o "$bin" -lm
"$bin"
rm -f "$bin"
