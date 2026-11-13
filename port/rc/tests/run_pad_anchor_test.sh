#!/usr/bin/env bash
# Host test for R&C patch 7062 (PAD_SCRIPT "@fs:" anchors). Cuts cellPad.c's script parser and player (from the
# pad_script_anchor.h include to the end of pad_script_read_file) out of the patched tree and runs them.
# Usage: port/rc/tests/run_pad_anchor_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/pad_anchor_XXXXXX")
trap 'rm -rf "$tmp"' EXIT
awk '/^#include "pad_script_anchor.h"/{on=1} /^\/\* HLE args are GUEST/{on=0} on' "$PT/libs/input/cellPad.c" > "$tmp/pad_script_cut.inc"
grep -q 'pad_script_poll' "$tmp/pad_script_cut.inc" || { echo "FAIL: could not cut the script player out of cellPad.c"; exit 1; }
cc -std=c11 -O1 -Wall -Wextra -Wno-unused-function -I"$tmp" -I"$PT/libs/input" "$here/test_pad_anchor.c" -o "$tmp/t"
"$tmp/t" "$here/../../../tools/pad/tods-newgame-anchored.txt"
