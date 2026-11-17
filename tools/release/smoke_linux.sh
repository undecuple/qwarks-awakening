#!/usr/bin/env bash
# Smoke test of the Linux launcher tarball (pack_linux.py) the way a player uses it: unpack into a temporary home,
# install.sh, a normal start under Xvfb (its first scan writes library.json through the engine with the system's
# python3), two screenshots from the installed copy, then install.sh --uninstall from the installed copy, which must
# leave only the player's data. release.yml's linux job runs it on the runner; locally it runs in an ubuntu:22.04
# container (docs/guides/tools.md). Needs bash, python3 >= 3.10, xvfb-run and the desktop libraries SDL loads
# (libX11, libEGL/libGL); never a GPU, never a game.
#
#   tools/release/smoke_linux.sh TARBALL [SHOTDIR]       (screenshots to SHOTDIR, default: none kept)
set -euo pipefail
TAR="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
SHOTS="${2:-}"
[[ -f "$TAR" ]] || { echo "smoke_linux: no $1" >&2; exit 2; }
command -v xvfb-run >/dev/null || { echo "smoke_linux: xvfb-run is missing" >&2; exit 2; }
T="$(mktemp -d)"
trap 'rm -rf -- "$T"' EXIT
export HOME="$T/home" XDG_DATA_HOME="$T/home/.local/share" XDG_CONFIG_HOME="$T/home/.config"
mkdir -p "$HOME"
fail() { echo "smoke_linux: FAIL $*" >&2; exit 1; }
tar -C "$T" -xf "$TAR"
top="$(tar -tf "$TAR" | sed -n 1p | cut -d/ -f1)"
src="$T/$top"
app="$XDG_DATA_HOME/qwarks-awakening/launcher"
prog="$HOME/.local/bin/qwarks-awakening"
x() { xvfb-run -a -s "-screen 0 1280x720x24" "$@"; }

echo "== ldd (the C library only)"
ldd "$src/qwarks-awakening" | tee "$T/ldd.txt"
if grep -v -E '^\s*(linux-vdso\.so|libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|/lib64/ld-linux-x86-64\.so\.2)' "$T/ldd.txt" | grep -q .; then
  fail "the launcher needs a library outside the C library"
fi
grep -q "not found" "$T/ldd.txt" && fail "a library was not found"

echo "== install.sh"
(cd "$src" && ./install.sh)
[[ -x "$app/qwarks-awakening" && -L "$prog" ]] || fail "install.sh did not install the launcher and its link"
grep -q "^Exec=\"$app/qwarks-awakening\"$" "$XDG_DATA_HOME/applications/qwarks-awakening.desktop" || fail "desktop entry"

echo "== a normal start for 10 s (the first scan through the engine), then ended"
cd "$T"
set +e
timeout -s TERM 10 xvfb-run -a -s "-screen 0 1280x720x24" "$prog" > "$T/start.log" 2>&1
rc=$?
set -e
tail -n 5 "$T/start.log" || true
[[ $rc == 124 ]] || fail "the launcher ended by itself (exit $rc) before the 10 s were up"
[[ -s "$XDG_DATA_HOME/qwarks-awakening/library.json" ]] || fail "no library.json: the engine did not run"
python3 -c 'import json, sys; d = json.load(open(sys.argv[1])); assert len(d["games"]) >= 1' \
  "$XDG_DATA_HOME/qwarks-awakening/library.json" || fail "library.json lists no game"

echo "== screenshots from the installed copy"
x "$prog" --screenshot "$T/library.png" --screen library
x "$prog" --screenshot "$T/settings.png" --screen settings
for s in library settings; do
  [[ "$(head -c 8 "$T/$s.png" | od -An -tx1 | tr -d ' \n')" == 89504e470d0a1a0a ]] || fail "no $s screenshot"
done
if [[ -n "$SHOTS" ]]; then mkdir -p "$SHOTS"; cp "$T/library.png" "$T/settings.png" "$SHOTS/"; fi

echo "== install.sh --uninstall (the installed copy)"
"$app/install.sh" --uninstall
[[ ! -e "$app" && ! -L "$prog" && ! -e "$XDG_DATA_HOME/applications/qwarks-awakening.desktop" ]] || fail "uninstall left files"
[[ -f "$XDG_DATA_HOME/qwarks-awakening/library.json" ]] || fail "uninstall removed the player's library"
echo "smoke_linux: PASS"
