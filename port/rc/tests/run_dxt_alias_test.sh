#!/usr/bin/env bash
# Host test for R&C patch 7035 (a DXT texture never samples a render surface). Usage: port/rc/tests/run_dxt_alias_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_dxt_alias_$$"
cc -std=c11 -O1 -Wall -Wextra -I"$PT/libs/video" "$here/test_dxt_alias.c" -o "$bin"
"$bin"
rm -f "$bin"
