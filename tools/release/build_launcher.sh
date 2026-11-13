#!/usr/bin/env bash
# Build the launcher preview (launcher/, SDL3 + Dear ImGui) for Windows x64 with the pinned toolchain that
# tools/release/fetch_toolchain.py unpacks (llvm-mingw, SDL3's MinGW devel package, CMake, Ninja). The release
# workflow runs it on a GitHub Windows runner in Git Bash; on Linux it cross-compiles the same way with llvm-mingw's
# Linux build. From the Killzone 3 port's tools/release/build_launcher.sh (same author, MIT): same idea, CMake instead
# of one C file.
#
#   tools/release/build_launcher.sh [OUTDIR]          (default build/launcher-release)
#
# Environment (defaults: the toolchain cache's trees, work/release/toolchain-cache/trees/<name>):
#   LLVM_MINGW  llvm-mingw root (its bin/ goes first on PATH: windres and clang find their helpers there)
#   SDL3_ROOT   SDL3 devel package root (x86_64-w64-mingw32/ inside it is the prefix CMake searches)
#   CMAKE       cmake program (default: the cache's cmake on Windows, else cmake on PATH)
#   NINJA       ninja program (default: the cache's ninja on Windows, else ninja on PATH)
#   LAUNCHER_KIT_MANIFEST  kit.json from pack_zip.py --make-kit-manifest (launcher phase 4): the launcher carries its
#               hash, the full build zip's URL and sizes; unset = a launcher that asks for no build kit
#   LAUNCHER_SYMBOLS=1  build with -g, then move the debug information to OUTDIR/launcher.exe.debug (objcopy
#               --only-keep-debug, --strip-all, --add-gnu-debuglink): the exe is as small as the plain build and names
#               the debug file and its CRC-32, which kit.py symbols checks before it accepts a download of it
# Needs third_party/imgui (git submodule update --init third_party/imgui). Nothing of a game is read or produced.
# Links libc++ and libunwind statically (no toolchain DLL next to the exe); the exe imports system DLLs and SDL3.dll
# only (tools/release/pack_zip.py checks it is a 64-bit Windows program). Prints the exe's size and SHA-256.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build/launcher-release}"
TREES="$ROOT/work/release/toolchain-cache/trees"
LLVM_MINGW="${LLVM_MINGW:-$TREES/llvm-mingw}"
SDL3_ROOT="${SDL3_ROOT:-$TREES/sdl3}"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) win=1; exe=.exe ;;
  *) win=0; exe= ;;
esac
if (( win )); then
  CMAKE="${CMAKE:-$TREES/cmake/bin/cmake.exe}"
  NINJA="${NINJA:-$TREES/ninja/ninja.exe}"
else
  CMAKE="${CMAKE:-cmake}"
  NINJA="${NINJA:-$(command -v ninja || echo ninja)}"
fi
CXX="$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++$exe"
[[ -x "$CXX" ]] || { echo "build_launcher: no $CXX (run tools/release/fetch_toolchain.py)" >&2; exit 1; }
[[ -f "$SDL3_ROOT/x86_64-w64-mingw32/lib/cmake/SDL3/SDL3Config.cmake" ]] \
  || { echo "build_launcher: no SDL3 devel package in $SDL3_ROOT" >&2; exit 1; }
[[ -f "$ROOT/third_party/imgui/imgui.cpp" ]] \
  || { echo "build_launcher: third_party/imgui is empty (git submodule update --init third_party/imgui)" >&2; exit 1; }
export PATH="$LLVM_MINGW/bin:$PATH"
# Native Windows programs (cmake.exe, clang++.exe) get Windows paths (D:/a/...); PATH above keeps the POSIX form.
native() { if (( win )); then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }
cross=()
(( win )) || cross=(-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64
                    -DCMAKE_FIND_ROOT_PATH="$SDL3_ROOT/x86_64-w64-mingw32"
                    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
                    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY)
strip=-s; cxxrel="-O3 -DNDEBUG"       # CMake's own Release flags; -s strips at link time
if [[ "${LAUNCHER_SYMBOLS:-0}" == 1 ]]; then strip=; cxxrel="-O3 -DNDEBUG -g"; fi
kitm=""; [[ -n "${LAUNCHER_KIT_MANIFEST:-}" ]] && kitm="$(native "$(cd "$(dirname "$LAUNCHER_KIT_MANIFEST")" && pwd)/$(basename "$LAUNCHER_KIT_MANIFEST")")"
# -mwindows: a GUI program (no console window opens beside it); --no-insert-timestamp: no build time in the PE header
"$CMAKE" -S "$(native "$ROOT/launcher")" -B "$(native "$OUT")" -G Ninja "${cross[@]}" \
  -DCMAKE_MAKE_PROGRAM="$(native "$NINJA")" \
  -DCMAKE_CXX_COMPILER="$(native "$CXX")" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(native "$SDL3_ROOT/x86_64-w64-mingw32")" \
  -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc -mwindows $strip -Wl,--no-insert-timestamp" \
  -DCMAKE_CXX_FLAGS_RELEASE="$cxxrel" \
  -DLAUNCHER_KIT_MANIFEST="$kitm" \
  -DLAUNCHER_FONT_DIR=fonts
"$CMAKE" --build "$(native "$OUT")"
EXE="$OUT/launcher.exe"
[[ -f "$EXE" ]] || { echo "build_launcher: $EXE was not built" >&2; exit 1; }
if [[ "${LAUNCHER_SYMBOLS:-0}" == 1 ]]; then
  OBJCOPY="$LLVM_MINGW/bin/x86_64-w64-mingw32-objcopy$exe"
  ( cd "$OUT" && "$OBJCOPY" --only-keep-debug launcher.exe launcher.exe.debug && "$OBJCOPY" --strip-all launcher.exe \
      && "$OBJCOPY" --add-gnu-debuglink=launcher.exe.debug launcher.exe )
  stat -c "%s %n" "$OUT/launcher.exe.debug" 2>/dev/null || ls -l "$OUT/launcher.exe.debug"
fi
stat -c "%s %n" "$EXE" 2>/dev/null || ls -l "$EXE"
sha256sum "$EXE"
