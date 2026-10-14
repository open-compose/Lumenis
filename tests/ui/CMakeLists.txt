set(ui_test_libs lumenis::ui Qt6::Widgets)

lumenis_add_test(tst_command_registry SOURCES tst_command_registry.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_workspace SOURCES tst_workspace.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_panels SOURCES tst_panels.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_theme SOURCES tst_theme.cpp LIBS ${ui_test_libs})

lumenis_add_test(tst_menu_inventory SOURCES tst_menu_inventory.cpp LIBS ${ui_test_libs})
target_compile_definitions(tst_menu_inventory PRIVATE
    MENU_INVENTORY="${PROJECT_SOURCE_DIR}/tests/data/menu_inventory.json")
lumenis_add_test(tst_select_project_dialog SOURCES tst_select_project_dialog.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_main_window SOURCES tst_main_window.cpp LIBS ${ui_test_libs})

lumenis_add_test(tst_keyboard SOURCES tst_keyboard.cpp LIBS ${ui_test_libs})
target_compile_definitions(tst_keyboard PRIVATE
    KEYBOARD_INVENTORY="${PROJECT_SOURCE_DIR}/tests/data/keyboard_inventory.json")
lumenis_add_test(tst_bin_container SOURCES tst_bin_container.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_views SOURCES tst_bin_views.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_bin_dock SOURCES tst_bin_dock.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_platform_check SOURCES tst_platform_check.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_composer SOURCES tst_composer.cpp LIBS ${ui_test_libs})
