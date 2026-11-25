# R&C: shader translator identity (patch 2141). id = first 16 hex digits of SHA-256 over "<name>:<sha256>;" for every
# <toolkit>/libs/video/*.c and *.h (top level only, sorted by name). Same rule as the Killzone 3 port
# (port/kz3/CMakeLists.txt, design from the Killzone 2 port's 6047); kept in a module so a `cmake -P` host test
# (port/rc/tests/run_rsx_translator_id_test.sh) runs this exact code without a lift or a configure.
#
#   rc_rsx_translator_id(<toolkit dir> <id out var> <file list out var>)
#
# Unlike the inline KZ3 text it refuses an empty glob: a wrong PS3RECOMP_DIR would otherwise hash the empty string and
# look like a valid id.
function(rc_rsx_translator_id toolkit out_id out_files)
    file(GLOB files LIST_DIRECTORIES false "${toolkit}/libs/video/*.c" "${toolkit}/libs/video/*.h")
    list(SORT files)
    list(LENGTH files count)
    if(count EQUAL 0)
        message(FATAL_ERROR "RSX_TRANSLATOR_ID: no libs/video/*.c|*.h under '${toolkit}' (wrong PS3RECOMP_DIR?)")
    endif()
    set(cat "")
    foreach(f IN LISTS files)
        file(SHA256 "${f}" h)
        get_filename_component(nm "${f}" NAME)
        string(APPEND cat "${nm}:${h};")
    endforeach()
    string(SHA256 id "${cat}")
    string(SUBSTRING "${id}" 0 16 id)
    set(${out_id} "${id}" PARENT_SCOPE)
    set(${out_files} "${files}" PARENT_SCOPE)
endfunction()
