#!/usr/bin/env bash
# Builds and runs test_rc_settings.c (per-game settings folder, per-game key tables, R&C patch 6011) against
# libs/input of the patched tree (RC_P3R_TREE, default work/ps3recomp-patched of the main checkout), with patch 6011
# applied to a scratch copy when the tree lacks it; then an ASan/UBSan pass, a C++ compile of rc_settings.cpp, a C compile
# of rc_keybinds_shared.c (shared key bindings) on its own, and,
# when x86_64-w64-mingw32-gcc is on PATH (.#win shell), Windows compiles of cellPad.c, rc_settings.cpp and rc_keybinds_shared.c.
#   nix develop .#re --command port/rc/tests/run_rc_settings_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"
tmp="${TMPDIR:-/tmp}/rcset_$$"; mkdir -p "$tmp/t/libs"; trap 'rm -rf "$tmp"' EXIT
cp -r "$tree/libs/input" "$tmp/t/libs/"
[[ -f "$tmp/t/libs/input/pad_kb_profile.h" ]] || (cd "$tmp/t" && patch -p1 -s < "$repo/port/patches/6011-keybinds-profile-per-game.patch")
I=(-I"$tmp/t/libs/input" -I"$repo/port/rc")
cc="${HOST_CC:-gcc}"   # the .#win shell sets CC to MinGW: the host parts need a native compiler
W="-std=c11 -Wall -Wextra -Werror -Wno-unused-function"
if command -v "$cc" >/dev/null; then
"$cc" $W -O2 "${I[@]}" "$here/test_rc_settings.c" -o "$tmp/test"
"$tmp/test" | grep -v '^\[PASS\]' || true
"$tmp/test" >/dev/null || { echo "[FAIL] test_rc_settings"; exit 1; }
if echo "int main(void){return 0;}" | "$cc" -fsanitize=address,undefined -x c - -o "$tmp/probe" 2>/dev/null && "$tmp/probe"; then
  "$cc" $W -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "${I[@]}" "$here/test_rc_settings.c" -o "$tmp/test_asan"
  "$tmp/test_asan" >/dev/null && echo "[PASS] ASan/UBSan pass" || { echo "[FAIL] ASan/UBSan pass"; exit 1; }
fi
"${HOST_CXX:-g++}" -std=gnu++20 -Wall -Wextra -Werror -Wno-unused-function -fsyntax-only "${I[@]}" "$repo/port/rc/rc_settings.cpp" \
  && echo "[PASS] rc_settings.cpp compiles (host)" || { echo "[FAIL] rc_settings.cpp host compile"; exit 1; }
"$cc" $W -fsyntax-only "${I[@]}" "$repo/port/rc/rc_keybinds_shared.c" \
  && echo "[PASS] rc_keybinds_shared.c compiles on its own (host)" || { echo "[FAIL] rc_keybinds_shared.c host compile"; exit 1; }
else echo "SKIP host parts: no $cc (run in the .#re shell)"; fi
if command -v x86_64-w64-mingw32-gcc >/dev/null; then
  x86_64-w64-mingw32-gcc -std=c11 -Wall -Werror -Wno-unused-function -Wno-unknown-pragmas -fsyntax-only \
    -I"$tree/include" -I"$tree" -I"$tree/runtime/ppu" -I"$tree/runtime/memory" -I"$tree/runtime/platform" "${I[@]}" "$tmp/t/libs/input/cellPad.c" \
    && echo "[PASS] cellPad.c (6011) compiles for Windows" || { echo "[FAIL] cellPad.c Windows compile"; exit 1; }
  x86_64-w64-mingw32-g++ -std=gnu++20 -Wall -Werror -Wno-unused-function -fsyntax-only "${I[@]}" "$repo/port/rc/rc_settings.cpp" \
    && echo "[PASS] rc_settings.cpp compiles for Windows" || { echo "[FAIL] rc_settings.cpp Windows compile"; exit 1; }
  x86_64-w64-mingw32-gcc -std=c11 -Wall -Werror -Wno-unused-function -fsyntax-only "${I[@]}" "$repo/port/rc/rc_keybinds_shared.c" \
    && echo "[PASS] rc_keybinds_shared.c compiles for Windows" || { echo "[FAIL] rc_keybinds_shared.c Windows compile"; exit 1; }
fi
echo "[run_rc_settings_test] PASS"
