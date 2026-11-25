#!/usr/bin/env bash
# Host test of R&C patch 7120 (was 7063 on rc-sse41; SPU: an interpreter handoff stops at the innermost open call
# bracket's return address): runs the patched tree's tools/test_spu_interpreter_handoff.py, which builds
# runtime/spu/tests/test_spu_interpreter_handoff.c against the real SPU runtime and runs every mode (KZ3 1069's
# interpreter fallback and the SMC microstep handoff), checks the "[spu] owned-stop (R&C 7120)" note, and runs the
# RC_SPU_OWNED_STOP=0 controls of both handoffs, which must fail. RC_P3R_TREE = a tree made by
# tools/ps3recomp-patched.sh (default work/ps3recomp-patched of this checkout). cc (CC). Exit 0 = PASS.
#   nix develop --command tools/sandbox.sh -- port/rc/tests/run_spu_interp_handoff_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${RC_P3R_TREE:-$repo/work/ps3recomp-patched}"
[[ -f "$tree/tools/test_spu_interpreter_handoff.py" ]] || { echo "SKIP: no runtime tree at $tree"; exit 0; }
src=$(cat "$tree/runtime/spu/spu_channels.c")
[[ "$src" == *"R&C patch 7120"* ]] || { echo "SKIP: $tree has no patch 7120"; exit 0; }
echo "== test_spu_interpreter_handoff.py"
python3 "$tree/tools/test_spu_interpreter_handoff.py"
echo "PASS run_spu_interp_handoff_test"
