#!/usr/bin/env bash
# Host test for R&C patch 7017 (report readback with reused offsets). Usage: port/rc/tests/run_oq_readback_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_oq_readback_$$"
cc -std=c11 -O1 -Wall -Wextra -I"$PT/libs/video" "$here/test_oq_readback.c" -o "$bin"
"$bin"
