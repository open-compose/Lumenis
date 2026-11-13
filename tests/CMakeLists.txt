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
    # The tests below the UI (core, model, media, interop) are labelled "core": the tsan preset runs only them. In the
    # UI tests Qt's own threads (its thread pool, the file system model's gatherer) synchronise inside a Qt that is not
    # built with ThreadSanitizer, which it reports as races that no suppression can tell from ours (tests/tsan.supp).
    get_filename_component(dir ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    if(dir MATCHES "^(core|model|media|interop)$")
        set_tests_properties(${name} PROPERTIES LABELS core)
    endif()
endfunction()

# Every test gets a media drive folder of its own and none of the system's volumes
# (LUMENIS_NO_SYSTEM_DRIVES, lumenis/media/managed.h): tests that make managed media wrote to the
# real drive's Avid MediaFiles, and a test scanning it while another wrote there could read a file
# half written (FFmpeg crashed probing one, tst_motion_adapter_ui under ctest -j8). Tests that set
# their own drives (media::setExtraMediaDrives) still do.
# Each tests/ subdirectory calls this last, on its own tests. The folders are made when CMake runs
# and again by the test_media_drives fixture before any test, so deleting build/<preset>/test-media
# (to clear old test media) cannot leave a test without its drive.
function(lumenis_isolate_media_drives)
    get_property(tests DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY TESTS)
    foreach(test IN LISTS tests)
        set(drive ${CMAKE_BINARY_DIR}/test-media/${test})
        file(MAKE_DIRECTORY ${drive})
        set_property(GLOBAL APPEND PROPERTY LUMENIS_TEST_MEDIA_DRIVES ${drive})
        set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT
            "LUMENIS_NO_SYSTEM_DRIVES=1" "LUMENIS_MEDIA_DRIVES=${drive}"
            # Transcripts (lumenis/model/transcript.h) in the test's folder, never the user's.
            "LUMENIS_TRANSCRIPTS=${drive}/Transcripts")
        set_property(TEST ${test} APPEND PROPERTY FIXTURES_REQUIRED test_media_drives)
    endforeach()
endfunction()


add_subdirectory(core)
add_subdirectory(ext)
add_subdirectory(model)
add_subdirectory(interop)
add_subdirectory(media)
add_subdirectory(ui)
add_subdirectory(app)
add_subdirectory(perf)

# Makes every test's drive folder (lumenis_isolate_media_drives); ctest runs it before the tests
# that need it, also under -R.
get_property(lumenis_test_drives GLOBAL PROPERTY LUMENIS_TEST_MEDIA_DRIVES)
add_test(NAME test_media_drives COMMAND ${CMAKE_COMMAND} -E make_directory ${lumenis_test_drives})
set_tests_properties(test_media_drives PROPERTIES FIXTURES_SETUP test_media_drives)

