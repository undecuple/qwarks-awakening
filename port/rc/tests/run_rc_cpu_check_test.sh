#!/usr/bin/env bash
# Builds and runs test_rc_cpu_check.c (the startup CPU-feature decision, port/rc/rc_cpu_check.c) with injected CPUID
# values, once as a baseline build and once with -march=haswell (so rc_cpu_required() is non-empty), then ASan/UBSan.
#   port/rc/tests/run_rc_cpu_check_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tmp="${TMPDIR:-/tmp}/rccpu_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${HOST_CC:-cc}"
W="-std=c11 -Wall -Wextra -Werror"
src=("$here/test_rc_cpu_check.c" "$repo/port/rc/rc_cpu_check.c")
"$cc" $W -O2 -I"$repo/port/rc" "${src[@]}" -o "$tmp/test"
"$tmp/test"
"$cc" $W -O2 -march=haswell -I"$repo/port/rc" "${src[@]}" -o "$tmp/test_hsw"
"$tmp/test_hsw"
if echo "int main(void){return 0;}" | "$cc" -fsanitize=address,undefined -x c - -o "$tmp/probe" 2>/dev/null && "$tmp/probe"; then
  "$cc" $W -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I"$repo/port/rc" "${src[@]}" -o "$tmp/test_asan"
  "$tmp/test_asan" >/dev/null && echo "[PASS] ASan/UBSan pass" || { echo "[FAIL] ASan/UBSan pass"; exit 1; }
fi
