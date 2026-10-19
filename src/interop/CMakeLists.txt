# Interchange: AAF and the formats other editors read and write (ROADMAP
# Phase 3, docs/spec/aaf.md).
add_library(lumenis_interop STATIC
    cfb.cpp
    aaf.cpp
    aaf_dump.cpp
    aaf_import.cpp
    aaf_model.inc
    include/lumenis/interop/cfb.h
    include/lumenis/interop/aaf.h
    include/lumenis/interop/aaf_import.h
)
add_library(lumenis::interop ALIAS lumenis_interop)

target_include_directories(lumenis_interop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_interop PUBLIC lumenis::model Qt6::Core)
lumenis_set_warnings(lumenis_interop)
