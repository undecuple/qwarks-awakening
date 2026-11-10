#!/usr/bin/env bash
# Memory-safe Windows build of one R&C game (port/rc, RC_TITLE, default tods) on the machine shared with the other ports.
# From the Toro Lets Party port's tools/build-tlp.sh at a46f947 (itself KZ2 bffa40a / KZ3 78bf574 tools/build-kz3.sh;
# RC_* -> RC_*). Take a build slot outside the sandbox (shared-docs/locks.md; the lock directory is not mounted inside):
#   LOCK_EST_MIN=25 "$LOCK_SH" build -- nix develop .#win --command tools/sandbox.sh -- env RC_HELD_LOCKS=build tools/build-rc.sh build/rc-tods-win
# ($LOCK_SH: the machine's shared lock helper, the Killzone 3 port's tools/agents/lock.sh here.)
# Inputs: $RC_OUT/ps3recomp-patched (tools/ps3recomp-patched.sh) and $RC_OUT/recomp/<game> (tools/lift.sh); RC_OUT
# defaults to work.
# Memory (KZ3's measurements, 2026-10-08..11): a cc1plus on a 64-chunk lifted file peaks at 0.7-1.3 GB (up to 5.3 GB on
# some); -j8/-j20 next to Proton ran the 31 GB machine out of memory three times. So: jobs = (MemAvailable - 8 GB
# reserve) / RC_BUILD_JOB_MB (default 2048) / build slots in use, 1..RC_BUILD_JOBS_MAX (default 6); oom_score_adj 1000.
set -euo pipefail
DIR="${1:?usage: build-rc.sh BUILD_DIR [cmake --build args]}"; shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GAME="${RC_TITLE:-tods}"   # RC_GAME is the sandbox's game-disc path, not a title
EXE="$GAME"   # the exe's name: the title, except update lifts (acit120 -> acit.exe, nexus101 -> nexus.exe)
case "$GAME" in acit120) EXE=acit ;; nexus101) EXE=nexus ;; collection_rc[123]) EXE="${GAME#collection_}" ;; esac   # the Collection's games: rc1.exe..rc3.exe (build_profiles.json)
OUT="${RC_OUT:-work}"; case "$OUT" in /*) ;; *) OUT="$ROOT/$OUT" ;; esac
PT="$OUT/ps3recomp-patched"
MAINC="$ROOT"; [[ -f "$ROOT/.rc-main" ]] && MAINC="$(cat "$ROOT/.rc-main")"
LOCKS="${RC_LOCK_DIR:-$(cat "$MAINC/.rc-lockdir" 2>/dev/null || echo "$MAINC/work/locks")}"
[[ "$LOCKS" == "$MAINC/work/locks" ]] && mkdir -p "$LOCKS"   # only the repo's own lock dir is created
[[ -d "$LOCKS" ]] || { echo "build-rc: $LOCKS missing (lock directory from .rc-lockdir)" >&2; exit 2; }
# Refuse a patched toolkit older than the patch set (RC_ALLOW_STALE_PATCHES=1 overrides).
[[ -f "$PT/CMakeLists.txt" ]] || { echo "build-rc: no $PT: run tools/ps3recomp-patched.sh first" >&2; exit 3; }
if [[ "${RC_ALLOW_STALE_PATCHES:-0}" != 1 ]]; then
  want="$(cat "$ROOT"/port/patches/*.patch | sha256sum | cut -c1-16)-$(git -C "$ROOT/third_party/ps3recomp" rev-parse --short HEAD 2>/dev/null)"
  have="$(cat "$PT/.rc-patchset" 2>/dev/null || true)"
  [[ "$have" == "$want" ]] || { echo "build-rc: $PT is stale ($have, want $want): run tools/ps3recomp-patched.sh first" >&2; exit 3; }
fi
# Release builds (the version comes from the tag): RC_RELEASE_TAG=vX.Y.Z must be a plain vMAJOR.MINOR.PATCH tag
# and equal the exe's version, the project VERSION in port/rc/CMakeLists.txt (tools/release/bump_version.py keeps it
# equal to names.VERSION). Unset (every dev build) = no check.
if [[ -n "${RC_RELEASE_TAG:-}" ]]; then
  [[ "$RC_RELEASE_TAG" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)$ ]] || { echo "build-rc: RC_RELEASE_TAG '$RC_RELEASE_TAG' is not vMAJOR.MINOR.PATCH" >&2; exit 3; }
  tagv="${BASH_REMATCH[1]}"
  exev="$(sed -nE 's/^[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)[[:space:]]*$/\1/p' "$ROOT/port/rc/CMakeLists.txt" | head -1)"
  [[ "$tagv" == "$exev" ]] || { echo "build-rc: tag $RC_RELEASE_TAG but port/rc/CMakeLists.txt VERSION is ${exev:-none}" >&2; exit 3; }
fi
# Configure once: MinGW toolchain (port/cmake), the patched toolkit, the lift.
GO="${RC_BUILD_GUARDS_ONLY:-0}"   # guards only: check, never configure (the remote client runs this outside the nix shell)
if [[ "$GO" != 1 && ! -f "$DIR/CMakeCache.txt" ]]; then
  cmake -S "$ROOT/port/rc" -B "$DIR" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64.cmake" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE="$(command -v python3)" -DRC_GAME="$GAME" -DRC_EXE="$EXE" \
    -DPS3RECOMP_DIR="$PT" -DRECOMP_DIR="$OUT/recomp/$GAME" >"$DIR.configure.log" 2>&1 \
    || { tail -20 "$DIR.configure.log" >&2; exit 4; }
fi
# Re-configure when the patched toolkit changed: ps3recomp collects its sources with file(GLOB_RECURSE) without
# CONFIGURE_DEPENDS, so a patch that adds a source file (6003's cellSpursLFQueue.c) is not linked from an old cache
# (the link then fails with undefined _cellSpursLFQueuePushBody).
pt_stamp="$(cat "$PT/.rc-patchset" 2>/dev/null || true)"
if [[ "$GO" != 1 && "$(cat "$DIR/.rc-toolkit-stamp" 2>/dev/null || true)" != "$pt_stamp" ]]; then
  cmake -S "$ROOT/port/rc" -B "$DIR" >>"$DIR.configure.log" 2>&1 || { tail -20 "$DIR.configure.log" >&2; exit 4; }
  echo "$pt_stamp" > "$DIR/.rc-toolkit-stamp"
fi
# Lifted SPU images (tools/spu/lift_spu.sh): point the cache at $OUT/spu/lift when it has a registration TU
# (the CMake default port/rc/spu does not exist). RC_SPU=0 builds without them.
SPU_LIFT="$OUT/spu/$GAME/lift"
if [[ "${RC_SPU:-1}" != 0 && -f "$SPU_LIFT/spu_workloads.c" ]]; then want_spu="$SPU_LIFT"; else want_spu="$ROOT/port/rc/$GAME/spu"; fi
if [[ "$GO" != 1 && "$(sed -n 's/^SPU_DIR:[A-Z]*=//p' "$DIR/CMakeCache.txt" 2>/dev/null)" != "$want_spu" ]]; then
  cmake -S "$ROOT/port/rc" -B "$DIR" -DSPU_DIR="$want_spu" >>"$DIR.configure.log" 2>&1 || { tail -20 "$DIR.configure.log" >&2; exit 4; }
fi
# The lift must announce exactly the overrides of the game's list port/rc/<game>/ppu_overrides.txt (a host override is
# compiled only when the lift announced PPU_OVERRIDE_<addr>); RC_ALLOW_STALE_LIFT=1 overrides.
recomp_l="$(sed -n 's/^RECOMP_DIR:[A-Z]*=//p' "$DIR/CMakeCache.txt" 2>/dev/null || true)"; recomp_l="${recomp_l:-$OUT/recomp/$GAME}"
ann="$recomp_l/ppu_overrides.h"; [[ -f "$ann" ]] || ann="$recomp_l/ppu_recomp.h"
ovr_list="$ROOT/port/rc/$GAME/ppu_overrides.txt"; [[ -f "$ovr_list" ]] || ovr_list=/dev/null
if [[ -f "$ann" && "${RC_ALLOW_STALE_LIFT:-0}" != 1 ]]; then
  want="$( (grep -oE '^[[:space:]]*[0-9A-Fa-f]{8}' "$ovr_list" || true) | tr -d ' \t' | tr a-f A-F | sort -u)"
  have="$( (grep -oE 'PPU_OVERRIDE_[0-9A-Fa-f]{8}' "$ann" || true) | cut -c14- | tr a-f A-F | sort -u)"
  [[ "$want" == "$have" ]] || { echo "build-rc: the lift in $recomp_l does not match ${ovr_list#$ROOT/}: run tools/lift.sh" >&2; exit 3; }
fi
# Guards only: the checks above passed, build nothing here.
[[ "${RC_BUILD_GUARDS_ONLY:-0}" == 1 ]] && { echo "build-rc: guards ok" >&2; exit 0; }
# Build slots across checkouts (tools/agents/slotlock.sh, private tooling): skipped when it is not there.
SLOTS="${RC_BUILD_SLOTS:-1}"; [[ "$SLOTS" =~ ^[1-9][0-9]*$ ]] || SLOTS=1; SLOT=none; in_use=1
if [[ -f "$ROOT/tools/agents/slotlock.sh" ]]; then
  # shellcheck source=agents/slotlock.sh
  source "$ROOT/tools/agents/slotlock.sh"
  if [[ " ${RC_HELD_LOCKS:-} " == *" build "* ]]; then SLOT=held   # run under the build lock
  else slot_take "$LOCKS" build "$SLOTS"; fi
  in_use=$(slot_held "$LOCKS" build "$SLOTS"); (( in_use < 1 )) && in_use=1
elif [[ " ${RC_HELD_LOCKS:-} " != *" build "* ]]; then
  # R&C has no slotlock.sh and the shared lock dir is not mounted in the sandbox: the slot is taken outside it
  # (2026-10-19: builds ran without one, unseen). Wrap the whole command, see the header.
  echo "build-rc: WARNING: no shared build slot held (run under KZ3's tools/agents/lock.sh build, see header)" >&2
fi
echo 1000 > /proc/self/oom_score_adj 2>/dev/null || true
avail_mb=$(( $(awk '/MemAvailable/{print $2}' /proc/meminfo) / 1024 ))
job_mb="${RC_BUILD_JOB_MB:-2048}"; jobs_max="${RC_BUILD_JOBS_MAX:-6}"
jobs=$(( (avail_mb - 8192) / job_mb / in_use )); (( jobs < 1 )) && jobs=1; (( jobs > jobs_max )) && jobs=$jobs_max
# Optional temporary cap in the lock directory (build-cap = "EXPIRY_EPOCH MAXJOBS").
if read -r cap_until cap_jobs < "$LOCKS/build-cap" 2>/dev/null && (( $(date +%s) < cap_until )); then
  (( jobs > cap_jobs )) && jobs=$cap_jobs; renice -n 10 -p $$ >/dev/null 2>&1 || true
fi
echo "build-rc: $DIR with -j$jobs ($(( avail_mb / 1024 )) GB available; build slot $SLOT)" >&2
if command -v ccache >/dev/null && [[ "${RC_CCACHE:-1}" != 0 ]]; then
  export CCACHE_DIR="$OUT/ccache" CCACHE_BASEDIR="$ROOT" CCACHE_MAXSIZE=20G CCACHE_COMPRESS=1
  grep -q '^CMAKE_CXX_COMPILER_LAUNCHER:.*=ccache' "$DIR/CMakeCache.txt" 2>/dev/null \
    || cmake -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache "$DIR" >/dev/null
fi
t0=$(date +%s); rc=0
cmake --build "$DIR" -j"$jobs" "$@" || rc=$?
echo "build-rc: done in $(( $(date +%s) - t0 )) s (-j$jobs, exit $rc)" >&2
# Stage the exe alone with SDL3.dll and its zlib licence in work/stage/<game>:
# proton-test.sh copies the exe's whole directory, and the build dir's read-only CMake files broke that copy (2026-10-19).
if (( rc == 0 )) && [[ -f "$DIR/$EXE.exe" ]]; then
  STAGE="$OUT/stage/$GAME"; mkdir -p "$STAGE"
  cp "$DIR/$EXE.exe" "$STAGE/"
  if python3 "$ROOT/tools/release/stage_sdl3.py" --out "$STAGE" >&2; then
    mv -f "$STAGE/LICENSE.txt" "$STAGE/SDL3-LICENSE.txt"
  else
    echo "build-rc: SDL3.dll not staged: pads fall back to XInput" >&2
  fi
  echo "build-rc: staged $STAGE" >&2
fi
exit $rc
