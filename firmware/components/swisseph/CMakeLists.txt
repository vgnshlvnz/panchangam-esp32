set(SWE ${CMAKE_CURRENT_LIST_DIR}/../../../third_party/swisseph)

# Same object list as the upstream Makefile's SWEOBJ (no swetest/swemini/etc.)
idf_component_register(
    SRCS ${SWE}/swedate.c ${SWE}/swehouse.c ${SWE}/swejpl.c ${SWE}/swemmoon.c
         ${SWE}/swemplan.c ${SWE}/sweph.c ${SWE}/swephlib.c ${SWE}/swecl.c
         ${SWE}/swehel.c
    INCLUDE_DIRS ${SWE}
)

# Upstream code is not warning-clean; don't touch it, just silence it.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)

# swe_get_library_path needs dlfcn.h, which newlib lacks; upstream provides this switch.
target_compile_definitions(${COMPONENT_LIB} PRIVATE NO_SWE_GLP)
