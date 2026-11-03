#!/usr/bin/env bash
# Builds and runs test_inline_divider.c against libs/video/rsx_vertex_compact.c + rsx_dispatch.c of the patched tree
# (RC_P3R_TREE, default work/ps3recomp-patched of the main checkout). Without patch 7009 in the tree it is applied to a
# scratch copy first. usage: port/rc/tests/run_inline_divider_test.sh [base|patched]   (one compiler: CC, default gcc)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"; mode="${1:-patched}"
[[ -f "$tree/libs/video/rsx_vertex_compact.c" ]] || { echo "SKIP: no tree at $tree"; exit 0; }
tmp="${TMPDIR:-/tmp}/inldiv_$$"; mkdir -p "$tmp/t/libs/video"; trap 'rm -rf "$tmp"' EXIT
cp -r "$tree/include" "$tmp/t/"; cp "$tree/libs/video/"*.h "$tree/libs/video/rsx_vertex_compact.c" "$tree/libs/video/rsx_dispatch.c" "$tmp/t/libs/video/"
cp "$tree/libs/video/rsx_live_draw.c" "$tmp/t/libs/video/"   # the patch also touches it; only patched for applying
has=0; grep -q "R&C patch 7009" "$tmp/t/libs/video/rsx_vertex_compact.c" && has=1
if [[ "$mode" == patched && $has == 0 ]]; then (cd "$tmp/t" && patch -p1 -s < "$repo/port/patches/7009-live-draw-inline-array-ignores-divider.patch"); fi
if [[ "$mode" == base && $has == 1 ]]; then echo "tree already carries 7009"; exit 2; fi
cc="${CC:-gcc}"
"$cc" -std=gnu11 -O1 -w -I"$tmp/t/libs/video" -I"$tmp/t/include" -I"$tmp/t" -o "$tmp/test" "$here/test_inline_divider.c" \
   "$tmp/t/libs/video/rsx_vertex_compact.c" "$tmp/t/libs/video/rsx_dispatch.c" -lm
echo "== $mode ($cc)"; "$tmp/test"
