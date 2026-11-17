# The commit (and release) a game build names in its log header (port/rc/rc_log_ident.h.in, "Qwark's Awakening X.Y.Z
# (commit C)"). Two sources, first match wins:
#   1. cmake/release-ident.txt (beside this file): written into the release downloads by tools/release/pack_zip.py and pack_linux.py
#      (lines "version=X.Y.Z" and "commit=<public commit, 12 hex or empty>"). A build from a download names the public
#      release it came from, never whatever git repository happens to hold the folder it was unpacked into.
#   2. git, only when <root> is itself the top of a git checkout (a clone of the repository): a folder that git finds
#      by walking up into some other repository gets no commit.
# rc_build_ident(<root> <commit var> <version var>): the version var is left as it is when the file names none.
function(rc_build_ident root out_commit out_version)
    set(commit "")
    set(version "${${out_version}}")
    set(ident "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/release-ident.txt")
    if(EXISTS "${ident}")
        file(STRINGS "${ident}" lines REGEX "^(version|commit)=")
        foreach(l IN LISTS lines)
            if(l MATCHES "^version=([0-9]+\\.[0-9]+\\.[0-9]+)$")
                set(version "${CMAKE_MATCH_1}")
            elseif(l MATCHES "^commit=([0-9a-f]*)$")
                set(commit "${CMAKE_MATCH_1}")
            endif()
        endforeach()
    else()
        find_package(Git QUIET)
        if(GIT_FOUND)
            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --show-toplevel
                            WORKING_DIRECTORY "${root}" OUTPUT_VARIABLE top
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE rc)
            get_filename_component(top_real "${top}" REALPATH)
            get_filename_component(root_real "${root}" REALPATH)
            if(rc EQUAL 0 AND top_real STREQUAL root_real)
                execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=9 HEAD
                                WORKING_DIRECTORY "${root}" OUTPUT_VARIABLE commit
                                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            endif()
        endif()
    endif()
    set(${out_commit} "${commit}" PARENT_SCOPE)
    set(${out_version} "${version}" PARENT_SCOPE)
endfunction()
