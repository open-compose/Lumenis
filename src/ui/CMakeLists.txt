add_library(lumenis_ui STATIC
    main_window.cpp
    include/lumenis/ui/main_window.h
)
add_library(lumenis::ui ALIAS lumenis_ui)

target_include_directories(lumenis_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_ui PUBLIC Qt6::Widgets lumenis::core lumenis::ext)
lumenis_set_warnings(lumenis_ui)
