#!/usr/bin/env bash
# Host test for R&C patch 7108 (FP decompiler: WPOS.w is 1/w). Usage: port/rc/tests/run_fp_wpos_w_test.sh [patched tree]
# Text checks on the emitted HLSL, then the helper macros evaluated as C for the FXC path (SV_POSITION.w = clip w)
# and the glslang path (RSX_FRAGCOORD_W_IS_RCP, FragCoord.w = 1/w), buffered and unbuffered.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-"$here/../../../work/ps3recomp-patched"}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/test_fp_wpos_w.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -O1 -w -I"$PT/libs/video" -I"$PT/include" "$here/test_fp_wpos_w.c" "$PT/libs/video/rsx_fp_decompiler.c" \
   -o "$tmp/text" -lm
rc=0
"$tmp/text" "$tmp" || rc=1
for v in buf unbuf; do
  for mode in fxc glslang; do
    if [ "$mode" = fxc ]; then defs=(-DHOST_W=4.0f); else defs=(-DHOST_W=0.25f -DRSX_FRAGCOORD_W_IS_RCP=1); fi
    printf '%s %s: ' "$v" "$mode"
    if cc -std=c11 -O1 -w "${defs[@]}" -include "$tmp/wpos_$v.h" "$here/test_fp_wpos_w_eval.c" -o "$tmp/eval" 2>"$tmp/cc.err"; then
      "$tmp/eval" || rc=1
    else
      echo "FAIL: helper macros do not build as C"; sed 's/^/  /' "$tmp/cc.err" | head -5; rc=1
    fi
  done
done
[ "$rc" = 0 ] && echo "fp wpos.w test: ok" || echo "fp wpos.w test: FAIL"
exit "$rc"
