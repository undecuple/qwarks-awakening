#!/usr/bin/env bash
# Host test of ToD's pause-menu quit entries (port/rc/tods/tods_pause_quit.inc, ENHANCEMENT): extracts the Lua source kMaker from the
# .inc and runs test_pause_quit.lua against a mock of the game's ListUpdater behaviour. Needs a Lua 5.1-family interpreter: $LUA, else
# luajit or lua on PATH, e.g.  nix shell nixpkgs#luajit --command port/rc/tests/run_pause_quit_test.sh
# Also checks that the C side of the .inc compiles (g++ -fsyntax-only with stub declarations) when g++ is on PATH. No game data.
# Exit 0 = PASS, 2 = no interpreter.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
inc="$repo/port/rc/tods/tods_pause_quit.inc"
tmp="${TMPDIR:-/tmp}/rcpq_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
LUA="${LUA:-$(command -v luajit || command -v lua || true)}"
[[ -n "$LUA" ]] || { echo "run_pause_quit_test: no Lua interpreter (set LUA=...)"; exit 2; }
# kMaker = the C string literal lines between 'kMaker[] =' and the closing ';'
python3 -I - "$inc" "$tmp/maker.lua" <<'EOF' || exit 1
import ast, re, sys
s = open(sys.argv[1]).read()
m = re.search(r'static const char kMaker\[\] =\n(.*?);\n', s, re.S)
lines = [l.strip() for l in m.group(1).splitlines() if l.strip()]
open(sys.argv[2], 'w').write(''.join(ast.literal_eval(l) for l in lines))
EOF
rc=0
"$LUA" "$here/test_pause_quit.lua" "$tmp/maker.lua" || rc=1
if command -v g++ >/dev/null; then
  cat > "$tmp/stub.cpp" <<'EOF'
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define PPU_THREAD_LOCAL thread_local
struct ppu_vr { uint32_t u[4]; };
typedef struct ppu_context { uint64_t gpr[32]; double fpr[32]; ppu_vr vr[32]; uint32_t cr; uint64_t lr, ctr; uint32_t xer, fpscr, vscr;
  uint64_t cia, thread_id, reserve_addr, reserve_value; int reserve_valid; uint32_t vrsave; } ppu_context;
uint8_t vm_read8(uint32_t); uint32_t vm_read32(uint32_t); void vm_write8(uint32_t, uint8_t); void vm_write32(uint32_t, uint32_t);
void vm_write64(uint32_t, uint64_t);
void func_0066C520(ppu_context*); void func_006696C8(ppu_context*);
#include "tods_pause_quit.inc"
void use(ppu_context* c) { TodsPauseQuitCall q; tods_pause_quit_before(c, &q); tods_pause_quit_after(c, &q, 0); }
EOF
  g++ -std=c++17 -Wall -Wextra -fsyntax-only -I"$repo/port/rc/tods" "$tmp/stub.cpp" && echo "pause-quit C side: compiles" || rc=1
fi
[[ $rc == 0 ]] && echo "PASS" || echo "FAIL"
exit $rc
