#!/usr/bin/env bash
# Builds and runs test_vtx_gpu_decode.c against libs/video (rsx_vtx_gpu.h, rsx_vertex_compact.c, rsx_dispatch.c) of the
# patched tree (RC_P3R_TREE, default work/ps3recomp-patched of the main checkout). One compiler (CC, default gcc).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
main="$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)"
tree="${RC_P3R_TREE:-$main/work/ps3recomp-patched}"
tmp="${TMPDIR:-/tmp}/vtxgpu_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${CC:-gcc}"
"$cc" -std=gnu11 -O1 -w -I"$tree/libs/video" -I"$tree/include" -I"$tree" -o "$tmp/test" "$here/test_vtx_gpu_decode.c" \
   "$tree/libs/video/rsx_vertex_compact.c" "$tree/libs/video/rsx_dispatch.c" -lm
echo "== $cc"; "$tmp/test"
