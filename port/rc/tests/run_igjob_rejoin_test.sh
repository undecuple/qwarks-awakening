#!/usr/bin/env bash
# Host test of tools/spu/lift_igjobs.py and patch 7039 (an interpreted policy module hands over to lifted code in verified
# resident spans, and back): test_igjob_rejoin.py lifts a synthetic job kernel and job blob, links them with the patched SPU
# runtime and runs an image -1 policy context through them. No game data. RC_P3R_TREE = a tree made by
# tools/ps3recomp-patched.sh (default work/ps3recomp-patched of this checkout). cc (CC). Exit 0 = PASS.
#   nix develop --command tools/sandbox.sh -- port/rc/tests/run_igjob_rejoin_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${RC_P3R_TREE:-$repo/work/ps3recomp-patched}"
[[ -f "$tree/runtime/spu/spu_interp.c" ]] || { echo "SKIP: no runtime tree at $tree"; exit 0; }
grep -q "R&C patch 7039" "$tree/runtime/spu/spu_interp.c" || { echo "SKIP: $tree has no patch 7039"; exit 0; }
python3 -I "$here/test_igjob_rejoin.py" "$tree"
echo "PASS run_igjob_rejoin_test"
