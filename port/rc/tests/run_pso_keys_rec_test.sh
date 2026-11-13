#!/usr/bin/env bash
# Host test for R&C patch 7051 (PSO key file: GPU-fetch records, per-key append survives a kill).
# Usage: port/rc/tests/run_pso_keys_rec_test.sh [patched tree]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
bin="${TMPDIR:-/tmp}/test_pso_keys_rec_$$"
cc -std=c11 -O1 -Wall -Wextra -I"$PT/libs/video" "$here/test_pso_keys_rec.c" -o "$bin"
"$bin"
rm -f "$bin"
