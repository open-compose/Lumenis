add_library(lumenis_model STATIC
    bin.cpp
    project_bins.cpp
    project_store.cpp
    locations.cpp
    monitor.cpp
    sequence.cpp
    marker.cpp
    settings.cpp
    include/lumenis/model/bin.h
    include/lumenis/model/project_bins.h
    include/lumenis/model/project_store.h
    include/lumenis/model/locations.h
    include/lumenis/model/monitor.h
    include/lumenis/model/sequence.h
    include/lumenis/model/marker.h
    include/lumenis/model/settings.h
)
add_library(lumenis::model ALIAS lumenis_model)

target_include_directories(lumenis_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_model PUBLIC lumenis::core Qt6::Core)
lumenis_set_warnings(lumenis_model)
