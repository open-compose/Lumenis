add_library(lumenis_ui STATIC
    command_registry.cpp
    main_window.cpp
    menu_builder.cpp
    theme.cpp
    workspace.cpp
    include/lumenis/ui/command_registry.h
    include/lumenis/ui/main_window.h
    include/lumenis/ui/menu_builder.h
    include/lumenis/ui/theme.h
    include/lumenis/ui/workspace.h
)
add_library(lumenis::ui ALIAS lumenis_ui)

qt_add_resources(lumenis_ui lumenis_ui_resources
    PREFIX /lumenis/ui
    BASE resources
    FILES
        resources/commands.json
        resources/menus.json
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-SemiBold.ttf
)

target_include_directories(lumenis_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_ui PUBLIC Qt6::Widgets lumenis::core lumenis::ext)
lumenis_set_warnings(lumenis_ui)
