lumenis_add_test(tst_provider_registry SOURCES tst_provider_registry.cpp LIBS lumenis::ext)

# Three copies of the same sample module: one well-formed, one claiming a
# future ABI version that the loader must refuse, and one whose register
# function adds its provider and then fails (the loader must remove it).
foreach(variant good bad_abi fails)
    set(target test_module_${variant})
    add_library(${target} MODULE test_module.cpp)
    target_link_libraries(${target} PRIVATE lumenis::module_sdk)
    lumenis_set_warnings(${target})
    set_target_properties(${target} PROPERTIES
        # $<0:> stops multi-config generators appending a per-config subdirectory.
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test-modules/${variant}$<0:>
        PREFIX "")
endforeach()
target_compile_definitions(test_module_bad_abi PRIVATE TEST_MODULE_BAD_ABI)
target_compile_definitions(test_module_fails PRIVATE TEST_MODULE_FAILS)

lumenis_add_test(tst_module_loader SOURCES tst_module_loader.cpp LIBS lumenis::ext)
add_dependencies(tst_module_loader test_module_good test_module_bad_abi test_module_fails)
target_compile_definitions(tst_module_loader PRIVATE
    TEST_MODULE_GOOD="$<TARGET_FILE:test_module_good>"
    TEST_MODULE_GOOD_DIR="$<TARGET_FILE_DIR:test_module_good>"
    TEST_MODULE_BAD_ABI="$<TARGET_FILE:test_module_bad_abi>"
    TEST_MODULE_FAILS="$<TARGET_FILE:test_module_fails>")

lumenis_isolate_media_drives()
