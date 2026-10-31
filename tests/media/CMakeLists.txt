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
# AudioSuite: the processes, the effect on segments, and rendering (docs/spec/audiosuite.md).
lumenis_add_test(tst_audio_suite SOURCES tst_audio_suite.cpp LIBS lumenis::media)
# Multichannel tracks as they play (docs/spec/multichannel.md).
lumenis_add_test(tst_multichannel_audio SOURCES tst_multichannel_audio.cpp LIBS lumenis::media)
# Surround mixing: folds, output orders and surround mixdowns (docs/spec/surround.md).
lumenis_add_test(tst_surround SOURCES tst_surround.cpp LIBS lumenis::media)

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
# Audio timecode (LTC) and Change Sample Rate (docs/spec/clip-audio.md).
lumenis_add_test(tst_ltc SOURCES tst_ltc.cpp LIBS lumenis::media)
lumenis_add_test(tst_tone SOURCES tst_tone.cpp LIBS lumenis::media)
lumenis_add_test(tst_waveform SOURCES tst_waveform.cpp LIBS lumenis::media)
lumenis_add_test(tst_audio_project SOURCES tst_audio_project.cpp LIBS lumenis::media)
# The compositor: tracks, the dissolve family, Superimpose and precision (docs/spec/compositor.md).
lumenis_add_test(tst_compositor SOURCES tst_compositor.cpp LIBS lumenis::media)
# Timecode Burn-In (docs/spec/burn-in.md); draws text, so a GUI test.
lumenis_add_test(tst_burn_in SOURCES tst_burn_in.cpp LIBS lumenis::media)
# The Illusion FX segment effects (docs/spec/illusion-fx.md).
lumenis_add_test(tst_illusion_fx SOURCES tst_illusion_fx.cpp LIBS lumenis::media)
lumenis_add_test(tst_optical_flow SOURCES tst_optical_flow.cpp LIBS lumenis::media)
# Render Effect: precomputes and playing them (docs/spec/rendering.md).
lumenis_add_test(tst_render SOURCES tst_render.cpp LIBS lumenis::media)
