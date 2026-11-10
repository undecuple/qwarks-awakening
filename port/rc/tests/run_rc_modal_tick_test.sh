#!/usr/bin/env bash
# Builds and runs test_rc_modal_tick.c (window stage 1c: the modal-loop clock's state machine, port/rc/rc_modal_tick.h),
# then an ASan/UBSan pass. The Win32 side (subclass, pacer thread) is checked by the tods build and a Proton run with
# RC_XDO=<a script that drags the window with xdotool> RC_WINE_UNMANAGED=1.
#   nix develop .#re --command port/rc/tests/run_rc_modal_tick_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tmp="${TMPDIR:-/tmp}/rcmodal_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
I=(-I"$repo/port/rc")
cc="${HOST_CC:-gcc}"
W="-std=c11 -Wall -Wextra -Werror"
"$cc" $W -O2 "${I[@]}" "$here/test_rc_modal_tick.c" -o "$tmp/test"
"$tmp/test" | grep -v '^\[PASS\]' || true
"$tmp/test" >/dev/null || { echo "[FAIL] test_rc_modal_tick"; exit 1; }
if echo "int main(void){return 0;}" | "$cc" -fsanitize=address,undefined -x c - -o "$tmp/probe" 2>/dev/null && "$tmp/probe"; then
  "$cc" $W -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "${I[@]}" "$here/test_rc_modal_tick.c" -o "$tmp/test_asan"
  "$tmp/test_asan" >/dev/null && echo "[PASS] ASan/UBSan pass" || { echo "[FAIL] ASan/UBSan pass"; exit 1; }
fi
