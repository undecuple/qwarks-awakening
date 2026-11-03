#!/usr/bin/env bash
# Host tests of the system-function handlers added by patches 7018-7028 (2026-11-03): each test is a unity
# build of one patched ps3recomp source. RC_P3R_TREE = a tree made by tools/ps3recomp-patched.sh (default work/rt-imports-patched
# of this worktree; the main checkout's work/ps3recomp-patched once merged). gcc (CC).
#   nix develop --command tools/sandbox.sh -- tests: port/rc/tests/run_imports_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${RC_P3R_TREE:-$repo/work/rt-imports-patched}"
tmp="${TMPDIR:-/tmp}/rcimports_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${CC:-gcc}"
for t in "$@"; do
  [[ -e "$here/$t" ]] || { echo "no test $t" >&2; exit 1; }
done
tests=("$@"); [[ ${#tests[@]} -gt 0 ]] || tests=(test_savedata_list_auto.c test_sysprx_sysmodule.c test_spurs_global_exception.c test_osk_userinfo_avconf.c
       test_game_exit_broken.c test_avc2_state.c test_gcm_func15_log.c)
for t in "${tests[@]}"; do
  echo "== $t"
  lenient=""   # a test marked RC_LINK_LENIENT unity-builds a source with many unrelated references (cellSpurs.c): they are never called
  if grep -q RC_LINK_LENIENT "$here/$t"; then lenient="-Wl,--unresolved-symbols=ignore-all"; fi
  "$cc" -std=gnu11 -O1 -w -I"$tree/include" -I"$tree" -I"$tree/runtime/spu" ${RC_TEST_CFLAGS:-} $lenient -o "$tmp/${t%.c}" "$here/$t"
  "$tmp/${t%.c}"
done
