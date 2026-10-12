add_library(lumenis_model STATIC
    project_store.cpp
    locations.cpp
    include/lumenis/model/project_store.h
    include/lumenis/model/locations.h
)
add_library(lumenis::model ALIAS lumenis_model)

target_include_directories(lumenis_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_model PUBLIC lumenis::core Qt6::Core)
lumenis_set_warnings(lumenis_model)
