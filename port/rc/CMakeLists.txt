# Ratchet & Clank port: one exe per game (RC_GAME, default tods) from the shared host (main.cpp, stubs.cpp) and the game's
# glue in port/rc/<game>/. From the Toro Lets Party port's port/tlp/CMakeLists.txt at a46f947 (itself KZ2 bffa40a and KZ3
# 78bf574 port/kz3/CMakeLists.txt, based on ps3recomp templates/project, MIT licence: third_party/ps3recomp/LICENSE).
# Lifted code: work/recomp/<game> (tools/lift.sh, not committed). Toolkit: work/ps3recomp-patched.
cmake_minimum_required(VERSION 3.20)
set(RC_GAME "tods" CACHE STRING "Game to build: port/rc/<game>, work/recomp/<game>")
# The exe's file name (default: the game's name); acit120 builds acit.exe (tools/build-rc.sh passes -DRC_EXE).
set(RC_EXE "" CACHE STRING "Output exe name without .exe (default RC_GAME)")
if(NOT RC_EXE)
    set(RC_EXE "${RC_GAME}")
endif()
project(rc_${RC_GAME}
    VERSION 0.2.1
    LANGUAGES C CXX
    DESCRIPTION "Recompiled PS3 game project (ps3recomp)"
)

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer Ninja if available
if(NOT CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED ENV{CMAKE_GENERATOR})
    message(STATUS "Tip: use -G Ninja for faster builds")
endif()

# ---------------------------------------------------------------------------
# The two directories this project is built from
# ---------------------------------------------------------------------------
# PS3RECOMP_DIR is the toolkit checkout: include/, runtime/, libs/, tools/.
# RECOMP_DIR is where the lifter's output for THIS game lives. They are
# separate on purpose -- a port keeps its generated code in its own tree and
# points at whatever toolkit checkout it is building against:
#
#   cmake -B build -G Ninja \
#         -DPS3RECOMP_DIR=/path/to/ps3recomp \
#         -DRECOMP_DIR=/path/to/my_game/recompiled
#
if(NOT DEFINED PS3RECOMP_DIR)
    # The patched copy (third_party/ps3recomp + port/patches), made by tools/ps3recomp-patched.sh.
    get_filename_component(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../work/ps3recomp-patched" ABSOLUTE)
endif()
get_filename_component(PS3RECOMP_DIR "${PS3RECOMP_DIR}" ABSOLUTE)

set(RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../work/recomp/${RC_GAME}" CACHE PATH
    "Directory holding the lifter's output: ppu_recomp.h and the lifted sources")
get_filename_component(RECOMP_DIR "${RECOMP_DIR}" ABSOLUTE)

message(STATUS "ps3recomp SDK:   ${PS3RECOMP_DIR}")
message(STATUS "Lifted output:   ${RECOMP_DIR}")

if(NOT EXISTS "${RECOMP_DIR}/ppu_recomp.h")
    message(FATAL_ERROR
        "No ppu_recomp.h in ${RECOMP_DIR}.\n"
        "That header and the lifted sources beside it are what "
        "tools/ppu_lifter.py writes. Run the lifter, drop its output there, "
        "or point -DRECOMP_DIR= at wherever it already is.")
endif()

# The lifted translation units. A game's lift is hundreds of generated files,
# so they are globbed rather than listed; re-run CMake after a fresh lift.
file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS
    "${RECOMP_DIR}/*.c"
    "${RECOMP_DIR}/*.cpp"
)
if(NOT RECOMP_SOURCES)
    message(FATAL_ERROR
        "${RECOMP_DIR} has a ppu_recomp.h but no .c or .cpp beside it. The "
        "lifted functions and the function table live in those; without them "
        "there is nothing to run.")
endif()
list(LENGTH RECOMP_SOURCES RECOMP_SOURCE_COUNT)
message(STATUS "Lifted sources:  ${RECOMP_SOURCE_COUNT} file(s)")

# ---------------------------------------------------------------------------
# ps3recomp runtime library
# ---------------------------------------------------------------------------
# Built from source as a subdirectory rather than imported as a prebuilt
# archive. A static library carries none of its own link requirements, so an
# imported one arrives without SDL2, pthreads, the Metal frameworks or the
# Winsock/XInput imports -- the link then fails on every platform for a
# different reason. add_subdirectory hands all of that over with the target.
add_subdirectory("${PS3RECOMP_DIR}" "${CMAKE_BINARY_DIR}/ps3recomp_sdk"
                 EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The HLE NID table
# ---------------------------------------------------------------------------
# Maps each firmware import NID onto the handler in the runtime that answers
# it. A port may keep a hand-curated one beside its lift; otherwise generate
# the canonical every-module table, which is what the toolkit's own boot
# harness uses.
if(EXISTS "${RECOMP_DIR}/ppu_hle_nids.cpp")
    set(HLE_NID_TABLE "${RECOMP_DIR}/ppu_hle_nids.cpp")
    message(STATUS "HLE NID table:   ${HLE_NID_TABLE}")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(HLE_NID_TABLE "${CMAKE_CURRENT_BINARY_DIR}/ppu_hle_nids.cpp")
    set(RC_PATCHSET_STAMP "")
    if(EXISTS "${PS3RECOMP_DIR}/.rc-patchset")
        set(RC_PATCHSET_STAMP "${PS3RECOMP_DIR}/.rc-patchset")
    endif()
    add_custom_command(
        OUTPUT "${HLE_NID_TABLE}"
        COMMAND "${Python3_EXECUTABLE}"
                "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                --out "${HLE_NID_TABLE}" --all
        DEPENDS "${PS3RECOMP_DIR}/tools/gen_hle_nids.py"
                "${PS3RECOMP_DIR}/tools/nid_database.py"
                # The patch-set stamp (tools/ps3recomp-patched.sh) changes with port/patches, so an HLE function a
                # patch adds is in the table without a clean build (three cellHttp NIDs once stayed unresolved).
                ${RC_PATCHSET_STAMP}
        COMMENT "Generating the HLE NID -> handler table"
        # VERBATIM: this repo's path holds a space and '&'; unquoted, the shell split it ("Ratchet & Clank").
        VERBATIM)
    message(STATUS "HLE NID table:   generated (every module)")
endif()

# ---------------------------------------------------------------------------
# The game executable
# ---------------------------------------------------------------------------
# The PPU boot scaffold is compiled here rather than taken from the runtime
# library: every one of these files #includes the lifter's per-game
# ppu_recomp.h, so they can only be built against a particular lift.
set(PS3RECOMP_SCAFFOLD
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_loader.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_hle.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_fs.cpp"
    "${PS3RECOMP_DIR}/runtime/ppu/ppu_sysprx.cpp"
)

# ---------------------------------------------------------------------------
# Lifted SPU images (optional)
# ---------------------------------------------------------------------------
# A title that runs SPU work gets its images lifted into <SPU_DIR>/<image>/ by
# tools/spu_lifter.py, with a registration TU generated by
# tools/build_spu_workloads.py. cellSpurs dispatches them by content
# fingerprint, so the generated table is keyed to the images THIS build ships:
# a table generated from another release registers signatures that can never
# match, which is worse than shipping none. Both the table and the job-module
# registry self-register from constructors and are therefore optional -- a tree
# that has not lifted its SPU images yet configures and links without them.
set(SPU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${RC_GAME}/spu" CACHE PATH
    "Directory holding the lifted SPU images (<SPU_DIR>/<image>/spu_recomp.c)")
option(PORT_ENABLE_SPU "Compile and register the title's lifted SPU images" ON)

set(SPU_SOURCES "")
if(PORT_ENABLE_SPU)
    file(GLOB SPU_LIFTED CONFIGURE_DEPENDS "${SPU_DIR}/*/spu_recomp.c")
    if(SPU_LIFTED)
        list(LENGTH SPU_LIFTED SPU_IMAGE_COUNT)
        message(STATUS "Lifted SPU:      ${SPU_IMAGE_COUNT} image(s)")
        # The musttail indirect-branch fast path. Excluded from the runtime
        # library on purpose (see the SDK root CMakeLists): it must be built
        # here, by clang, or the tail call degrades to a plain call and a guest
        # loop that branches indirectly walks the thread stack off its guard
        # page a few thousand iterations in.
        list(APPEND SPU_SOURCES ${SPU_LIFTED}
             "${PS3RECOMP_DIR}/runtime/spu/spu_dispatch_mt.c")
        # Generated beside the lift; each is present only once its generator
        # has been run, so each is included only if it is actually here.
        foreach(gen IN ITEMS spu_workloads.c jobmods_register.c taskset_policy_bytes.c rawspu_register.c igjobs_register.c)
            if(EXISTS "${SPU_DIR}/${gen}")
                list(APPEND SPU_SOURCES "${SPU_DIR}/${gen}")
            else()
                message(STATUS "  (no ${gen} -- not registered)")
            endif()
        endforeach()
    else()
        message(STATUS "Lifted SPU:      none found in ${SPU_DIR}")
    endif()
endif()

# Host bodies for port/rc/<game>/ppu_overrides.txt: only a game with overrides has the file (acit120: none yet).
set(GAME_OVERRIDES "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${RC_GAME}/${RC_GAME}_ppu_overrides.cpp")
    set(GAME_OVERRIDES "${RC_GAME}/${RC_GAME}_ppu_overrides.cpp")
endif()
# The logger (port/common/log: KZ3's logger plus rc_log_info.c, the header and end block) and the game's identity
# for the log header, read from data/titles/catalog.json by build_title (acit120 -> id acit).
set(RC_LOG_DIR_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../common/log")
set(RC_LOG_SOURCES "${RC_LOG_DIR_SRC}/kz3_log.c" "${RC_LOG_DIR_SRC}/rc_log_info.c")
set(RC_GAME_ID "${RC_GAME}")
set(RC_GAME_TITLE "${RC_GAME}")
set(RC_GAME_TITLE_ID "")
set(RC_GAME_UPDATE "")
set(RC_CATALOG "${CMAKE_CURRENT_SOURCE_DIR}/../../data/titles/catalog.json")
if(EXISTS "${RC_CATALOG}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RC_CATALOG}")
    file(READ "${RC_CATALOG}" RC_CATALOG_JSON)
    string(JSON RC_NGAMES LENGTH "${RC_CATALOG_JSON}" games)
    math(EXPR RC_LAST "${RC_NGAMES} - 1")
    foreach(i RANGE ${RC_LAST})
        string(JSON bt GET "${RC_CATALOG_JSON}" games ${i} build_title)
        if(bt STREQUAL RC_GAME)
            string(JSON RC_GAME_ID GET "${RC_CATALOG_JSON}" games ${i} id)
            string(JSON RC_GAME_TITLE GET "${RC_CATALOG_JSON}" games ${i} title)
            string(JSON nreg LENGTH "${RC_CATALOG_JSON}" games ${i} title_ids disc)
            if(nreg GREATER 0)
                math(EXPR nreg1 "${nreg} - 1")
                foreach(r RANGE ${nreg1})
                    # the US disc id (the one the setup accepts); another region only when there is no US one
                    string(JSON reg MEMBER "${RC_CATALOG_JSON}" games ${i} title_ids disc ${r})
                    string(JSON nid LENGTH "${RC_CATALOG_JSON}" games ${i} title_ids disc ${reg})
                    if(nid GREATER 0 AND (RC_GAME_TITLE_ID STREQUAL "" OR reg STREQUAL "US"))
                        string(JSON RC_GAME_TITLE_ID GET "${RC_CATALOG_JSON}" games ${i} title_ids disc ${reg} 0)
                    endif()
                endforeach()
            endif()
            string(JSON utype TYPE "${RC_CATALOG_JSON}" games ${i} update)
            if(utype STREQUAL "OBJECT")
                string(JSON RC_GAME_UPDATE GET "${RC_CATALOG_JSON}" games ${i} update version)
            endif()
            break()
        endif()
    endforeach()
endif()
set(RC_GIT_COMMIT "")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=9 HEAD
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE RC_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
message(STATUS "Log identity:    ${RC_GAME_ID} / ${RC_GAME_TITLE} / ${RC_GAME_TITLE_ID} / update '${RC_GAME_UPDATE}' / commit '${RC_GIT_COMMIT}'")
# The identity goes into a generated header that only main.cpp includes (a commit change rebuilds one file, not the lift).
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/rc_log_ident.h.in" "${CMAKE_CURRENT_BINARY_DIR}/rc_log_ident.h" @ONLY)

add_executable(${PROJECT_NAME}
    main.cpp
    rc_settings.cpp                          # per-game settings folder (keybinds.ini, display.ini, settings.ini)
    rc_trace_read.cpp                        # RC_TRACE_READ access tracer (diagnostic, default off)
    ${RC_LOG_SOURCES}
    stubs.cpp
    ${RC_GAME}/hle_${RC_GAME}.cpp            # the game's HLE for imports the runtime leaves unresolved
    ${GAME_OVERRIDES}
    ${PS3RECOMP_SCAFFOLD}
    ${HLE_NID_TABLE}
    ${RECOMP_SOURCES}
    ${SPU_SOURCES}
)

# The lifted SPU sources use bare #include "spu_context.h". Scope that include
# directory (and their flags) to those TUs alone: adding it to the whole target
# changes the compile command of every lifted PPU TU, and those are large enough
# that the needless rebuild is measured in tens of minutes.
#   SPU_USE_DISPATCH_MT -- route SPU_IB_DISPATCH through the musttail path.
#   -O2                 -- the SPU sources are small and decode-hot, so they get
#                          full optimization. (There is no target-wide -O1:
#                          the lifted PPU TUs build at the RelWithDebInfo -O2 -g,
#                          checked 2026-10-27.) The last -O on the line wins.
if(SPU_SOURCES)
    set_source_files_properties(${SPU_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${PS3RECOMP_DIR}/runtime/spu"
        COMPILE_DEFINITIONS SPU_USE_DISPATCH_MT)
    if(NOT MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set_source_files_properties(${SPU_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")
    endif()
endif()

# app.rc carries the icon, the version block and the manifest, and needs a
# resource compiler. Listing it as a source on a host that has none is not a
# link error, it is a "no rule to make" for a file nothing can consume.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    "${RECOMP_DIR}"                            # the generated ppu_recomp.h
    "${PS3RECOMP_DIR}/include"
    "${PS3RECOMP_DIR}/runtime/ppu"
    "${PS3RECOMP_DIR}/runtime/spu"
    "${PS3RECOMP_DIR}/runtime/memory"
    "${PS3RECOMP_DIR}/runtime/syscalls"
    "${PS3RECOMP_DIR}/runtime/platform"        # win32_compat.h and friends
    "${PS3RECOMP_DIR}/libs/video"
    "${PS3RECOMP_DIR}/libs/input"              # pad_kb_profile.h (R&C patch 6011)
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ps3recomp_runtime
)

# KZ3 logger: route the runtime's and the port's printf/fprintf/puts/fputs/fflush through kz3_log (the line router:
# [tag] -> level; log file in <exe dir>/logs/<game id>; stdout only with RC_LOG_STDOUT=1). Only the runtime library, the
# PPU scaffold and the port's own .cpp files get it: the lifted PPU/SPU sources keep their compile command (changing it
# would rebuild all of them) and print nothing of their own. The logger's own sources must not get the redirect.
# This is what turns patches 2030/2032 (crash files, thread labels, minidump) on.
option(RC_LOG_REDIRECT "route the runtime's stdio through the logger" ON)
target_include_directories(${PROJECT_NAME} PRIVATE "${RC_LOG_DIR_SRC}")
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(RC_LOG_REDIRECT)
    set(RC_LOG_HEADER "${RC_LOG_DIR_SRC}/kz3_log.h")
    target_compile_definitions(ps3recomp_runtime PRIVATE KZ3_LOG_REDIRECT_STDIO)
    target_compile_options(ps3recomp_runtime PRIVATE -include "${RC_LOG_HEADER}")
    target_include_directories(ps3recomp_runtime PRIVATE "${RC_LOG_DIR_SRC}")
    set(RC_REDIRECTED main.cpp rc_settings.cpp rc_trace_read.cpp stubs.cpp ${RC_GAME}/hle_${RC_GAME}.cpp ${GAME_OVERRIDES} ${PS3RECOMP_SCAFFOLD})
    set_property(SOURCE ${RC_REDIRECTED} APPEND PROPERTY COMPILE_DEFINITIONS KZ3_LOG_REDIRECT_STDIO)
    set_property(SOURCE ${RC_REDIRECTED} APPEND PROPERTY COMPILE_OPTIONS -include "${RC_LOG_HEADER}")
endif()
# Patch 6010: the name in the player's settings folder (%APPDATA%\Qwark's Awakening) and the settings files' comments
# (tools/release/names.py PRODUCT). PUBLIC so main.cpp's display.ini writer gets the same name.
target_compile_definitions(ps3recomp_runtime PUBLIC "PS3RECOMP_PRODUCT_NAME=\"Qwark's Awakening\"")

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${RC_EXE}")   # tods.exe, acit.exe
# No online setup: added when the port has its own. The file logger (KZ3 ) is above (port/common/log).


# The runtime is C++, and on Apple also Objective-C. Letting CMake infer the
# link language from the generated C would leave the C++ standard library out.
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

# Windows system import libraries. dbghelp symbolizes crash-handler addresses
# against the PDB, user32 and gdi32 are what the backend's window and its GDI
# present are made of, and winmm is timeBeginPeriod. None of the four names
# anything a POSIX linker can find; the D3D12 backend's own imports come in
# through the #pragma comment(lib) in libs/video/rsx_d3d12_backend.c.
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dbghelp
        user32
        gdi32
        winmm
    )
    # MinGW ignores #pragma comment(lib), so name the backend's imports (and the
    # rest of the runtime's) explicitly. Same list as tools/smoke/build-host.sh.
    if(NOT MSVC)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            # No xinput: KZ3 patch 4019 loads XInput1_4/1_3/9_1_0 at run time (libxinput = xinput1_3.dll, absent
            # from stock Windows 10/11, so the exe did not start). tools/release/test_pe_imports.py guards the imports.
            d3d12 dxgi d3dcompiler dxguid ole32 ws2_32 bcrypt
            shlwapi synchronization psapi
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Compiler-specific flags
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -fno-strict-aliasing
    )
elseif(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3 /wd4100 /wd4244 /wd4267
        /Zc:__cplusplus         # report correct __cplusplus value
        # ppu_recomp.h defines PPU_FENCE, the lowering of the guest's
        # sync/lwsync/eieio/isync, and in C that is <stdatomic.h>. The lifter
        # writes C++ by default, but --single-file writes ppu_recomp.c, and a
        # port that took it would not compile without this. The runtime's own
        # build already carries the same flag for the same reason.
        /experimental:c11atomics
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# The lifted translation units are machine-generated and can run to hundreds of
# thousands of lines each, so they get their own flags:
#
#   - warnings off. They are about generated code and nobody will act on them,
#     and at this volume they bury the diagnostics that matter.
#   - no floating-point contraction. arm64 fuses a multiply and an add by
#     default; a PPC fmadd is spelt as one where the lifter means one.
#   - /bigobj on MSVC. A single lifted file routinely passes the 65,536-section
#     limit of the default object format.
if(MSVC)
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "/w;/bigobj")
else()
    set_source_files_properties(${RECOMP_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-w;-ffp-contract=off")
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
