#!/usr/bin/env bash
# Builds and runs test_lv2_mutex_cond.c (lv2 mutex/cond semantics, POSIX threads) against the runtime's sys_mutex.c and
# sys_cond.c. The sources come from the patched tree (RC_P3R_TREE, default work/ps3recomp-patched of the main checkout);
# when that tree does not carry patches 7006/7007 yet, they are applied to a scratch copy of the three files first.
# usage: port/rc/tests/run_lv2_sync_test.sh [base|patched] [case]   (base = the tree's files without 7006/7007 applied;
#        case = all|handoff|destroy|waiter|depth|stress|create|diag). One compiler (CC, default gcc). Exit 0 = PASS.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"
mode="${1:-patched}"; case_="${2:-all}"
[[ -f "$tree/runtime/syscalls/sys_mutex.c" ]] || { echo "SKIP: no runtime tree at $tree"; exit 0; }
tmp="${TMPDIR:-/tmp}/lv2sync_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/t/runtime"
cp -r "$tree/include" "$tmp/t/"
for d in syscalls ppu memory platform; do mkdir -p "$tmp/t/runtime/$d"; cp "$tree/runtime/$d/"*.h "$tmp/t/runtime/$d/" 2>/dev/null || true; done
cp "$tree/runtime/syscalls/sys_mutex.c" "$tree/runtime/syscalls/sys_cond.c" "$tmp/t/runtime/syscalls/"
has7007=0; grep -q "R&C patch 7007" "$tmp/t/runtime/syscalls/sys_mutex.c" && has7007=1
if [[ "$mode" == patched && $has7007 == 0 ]]; then
  (cd "$tmp/t" && patch -p1 -s < "$repo/port/patches/7006-lv2-sync-watchdog-diagnostics.patch" \
               && patch -p1 -s < "$repo/port/patches/7007-lv2-mutex-handoff-and-destroy-rules.patch")
elif [[ "$mode" == base && $has7007 == 1 ]]; then
  echo "the tree already carries 7007; set RC_P3R_TREE to a tree without it for the base run"; exit 2
fi
cc="${CC:-gcc}"
"$cc" -std=gnu11 -O1 -w -pthread -I"$tmp/t" -I"$tmp/t/include" -o "$tmp/test" "$here/test_lv2_mutex_cond.c" \
   "$tmp/t/runtime/syscalls/sys_mutex.c" "$tmp/t/runtime/syscalls/sys_cond.c"
echo "== $mode ($cc) case=$case_"
"$tmp/test" "$case_"
