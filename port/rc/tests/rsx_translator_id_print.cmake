# cmake -DTREE=<toolkit dir> -P port/rc/tests/rsx_translator_id_print.cmake: prints the id the production module computes.
include("${CMAKE_CURRENT_LIST_DIR}/../rsx_translator_id.cmake")
rc_rsx_translator_id("${TREE}" id files)
list(LENGTH files n)
message(STATUS "RSX_TRANSLATOR_ID ${id} ${n}")   # -P mode writes STATUS lines to stderr
