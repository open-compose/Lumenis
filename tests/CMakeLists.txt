# lumenis_add_test(<name> SOURCES ... LIBS ...) builds a Qt Test executable
# and registers it with CTest.
set(LUMENIS_TEST_LOGS ${CMAKE_BINARY_DIR}/test-logs)
file(MAKE_DIRECTORY ${LUMENIS_TEST_LOGS})

function(lumenis_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    lumenis_set_warnings(${name})
    # The report goes to the console and to test-logs/<name>.txt, which CI
    # prints when a test fails: ctest's capture of a test's output can come
    # back without it.
    add_test(NAME ${name} COMMAND ${name} -o ${LUMENIS_TEST_LOGS}/${name}.txt,txt -o -,txt)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

add_subdirectory(core)
add_subdirectory(ext)
add_subdirectory(model)
add_subdirectory(media)
add_subdirectory(ui)
add_subdirectory(app)
