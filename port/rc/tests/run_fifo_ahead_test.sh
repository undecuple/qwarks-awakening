#!/usr/bin/env bash
# Host test for R&C patch 7050 (FIFO lookahead decode). Usage: port/rc/tests/run_fifo_ahead_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_fifo_ahead_$$"
cc -std=c11 -O1 -Wall -Wextra -I"$PT/libs/video" "$here/test_fifo_ahead.c" -o "$bin"
"$bin"
rm -f "$bin"
