lumenis_add_test(tst_media SOURCES tst_media.cpp LIBS lumenis::media)
target_compile_definitions(tst_media PRIVATE LUMENIS_PATENT_CODECS=$<BOOL:${LUMENIS_PATENT_CODECS}>)
if(WIN32)
    target_link_libraries(tst_media PRIVATE ole32)  # soundLeavesComAlone
endif()
# Audio Mixdown and ROADMAP Phase 5 gate 1's null test (docs/spec/audio-mixdown.md).
lumenis_add_test(tst_mixdown SOURCES tst_mixdown.cpp LIBS lumenis::media)
# Audio EQ: the bands, setting the effect, and the filtered sound (docs/spec/audio-eq.md).
lumenis_add_test(tst_audio_eq SOURCES tst_audio_eq.cpp LIBS lumenis::media)
lumenis_add_test(tst_audio_fx SOURCES tst_audio_fx.cpp LIBS lumenis::media)

# The CLAP host's tests load a CLAP file of our own (clap_test_plugin.cpp), built as
# cutthulhu-test.clap in a folder of its own.
add_library(cutthulhu_test_clap MODULE clap_test_plugin.cpp)
target_link_libraries(cutthulhu_test_clap PRIVATE lumenis::clap)
set_target_properties(cutthulhu_test_clap PROPERTIES
    PREFIX "" SUFFIX ".clap" OUTPUT_NAME "cutthulhu-test"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test-clap
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test-clap)
lumenis_add_test(tst_clap_host SOURCES tst_clap_host.cpp LIBS lumenis::media)
add_dependencies(tst_clap_host cutthulhu_test_clap)
target_compile_definitions(tst_clap_host PRIVATE LUMENIS_TEST_CLAP="$<TARGET_FILE:cutthulhu_test_clap>")
