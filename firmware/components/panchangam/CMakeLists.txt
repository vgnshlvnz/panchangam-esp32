# Wraps the host-tested, ESP-IDF-free core in core/.
idf_component_register(
    SRCS ${CMAKE_CURRENT_LIST_DIR}/../../../core/panchangam.c
         ${CMAKE_CURRENT_LIST_DIR}/../../../core/format.c
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../../core
    REQUIRES swisseph
)
