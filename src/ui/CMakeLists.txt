add_library(lumenis_ui STATIC
    bin_container.cpp
    bin_dialogs.cpp
    bin_dock.cpp
    platform_check.cpp
    bin_view.cpp
    composer.cpp
    capture.cpp
    command_registry.cpp
    keyboard_map.cpp
    main_window.cpp
    menu_builder.cpp
    panel_manager.cpp
    panels.cpp
    select_project_dialog.cpp
    theme.cpp
    workspace.cpp
    workspace_bar.cpp
    include/lumenis/ui/bin_container.h
    include/lumenis/ui/bin_dialogs.h
    include/lumenis/ui/bin_dock.h
    include/lumenis/ui/platform_check.h
    include/lumenis/ui/bin_view.h
    include/lumenis/ui/composer.h
    include/lumenis/ui/capture.h
    include/lumenis/ui/command_registry.h
    include/lumenis/ui/keyboard_map.h
    include/lumenis/ui/main_window.h
    include/lumenis/ui/menu_builder.h
    include/lumenis/ui/panels.h
    include/lumenis/ui/select_project_dialog.h
    include/lumenis/ui/theme.h
    include/lumenis/ui/workspace.h
    include/lumenis/ui/workspace_bar.h
)
add_library(lumenis::ui ALIAS lumenis_ui)

qt_add_resources(lumenis_ui lumenis_ui_resources
    PREFIX /lumenis/ui
    BASE resources
    FILES
        resources/commands.json
        resources/keyboard.json
        resources/menus.json
        resources/whats_new.html
        resources/icons/app.png
        resources/icons/about.png
        resources/fonts/Inter-Regular.ttf
        resources/fonts/Inter-SemiBold.ttf
)

target_include_directories(lumenis_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_ui PUBLIC Qt6::Widgets lumenis::core lumenis::ext lumenis::model)
lumenis_set_warnings(lumenis_ui)
