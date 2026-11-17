#!/usr/bin/env bash
# Qwark's Awakening launcher for Linux: install for this user, or remove it again. No administrator rights; only files
# under your home folder are written (the XDG base directories):
#
#   ./install.sh               install, or replace an earlier install with this download
#   ./install.sh --uninstall   remove the launcher (your library, built games, prefixes, Proton runtimes, build tools,
#                              saves and settings stay; the last lines say where)
#   ./install.sh --help
#
# Where the files go ($XDG_DATA_HOME defaults to ~/.local/share):
#   $XDG_DATA_HOME/qwarks-awakening/launcher/                     the launcher, its engine and the build files
#   ~/.local/bin/qwarks-awakening                                 a link to the launcher, to start it from a terminal
#   $XDG_DATA_HOME/applications/qwarks-awakening.desktop          the menu entry
#   $XDG_DATA_HOME/icons/hicolor/256x256/apps/qwarks-awakening.png  its icon
# The launcher keeps your library and games in $XDG_DATA_HOME/qwarks-awakening/ and its settings in
# $XDG_CONFIG_HOME/qwarks-awakening/; neither is touched by this script.
#
# Needs Python 3.10 or newer as `python3` (the launcher's engine). Part of the Qwark's Awakening release (MIT).
set -euo pipefail

APPNAME=qwarks-awakening
PROGRAM=qwarks-awakening
MARK=.qwarks-awakening-install           # in the launcher folder: what this script installed (removed on --uninstall)

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; }
say() { printf '%s\n' "$*"; }
die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

xdg() {   # an absolute $1, else $HOME/$2 (the XDG rule: relative values are ignored)
  local v="${!1:-}"
  if [[ "$v" == /* ]]; then printf '%s\n' "$v"; else printf '%s/%s\n' "$HOME" "$2"; fi
}

[[ -n "${HOME:-}" && "$HOME" == /* ]] || die "HOME is not set to an absolute folder."
DATA="$(xdg XDG_DATA_HOME .local/share)"
CONFIG="$(xdg XDG_CONFIG_HOME .config)"
APP="$DATA/$APPNAME/launcher"
BIN="$HOME/.local/bin"
DESKTOP="$DATA/applications/$APPNAME.desktop"
ICON="$DATA/icons/hicolor/256x256/apps/$APPNAME.png"
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"

kept() {
  say "Kept: your library, built games, prefixes, Proton runtimes, build tools and saves in $DATA/$APPNAME/"
  say "      and the settings in $CONFIG/$APPNAME/. Delete those folders yourself to remove them too."
}

desktop_exec() {   # the Exec value: the path in double quotes, with the characters the desktop spec escapes
  local p="$1"
  p="${p//\\/\\\\}"; p="${p//\"/\\\"}"; p="${p//\`/\\\`}"; p="${p//\$/\\\$}"; p="${p//%/%%}"
  printf '"%s"' "$p"
}

uninstall() {
  if [[ ! -f "$APP/$MARK" ]]; then
    say "The launcher is not installed in $APP (nothing removed)."
    kept
    return 0
  fi
  # the outside files only when they are still ours: the link pointing into the launcher folder, the menu entry
  # naming it, the icon equal to the installed one
  if [[ -L "$BIN/$PROGRAM" && "$(readlink "$BIN/$PROGRAM")" == "$APP/$PROGRAM" ]]; then
    rm -f "$BIN/$PROGRAM"; say "Removed $BIN/$PROGRAM"
  fi
  if [[ -f "$DESKTOP" ]] && grep -qxF "X-Qwarks-Awakening-Install=$APP" "$DESKTOP"; then
    rm -f "$DESKTOP"; say "Removed $DESKTOP"
  fi
  if [[ -f "$ICON" && -f "$APP/$APPNAME.png" ]] && cmp -s "$ICON" "$APP/$APPNAME.png"; then
    rm -f "$ICON"; say "Removed $ICON"
  fi
  rm -rf -- "$APP"
  say "Removed $APP"
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$DATA/applications" 2>/dev/null || true
  kept
}

install() {
  [[ -x "$SRC/$PROGRAM" && -f "$SRC/tools/release/catalog.py" && -f "$SRC/$APPNAME.desktop" ]] \
    || die "run the install.sh of an unpacked download (the folder holding $PROGRAM, tools/ and licenses/)."
  [[ "$SRC" != "$APP" ]] || die "this is the installed copy; run install.sh from a new download to update, or --uninstall."
  if [[ -e "$APP" && ! -f "$APP/$MARK" ]]; then
    die "$APP exists but was not made by this script; move it away first."
  fi
  local py
  py="$(command -v python3 || true)"
  [[ -n "$py" ]] || die "python3 was not found. Install Python 3.10 or newer from your distribution, then run install.sh again."
  "$py" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' \
    || die "$("$py" --version 2>&1) is too old: the launcher needs Python 3.10 or newer."
  "$py" -c 'import tarfile; tarfile.data_filter' 2>/dev/null \
    || say "Note: this Python cannot unpack GE-Proton safely (no tarfile data filter; 3.10.12 or newer has it): choose a Proton you already have in Settings > Proton."

  # copy into a new folder next to the old one, then swap: an interrupted install leaves the old one working
  mkdir -p "$DATA/$APPNAME"
  local new="$APP.new.$$" old="$APP.old.$$"
  rm -rf -- "$new"
  cp -R -- "$SRC/." "$new"
  local version
  version="$(sed -n 's/^Qwark.s Awakening \([0-9][0-9.]*\) for Linux.*/\1/p' "$SRC/README.txt" | head -n 1)"
  printf 'version=%s\nlauncher=%s\n' "${version:-unknown}" "$APP" > "$new/$MARK"
  if [[ -e "$APP" ]]; then mv -- "$APP" "$old"; fi
  mv -- "$new" "$APP"
  rm -rf -- "$old"

  mkdir -p "$BIN" "$(dirname "$DESKTOP")" "$(dirname "$ICON")"
  if [[ -e "$BIN/$PROGRAM" && ! -L "$BIN/$PROGRAM" ]]; then
    say "Left $BIN/$PROGRAM alone (not a link made by this script); start the launcher from the menu or $APP/$PROGRAM."
  else
    ln -sfn -- "$APP/$PROGRAM" "$BIN/$PROGRAM"
  fi
  cp -- "$APP/$APPNAME.png" "$ICON"
  {
    grep -v -e '^Exec=' -e '^Icon=' -e '^TryExec=' -e '^Path=' "$APP/$APPNAME.desktop"
    printf 'Exec=%s\n' "$(desktop_exec "$APP/$PROGRAM")"
    printf 'Icon=%s\n' "$APPNAME"
    printf 'X-Qwarks-Awakening-Install=%s\n' "$APP"
  } > "$DESKTOP.tmp"
  mv -- "$DESKTOP.tmp" "$DESKTOP"
  command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$DATA/applications" 2>/dev/null || true

  say "Installed Qwark's Awakening ${version:-} for $(id -un 2>/dev/null || echo you) in $APP"
  say "Start it from your applications menu, or run: $BIN/$PROGRAM"
  case ":${PATH:-}:" in
    *":$BIN:"*) ;;
    *) say "($BIN is not on your PATH: add it there to start the launcher by its name, $PROGRAM.)" ;;
  esac
  say "To remove it later: $APP/install.sh --uninstall"
}

case "${1:-}" in
  "") install ;;
  --uninstall) uninstall ;;
  -h|--help) usage ;;
  *) usage >&2; exit 2 ;;
esac
