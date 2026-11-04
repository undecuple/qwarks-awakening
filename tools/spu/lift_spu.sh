#!/usr/bin/env bash
# Lift every SPU image of a title into work/spu/<game>/lift, the SPU_DIR tools/build-rc.sh points the build at.
#   nix develop .#re --command tools/sandbox.sh -- tools/spu/lift_spu.sh [GAME]      (GAME: tods (default), acit, nexus, acit120, nexus101, ffa, a4o, qfb; dlhd and collection* have tables but no policy module yet)
# 1. tools/spu/spu_lift.py GAME: the WWS job modules, the EDGE inflate ELF (when the title has one), the policy module, spu_workloads.c,
#    jobmods_register.c (it recreates the lift dir, so it runs first); tables: data/titles/<game>_spu_{config,jobs,code}.json
#    and the optional lift table named in the config;
# 2. tools/spu/lift_raw_spu.py: the raw SPU programs of tools/spu/<game>_raw_spu.json (ToD: igcollspu, igCollBroadSpu) and rawspu_register.c;
#    a table with no programs (ACiT, Nexus) writes only a manifest;
# 3. tools/spu/lift_igjobs.py, when data/titles/<game>_igjobs.json exists (acit120, nexus101, a4o, ffa): Insomniac's igPm job kernel and job
#    blobs as verified streamed regions (patch 7039) and igjobs_register.c.
# Needs work/bin/<game>/EBOOT.elf and work/ps3recomp-patched (tools/ps3recomp-patched.sh). Writes only under work/.
# RC_SPU_OUT (default work/spu/<game>) redirects the output, e.g. to a scratch dir for a byte-identity check against an earlier lift.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GAME="${1:-tods}"
# data tables come from the checkout holding this script; inputs (EBOOT, patched tree, output) from RC_ROOT (default: the same checkout)
WORK="${RC_ROOT:-$ROOT}"
[[ -f "$ROOT/data/titles/${GAME}_spu_config.json" ]] || { echo "lift_spu.sh: no SPU tables for '$GAME' (data/titles/${GAME}_spu_config.json)" >&2; exit 2; }
case "$GAME" in ffa) BIN=ffa105 ;; a4o) BIN=a4o103 ;; *) BIN="$GAME" ;; esac   # update executables live under their package version
# Collection: tables collection (launcher EBOOT.elf), collection_rc1/2/3 (work/bin/collection/RC<n>.ppu.elf)
case "$GAME" in collection_rc?) BIN=collection; RCN="${GAME#collection_}"; RCN="${RCN^^}"; ELFNAME="$RCN.ppu.elf" ;; *) ELFNAME=EBOOT.elf ;; esac
ELF="${RC_EBOOT_ELF:-$WORK/work/bin/$BIN/$ELFNAME}"
# a separate git worktree has no work/bin of its own: fall back to the main checkout's (RC_MAIN names it)
[[ -f "$ELF" || -z "${RC_MAIN:-}" ]] || ELF="$RC_MAIN/work/bin/$BIN/$ELFNAME"
PS3="$WORK/work/ps3recomp-patched"
OUT="${RC_SPU_OUT:-$WORK/work/spu/$GAME}"
[[ -f "$ELF" ]] || { echo "lift_spu.sh: no $ELF" >&2; exit 1; }
[[ -d "$PS3/tools" ]] || { echo "lift_spu.sh: run tools/ps3recomp-patched.sh first" >&2; exit 1; }
mkdir -p "$OUT"
nice -n 19 python3 -I "$ROOT/tools/spu/spu_lift.py" "$GAME" "$ELF" "$PS3" "$OUT" >"$OUT.lift.log"
nice -n 19 python3 -I "$ROOT/tools/spu/lift_raw_spu.py" "$ELF" "$PS3" "$OUT/lift" "$ROOT/tools/spu/${GAME}_raw_spu.json" \
  >"$OUT/lift/rawspu.log"
if [[ -f "$ROOT/data/titles/${GAME}_igjobs.json" ]]; then
  nice -n 19 python3 -I "$ROOT/tools/spu/lift_igjobs.py" "$ELF" "$PS3" "$OUT/lift" "$ROOT/data/titles/${GAME}_igjobs.json" \
    >"$OUT/lift/igjobs.log"
fi
nice -n 19 python3 -I "$ROOT/tools/spu/lift_summary.py" "$OUT/lift" "$OUT/lift_summary.json" >"$OUT/lift_summary.log"
echo "lift_spu.sh: $(ls "$OUT"/lift/*/spu_recomp.c | wc -l) lifted images in $OUT/lift; registration TUs: $(cd "$OUT/lift" && ls *.c | tr '\n' ' ')"
