#!/usr/bin/env bash
# Host test for R&C patch 7068 (INLINE_ARRAY buffer grows). Usage: port/rc/tests/run_inline_grow_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_inline_grow_$$"
cc -std=gnu11 -O1 -w -I"$PT/libs/video" -I"$PT/include" "$here/test_inline_grow.c" "$PT/libs/video/rsx_dispatch.c" -o "$bin" -lm
"$bin"
rm -f "$bin"
