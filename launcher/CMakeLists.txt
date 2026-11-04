# Qwark's Awakening launcher. SDL3 + Dear ImGui (SDL3 + SDLRenderer3 backends).
# Native preview (Linux):  nix develop --command bash -c 'cmake -S launcher -B build-native -G Ninja && cmake --build build-native'
# Windows (MinGW cross):   nix develop .#win --command bash -c 'cmake -S launcher -B build/launcher-win -G Ninja \
#                              -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake && cmake --build build/launcher-win'
cmake_minimum_required(VERSION 3.20)
project(qwark_launcher VERSION 0.2.1 LANGUAGES CXX)
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
