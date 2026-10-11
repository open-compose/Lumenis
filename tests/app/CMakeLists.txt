# End-to-end checks on the real executable.

add_test(NAME app_version COMMAND lumenis_app --version)
set_tests_properties(app_version PROPERTIES
    PASS_REGULAR_EXPRESSION "${PROJECT_VERSION}")

add_test(NAME app_smoke COMMAND lumenis_app --smoke-test)
set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    PASS_REGULAR_EXPRESSION "smoke-test: ok"
    TIMEOUT 60)

# The application discovers a module through LUMENIS_MODULE_PATH.
add_test(NAME app_list_providers COMMAND lumenis_app --list-providers)
set_tests_properties(app_list_providers PROPERTIES
    ENVIRONMENT "LUMENIS_MODULE_PATH=${CMAKE_BINARY_DIR}/test-modules/good"
    PASS_REGULAR_EXPRESSION "org\\.lumenis\\.test\\.codec"
    DEPENDS tst_module_loader)
