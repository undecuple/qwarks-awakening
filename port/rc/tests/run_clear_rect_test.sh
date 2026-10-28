#!/usr/bin/env bash
# Builds and runs test_clear_rect.c against libs/video/rsx_dispatch.c and rsx_clear_rect.h of the patched tree
# (RC_P3R_TREE, default work/ps3recomp-patched of the main checkout); without patch 7010 in the tree it is applied to a
# scratch copy first. One compiler (CC, default gcc). Exit 0 = PASS.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"
[[ -f "$tree/libs/video/rsx_dispatch.c" ]] || { echo "SKIP: no tree at $tree"; exit 0; }
tmp="${TMPDIR:-/tmp}/clrrect_$$"; mkdir -p "$tmp/t/libs/video"; trap 'rm -rf "$tmp"' EXIT
cp -r "$tree/include" "$tmp/t/"; cp "$tree/libs/video/"*.h "$tree/libs/video/rsx_dispatch.c" "$tree/libs/video/rsx_live_draw.c" "$tmp/t/libs/video/"
[[ -f "$tmp/t/libs/video/rsx_clear_rect.h" ]] || (cd "$tmp/t" && patch -p1 -s < "$repo/port/patches/7010-live-draw-clear-scissor-on-aa-sample-grid.patch")
cc="${CC:-gcc}"
"$cc" -std=gnu11 -O1 -w -I"$tmp/t/libs/video" -I"$tmp/t/include" -I"$tmp/t" -o "$tmp/test" "$here/test_clear_rect.c" "$tmp/t/libs/video/rsx_dispatch.c" -lm
echo "== $cc"; "$tmp/test"
