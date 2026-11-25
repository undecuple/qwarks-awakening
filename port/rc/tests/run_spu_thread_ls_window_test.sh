#!/usr/bin/env bash
# Host test for R&C patch 7115 (the SPU-thread LS window, 0xF0000000 + n*0x100000 + LSA), against the real runtime:
# port/rc/tests/test_spu_thread_ls_window.c linked with the patched tree's spu_channels.c, spurs_policy.c and the rest of
# the SPU link set (as run_spu_machstat_test.sh), built with ASan + UBSan. Negative control: the same test against a
# copy of the tree with 7115 reverse-applied (runtime/spu only) must FAIL; the runner exits 0 only when the test passes
# and the control fails.
# Usage: port/rc/tests/run_spu_thread_ls_window_test.sh [patched tree]   (or RC_P3R_TREE; default work/ps3recomp-patched)
#   nix develop .#ci --command port/rc/tests/run_spu_thread_ls_window_test.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-${RC_P3R_TREE:-"$here/../../../work/ps3recomp-patched"}}
patch=$(ls "$here"/../../patches/7115-*.patch)
grep -q "R&C patch 7115" "$PT/runtime/spu/spu_dma.h" || { echo "FAIL: $PT has no patch 7115" >&2; exit 1; }
tmp="${TMPDIR:-/tmp}/rc_tlsw_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${CC:-cc}"
san=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
# The runtime's own hot paths read unaligned guest words on purpose; UBSan's alignment check would flag those, not this.
san+=(-fno-sanitize=alignment)

build() {   # build <tree> <out>
  local t="$1" spu="$1/runtime/spu"
  "$cc" -std=gnu17 -O1 -g -UNDEBUG -w "${san[@]}" -I"$t/include" -I"$spu" -I"$t/runtime/platform" -I"$t/runtime/diag" \
    "$here/test_spu_thread_ls_window.c" \
    "$spu"/{spu_channels.c,spu_drain.c,spu_lockstep.c,spu_coherency.c,spu_workload.c,spurs_policy.c,spurs_job.c} \
    "$spu"/{spu_tsp_weak.c,spu_vm_pagemap.c,spurs_policy_blob_weak.c,spu_interp.c} \
    "$t/runtime/platform/win32_compat.c" "$t/runtime/diag/kz3_wait_acct.c" -pthread -lm -rdynamic -o "$2"
}
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1

echo "== test (patched tree, ASan + UBSan)"
build "$PT" "$tmp/t"
"$tmp/t" 2> "$tmp/t.err" || { cat "$tmp/t.err"; echo "FAIL: run_spu_thread_ls_window_test" >&2; exit 1; }
grep -E "\[spu-tlsw\]|runtime error|AddressSanitizer" "$tmp/t.err" | sed 's/^/  | /' || true
if grep -qE "runtime error|AddressSanitizer" "$tmp/t.err"; then echo "FAIL: sanitizer report" >&2; exit 1; fi

echo "== negative control (tree without 7115)"
# The tree by symlinks, with runtime/spu a real copy (the only directory the reverse apply touches).
mkdir -p "$tmp/neg/runtime"
for e in "$PT"/*; do [ "$(basename "$e")" = runtime ] || ln -s "$(cd "$e" 2>/dev/null && pwd || echo "$e")" "$tmp/neg/"; done
for e in "$PT"/runtime/*; do [ "$(basename "$e")" = spu ] || ln -s "$(cd "$e" 2>/dev/null && pwd || echo "$e")" "$tmp/neg/runtime/"; done
cp -r "$PT/runtime/spu" "$tmp/neg/runtime/"
# Reverse only the runtime/spu hunks (the lv2 hunk is in syscalls/, outside the link set).
( cd "$tmp/neg" && git apply -R --include='runtime/spu/*' "$patch" )
! grep -q "R&C patch 7115" "$tmp/neg/runtime/spu/spu_dma.h" || { echo "FAIL: reverse apply left 7115 in place" >&2; exit 1; }
build "$tmp/neg" "$tmp/neg.t"
if "$tmp/neg.t" > "$tmp/neg.out" 2>/dev/null; then
  cat "$tmp/neg.out"; echo "FAIL: negative control passed: the test does not detect the missing window" >&2; exit 1
fi
echo "negative control fails as expected:"
sed 's/^/  | /' "$tmp/neg.out"
echo "PASS run_spu_thread_ls_window_test"
