#!/usr/bin/env bash
# Host test of patch 7032 (the offline network model, 2026-11-03): builds each patched network source as its own object
# (sceNp.h and sceNpBasic.h cannot share a translation unit), links them with test_net_offline.c and runs it, once per
# compiler (default gcc and clang; RC_TEST_CC=... for one). RC_P3R_TREE = a tree made by tools/ps3recomp-patched.sh (default
# work/ps3recomp-patched of this checkout).
#   nix develop --command tools/sandbox.sh -- port/rc/tests/run_net_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${RC_P3R_TREE:-$repo/work/ps3recomp-patched}"
tmp="${TMPDIR:-/tmp}/rcnet_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
inc=(-I"$tree/include" -I"$tree" -I"$tree/runtime/spu" -I"$tree/libs/system" -I"$tree/libs/network")
srcs=(cellHttp cellNetCtl cellSsl cellRudp net_offline net_policy sceNp sceNpBasic sceNpSignaling sceNpLookup
      sceNpMatching2 sceNpCommerce2 sceNpScore sceNpTus sceNpClans)
for cc in ${RC_TEST_CC:-gcc clang}; do   # not $CC: nix develop sets it to gcc
  echo "== $cc"
  mkdir -p "$tmp/$cc"
  for f in "${srcs[@]}"; do
    "$cc" -std=gnu11 -O1 -w -c "${inc[@]}" -o "$tmp/$cc/$f.o" "$tree/libs/network/$f.c"
  done
  "$cc" -std=gnu11 -O1 -w "${inc[@]}" -o "$tmp/$cc/test_net_offline" "$here/test_net_offline.c" "$tmp/$cc"/*.o -lpthread
  "$tmp/$cc/test_net_offline" | grep -v '^\['   # the sources' own [lib] log lines
done
