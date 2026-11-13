# Qwark's Awakening launcher. SDL3 + Dear ImGui (SDL3 + SDLRenderer3 backends).
# Native preview (Linux):  nix develop --command bash -c 'cmake -S launcher -B build-native -G Ninja && cmake --build build-native'
# Windows (MinGW cross):   nix develop .#win --command bash -c 'cmake -S launcher -B build/launcher-win -G Ninja \
#                              -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake && cmake --build build/launcher-win'
cmake_minimum_required(VERSION 3.20)
project(qwark_launcher VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# SDL3: its CMake config when one is on CMAKE_PREFIX_PATH (the release build: SDL3's official MinGW devel package,
# tools/release/build_launcher.sh), else pkg-config (the Nix shells).
find_package(SDL3 CONFIG QUIET COMPONENTS SDL3-shared)
if(SDL3_FOUND)
  set(LAUNCHER_SDL3 SDL3::SDL3-shared)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
  set(LAUNCHER_SDL3 PkgConfig::SDL3)
endif()
# The fonts' fallback folder baked into the exe (the exe looks in <exe dir>/fonts first); the release build passes a
# relative one so no build machine path ends up in the program.
if(NOT DEFINED LAUNCHER_FONT_DIR)
  set(LAUNCHER_FONT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fonts)
endif()

# Launcher phase 4: the build kit this launcher accepts. -DLAUNCHER_KIT_MANIFEST=<kit.json> (pack_zip.py
# --make-kit-manifest, before the launcher is built) compiles in kit.json's SHA-256, the full build zip's URL and its
# sizes; kit.py refuses any kit whose kit.json hashes otherwise. Without it (a developer build) the launcher asks for no
# kit. The releases list (opt-in "check for new releases") and the launcher symbols asset are derived from the
# same GitHub release URL, so every address the launcher can reach comes from that one file.
set(LAUNCHER_KIT_SHA256 "")
set(LAUNCHER_KIT_URL "")
set(LAUNCHER_KIT_RELEASE "")
set(LAUNCHER_KIT_BYTES 0)
set(LAUNCHER_KIT_UNPACKED 0)
set(LAUNCHER_REPO_API "")
set(LAUNCHER_SYMBOLS_URL "")
if(LAUNCHER_KIT_MANIFEST)
  file(READ "${LAUNCHER_KIT_MANIFEST}" _kit)
  file(SHA256 "${LAUNCHER_KIT_MANIFEST}" LAUNCHER_KIT_SHA256)
  string(JSON LAUNCHER_KIT_URL GET "${_kit}" download_url)
  string(JSON LAUNCHER_KIT_RELEASE GET "${_kit}" release)
  string(JSON LAUNCHER_KIT_BYTES GET "${_kit}" download_bytes)
  string(JSON LAUNCHER_KIT_UNPACKED GET "${_kit}" unpacked_bytes)
  string(JSON _kit_file GET "${_kit}" download_file)
  if(NOT LAUNCHER_KIT_URL MATCHES "^https://github\\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/releases/download/launcher-v[0-9.]+/")
    message(FATAL_ERROR "LAUNCHER_KIT_MANIFEST: download_url is not a GitHub release download: ${LAUNCHER_KIT_URL}")
  endif()
  set(LAUNCHER_REPO_API "https://api.github.com/repos/${CMAKE_MATCH_1}/${CMAKE_MATCH_2}")
  string(REPLACE "-build-windows-x64.zip" "-launcher-symbols.zip" LAUNCHER_SYMBOLS_URL "${LAUNCHER_KIT_URL}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LAUNCHER_KIT_MANIFEST}")
  message(STATUS "launcher: build kit ${LAUNCHER_KIT_RELEASE} sha256 ${LAUNCHER_KIT_SHA256}")
endif()

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/imgui)
if(NOT EXISTS ${IMGUI_DIR}/imgui.cpp)
  message(FATAL_ERROR "third_party/imgui is empty: git submodule update --init third_party/imgui")
endif()

add_executable(launcher
  src/main.cpp
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(launcher PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(launcher PRIVATE ${LAUNCHER_SDL3})
target_compile_definitions(launcher PRIVATE
  LAUNCHER_VERSION="${PROJECT_VERSION}"
  LAUNCHER_FONT_DIR="${LAUNCHER_FONT_DIR}"
  LAUNCHER_KIT_SHA256="${LAUNCHER_KIT_SHA256}" LAUNCHER_KIT_URL="${LAUNCHER_KIT_URL}"
  LAUNCHER_KIT_RELEASE="${LAUNCHER_KIT_RELEASE}" LAUNCHER_KIT_BYTES=${LAUNCHER_KIT_BYTES}LL
  LAUNCHER_KIT_UNPACKED=${LAUNCHER_KIT_UNPACKED}LL LAUNCHER_REPO_API="${LAUNCHER_REPO_API}"
  LAUNCHER_SYMBOLS_URL="${LAUNCHER_SYMBOLS_URL}"
  IMGUI_DISABLE_DEBUG_TOOLS IMGUI_DISABLE_DEMO_WINDOWS)
if(MSVC)
  target_compile_options(launcher PRIVATE /W3)
else()
  # Warnings apply to our code only; the vendored imgui files are built as upstream ships them.
  set_source_files_properties(src/main.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()

# Fonts next to the exe (the launcher looks in <exe dir>/fonts first), with their OFL licence texts.
add_custom_command(TARGET launcher POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/fonts $<TARGET_FILE_DIR:launcher>/fonts)

# Windows: SDL3.dll next to the exe (same as the game exes ship it).
if(WIN32 AND LAUNCHER_SDL3 STREQUAL "SDL3::SDL3-shared")
  add_custom_command(TARGET launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:launcher>)
elseif(WIN32)
  pkg_get_variable(SDL3_PREFIX sdl3 prefix)
  find_file(SDL3_DLL SDL3.dll HINTS ${SDL3_PREFIX}/bin ${SDL3_LIBRARY_DIRS}/../bin NO_DEFAULT_PATH)
  if(SDL3_DLL)
    add_custom_command(TARGET launcher POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL3_DLL} $<TARGET_FILE_DIR:launcher>)
  endif()
endif()
