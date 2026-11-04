#!/usr/bin/env bash
# Host test of the game logger (port/common/log: KZ3's logger + rc_log_info.c): builds test_rc_log.c with every
# available native compiler (gcc, clang; RC_TEST_CC=... for just one), runs the modes, and checks the per-game file name and
# folder, the header fields, rotation (newest 10 per game), redaction of a fake user name, the stdout echo only with
# RC_LOG_STDOUT=1, level/tag filtering and the crash report (the text and the ring; the minidump is Windows only). Then
# compiles the two logger sources with the MinGW cross compiler alone (-c) when x86_64-w64-mingw32-gcc is on PATH
# (`nix develop .#win`). No game data. Exit 0 = PASS.
#   nix develop --command nice -n 19 port/rc/tests/run_log_test.sh
#   nix develop .#win --command port/rc/tests/run_log_test.sh      (adds the MinGW compile)
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
log="$repo/port/common/log"
tmp="${TMPDIR:-/tmp}/rclog_$$"; mkdir -p "$tmp"; trap 'rm -rf "$tmp"' EXIT
fails=0; checks=0
ok()   { checks=$((checks+1)); }
bad()  { checks=$((checks+1)); fails=$((fails+1)); echo "  FAIL: $*"; }
has()  { grep -qaE -- "$2" "$1" && ok || bad "$1 lacks /$2/ ($3)"; }
hasnt(){ grep -qaE -- "$2" "$1" && bad "$1 has /$2/ ($3)" || ok; }

compilers=()
if [[ -n "${RC_TEST_CC:-}" ]]; then compilers=("$RC_TEST_CC"); else
  for c in gcc clang; do command -v "$c" >/dev/null && compilers+=("$c"); done; fi
[[ ${#compilers[@]} -gt 0 ]] || echo "(no native C compiler on PATH: only the MinGW compile runs)"

FAKEHOME=/home/zq_fakeuser_77
FAKEPROF='C:\Users\zq_fakeuser_77'

run_mode() { # dir mode args... ; env from the caller
  local d="$1"; shift
  env -u RC_LOG_STDOUT -u KZ3_LOG_STDOUT -u RC_LOG -u RC_LOG_LEVEL HOME="$FAKEHOME" USERPROFILE="$FAKEPROF" RC_LOG_DIR="$d" \
      RC_TEST_PATH="$FAKEHOME/secret/place" PS3_TEST_FLAG=1 LD_LIBRARY_PATH=/nope "${EXTRA_ENV[@]}" "$tmp/t" "$@"
}

for cc in "${compilers[@]}"; do
  echo "== $cc"
  "$cc" -std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -pthread -I"$log" -o "$tmp/t" "$here/test_rc_log.c" "$log/kz3_log.c" "$log/rc_log_info.c" \
    || { echo "  FAIL: $cc build"; fails=$((fails+1)); continue; }
  # the logger sources alone, warnings on, so a new warning in them shows
  "$cc" -std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -c -I"$log" -o "$tmp/l1.o" "$log/kz3_log.c" 2>"$tmp/w1.txt" && ok || bad "$cc compiles kz3_log.c"
  "$cc" -std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -c -I"$log" -o "$tmp/l2.o" "$log/rc_log_info.c" 2>"$tmp/w2.txt" && ok || bad "$cc compiles rc_log_info.c"
  [[ -s "$tmp/w2.txt" ]] && { echo "  (warnings in rc_log_info.c:)"; head -5 "$tmp/w2.txt"; }

  # ---- basic: folder, file name, header, end block, redaction, no stdout without RC_LOG_STDOUT
  d="$tmp/basic_$cc"; mkdir -p "$d"; EXTRA_ENV=()
  run_mode "$d" basic "$d" >"$d/stdout.txt" 2>"$d/stderr.txt"; rc=$?
  [[ $rc -eq 0 ]] && ok || bad "basic exit $rc"
  f="$(ls "$d"/logs/tods/QwarksAwakening-tods-[0-9]*.log 2>/dev/null | head -1)"
  [[ -n "$f" ]] && ok || { bad "no log file logs/tods/QwarksAwakening-tods-YYYYMMDD-HHMMSS.log in $d"; continue; }
  [[ "$(basename "$f")" =~ ^QwarksAwakening-tods-[0-9]{8}-[0-9]{6}\.log$ ]] && ok || bad "file name $(basename "$f")"
  has "$f" '=== Qwark.s Awakening 9\.8\.7 \(commit abc123def\) ===' header-product
  has "$f" 'game: tods, Test Title: A & B, title ID BCUS98127' header-game
  has "$f" 'update version expected: 01\.20' header-update
  has "$f" 'exe t: sha256 [0-9a-f]{16} \([0-9]+ bytes' header-sha
  has "$f" 'os: Linux' header-os
  has "$f" 'cpu: .+, [0-9]+ threads' header-cpu
  has "$f" 'ram: [0-9]+ MB total' header-ram
  has "$f" 'env RC_TEST_PATH=' env-listed
  has "$f" 'env PS3_TEST_FLAG=1' env-listed2
  hasnt "$f" 'LD_LIBRARY_PATH' env-ld-skipped
  has "$f" 'PARAM\.SFO: TITLE_ID=BCUS00000, VERSION=02\.00, APP_VER=02\.10, TITLE=Test & Game' sfo-line
  has "$f" 'end: exit code 7 \(test exit\), run time [0-9]+\.[0-9]{3} s' end-block
  has "$f" ' I \[fps\] 59\.9 from printf' printf-router
  hasnt "$f" 'debug line' debug-hidden-by-default
  hasnt "$f" 'zq_fakeuser_77' redaction-file
  hasnt "$d/stdout.txt" . no-stdout-without-RC_LOG_STDOUT
  # time and thread id on every line
  grep -aE '^ *[0-9]+ +[0-9]+( main)? [EWIDT] ' "$f" >/dev/null && ok || bad "line format (ms, tid, label, level)"
  # the header comes before anything else and in the same file as the end block
  [[ "$(sed -n '2p' "$f")" == *"=== Qwark"* ]] && ok || bad "header block is not at the top: $(sed -n 2p "$f")"

  # ---- stdout echo with RC_LOG_STDOUT=1 in today's "[tag] text" format
  d="$tmp/echo_$cc"; mkdir -p "$d"; EXTRA_ENV=(RC_LOG_STDOUT=1)
  run_mode "$d" basic "$d" >"$d/stdout.txt" 2>"$d/stderr.txt"
  has "$d/stdout.txt" '^\[fps\] 59\.9 from printf$' stdout-echo-format
  has "$d/stdout.txt" '^\[boot\] VFS root: ' stdout-echo-tagged
  hasnt "$d/stdout.txt" 'zq_fakeuser_77' redaction-stdout
  hasnt "$d/stdout.txt" '^ *[0-9]+ +[0-9]+ ' stdout-has-no-file-prefix

  # ---- levels and tag filtering: default warn, foo:debug, bar:error
  d="$tmp/lv_$cc"; mkdir -p "$d"; EXTRA_ENV=(RC_LOG_LEVEL=warn RC_LOG=foo:debug,bar:error)
  run_mode "$d" levels "$d" >/dev/null 2>&1
  f="$(ls "$d"/logs/tods/*.log | head -1)"
  c() { grep -ac -- "$1" "$f"; }
  [[ "$(c '\[foo\] lvl')" == 4 ]] && ok || bad "foo:debug shows error..debug (4 lines), got $(c '\[foo\] lvl')"
  [[ "$(c '\[bar\] lvl')" == 1 ]] && ok || bad "bar:error shows 1 line, got $(c '\[bar\] lvl')"
  [[ "$(c '\[baz\] lvl')" == 2 ]] && ok || bad "default warn shows error+warn (2 lines), got $(c '\[baz\] lvl')"
  d="$tmp/lv2_$cc"; mkdir -p "$d"; EXTRA_ENV=()
  run_mode "$d" levels "$d" >/dev/null 2>&1; f="$(ls "$d"/logs/tods/*.log | head -1)"
  [[ "$(c '\[baz\] lvl')" == 3 ]] && ok || bad "default level is info (3 lines), got $(c '\[baz\] lvl')"
  # the KZ3_* names still work as a fallback
  d="$tmp/lv3_$cc"; mkdir -p "$d"; EXTRA_ENV=(KZ3_LOG_LEVEL=trace)
  run_mode "$d" levels "$d" >/dev/null 2>&1; f="$(ls "$d"/logs/tods/*.log | head -1)"
  [[ "$(c '\[baz\] lvl')" == 5 ]] && ok || bad "KZ3_LOG_LEVEL=trace fallback (5 lines), got $(c '\[baz\] lvl')"

  # ---- rotation: 15 old tods logs, 12 old crash reports, one acit log, one build log; one run keeps 10 per kind
  d="$tmp/rot_$cc"; mkdir -p "$d/logs/tods" "$d/logs/acit"; EXTRA_ENV=()
  for i in $(seq -w 1 15); do : >"$d/logs/tods/QwarksAwakening-tods-202001$i-000000.log"; done
  for i in $(seq -w 1 12); do : >"$d/logs/tods/QwarksAwakening-tods-crash-202001$i-000000.txt"; done
  : >"$d/logs/acit/QwarksAwakening-acit-20200101-000000.log"; : >"$d/logs/tods/build-20200101-000000.log"
  run_mode "$d" crash "$d" >"$d/stdout.txt" 2>&1; rc=$?
  [[ $rc -eq 0 ]] && ok || bad "crash mode exit $rc"
  n=$(ls "$d"/logs/tods/QwarksAwakening-tods-[0-9]*.log | wc -l)
  [[ $n -eq 10 ]] && ok || bad "rotation keeps 10 logs, found $n"
  [[ -e "$d/logs/tods/QwarksAwakening-tods-20200101-000000.log" ]] && bad "oldest log not removed" || ok
  [[ -e "$d/logs/tods/QwarksAwakening-tods-20200115-000000.log" ]] && ok || bad "newest old log was removed"
  n=$(ls "$d"/logs/tods/QwarksAwakening-tods-crash-*.txt | wc -l)
  [[ $n -eq 10 ]] && ok || bad "rotation keeps 10 crash reports, found $n"
  [[ -e "$d/logs/acit/QwarksAwakening-acit-20200101-000000.log" ]] && ok || bad "another game's log was touched"
  [[ -e "$d/logs/tods/build-20200101-000000.log" ]] && ok || bad "a build log was touched"
  # ---- crash report (Linux: text + ring; the minidump is Windows only)
  cr="$(ls -t "$d"/logs/tods/QwarksAwakening-tods-crash-*.txt | head -1)"
  [[ "$(basename "$cr")" =~ ^QwarksAwakening-tods-crash-[0-9]{8}-[0-9]{6}(-[0-9]+)?\.txt$ ]] && ok || bad "crash file name $(basename "$cr")"
  has "$cr" "^Qwark's Awakening crash report \(game tods\)" crash-head
  has "$cr" 'reason: unit test fault' crash-reason
  has "$cr" 'rip=0x1234' crash-details
  has "$cr" 'line 299 at' crash-ring-newest
  hasnt "$cr" 'line 50 at' crash-ring-only-last-200
  hasnt "$cr" 'zq_fakeuser_77' redaction-crash
  for lf in "$d"/logs/tods/QwarksAwakening-tods-2*.log; do hasnt "$lf" zq_fakeuser_77 redaction-log-after-crash; done

  # ---- a shutdown that did not come through rc_log_exit still writes an end block
  d="$tmp/ne_$cc"; mkdir -p "$d"; EXTRA_ENV=()
  run_mode "$d" noexit "$d" >/dev/null 2>&1; f="$(ls "$d"/logs/tods/*.log | head -1)"
  has "$f" 'end: the runtime ended the process .*run time' end-without-code

  # ---- sha256 against the system tool
  mine="$("$tmp/t" sha "$tmp/t" | cut -d' ' -f1)"; theirs="$(sha256sum "$tmp/t" | cut -d' ' -f1)"
  [[ "$mine" == "$theirs" ]] && ok || bad "sha256 differs: $mine vs $theirs"
  # ---- router levels
  "$tmp/t" route >"$tmp/route.txt"
  has "$tmp/route.txt" '^\[mfc-list\] x -> 4 tag=.mfc-list.' route-trace
  has "$tmp/route.txt" '^\[hle\] unresolved -> 1 ' route-warn
  has "$tmp/route.txt" '^\[CRASH\] boom -> 0 ' route-error
  has "$tmp/route.txt" '^\[fps\] 60 -> 2 ' route-info
done

echo "== MinGW (Windows build of the logger sources)"
if command -v x86_64-w64-mingw32-gcc >/dev/null; then
  for s in kz3_log.c rc_log_info.c; do
    if x86_64-w64-mingw32-gcc -std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter -c -I"$log" -o "$tmp/w_$s.o" "$log/$s" 2>"$tmp/mw.txt"; then ok; echo "  $s: compiles"; [[ -s "$tmp/mw.txt" ]] && head -5 "$tmp/mw.txt"
    else bad "mingw compile of $s"; head -20 "$tmp/mw.txt"; fi
  done
else
  echo "  SKIP: x86_64-w64-mingw32-gcc not on PATH (run under nix develop .#win)"
fi

echo "$checks checks, $fails failed"
[[ $fails -eq 0 ]] && echo PASS || { echo FAIL; exit 1; }
