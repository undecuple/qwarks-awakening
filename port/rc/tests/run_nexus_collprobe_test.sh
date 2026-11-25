#!/usr/bin/env bash
# Builds and runs test_nexus_collprobe.c (Nexus 1.01 collision-query probe, port/rc/nexus101/nexus101_collprobe.inc,
# RC_COLLPROBE, diagnostic), as C and as C++ (the overrides file includes it from C++), then an ASan/UBSan pass.
#   nix develop .#re --command port/rc/tests/run_nexus_collprobe_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="${TMPDIR:-/tmp}/rccollprobe_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${HOST_CC:-gcc}"; cxx="${HOST_CXX:-g++}"
W="-Wall -Wextra -Werror -D_GNU_SOURCE"
"$cc" -std=c11 $W -O2 "$here/test_nexus_collprobe.c" -o "$tmp/test"
"$tmp/test" | grep -v '^\[PASS\]' || true
"$tmp/test" >/dev/null || { echo "[FAIL] test_nexus_collprobe"; exit 1; }
if command -v "$cxx" >/dev/null; then
  "$cxx" -std=c++17 $W -O2 -x c++ "$here/test_nexus_collprobe.c" -o "$tmp/test_cxx" 2>"$tmp/cxx.log" \
    || { cat "$tmp/cxx.log"; echo "[FAIL] C++ build"; exit 1; }
  "$tmp/test_cxx" >/dev/null && echo "[PASS] C++ build" || { echo "[FAIL] C++ build run"; exit 1; }
else echo "SKIP C++ build: no $cxx"
fi
if echo "int main(void){return 0;}" | "$cc" -fsanitize=address,undefined -x c - -o "$tmp/probe" 2>/dev/null && "$tmp/probe"; then
  "$cc" -std=c11 $W -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "$here/test_nexus_collprobe.c" -o "$tmp/test_asan"
  "$tmp/test_asan" >/dev/null && echo "[PASS] ASan/UBSan pass" || { echo "[FAIL] ASan/UBSan pass"; exit 1; }
else echo "SKIP ASan/UBSan pass: $cc cannot build and run a -fsanitize=address,undefined program here"
fi
echo "[run_nexus_collprobe_test] PASS"
