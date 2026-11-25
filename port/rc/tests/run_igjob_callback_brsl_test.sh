#!/usr/bin/env bash
# Host test of R&C patch 7106 (spu_lifter.py --callback-brsl through tools/spu/lift_igjobs.py): test_igjob_callback_brsl.py
# lifts a synthetic ig job kernel + job with the packer idiom (`brsl $r77, packer`; the packer calls r77 back and leaves by
# `bi $r0`), links them with the patched SPU runtime and runs them. Negative control inside: the same job lifted with
# "callback_brsl": false must leak (7099 unwind at n = 10, [spu-depth] halt at n = 1500); the test fails if it does not.
# No game data. RC_P3R_TREE = a tree made by tools/ps3recomp-patched.sh (default work/ps3recomp-patched). cc (CC). Exit 0 = PASS.
#   nix develop .#ci --command port/rc/tests/run_igjob_callback_brsl_test.sh [tree]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${1:-${RC_P3R_TREE:-$repo/work/ps3recomp-patched}}"
[[ -f "$tree/runtime/spu/spu_interp.c" ]] || { echo "FAIL: no runtime tree at $tree" >&2; exit 1; }
grep -q "R&C patch 7106" "$tree/tools/spu_lifter.py" || { echo "FAIL: $tree has no patch 7106" >&2; exit 1; }
python3 -I "$here/test_igjob_callback_brsl.py" "$tree"
echo "PASS run_igjob_callback_brsl_test"
