#!/usr/bin/env bash
# Reproducible PPU lift of one R&C game's decrypted EBOOT with ps3recomp + port/patches (adapted from the Killzone 3
# port's tools/lift.sh on 2026-10-19).
#
#   LOCK_EST_MIN=2 "$LOCK_SH" build -- nix develop .#re --command tools/sandbox.sh -- env RC_HELD_LOCKS=build tools/lift.sh [GAME]
#
# $LOCK_SH is the machine's shared lock helper (on the maintainer's machine, the Killzone 3 port's tools/agents/lock.sh;
# without one, run the inner command directly). The lift takes a build slot from outside the sandbox (the lock
# directory is not mounted inside), so it never lands next to another project's measured run. GAME (default tods) picks the ELF, code end, TOC
# and directories:
#   tods: work/bin/tods/EBOOT.elf  code end 0x83a50c (end of the last executable section, .text group ends there and
#         read-only data starts)  TOC 0x88ff38 (the entry OPD's TOC; the EBOOT has three TOCs, pass 4 of the survey,
#         each function's own OPD carries its TOC)  -> work/analysis/tods, work/recomp/tods
#   acit: code end 0xa4aebc, TOC 0xd349c8; nexus: code end 0xb60114, TOC 0xd8ee08 (2026-10-26, same rule: end of the
#         last SHF_EXECINSTR section, the entry OPD's TOC)  -> work/analysis/<game>, work/recomp/<game>
#   acit120 / nexus101 / ffa (ELF work/bin/ffa105) / a4o (work/bin/a4o103): the update executables (2026-11-03), code end and TOC read the same way.
#   RC_ROOT: the checkout whose work/ holds ELF, patched tree and outputs (a lane worktree runs its script against the main checkout's work/);
#   RC_SKIP_PATCHED=1: do not regenerate work/ps3recomp-patched.
# Steps: work/ps3recomp-patched fresh (tools/ps3recomp-patched.sh); ppu_loader + find_functions when
# functions.json is missing; ppu_lifter with --code-end, --overrides when port/rc/<game>/ppu_overrides.txt exists, and
# --chunks 64 (patch 3005: stable address-keyed chunks); lift_audit into <analysis>/lift_audit.log.
# Needs the decrypted EBOOT the player made from their own copy.
set -euo pipefail
echo 1000 > /proc/self/oom_score_adj 2>/dev/null || true   # first OOM victim, not the editor
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# RC_ROOT (default: this checkout) holds work/ (inputs, patched tree, outputs): a lane worktree's script runs against the main checkout's work/
WORK="${RC_ROOT:-$ROOT}"
cd "$WORK"
case ",${RC_HELD_LOCKS:-}," in
  *,build,*) ;;
  *) echo "lift.sh: warning: no build slot held (run it under KZ3's lock.sh build, see the header)" >&2 ;;
esac
GAME="${1:-tods}"; BIN=""
PT="${RC_PATCHED_DIR:-work/ps3recomp-patched}"   # RC_PATCHED_DIR / RC_ANALYSIS_DIR / RC_RECOMP_DIR: a side-by-side lift (v0.16.0 bump check)
case "$GAME" in
  tods) CODE_END=0x83a50c; TOC=0x88ff38 ;;
  acit) CODE_END=0xa4aebc; TOC=0xd349c8 ;;
  nexus) CODE_END=0xb60114; TOC=0xd8ee08 ;;
  # updated executables from the official update packages (2026-11-03): same rule, own ELF
  acit120) CODE_END=0xa4ab94; TOC=0xd347b0 ;;
  nexus101) CODE_END=0xb61d44; TOC=0xd8ee18 ;;
  ffa|ffa105) GAME=ffa; BIN=ffa105; CODE_END=0xc3a1ac; TOC=0xe6ef98 ;;
  a4o|a4o103) GAME=a4o; BIN=a4o103; CODE_END=0xbd2c48; TOC=0xdfd988 ;;
  *) echo "lift.sh: unknown game '$GAME' (tods acit nexus acit120 nexus101 ffa105 a4o103)" >&2; exit 2 ;;
esac
ELF=work/bin/${BIN:-$GAME}/EBOOT.elf AN="${RC_ANALYSIS_DIR:-work/analysis/$GAME}" OUT="${RC_RECOMP_DIR:-work/recomp/$GAME}" OVR="$ROOT/port/rc/$GAME/ppu_overrides.txt"
[[ -f "$ELF" ]] || { echo "missing $ELF (the player's own decrypted EBOOT)" >&2; exit 1; }

[[ "${RC_SKIP_PATCHED:-0}" = 1 ]] || "$ROOT/tools/ps3recomp-patched.sh"   # RC_SKIP_PATCHED=1: use the existing work/ps3recomp-patched as is

mkdir -p "$AN" "$OUT"
if [[ ! -f "$AN/functions.json" ]]; then
  python3 "$PT/tools/ppu_loader.py" "$ELF" -o "$AN/"
  python3 "$PT/tools/find_functions.py" "$ELF" --seed-json "$AN/EBOOT.functions.json" --output "$AN/functions.json"
fi
ovr=(); [[ -f "$OVR" ]] && ovr=(--overrides "$OVR")
RC_LIFT_CHUNKS="${RC_LIFT_CHUNKS:-64}"
nice -n 10 python3 "$PT/tools/ppu_lifter.py" "$ELF" --functions "$AN/functions.json" \
  --hle-stubs "$AN/EBOOT.imports.json" --toc "$TOC" --code-end "$CODE_END" \
  "${ovr[@]}" -j 4 --chunks "$RC_LIFT_CHUNKS" --output "$OUT" > "$OUT/lift.log" 2>&1
tail -5 "$OUT/lift.log"
printf 'game=%s elf=%s overrides=%s patchset=%s chunks=%s date=%s\n' "$GAME" "$(sha256sum "$ELF" | cut -c1-16)" \
  "$( [[ -f "$OVR" ]] && sha256sum "$OVR" | cut -c1-16 || echo none)" "$(cat "$PT/.rc-patchset" 2>/dev/null)" \
  "$RC_LIFT_CHUNKS" "$(date -Is)" > "$OUT/.lift-stamp"

python3 "$PT/tools/lift_audit.py" --elf "$ELF" --functions "$AN/functions.json" \
  --recomp-dir "$OUT" --baseline "$AN/none.json" > "$AN/lift_audit.log" 2>&1 || true
sed -n '/=== Pass 1/,/TOTAL \/\* unresolved/p' "$AN/lift_audit.log"
