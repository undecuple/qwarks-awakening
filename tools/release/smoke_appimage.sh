#!/usr/bin/env bash
# Smoke test of the Linux launcher AppImage (pack_appimage.py) the way a player starts it, without FUSE (the release
# runner and containers have none): --appimage-extract-and-run in an empty temporary home under Xvfb. A normal start
# for 10 s (its first scan writes library.json through the engine with the system's python3, into the XDG data
# folder), then a screenshot of the library from the same AppImage. Fails when the launcher ends early, when there is
# no library.json or no screenshot, or when the AppImage file changed (nothing may be written into it). release.yml's
# linux job runs it; locally it runs in an ubuntu:22.04 container (docs/guides/tools.md). Never a GPU, never a game.
#
#   tools/release/smoke_appimage.sh APPIMAGE [SHOTDIR]     (screenshot to SHOTDIR, default: none kept)
set -euo pipefail
IMG="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
SHOTS="${2:-}"
[[ -f "$IMG" ]] || { echo "smoke_appimage: no $1" >&2; exit 2; }
command -v xvfb-run >/dev/null || { echo "smoke_appimage: xvfb-run is missing" >&2; exit 2; }
T="$(mktemp -d)"
trap 'chmod -R u+w -- "$T" 2>/dev/null; rm -rf -- "$T"' EXIT
export HOME="$T/user" XDG_DATA_HOME="$T/user/.local/share" XDG_CONFIG_HOME="$T/user/.config" TMPDIR="$T/tmp"
mkdir -p "$HOME" "$TMPDIR"
fail() { echo "smoke_appimage: FAIL $*" >&2; exit 1; }
before="$(sha256sum "$IMG" | cut -d' ' -f1)"
[[ -x "$IMG" ]] || fail "the AppImage is not executable"
[[ "$(head -c 4 "$IMG" | od -An -tx1 | tr -d ' \n')" == 7f454c46 ]] || fail "not an ELF file (no AppImage runtime)"

echo "== a normal start for 10 s (--appimage-extract-and-run; the first scan through the engine), then ended"
cd "$T"
set +e
timeout -s TERM 10 xvfb-run -a -s "-screen 0 1280x720x24" "$IMG" --appimage-extract-and-run > "$T/start.log" 2>&1
rc=$?
set -e
tail -n 5 "$T/start.log" || true
[[ $rc == 124 ]] || fail "the launcher ended by itself (exit $rc) before the 10 s were up"
lib="$XDG_DATA_HOME/qwarks-awakening/library.json"
[[ -s "$lib" ]] || fail "no library.json in the XDG data folder: the engine did not run"
python3 -c 'import json, sys; d = json.load(open(sys.argv[1])); assert len(d["games"]) >= 1
for g in d["games"]: print("  %-12s %s" % (g["id"], g.get("state")))' "$lib" || fail "library.json lists no game"

echo "== screenshot from the AppImage"
xvfb-run -a -s "-screen 0 1280x720x24" "$IMG" --appimage-extract-and-run --screenshot "$T/library.png" --screen library
[[ "$(head -c 8 "$T/library.png" | od -An -tx1 | tr -d ' \n')" == 89504e470d0a1a0a ]] || fail "no library screenshot"
if [[ -n "$SHOTS" ]]; then mkdir -p "$SHOTS"; cp "$T/library.png" "$SHOTS/appimage-library.png"; fi

[[ "$(sha256sum "$IMG" | cut -d' ' -f1)" == "$before" ]] || fail "the AppImage file changed"
echo "smoke_appimage: PASS"
