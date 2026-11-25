#!/usr/bin/env bash
# Build the native Linux launcher for the release tarball (linux-launcher-plan.md s.6): SDL3 from its pinned source
# release (toolchain.json "sdl3-source"), built as a static library and linked in, so the program needs only the C
# library and what SDL3 loads at run time (X11 or Wayland, EGL/GL, D-Bus, udev: the desktop's own libraries); libstdc++
# and libgcc linked statically too. release.yml's linux job runs it on GitHub's ubuntu-22.04 runner, whose C library
# (glibc 2.35) is the oldest the program accepts; locally the same script runs in an ubuntu:22.04 container
# (docs/guides/tools.md). pack_linux.py checks the result (ELF imports, no RPATH, the newest glibc symbol version).
#
#   tools/release/build_launcher_linux.sh [OUTDIR]          (default build/launcher-linux)
#
# Environment (defaults: fetch_toolchain.py's trees, work/release/toolchain-cache/trees/<name>):
#   SDL3_SRC    SDL3 source tree (fetch_toolchain.py --only sdl3-source)
#   CMAKE       cmake program (default: the pinned cmake-linux tree, else cmake on PATH)
#   NINJA       ninja program (default: the pinned ninja-linux tree, else ninja on PATH)
#   CXX / CC    the system compiler (default g++ / gcc)
#   LAUNCHER_RELEASE_API  the GitHub releases API address compiled in for the opt-in "check for new releases"
#               (default: names.py REPO_API)
# Needs third_party/imgui and the X11, Wayland, xkbcommon, EGL/GL, D-Bus and udev development headers (release.yml's
# apt-get line). SDL's video, events, joystick and dialog parts only: no audio, camera, GPU API, haptics or sensors
# (the launcher initialises video and gamepads). Nothing of a game is read or produced. Prints the program's size and
# SHA-256.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/build/launcher-linux}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
TREES="$ROOT/work/release/toolchain-cache/trees"
SDL3_SRC="${SDL3_SRC:-$TREES/sdl3-source}"
CMAKE="${CMAKE:-$( [[ -x $TREES/cmake-linux/bin/cmake ]] && echo "$TREES/cmake-linux/bin/cmake" || command -v cmake || echo cmake)}"
NINJA="${NINJA:-$( [[ -x $TREES/ninja-linux/ninja ]] && echo "$TREES/ninja-linux/ninja" || command -v ninja || echo ninja)}"
CC="${CC:-gcc}"
CXX="${CXX:-g++}"
API="${LAUNCHER_RELEASE_API:-$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import names; print(names.REPO_API)' "$ROOT/tools/release")}"
[[ "$(uname -s)" == Linux ]] || { echo "build_launcher_linux: Linux only" >&2; exit 1; }
[[ -f "$SDL3_SRC/CMakeLists.txt" && -f "$SDL3_SRC/LICENSE.txt" ]] \
  || { echo "build_launcher_linux: no SDL3 source in $SDL3_SRC (fetch_toolchain.py --only sdl3-source)" >&2; exit 1; }
[[ -f "$ROOT/third_party/imgui/imgui.cpp" ]] \
  || { echo "build_launcher_linux: third_party/imgui is empty (git submodule update --init third_party/imgui)" >&2; exit 1; }
"$CMAKE" --version | sed -n 1p
"$CXX" --version | sed -n 1p
ldd --version | sed -n 1p

# The build machine's folders never end up in the program (__FILE__ in asserts, debug paths): mapped to "."
map="-ffile-prefix-map=$ROOT=. -ffile-prefix-map=$SDL3_SRC=sdl3 -ffile-prefix-map=$OUT=build"

# 1. SDL3, static, installed into OUT/sdl3 (headers, libSDL3.a, its CMake config). Video back ends are required, not
# optional: a missing X11 or Wayland header must fail the build, not give a launcher that cannot open a window.
"$CMAKE" -S "$SDL3_SRC" -B "$OUT/sdl3-build" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$map" -DCMAKE_INSTALL_PREFIX="$OUT/sdl3" -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF \
  -DSDL_AUDIO=OFF -DSDL_CAMERA=OFF -DSDL_GPU=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF -DSDL_POWER=OFF \
  -DSDL_X11=ON -DSDL_X11_SHARED=ON -DSDL_WAYLAND=ON -DSDL_WAYLAND_SHARED=ON -DSDL_WAYLAND_LIBDECOR=OFF \
  -DSDL_KMSDRM=OFF -DSDL_VULKAN=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_DBUS=ON -DSDL_LIBUDEV=ON \
  -DSDL_IBUS=OFF -DSDL_RPATH=OFF
"$CMAKE" --build "$OUT/sdl3-build"
"$CMAKE" --install "$OUT/sdl3-build" >/dev/null

# 2. The launcher, with SDL3 linked in (LAUNCHER_LINUX_RELEASE: no source tree path compiled in)
"$CMAKE" -S "$ROOT/launcher" -B "$OUT/launcher-build" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$OUT/sdl3" -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON \
  -DCMAKE_CXX_FLAGS="$map" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc -s -Wl,--as-needed -Wl,-z,relro,-z,now -Wl,-Map=$OUT/launcher.map" \
  -DLAUNCHER_LINUX_RELEASE=ON -DLAUNCHER_RELEASE_API="$API" -DLAUNCHER_FONT_DIR=fonts
"$CMAKE" --build "$OUT/launcher-build"
cp "$OUT/launcher-build/launcher" "$OUT/qwarks-awakening"
# The program loads <exe dir>/fonts (LAUNCHER_FONT_DIR): stage them so the build output runs where it is built (the
# navigation test and a developer run it from here; pack_linux.py/pack_appimage.py take launcher/fonts themselves).
# 2026-11-25: without them the CI navigation test failed 38 of 43 checks ("cannot load font fonts/...").
mkdir -p "$OUT/fonts"; cp "$ROOT"/launcher/fonts/*.ttf "$OUT/fonts/"
# The licence texts of what SDL3 links into the program: SDL itself (zlib) and the two third-party parts of its source
# that end up in it (linker map, 2026-11-16): HIDAPI in SDL_hidapi.c (used under its BSD-style licence, one of the three
# it offers) and yuv2rgb (BSD-3-Clause). SDL's own libm (Sun's notice) is only linked where the C library lacks the
# functions; on glibc it is not, and the map check below fails the build if that changes.
if grep -qE 'libSDL3\.a\((e|k|s)_[a-z0-9_]+\.c\.o\)' "$OUT/launcher.map"; then
  echo "build_launcher_linux: SDL's libm is linked in: ship its notice (src/libm) with the program" >&2; exit 1
fi
mkdir -p "$OUT/licenses"
cp "$SDL3_SRC/LICENSE.txt" "$OUT/licenses/SDL3-LICENSE.txt"
cp "$SDL3_SRC/src/hidapi/LICENSE-bsd.txt" "$OUT/licenses/SDL3-hidapi-LICENSE.txt"
cp "$SDL3_SRC/src/video/yuv2rgb/LICENSE" "$OUT/licenses/SDL3-yuv2rgb-LICENSE.txt"
stat -c "%s %n" "$OUT/qwarks-awakening"
sha256sum "$OUT/qwarks-awakening"
