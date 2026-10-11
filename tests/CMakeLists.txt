# lumenis_add_test(<name> SOURCES ... LIBS ...) builds a Qt Test executable
# and registers it with CTest.
function(lumenis_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    lumenis_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

add_subdirectory(core)
add_subdirectory(ext)
add_subdirectory(app)
