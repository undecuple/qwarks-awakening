#!/usr/bin/env bash
# Host test of patches 7030 + 7031 (SPURS LFQueue: _cellSpursLFQueueInitialize layout, _cellSpursLFQueuePushBody, waiter wake rule and taskset wid): a unity build of the
# patched libs/spurs/cellSpurs.c with test_lfqueue.c (model SPU consumer, threads). RC_P3R_TREE = a tree made by
# tools/ps3recomp-patched.sh (default work/rt-lfq-patched of this worktree). gcc (CC). Exit 0 = PASS.
#   nix develop --command tools/sandbox.sh -- env RC_P3R_TREE=work/rt-lfq-patched port/rc/tests/run_lfqueue_test.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
tree="${RC_P3R_TREE:-$repo/work/rt-lfq-patched}"
[[ -f "$tree/libs/spurs/cellSpurs.c" ]] || { echo "SKIP: no runtime tree at $tree"; exit 0; }
tmp="${TMPDIR:-/tmp}/rclfq_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
cc="${CC:-gcc}"
echo "== test_lfqueue.c"
"$cc" -std=gnu11 -O1 -w -pthread -I"$tree/include" -I"$tree" -I"$tree/runtime/spu" -I"$here" ${RC_TEST_CFLAGS:-} \
   -Wl,--unresolved-symbols=ignore-all -o "$tmp/test_lfqueue" "$here/test_lfqueue.c"
"$tmp/test_lfqueue"
