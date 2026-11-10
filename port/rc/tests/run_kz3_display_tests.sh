#!/usr/bin/env bash
# Killzone 3's host tests of the display runtime R&C carries (MIT, same author; copied unchanged into port/rc/tests/kz3/
# from KZ3 tools/smoke/ on 2026-11-09): 2062 window size (test_window_size.c), 2095 display modes (test_display_mode.c),
# 2126 placement (test_window_place.c), 2113 HUD squeeze (test_hud169.c), 2156 present geometry and UI ownership
# (test_display_geometry.py: functions extracted from the patched rsx_live_draw.c / rsx_null_backend.c, with source
# mutations that must fail), 2154 state memo after helper blits (test_ld_blit_state_memo.py). Each C test also runs
# under ASan/UBSan. No game data, no GPU.
#   nix develop .#re --command port/rc/tests/run_kz3_display_tests.sh
# RC_P3R_TREE = the patched tree (default work/ps3recomp-patched of this checkout, else of the main checkout).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${RC_P3R_TREE:-}"
if [[ -z "$tree" ]]; then
  tree="$repo/work/ps3recomp-patched"
  if [[ ! -f "$tree/libs/video/rsx_present_geometry.h" ]]; then
    main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
    tree="$main/work/ps3recomp-patched"
  fi
fi
V="$tree/libs/video"
[[ -f "$V/rsx_present_geometry.h" ]] || { echo "[FAIL] no $V/rsx_present_geometry.h (KZ3 2156; tools/ps3recomp-patched.sh)"; exit 1; }
tmp="${TMPDIR:-/tmp}/rckz3disp_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${HOST_CC:-gcc}"
W=(-std=c11 -Wall -Wextra -Werror -I"$V")
asan=0
echo "int main(void){return 0;}" | "$cc" -fsanitize=address,undefined -x c - -o "$tmp/probe" 2>/dev/null && "$tmp/probe" && asan=1
fail=0
for t in test_window_size test_display_mode test_window_place test_hud169; do
  extra=(); [[ $t == test_hud169 ]] && extra=(-lm)
  if ! "$cc" "${W[@]}" -O2 "$here/kz3/$t.c" -o "$tmp/$t" "${extra[@]}"; then echo "[FAIL] $t: compile"; fail=1; continue; fi
  if "$tmp/$t" > "$tmp/$t.out"; then echo "[PASS] $t ($(grep -c '^\[PASS\]' "$tmp/$t.out" || true) checks)"
  else grep -v '^\[PASS\]' "$tmp/$t.out" | head -20; echo "[FAIL] $t"; fail=1; fi
  if [[ $asan == 1 ]]; then
    "$cc" "${W[@]}" -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "$here/kz3/$t.c" -o "$tmp/$t.asan" "${extra[@]}" \
      && "$tmp/$t.asan" >/dev/null && echo "[PASS] $t ASan/UBSan" || { echo "[FAIL] $t ASan/UBSan"; fail=1; }
  fi
done
san=(); [[ $asan == 1 ]] && san=(--sanitize)
if python3 -I "$here/kz3/test_display_geometry.py" --candidate-sdk "$tree" --out "$tmp/geom" --cc "$cc" "${san[@]}" > "$tmp/geom.log" 2>&1
then echo "[PASS] test_display_geometry (2156): $(grep -c 'PASS\|rejected' "$tmp/geom.log") variants (candidate + mutations rejected)"
else tail -20 "$tmp/geom.log"; echo "[FAIL] test_display_geometry"; fail=1; fi
if KZ3_PATCHED_DIR="$tree" python3 -I "$here/kz3/test_ld_blit_state_memo.py" > "$tmp/memo.log" 2>&1
then echo "[PASS] test_ld_blit_state_memo (2154)"
else tail -20 "$tmp/memo.log"; echo "[FAIL] test_ld_blit_state_memo"; fail=1; fi
[[ $fail == 0 ]] && echo "[run_kz3_display_tests] PASS" || { echo "[run_kz3_display_tests] FAIL"; exit 1; }
