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
# Swiss Ephemeris marks its data tables thread-local (__thread). On ESP-IDF that
# becomes a ~300 KB TLS segment copied into every FreeRTOS task, which exhausts
# RAM at scheduler start. All swe_* calls stay in one task, so TLS is not needed.
target_compile_definitions(${COMPONENT_LIB} PRIVATE NO_SWE_GLP TLSOFF)
