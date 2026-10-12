set(ui_test_libs lumenis::ui Qt6::Widgets)

lumenis_add_test(tst_command_registry SOURCES tst_command_registry.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_workspace SOURCES tst_workspace.cpp LIBS ${ui_test_libs})
lumenis_add_test(tst_theme SOURCES tst_theme.cpp LIBS ${ui_test_libs})

lumenis_add_test(tst_menu_inventory SOURCES tst_menu_inventory.cpp LIBS ${ui_test_libs})
target_compile_definitions(tst_menu_inventory PRIVATE
    MENU_INVENTORY="${PROJECT_SOURCE_DIR}/tests/data/menu_inventory.json")
