#!/usr/bin/env bash
# Builds and runs test_rc_display.c (window resize stage 1a: display.ini read-modify-write, size =, R&C display defaults,
# RC_WINDOW_SIZE alias, off-screen placement check) against libs/video of the patched tree (RC_P3R_TREE, default
# work/ps3recomp-patched of the main checkout); then an ASan/UBSan pass, and, when x86_64-w64-mingw32-gcc is on PATH
# (.#win shell), a Windows compile of the header with <windows.h> (rsx_window_size.h's Win32 part).
#   nix develop .#re --command port/rc/tests/run_rc_display_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"
[[ -f "$tree/libs/video/rsx_window_place.h" ]] || { echo "[FAIL] no $tree/libs/video/rsx_window_place.h (tools/ps3recomp-patched.sh)"; exit 1; }
tmp="${TMPDIR:-/tmp}/rcdisp_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
I=(-I"$tree/libs/video" -I"$repo/port/rc")
cc="${HOST_CC:-gcc}"   # the .#win shell sets CC to MinGW: the host parts need a native compiler
W="-std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable"
if command -v "$cc" >/dev/null; then
"$cc" $W -O2 "${I[@]}" "$here/test_rc_display.c" -o "$tmp/test"
"$tmp/test" | grep -v '^\[PASS\]' || true
"$tmp/test" >/dev/null || { echo "[FAIL] test_rc_display"; exit 1; }
if echo "int main(void){return 0;}" | "$cc" -fsanitize=address,undefined -x c - -o "$tmp/probe" 2>/dev/null && "$tmp/probe"; then
  "$cc" $W -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "${I[@]}" "$here/test_rc_display.c" -o "$tmp/test_asan"
  "$tmp/test_asan" >/dev/null && echo "[PASS] ASan/UBSan pass" || { echo "[FAIL] ASan/UBSan pass"; exit 1; }
fi
else echo "SKIP host parts: no $cc (run in the .#re shell)"; fi
if command -v x86_64-w64-mingw32-g++ >/dev/null; then
  printf '#include <windows.h>\n#include "rc_display_ini.h"\nint main(){return 0;}\n' > "$tmp/w.cpp"
  x86_64-w64-mingw32-g++ -std=gnu++20 -Wall -Werror -Wno-unused-function -Wno-unused-variable -fsyntax-only "${I[@]}" "$tmp/w.cpp" \
    && echo "[PASS] rc_display_ini.h compiles for Windows (C++)" || { echo "[FAIL] rc_display_ini.h Windows compile"; exit 1; }
fi
echo "[run_rc_display_test] PASS"
