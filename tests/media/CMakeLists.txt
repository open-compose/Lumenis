lumenis_add_test(tst_media SOURCES tst_media.cpp LIBS lumenis::media)
target_compile_definitions(tst_media PRIVATE LUMENIS_PATENT_CODECS=$<BOOL:${LUMENIS_PATENT_CODECS}>)
if(WIN32)
    target_link_libraries(tst_media PRIVATE ole32)  # soundLeavesComAlone
endif()
# Audio Mixdown and ROADMAP Phase 5 gate 1's null test (docs/spec/audio-mixdown.md).
lumenis_add_test(tst_mixdown SOURCES tst_mixdown.cpp LIBS lumenis::media)
lumenis_add_test(tst_movie_export SOURCES tst_movie_export.cpp LIBS lumenis::media)
lumenis_add_test(tst_dcp SOURCES tst_dcp.cpp LIBS lumenis::media)
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

# The OFX host's tests load an OFX binary of our own (ofx_test_plugin.cpp), built inside a bundle as the standard lays
# it out: test-ofx/CutthulhuTest.ofx.bundle/Contents/<architecture>/CutthulhuTest.ofx. The $<1:...> keeps
# multi-configuration generators from adding a configuration folder.
if(WIN32)
    set(lumenis_ofx_arch Win64)
elseif(APPLE)
    set(lumenis_ofx_arch MacOS)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(lumenis_ofx_arch Linux-aarch64)
else()
    set(lumenis_ofx_arch Linux-x86-64)
endif()
set(lumenis_ofx_dir ${CMAKE_BINARY_DIR}/test-ofx/CutthulhuTest.ofx.bundle/Contents/${lumenis_ofx_arch})
add_library(cutthulhu_test_ofx MODULE ofx_test_plugin.cpp)
target_link_libraries(cutthulhu_test_ofx PRIVATE lumenis::openfx)
set_target_properties(cutthulhu_test_ofx PROPERTIES
    PREFIX "" SUFFIX ".ofx" OUTPUT_NAME "CutthulhuTest"
    LIBRARY_OUTPUT_DIRECTORY $<1:${lumenis_ofx_dir}>
    RUNTIME_OUTPUT_DIRECTORY $<1:${lumenis_ofx_dir}>)
lumenis_add_test(tst_ofx_host SOURCES tst_ofx_host.cpp LIBS lumenis::media)
add_dependencies(tst_ofx_host cutthulhu_test_ofx)
target_compile_definitions(tst_ofx_host PRIVATE LUMENIS_TEST_OFX="$<TARGET_FILE:cutthulhu_test_ofx>"
    LUMENIS_TEST_OFX_FOLDER="${CMAKE_BINARY_DIR}/test-ofx")
# Audio timecode (LTC) and Change Sample Rate (docs/spec/clip-audio.md).
lumenis_add_test(tst_ltc SOURCES tst_ltc.cpp LIBS lumenis::media)
lumenis_add_test(tst_dmf SOURCES tst_dmf.cpp LIBS lumenis::media)
lumenis_add_test(tst_frame_flex SOURCES tst_frame_flex.cpp LIBS lumenis::media)
lumenis_add_test(tst_color_adapter SOURCES tst_color_adapter.cpp LIBS lumenis::media)
lumenis_add_test(tst_tone SOURCES tst_tone.cpp LIBS lumenis::media)
lumenis_add_test(tst_waveform SOURCES tst_waveform.cpp LIBS lumenis::media)
# Syncing clips by their sound (Editing Guide p.1124, p.1126).
lumenis_add_test(tst_waveform_sync SOURCES tst_waveform_sync.cpp LIBS lumenis::media)
lumenis_add_test(tst_transcription SOURCES tst_transcription.cpp LIBS lumenis::media)
lumenis_add_test(tst_audio_project SOURCES tst_audio_project.cpp LIBS lumenis::media)
# The compositor: tracks, the dissolve family, Superimpose and precision (docs/spec/compositor.md).
lumenis_add_test(tst_compositor SOURCES tst_compositor.cpp LIBS lumenis::media)
lumenis_add_test(tst_warp3d SOURCES tst_warp3d.cpp LIBS lumenis::media)
lumenis_add_test(tst_intraframe SOURCES tst_intraframe.cpp LIBS lumenis::media)
lumenis_add_test(tst_color_correction SOURCES tst_color_correction.cpp LIBS lumenis::media)
lumenis_add_test(tst_scopes SOURCES tst_scopes.cpp LIBS lumenis::media)
lumenis_add_test(tst_auto_correct SOURCES tst_auto_correct.cpp LIBS lumenis::media)
lumenis_add_test(tst_color_match SOURCES tst_color_match.cpp LIBS lumenis::media)
lumenis_add_test(tst_effect_handles SOURCES tst_effect_handles.cpp LIBS lumenis::media)
lumenis_add_test(tst_pan_scan SOURCES tst_pan_scan.cpp LIBS lumenis::media)
lumenis_add_test(tst_pan_zoom SOURCES tst_pan_zoom.cpp LIBS lumenis::media)
lumenis_add_test(tst_tracking SOURCES tst_tracking.cpp LIBS lumenis::media)
lumenis_add_test(tst_titler SOURCES tst_titler.cpp LIBS lumenis::media)
lumenis_add_test(tst_classic_title SOURCES tst_classic_title.cpp LIBS lumenis::media)
# Timecode Burn-In (docs/spec/burn-in.md); draws text, so a GUI test.
lumenis_add_test(tst_burn_in SOURCES tst_burn_in.cpp LIBS lumenis::media)
# The Illusion FX segment effects (docs/spec/illusion-fx.md).
lumenis_add_test(tst_illusion_fx SOURCES tst_illusion_fx.cpp LIBS lumenis::media)
lumenis_add_test(tst_optical_flow SOURCES tst_optical_flow.cpp LIBS lumenis::media)
# Fields, pulldown and format conversions (FX Guide p.181-186, p.203-205).
lumenis_add_test(tst_fields SOURCES tst_fields.cpp LIBS lumenis::media)
# FluidBlur, FluidColorMap and FluidMorph (FX Guide p.467, p.510-512).
lumenis_add_test(tst_fluid_fx SOURCES tst_fluid_fx.cpp LIBS lumenis::media)
# Render Effect: precomputes and playing them (docs/spec/rendering.md).
lumenis_add_test(tst_render SOURCES tst_render.cpp LIBS lumenis::media)

lumenis_isolate_media_drives()
