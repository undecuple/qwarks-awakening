#!/usr/bin/env bash
# No-game host regression for the patched PPU reservation registry. Compile
# under the shared build lock; RC_P3R_TREE selects the source tree. The current
# unfixed runtime passes baseline and fails churn/all (three unbroken victims).
# all also checks concurrent registration, 128 distinct contexts and explicit
# overflow failure in a child process, with core dumps disabled.
#   RC_P3R_TREE=work/ps3recomp-patched bash port/rc/tests/run_ppu_resv_registry_test.sh [all|baseline|churn|concurrent|capacity|overflow]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${RC_P3R_TREE:-$repo/work/ps3recomp-patched}"
[[ -f "$tree/runtime/ppu/ppu_loader.cpp" ]] || { echo "missing patched ppu_loader.cpp in $tree" >&2; exit 2; }
tmp="$(mktemp -d "${TMPDIR:-/tmp}/rc-ppu-resv-XXXXXX")"
trap 'rm -rf -- "$tmp"' EXIT
python3 - "$tree/runtime/ppu/ppu_loader.cpp" "$tmp/ppu_resv_runtime.inc" <<'PY'
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
# Keep the actual upstream source header and its real globals/registration
# implementation. These anchors deliberately fail if the source is refactored.
prefix = re.search(r'^#define PPU_RESV_MAX\b.*?(?=^/\* Where every guest thread is)', source, re.M | re.S)
if not prefix or 'void ppu_resv_register(' not in prefix.group():
    raise SystemExit("reservation registry extraction anchors changed; review the host reproduction")
parts = [source[:source.index('*/') + 2], prefix.group()]
for name in ('ppu_resv_break_store', 'ppu_resv_break_line', 'ppu_resv_break'):
    pattern = r'^extern "C" void ' + name + r'\([^\n]*\)\s*(?:/\*[^\n]*\*/)?\n\{.*?^\}'
    matches = list(re.finditer(pattern, source, re.M | re.S))
    if len(matches) != 1:
        raise SystemExit(f"expected one complete definition of {name}; review the host reproduction")
    parts.append(matches[0].group())
Path(sys.argv[2]).write_text('\n\n'.join(parts) + '\n', encoding="utf-8")
PY
cxx="${RC_TEST_CXX:-${CXX:-c++}}"
"$cxx" -std=c++17 -O1 -Wall -Wextra -pthread -I"$tree" -I"$tmp" \
  "$here/test_ppu_resv_registry.cpp" -o "$tmp/test_ppu_resv_registry"
mode="${1:-all}"
if [[ "$mode" != overflow ]]; then "$tmp/test_ppu_resv_registry" "$mode"; fi
if [[ "$mode" == all || "$mode" == capacity || "$mode" == overflow ]]; then
  ulimit -c 0
  python3 - "$tmp/test_ppu_resv_registry" <<'PY'
import signal
import subprocess
import sys
r = subprocess.run([sys.argv[1], 'overflow-child'], capture_output=True, text=True)
if r.returncode != -signal.SIGABRT or 'reservation registry exhausted: capacity=128' not in r.stderr:
    print(f'FAIL: expected explicit capacity abort, got {r.returncode}: {r.stderr.strip()}', file=sys.stderr)
    raise SystemExit(1)
print('PASS: 129th distinct context fails explicitly with the 128-context capacity preserved')
PY
fi
