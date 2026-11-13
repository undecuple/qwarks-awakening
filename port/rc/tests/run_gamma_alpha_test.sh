#!/usr/bin/env bash
# Host test for R&C patch 7049 (texture gamma bit 3 decodes alpha). Usage: port/rc/tests/run_gamma_alpha_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_gamma_alpha_$$"
cc -std=c11 -O1 -w -I"$PT/libs/video" -I"$PT/include" "$here/test_gamma_alpha.c" "$PT/libs/video/rsx_fp_decompiler.c" -o "$bin" -lm
"$bin"
rm -f "$bin"
