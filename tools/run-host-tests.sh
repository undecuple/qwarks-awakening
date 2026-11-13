#!/usr/bin/env bash
# Run every self-contained host test of the port (no game data, no GPU, no Windows/Wine) and print one line per test
# (ok / FAIL / SKIP) and a total. Exit 1 when any test FAILs. The list is tools/host-tests.txt (one path per line,
# `#` comments; `.sh` runs with bash, `.py` with python3 -I); the export ships the list unfiltered (port/ is exported whole).
#
#   nix develop .#ci --command tools/run-host-tests.sh [-v] [LIST]       (after tools/ps3recomp-patched.sh)
#
# Needs work/ps3recomp-patched (a missing tree is a FAIL for every test that links it, never a silent pass).
# -v prints the output of failed tests. RC_HOST_TESTS_LOGDIR=DIR keeps every test's full output there.
# SKIP = the test printed SKIP and exited 0 with no PASS line (a test that cannot run here). With RC_HOST_TESTS_STRICT=1
# (CI) ANY line starting with SKIP (also indented / [SKIP]) is a FAIL, so a green CI run never means "tested nothing", except
# for tests named in a `#:allow-skip PATH reason` line of the list (a known partial skip, with its reason).
# Each test is limited to RC_HOST_TESTS_TIMEOUT seconds (default 600). Run at low load: it compiles with gcc/clang.
#
# Left out on purpose (the ledger is at the end of tools/host-tests.txt): tests that need game data or lifted game code,
# or a tree the CI step does not build, or that are not pass/fail checks.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
verbose=0; [[ "${1:-}" == -v ]] && { verbose=1; shift; }
LIST="${1:-tools/host-tests.txt}"
TMO="${RC_HOST_TESTS_TIMEOUT:-600}"
STRICT="${RC_HOST_TESTS_STRICT:-0}"
[[ -f "$LIST" ]] || { echo "no test list $LIST" >&2; exit 2; }
[[ -d work/ps3recomp-patched ]] || echo "warning: work/ps3recomp-patched is missing (tools/ps3recomp-patched.sh): tests that link it will FAIL" >&2
out="$(mktemp -d "${TMPDIR:-/tmp}/rc-host-tests.XXXXXX")"; trap 'rm -rf "$out"' EXIT
export TMPDIR="$out"   # every test's scratch files land here and go away with it

# Extra arguments per test (the list holds bare paths).
args_for() { case "$1" in
  port/rc/tests/fios_index_check.py) echo "--selftest" ;;   # the real mode needs a game-derived cache.idx
  esac; }

declare -A ALLOW_SKIP=()
while IFS= read -r l; do
  [[ "$l" =~ ^\#:allow-skip[[:space:]]+([^[:space:]]+)[[:space:]]+(.+)$ ]] && ALLOW_SKIP["${BASH_REMATCH[1]}"]="${BASH_REMATCH[2]}"
done < "$LIST"

ok=0; fail=0; skip=0; failed=()
while IFS= read -r t; do
  [[ -z "$t" || "$t" == \#* ]] && continue
  log="$out/$(echo "$t" | tr '/' '_').log"
  if [[ ! -f "$t" ]]; then printf 'FAIL  %s (listed but missing)\n' "$t"; fail=$((fail+1)); failed+=("$t"); continue; fi
  a=(); read -ra a <<<"$(args_for "$t")"
  case "$t" in
    *.sh) cmd=(bash "$t" "${a[@]}") ;;
    *.py) cmd=(python3 -I "$t" "${a[@]}") ;;
    *) printf 'FAIL  %s (unknown type)\n' "$t"; fail=$((fail+1)); failed+=("$t"); continue ;;
  esac
  t0=$SECONDS
  timeout "$TMO" "${cmd[@]}" </dev/null >"$log" 2>&1; rc=$?
  dt=$((SECONDS - t0))
  if (( rc == 0 )); then
    # the tests' own pass lines: "PASS", "[PASS] x", "[run_x_test] PASS" (at line start only), "ok", "[ok]"
    if grep -qE '^[[:space:]]*\[?SKIP' "$log"; then
      if grep -qE '^(PASS|ok|\[ok\]|\[PASS\]|\[[A-Za-z0-9_]+\] PASS)' "$log"; then st=ok; else st=SKIP; fi
      # strict: any SKIP line fails, unless the test is on the explicit partial-skip allow-list
      if [[ "$STRICT" == 1 && -z "${ALLOW_SKIP[$t]:-}" ]]; then st=FAIL; fi
    else st=ok; fi
  else st=FAIL; fi
  [[ "$st" == SKIP && "$STRICT" == 1 ]] && st=FAIL
  case "$st" in
    ok) ok=$((ok+1)); printf 'ok    %s (%ss)\n' "$t" "$dt" ;;
    SKIP) skip=$((skip+1)); printf 'SKIP  %s (%s)\n' "$t" "$(grep -m1 -E '^\[?SKIP' "$log" | cut -c1-120)" ;;
    FAIL) fail=$((fail+1)); failed+=("$t")
          if (( rc == 0 )); then printf 'FAIL  %s (strict: SKIP line: %s)\n' "$t" "$(grep -m1 -E '^[[:space:]]*\[?SKIP' "$log" | cut -c1-100)"
          else printf 'FAIL  %s (exit %s, %ss)\n' "$t" "$rc" "$dt"; fi
          if (( verbose )); then tail -40 "$log" | sed 's/^/      | /'; fi ;;
  esac
  if [[ -n "${RC_HOST_TESTS_LOGDIR:-}" ]]; then mkdir -p "$RC_HOST_TESTS_LOGDIR"; cp "$log" "$RC_HOST_TESTS_LOGDIR/"; fi
done < "$LIST"
echo "total: $ok ok, $fail FAIL, $skip SKIP"
if (( fail )); then echo "failed: ${failed[*]}"; exit 1; fi
exit 0
