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

# Every test gets a media drive folder of its own and none of the system's volumes
# (LUMENIS_NO_SYSTEM_DRIVES, lumenis/media/managed.h): tests that make managed media wrote to the
# real drive's Avid MediaFiles, and a test scanning it while another wrote there could read a file
# half written (FFmpeg crashed probing one, tst_motion_adapter_ui under ctest -j8). Tests that set
# their own drives (media::setExtraMediaDrives) still do.
# Each tests/ subdirectory calls this last, on its own tests.
function(lumenis_isolate_media_drives)
    get_property(tests DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY TESTS)
    foreach(test IN LISTS tests)
        set(drive ${CMAKE_BINARY_DIR}/test-media/${test})
        file(MAKE_DIRECTORY ${drive})
        set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT
            "LUMENIS_NO_SYSTEM_DRIVES=1" "LUMENIS_MEDIA_DRIVES=${drive}")
    endforeach()
endfunction()


add_subdirectory(core)
add_subdirectory(ext)
add_subdirectory(model)
add_subdirectory(interop)
add_subdirectory(media)
add_subdirectory(ui)
add_subdirectory(app)

