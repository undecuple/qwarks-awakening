#!/usr/bin/env bash
# Builds and runs test_vg_index.c against libs/video of the patched tree (RC_P3R_TREE, default work/ps3recomp-patched of
# the main checkout). Patch 7013 adds libs/video/rsx_vg_index.h; without it in the tree the patch is applied to a scratch
# copy ("base" mode shows that the tree has no direct path). usage: run_vg_index_test.sh [patched|base|bench]. One compiler.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"; mode="${1:-patched}"
tmp="${TMPDIR:-/tmp}/vgidx_$$"; mkdir -p "$tmp/t/libs/video"; trap 'rm -rf "$tmp"' EXIT
cp -r "$tree/include" "$tmp/t/"; cp "$tree/libs/video/"*.h "$tree/libs/video/rsx_vertex_compact.c" "$tree/libs/video/rsx_dispatch.c" "$tree/libs/video/rsx_live_draw.c" "$tmp/t/libs/video/"
if [[ "$mode" == base ]]; then
  if [[ -f "$tmp/t/libs/video/rsx_vg_index.h" ]]; then echo "tree already carries 7013"; exit 2; fi
  echo "== base: no rsx_vg_index.h, no LD_VTX_GPU_INDEX in rsx_live_draw.c: $(grep -c LD_VTX_GPU_INDEX "$tmp/t/libs/video/rsx_live_draw.c") mentions -> the old refs/remap path only (FAIL by design)"; exit 1
fi
[[ -f "$tmp/t/libs/video/rsx_vg_index.h" ]] || (cd "$tmp/t" && patch -p1 -s < "$repo/port/patches/7013-live-draw-vtx-gpu-direct-index.patch")
cc="${CC:-gcc}"
"$cc" -std=gnu11 -O2 -w -I"$tmp/t/libs/video" -I"$tmp/t/include" -I"$tmp/t" -o "$tmp/test" "$here/test_vg_index.c" \
   "$tmp/t/libs/video/rsx_vertex_compact.c" "$tmp/t/libs/video/rsx_dispatch.c" -lm
echo "== $mode ($cc)"; if [[ "$mode" == bench ]]; then "$tmp/test" bench; else "$tmp/test"; fi
