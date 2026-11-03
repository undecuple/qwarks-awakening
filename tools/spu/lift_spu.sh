#!/usr/bin/env bash
# Lift every SPU image of a title into work/spu/<game>/lift, the SPU_DIR tools/build-rc.sh points the build at.
#   nix develop .#re --command tools/sandbox.sh -- tools/spu/lift_spu.sh [GAME]      (GAME default: tods)
# 1. tools/spu/spu_lift_tods.py: the 21 WWS job modules, the EDGE inflate ELF, the policy module, spu_workloads.c,
#    jobmods_register.c (it recreates the lift dir, so it runs first);
# 2. tools/spu/lift_raw_spu.py: the raw SPU programs (igcollspu, igCollBroadSpu) and rawspu_register.c.
# Needs work/bin/<game>/EBOOT.elf and work/ps3recomp-patched (tools/ps3recomp-patched.sh). Writes only under work/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GAME="${1:-tods}"
[[ "$GAME" == tods ]] || { echo "lift_spu.sh: only tods has SPU tables so far" >&2; exit 2; }
ELF="${RC_EBOOT_ELF:-$ROOT/work/bin/$GAME/EBOOT.elf}"
PS3="$ROOT/work/ps3recomp-patched"
OUT="$ROOT/work/spu/$GAME"
[[ -f "$ELF" ]] || { echo "lift_spu.sh: no $ELF" >&2; exit 1; }
[[ -d "$PS3/tools" ]] || { echo "lift_spu.sh: run tools/ps3recomp-patched.sh first" >&2; exit 1; }
mkdir -p "$OUT"
nice -n 19 python3 -I "$ROOT/tools/spu/spu_lift_tods.py" "$ELF" "$PS3" "$OUT" >"$OUT.lift.log"
nice -n 19 python3 -I "$ROOT/tools/spu/lift_raw_spu.py" "$ELF" "$PS3" "$OUT/lift" "$ROOT/tools/spu/${GAME}_raw_spu.json" \
  >"$OUT/lift/rawspu.log"
echo "lift_spu.sh: $(ls "$OUT"/lift/*/spu_recomp.c | wc -l) lifted images in $OUT/lift; registration TUs: $(cd "$OUT/lift" && ls *.c | tr '\n' ' ')"
