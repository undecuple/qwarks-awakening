# Build a game exe with llvm-mingw (clang + lld + libc++/libunwind, MinGW-w64 UCRT): the compiler the setup engine
# (tools/release/firstrun.py --toolchain) uses on the player's Windows PC, from tools/release/fetch_toolchain.py's pin.
# From the Killzone 3 port's cmake/llvm-mingw.cmake (Project Helghast, MIT, same author), names changed; KZ3 builds its
# game with it on native Windows and under Proton. ASSUMPTION for R&C until a build runs through it: port/rc and the
# R&C patches compile with clang as KZ3's port does (mingw-w64.cmake's GCC is the only compiler R&C has built with).
#
#   cmake -S port/rc -B build/x -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DLLVM_MINGW_ROOT=DIR
#
# Differences from mingw-w64.cmake (KZ3's survey): target-prefixed clang wrappers, lld, llvm-ar; "-static" alone links
# libc++, libunwind, compiler-rt and winpthreads statically; llvm-windres behind the x86_64-w64-mingw32-windres
# wrapper; native TLS instead of GCC's emulated TLS; _WIN32_WINNT set to Windows 10 (llvm-mingw defaults to 7).

if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(CMAKE_SYSTEM_NAME Windows)      # cross from Linux; on a Windows host the host system is already right
endif()
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT LLVM_MINGW_ROOT AND DEFINED ENV{LLVM_MINGW_ROOT})
    set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
endif()
set(LLVM_MINGW_ROOT "${LLVM_MINGW_ROOT}" CACHE PATH "llvm-mingw folder (holds bin/); empty = the compilers on PATH")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_ROOT)   # try_compile projects re-read this file

set(_rc_tc_prefix "x86_64-w64-mingw32-")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_rc_tc_exe ".exe")
else()
    set(_rc_tc_exe "")
endif()
if(LLVM_MINGW_ROOT)
    set(_rc_tc_bin "${LLVM_MINGW_ROOT}/bin/")
else()
    set(_rc_tc_bin "")
endif()

set(CMAKE_C_COMPILER   "${_rc_tc_bin}${_rc_tc_prefix}clang${_rc_tc_exe}")
set(CMAKE_CXX_COMPILER "${_rc_tc_bin}${_rc_tc_prefix}clang++${_rc_tc_exe}")
set(CMAKE_RC_COMPILER  "${_rc_tc_bin}${_rc_tc_prefix}windres${_rc_tc_exe}")
set(CMAKE_AR           "${_rc_tc_bin}llvm-ar${_rc_tc_exe}" CACHE FILEPATH "archiver")
set(CMAKE_RANLIB       "${_rc_tc_bin}llvm-ranlib${_rc_tc_exe}" CACHE FILEPATH "ranlib")

if(LLVM_MINGW_ROOT)
    set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/x86_64-w64-mingw32")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_C_FLAGS_INIT   "-D_WIN32_WINNT=0x0A00")
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0A00")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
