#!/usr/bin/env bash
# Inside the sandbox (called by tools/proton-test.sh): Xvfb + Proton run + screenshot + cleanup.
set -uo pipefail
PROTON_DIR="$1" RUN="$2" SECS="$3" SHOT="$4" EXE="$5"; shift 5

if [[ -n "${RC_REAL_DISPLAY:-}" ]]; then
  export DISPLAY="$RC_REAL_DISPLAY"; XVFB=""          # user's display (proton-test.sh --real-display)
else
  export DISPLAY=:99
  Xvfb :99 -screen 0 "${RC_XVFB_SCREEN:-1280x720}x24" -nolisten tcp >/dev/null 2>&1 &
  XVFB=$!
  for _ in $(seq 50); do [[ -e /tmp/.X11-unix/X99 ]] && break; sleep 0.1; done
fi

export STEAM_COMPAT_DATA_PATH="$RUN/compat"
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$RUN/steam"
export WINEDLLOVERRIDES="winepulse.drv,winealsa.drv,wineoss.drv=d${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
export PROTON_LOG=1 PROTON_LOG_DIR="$RUN"
export VK_ICD_FILENAMES=/run/opengl-driver/share/vulkan/icd.d/nvidia_icd.json
# rc.exe with the logger (patch 2030) writes only errors to stdout unless this is set; checks parse stdout.
export RC_LOG_STDOUT="${RC_LOG_STDOUT:-1}" RC_NO_ERROR_BOX="${RC_NO_ERROR_BOX:-1}"
# The flip log ("[flip] N at T ms") is what shot_overlay.py and make_clip.py take the fps from (shared-docs/overlay.md).
export GCM_FLIPCOUNT="${GCM_FLIPCOUNT:-1}"
cd "$RUN/app" || exit 1

# Proton does not wire a console program's stdout to ours; capture it with cmd's redirect instead
# and pass the program's exit code back with delayed expansion (!errorlevel!). Proton's log: proton.log.
# Each argument is quoted (2026-10-19: `$*` split arguments with spaces; this repo's path has a space and
# '&'). The command goes into app/run.bat rather than one `cmd /c "..."` string: the quotes around an argument closed
# the outer /c quote, so the '&' in the path split the command (first ToD start, 2026-10-19: the EBOOT path arrived
# cut at "Ratchet"). In a batch file a quoted '&' is literal. A '%' in an argument would still expand (none here).
# EXE is a bare name in app/ (proton-test.sh passes its basename), so it needs no quotes.
ARGQ=""; for a in "$@"; do ARGQ+=" \"$a\""; done
# RC_WINE_UNMANAGED=1 (2026-11-09, window stage 1c): X11 driver Managed=N in this run's prefix before the program
# starts. Bare Xvfb has no window manager, so a managed Wine window is only its client area with nobody drawing the
# frame; unmanaged, Wine draws the title bar and borders itself, and a mouse drag on them runs the real modal
# move/size loop (driven by an RC_XDO script). Changes window decoration only; never set for ordinary runs.
PRE=""
[[ "${RC_WINE_UNMANAGED:-0}" == 1 ]] && PRE='reg add "HKCU\Software\Wine\X11 Driver" /v Managed /t REG_SZ /d N /f >nul 2>&1'$'\r\n'
printf '@echo off\r\n%s%s%s > app_stdout.txt 2>&1\r\nset RC_EXIT=%%errorlevel%%\r\necho [exit code %%RC_EXIT%%]>> app_stdout.txt\r\nexit /b %%RC_EXIT%%\r\n' \
  "$PRE" "$EXE" "$ARGQ" > run.bat
steam-run "$PROTON_DIR/proton" run cmd /c run.bat >"$RUN/proton.log" 2>&1 &
APP=$!
# RC_XDO=SCRIPT (2026-11-09, window stage 1c): a bash script run alongside the program on the same display (Xvfb or
# the real one), for X input the Win32 side cannot fake from inside, e.g. an xdotool script (a mouse drag on the title bar
# or border, or Alt+Space -> Move / Size: Wine's own modal move/size loop). Its output goes to <run>/xdo.log; it is killed with the run.
XDO=""
if [[ -n "${RC_XDO:-}" ]]; then
  bash "$RC_XDO" > "$RUN/xdo.log" 2>&1 &
  XDO=$!
fi
# RC_VIDEO=START:SECONDS: snapshot the hidden display from START s
# for SECONDS s as fast as import manages (~5-10/s), then tools/make_clip.py builds <shot>.mp4 at wall-clock speed
# with the game's own fps (from its [flip] log, GCM_FLIPCOUNT=1) burned in. Xvfb only, never the user's display.
VID=""
if [[ -n "${RC_VIDEO:-}" && -n "$XVFB" ]]; then
  VSTART="${RC_VIDEO%%:*}" VLEN="${RC_VIDEO##*:}"; VDIR="$RUN/clip"; mkdir -p "$VDIR"
  ( sleep "$VSTART"; T0=$(date +%s.%N); i=0
    while :; do
      now=$(date +%s.%N); awk -v a="$now" -v b="$T0" -v l="$VLEN" 'BEGIN{exit !(a-b < l)}' || break
      f=$(printf '%06d.jpg' $i); import -window root -quality 85 "$VDIR/$f" 2>/dev/null \
        && echo "$f $(awk -v a="$now" -v b="$T0" -v s="$VSTART" 'BEGIN{printf "%.3f", a-b+s}')" >> "$VDIR/times.txt"
      i=$((i+1))
    done ) &
  VID=$!
fi
# RC_EXIT_EARLY=1 (opt-in, for runs that end by themselves such as a toolchain trial): stop waiting once the
# program has exited, so a long --seconds cap does not hold the GPU lock after the work is done.
if [[ "${RC_EXIT_EARLY:-0}" == 1 ]]; then
  for (( _t = 0; _t < SECS; _t += 5 )); do
    grep -aq '\[exit code' "$RUN/app/app_stdout.txt" 2>/dev/null && break
    sleep 5
  done
else
  sleep "$SECS"
fi
[[ -n "$VID" ]] && { kill "$VID" 2>/dev/null; wait "$VID" 2>/dev/null; }
[[ -n "$XDO" ]] && { kill "$XDO" 2>/dev/null; wait "$XDO" 2>/dev/null; }
# Alive = the cmd wrapper has not written its "[exit code N]" line yet. The steam-run/proton launcher PID is not
# a reliable signal: it can return while the game keeps running, which used to report a live run as "status 255".
ALIVE=1; grep -aq '\[exit code' "$RUN/app/app_stdout.txt" 2>/dev/null && ALIVE=0
if [[ -z "$XVFB" ]]; then
  # Real display: capture the game's own window only, never the user's screen.
  WID=""
  for q in "--class ${EXE}" "--class ${EXE%.exe}" "--name ${EXE%.exe}" "--name Ratchet"; do
    WID="$(xdotool search --onlyvisible $q 2>/dev/null | tail -1)"; [[ -n "$WID" ]] && break
  done
  [[ -n "$WID" ]] && import -window "$WID" "$SHOT" 2>/dev/null && echo "screenshot: $SHOT (game window)"
else
  import -window root "$SHOT" 2>/dev/null && echo "screenshot: $SHOT"
fi
if [[ -n "$VID" && -s "$RUN/clip/times.txt" ]]; then
  python3 "$(dirname "$0")/make_clip.py" "$RUN/clip" "$RUN/app/app_stdout.txt" "${SHOT%.png}.mp4" || echo "clip: failed"
fi
STATUS=0
if [[ $ALIVE -eq 1 ]]; then
  echo "program still running after ${SECS}s (expected for a render loop); stopping it"
else
  wait "$APP" 2>/dev/null
  STATUS="$(grep -ao 'exit code -\?[0-9]*' "$RUN/app/app_stdout.txt" 2>/dev/null | tail -1 | grep -o -- '-\?[0-9]*$')"
  STATUS="${STATUS:-255}"; echo "program exited with status $STATUS"
fi
# Stop Proton/wine for this prefix and Xvfb; everything is inside this sandbox's PID namespace.
STEAM_COMPAT_DATA_PATH="$RUN/compat" steam-run "$PROTON_DIR/files/bin/wineserver" -k >/dev/null 2>&1 || true
kill "$APP" "$XVFB" 2>/dev/null || true
echo "--- program output (tail) ---"; tail -n 30 "$RUN/app/app_stdout.txt" 2>/dev/null || echo "(no output file)"
[[ "$STATUS" =~ ^[0-9]+$ ]] || STATUS=1
exit $(( STATUS > 255 ? 1 : STATUS ))
