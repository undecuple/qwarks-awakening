#!/usr/bin/env bash
# Builds and runs test_rc_mouse.c (ToD mouse look arithmetic and the stage 3 in-play rule, port/rc/rc_mouse.h) against
# libs/input/pad_mouse.h of the patched tree (RC_P3R_TREE, default work/ps3recomp-patched of the main checkout), then an
# ASan/UBSan pass. tods_mouse_look.inc itself (Win32, lifted code) is checked by the tods build.
#   nix develop .#re --command port/rc/tests/run_rc_mouse_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"
tmp="${TMPDIR:-/tmp}/rcmouse_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
I=(-I"$tree/libs/input" -I"$repo/port/rc")
cc="${HOST_CC:-gcc}"
W="-std=c11 -Wall -Wextra -Werror -Wno-unused-function"
"$cc" $W -O2 "${I[@]}" "$here/test_rc_mouse.c" -o "$tmp/test" -lm
"$tmp/test" | grep -v '^\[PASS\]' || true
"$tmp/test" >/dev/null || { echo "[FAIL] test_rc_mouse"; exit 1; }
if echo "int main(void){return 0;}" | "$cc" -fsanitize=address,undefined -x c - -o "$tmp/probe" 2>/dev/null && "$tmp/probe"; then
  "$cc" $W -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "${I[@]}" "$here/test_rc_mouse.c" -o "$tmp/test_asan" -lm
  "$tmp/test_asan" >/dev/null && echo "[PASS] ASan/UBSan pass" || { echo "[FAIL] ASan/UBSan pass"; exit 1; }
fi
