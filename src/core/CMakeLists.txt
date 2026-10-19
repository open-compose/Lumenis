configure_file(branding.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/lumenis/core/branding.h @ONLY)

add_library(lumenis_core STATIC
    rational.cpp
    timecode.cpp
    project_format.cpp
    mob_id.cpp
    include/lumenis/core/mob_id.h
    include/lumenis/core/project_format.h
    include/lumenis/core/rational.h
    include/lumenis/core/timecode.h
)
add_library(lumenis::core ALIAS lumenis_core)

target_include_directories(lumenis_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
lumenis_set_warnings(lumenis_core)
set_target_properties(lumenis_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
