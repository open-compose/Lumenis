# End-to-end checks on the real executable.

add_test(NAME app_version COMMAND lumenis_app --version)
set_tests_properties(app_version PROPERTIES
    PASS_REGULAR_EXPRESSION "${PROJECT_VERSION}")

add_test(NAME app_smoke COMMAND lumenis_app --smoke-test)
set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    PASS_REGULAR_EXPRESSION "smoke-test: ok"
    TIMEOUT 60)

# The application discovers a module through <PRODUCT>_MODULE_PATH.
add_test(NAME app_list_providers COMMAND lumenis_app --list-providers)
set_tests_properties(app_list_providers PROPERTIES
    ENVIRONMENT "${PRODUCT_ENV_PREFIX}_MODULE_PATH=${CMAKE_BINARY_DIR}/test-modules/good"
    PASS_REGULAR_EXPRESSION "org\\.lumenis\\.test\\.codec"
    DEPENDS tst_module_loader)

# The Phase 1 gate 5 checklist (docs/checklists/phase1-gate5.md) matches the
# menu bar; scripts/make-checklist.py rewrites it.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME checklist_current
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/make-checklist.py
                --app $<TARGET_FILE:lumenis_app> --check)
    set_tests_properties(checklist_current PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        PASS_REGULAR_EXPRESSION "checklist: current"
        TIMEOUT 60)
endif()
