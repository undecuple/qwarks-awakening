#!/usr/bin/env bash
# Host test for R&C patch 7114: the patched edat.c refuses a synthetic licence type 2 EDAT without licence data
# and its log line names no key, no key file and no procedure (port/rc/tests/test_edat_licence_hint.c; no key and no
# game file is used). Negative control: RC_EDAT_CONTROL_TREE=<a tree without 7114> (e.g. the pristine submodule
# third_party/ps3recomp) builds the same test with that tree's edat.c and must FAIL.
# Usage: port/rc/tests/run_edat_hint_test.sh [patched tree]   (or RC_P3R_TREE; default work/ps3recomp-patched)
#   nix develop .#ci --command port/rc/tests/run_edat_hint_test.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
PT=${1:-${RC_P3R_TREE:-"$here/../../../work/ps3recomp-patched"}}
grep -q "R&C 7114" "$PT/libs/filesystem/edat.c" || { echo "FAIL: $PT has no patch 7114" >&2; exit 1; }
tmp="${TMPDIR:-/tmp}/rc_edat_hint_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${CC:-cc}"
build() {   # build OUT TREE
  "$cc" -std=gnu17 -O1 -UNDEBUG -I"$2/libs/filesystem" -I"$2/include" "$here/test_edat_licence_hint.c" -o "$1"
}
build "$tmp/t" "$PT"
mkdir -p "$tmp/run"
env -u PS3_RIF_KEY "$tmp/t" "$tmp/run"
if [[ -n "${RC_EDAT_CONTROL_TREE:-}" ]]; then
  grep -q "R&C 7114" "$RC_EDAT_CONTROL_TREE/libs/filesystem/edat.c" && {
    echo "FAIL: the control tree $RC_EDAT_CONTROL_TREE has patch 7114" >&2; exit 1; }
  build "$tmp/ctl" "$RC_EDAT_CONTROL_TREE"
  mkdir -p "$tmp/ctlrun"
  if env -u PS3_RIF_KEY "$tmp/ctl" "$tmp/ctlrun" > "$tmp/ctl.out"; then
    cat "$tmp/ctl.out"
    echo "FAIL: negative control (edat.c without 7114) passed: the test does not see the old hint" >&2
    exit 1
  fi
  echo "negative control (edat.c without 7114) fails as expected:"
  grep '^FAIL' "$tmp/ctl.out" | sed 's/^/  | /'
fi
echo "PASS run_edat_hint_test"
