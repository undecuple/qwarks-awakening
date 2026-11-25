#!/usr/bin/env bash
# Run a command inside a bubblewrap sandbox.
#
#   tools/sandbox.sh [--gpu] [--net] [--ro PATH]... -- <command> [args...]
#
# Inside the sandbox:
#   - /nix (store) is read-only, the game disc ($RC_ROOT/Game) is read-only,
#   - only $RC_ROOT/work and $RC_ROOT/build are writable,
#   - the rest of the repo is read-only, $HOME is an empty tmpfs,
#   - no network (unless --net), no access to the user's display, audio or sessions,
#   - --gpu exposes the NVIDIA device nodes + /run/opengl-driver (for Vulkan/RPCS3 runs).
#   - --ro PATH binds an extra host path read-only at the same location (e.g. a Proton install).
#   - --x11 (maintainer only) exposes the user's X display :0
#     (Xwayland socket) as $RC_REAL_DISPLAY; still no network, no audio, no D-Bus. Game controllers only from
#     /dev/input (see input_nodes below), never the keyboard, mouse or security-key nodes. The X socket itself is what
#     any X client gets: the program can see input sent to the X display and read the screen through it, which is why
#     --x11 stays maintainer-only (a nested X server would close that; not done).
#   tools/sandbox.sh --list-input-nodes   prints the /dev/input nodes --x11 would bind (nothing runs)
#   RC_SANDBOX_DRY=1 tools/sandbox.sh ... prints the bwrap arguments one per line instead of running (tests); only then
#     RC_SANDBOX_TEST_{DEV,SYS,UDEV,XSOCK} replace /dev/input, /sys/class/input, /run/udev/data and the X socket
# Run it from inside `nix develop` so the tools are on PATH.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IN_DEV=/dev/input IN_SYS=/sys/class/input IN_UDEV=/run/udev/data XSOCK=/tmp/.X11-unix/X0
DRY=0
if [[ "${RC_SANDBOX_DRY:-0}" == 1 || "${1:-}" == --list-input-nodes ]]; then
  DRY=1; IN_DEV="${RC_SANDBOX_TEST_DEV:-$IN_DEV}" IN_SYS="${RC_SANDBOX_TEST_SYS:-$IN_SYS}"
  IN_UDEV="${RC_SANDBOX_TEST_UDEV:-$IN_UDEV}" XSOCK="${RC_SANDBOX_TEST_XSOCK:-$XSOCK}"
fi
# input_nodes (2026-11-25; it used to bind all of /dev/input, keyboards, mice and security keys included):
# the evdev (event*) and joystick (js*) nodes whose udev record (/run/udev/data/c<major>:<minor>, the database Wine's
# winebus/SDL enumerate with; major:minor from /sys/class/input/<node>/dev) says E:ID_INPUT_JOYSTICK=1. udev's input_id
# builtin sets that from the device's own capabilities (gamepad buttons, joystick axes), whatever made the device: a
# USB/Bluetooth pad on xpad or hid-sony, Steam Input's or another uinput virtual pad. A keyboard (ID_INPUT_KEY/KEYBOARD),
# a mouse (ID_INPUT_MOUSE) or a security key never carries it. Nodes without a udev record are left out.
input_nodes() { local n mm
  for n in "$IN_DEV"/event* "$IN_DEV"/js*; do
    [[ -e $n ]] || continue
    mm="$(cat "$IN_SYS/${n##*/}/dev" 2>/dev/null)" || continue
    grep -qsx 'E:ID_INPUT_JOYSTICK=1' "$IN_UDEV/c$mm" && echo "$n"
  done
  return 0; }
[[ "${1:-}" == --list-input-nodes ]] && { input_nodes; exit 0; }
GPU=0 NET=0
EXTRA_RO=()
LANE_RO=()
X11=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --gpu) GPU=1; shift ;;
    --net) NET=1; shift ;;
    --ro) EXTRA_RO+=("$2"); shift 2 ;;
    --x11) X11=1; shift ;;
    --) shift; break ;;
    *) break ;;
  esac
done
[[ $# -gt 0 ]] || { echo "usage: $0 [--gpu] [--net] -- cmd..." >&2; exit 2; }

# macOS build hosts (docs/BUILDING.md): bubblewrap is Linux-only, so the command runs directly, from the repo root,
# with the same RC_* paths. The game dump stays untouched because the build only reads it; lanes never run here.
if [[ "$(uname -s)" == Darwin ]]; then
  [[ -e "$ROOT/.rc-lane" ]] && { echo "sandbox.sh: lane worktrees need the Linux sandbox" >&2; exit 1; }
  echo "sandbox.sh: macOS, no bubblewrap: running without a sandbox" >&2
  cd "$ROOT" && export RC_ROOT="$ROOT" RC_GAME="$ROOT/Game" RC_WORK="$ROOT/work" && exec "$@"
fi

# Lane profile (least privilege). In a lane worktree (.rc-lane exists) the policy comes from the MAIN checkout's
# tools/agents/lanes.json, which lanes cannot edit: never network; GPU and extra read-only binds only for lanes
# with "gpu": true. Fails closed if the policy cannot be read.
if [[ -f "$ROOT/.rc-lane" ]]; then
  LANE="$(cat "$ROOT/.rc-lane")"
  POLICY_MAIN="$(cat "$ROOT/.rc-main" 2>/dev/null || true)"
  LANE_GPU="$(python3 -c 'import json,sys; print(int(bool(json.load(open(sys.argv[1]))["lanes"][sys.argv[2]]["gpu"])))' \
               "$POLICY_MAIN/tools/agents/lanes.json" "$LANE" 2>/dev/null || echo 0)"
  [[ $NET -eq 1 ]] && { echo "sandbox: lane '$LANE' may not use --net" >&2; exit 3; }
  [[ $X11 -eq 1 ]] && { echo "sandbox: lane '$LANE' may not use the user's display (--x11)" >&2; exit 3; }
  if [[ $GPU -eq 1 || ${#EXTRA_RO[@]} -gt 0 ]] && [[ "$LANE_GPU" != 1 ]]; then
    echo "sandbox: lane '$LANE' may not use --gpu or extra binds" >&2; exit 3
  fi
  # read_dirs (2026-10-16, the kz2 lane: RC agents doing static work for the Ratchet 2 port): host folders the
  # lane reads but never writes, bound read-only from the policy (never from the lane's own arguments).
  while IFS= read -r d; do [[ -n "$d" && -d "$d" ]] && LANE_RO+=("$d"); done < <(python3 -c 'import json,sys
for d in json.load(open(sys.argv[1]))["lanes"][sys.argv[2]].get("read_dirs", []): print(d)' "$POLICY_MAIN/tools/agents/lanes.json" "$LANE" 2>/dev/null)
fi

mkdir -p "$ROOT/work" "$ROOT/build"

args=(
  --die-with-parent --new-session
  --unshare-all
  --ro-bind /nix /nix
  --ro-bind /etc/resolv.conf /etc/resolv.conf
  --ro-bind-try /etc/ssl /etc/ssl
  --ro-bind-try /etc/static/ssl /etc/static/ssl
  --ro-bind /run/current-system/sw /run/current-system/sw
  --ro-bind-try /usr/bin/env /usr/bin/env
  --ro-bind-try /bin/sh /bin/sh
  --proc /proc --dev /dev --tmpfs /tmp
  --tmpfs "$HOME"
  --ro-bind "$ROOT" "$ROOT"
  --bind "$ROOT/work" "$ROOT/work"
  --bind "$ROOT/build" "$ROOT/build"
  --chdir "$ROOT"
  --setenv HOME "$HOME"
  --setenv RC_ROOT "$ROOT"
  --setenv RC_GAME "$ROOT/Game"
  --setenv RC_WORK "$ROOT/work"
  --unsetenv DISPLAY --unsetenv WAYLAND_DISPLAY
  --unsetenv PULSE_SERVER --unsetenv DBUS_SESSION_BUS_ADDRESS
  --setenv PATH "$ROOT/tools/agents/shims:$PATH"   # ninja shim: every build takes a build slot (tools/agents/shims/ninja)
  --setenv TMPDIR /tmp --setenv TMP /tmp --setenv TEMP /tmp --setenv TEMPDIR /tmp
)
# Game is read-only. In lane worktrees (and when it lives on another disk) Game is a symlink: bwrap cannot
# mount over a symlink, so bind the real target read-only at its own path and let the link resolve.
if [[ -L "$ROOT/Game" ]]; then
  G="$(readlink -f "$ROOT/Game")"; [[ -e "$G" ]] && args+=(--ro-bind "$G" "$G")
elif [[ -e "$ROOT/Game" ]]; then
  args+=(--ro-bind "$ROOT/Game" "$ROOT/Game")
fi
# GameUpdate: link to the installed game update (RPCS3 dev_hdd0/game/<title id>), read-only like Game.
if [[ -L "$ROOT/GameUpdate" ]]; then
  U="$(readlink -f "$ROOT/GameUpdate")"; [[ -e "$U" ]] && args+=(--ro-bind "$U" "$U") && args+=(--setenv RC_UPDATE "$ROOT/GameUpdate")
fi
# library/ game folders kept on another drive and linked back: point work/static-library (a symlink, untracked) or
# RC_STATIC_LIBRARY at that folder and it is bound read-only at its real path so the links resolve. Not for lanes.
SL=${RC_STATIC_LIBRARY:-$ROOT/work/static-library}
if [[ ! -f "$ROOT/.rc-lane" && -e "$SL" ]]; then SL="$(readlink -f "$SL")"; [[ -d "$SL" ]] && args+=(--ro-bind "$SL" "$SL"); fi
if [[ $NET -eq 1 ]]; then
  args+=(--share-net)
  # Inside the steam-run FHS shell (`nix develop`), /etc/resolv.conf and /etc/ssl point into /.host-etc, which is not
  # mounted here, so DNS and the CA bundle were unreachable (telemetry trial 2026-10-13: WinHTTP 12157). Only with
  # --net, and only these three read-only paths of the host's /etc.
  [[ -d /.host-etc ]] && args+=(--ro-bind-try /.host-etc/resolv.conf /.host-etc/resolv.conf
                                --ro-bind-try /.host-etc/ssl /.host-etc/ssl --ro-bind-try /.host-etc/static/ssl /.host-etc/static/ssl)
fi
# Lane worktrees (tools/agents/new-lane.sh): the main checkout's work/ is shared read-only (decrypted EBOOT,
# analysis, lifted code via symlinks), its work/locks stays writable for flock.
if [[ -f "$ROOT/.rc-main" ]]; then
  MAIN="$(cat "$ROOT/.rc-main")"
  if [[ -d "$MAIN/work" && "$MAIN" != "$ROOT" ]]; then
    mkdir -p "$MAIN/work/locks"
    args+=(--ro-bind "$MAIN/work" "$MAIN/work" --bind "$MAIN/work/locks" "$MAIN/work/locks" --setenv RC_MAIN "$MAIN")
    # A worktree's .git is a file pointing into the main checkout's .git; bind it read-only so git works inside the
    # sandbox (tools/ps3recomp-patched.sh's git apply, tools/checkpoint.py's commit stamp). Nothing can be written.
    [[ -d "$MAIN/.git" ]] && args+=(--ro-bind "$MAIN/.git" "$MAIN/.git")
  fi
fi
for p in "${EXTRA_RO[@]}"; do args+=(--ro-bind "$(readlink -f "$p")" "$p"); done
for p in "${LANE_RO[@]}"; do args+=(--ro-bind "$p" "$p"); done
if [[ $X11 -eq 1 ]]; then
  [[ -S "$XSOCK" ]] || { echo "sandbox: no X display socket $XSOCK" >&2; exit 2; }
  args+=(--bind "$XSOCK" /tmp/.X11-unix/X0 --setenv RC_REAL_DISPLAY :0)
  # The user's game controller for real-display play sessions (maintainer only, like --x11): only the pad nodes
  # (input_nodes above; a pad plugged in after the start is not seen) and the udev database Wine's winebus (SDL)
  # enumerates them with. Xbox pads on xpad have no hidraw node.
  while IFS= read -r d; do args+=(--dev-bind "$d" "$d"); done < <(input_nodes)
  args+=(--ro-bind-try /run/udev /run/udev)
  # Sony pads (DualShock 4, DualSense) reach Wine's winebus through their hidraw node, not evdev: a DualShock 4 gave no
  # input without it. Only Sony (vendor 054C) nodes the user can open are bound: the seat's ACL can also grant security
  # keys and other HID devices, which game code must not reach.
  for d in /dev/hidraw*; do
    [[ -r $d && -w $d ]] || continue
    grep -qs '^HID_ID=[0-9A-Fa-f]*:0000054[Cc]:' "/sys/class/hidraw/${d#/dev/}/device/uevent" && args+=(--dev-bind "$d" "$d")
  done
fi
if [[ $GPU -eq 1 ]]; then
  args+=(--ro-bind /run/opengl-driver /run/opengl-driver)
  for d in /dev/nvidia* /dev/dri; do [[ -e $d ]] && args+=(--dev-bind "$d" "$d"); done
  args+=(--ro-bind-try /sys /sys)
fi

(( DRY )) && { printf '%s\n' "${args[@]}" -- "$@"; exit 0; }
exec bwrap "${args[@]}" -- "$@"
