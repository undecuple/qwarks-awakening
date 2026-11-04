#!/usr/bin/env bash
# Host test for R&C patch 7036 (cube faces 128-byte aligned). Usage: port/rc/tests/run_cube_face_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_cube_face_$$"
cc -std=c11 -O1 -Wall -Wextra -I"$PT/libs/video" "$here/test_cube_face.c" -o "$bin"
"$bin"
rm -f "$bin"
