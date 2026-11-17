add_library(lumenis_media STATIC
    media.cpp
    compositor.cpp
    optical_flow.cpp
    fields.cpp
    color_transform.cpp
    dynamic_relink.cpp
    display_color.cpp
    aces.cpp
    lut_file.cpp
    frame_flex.cpp
    target_mask.cpp
    stereo.cpp
    proxy_media.cpp
    copy_media.cpp
    fluid_fx.cpp
    render.cpp
    wipes.cpp
    warp3d.cpp
    pan_scan.cpp
    pan_zoom.cpp
    tracking.cpp
    region_stabilize.cpp
    titler.cpp
    classic_title.cpp
    effect_handles.cpp
    effect_drawing.cpp
    effect_drawing.h
    image_effects.cpp
    color_effects.cpp
    intraframe.cpp
    pict.cpp
    psd.cpp
    plasma_wipe.cpp
    auto_correct.cpp
    color_correction.cpp
    color_match.cpp
    custom_color.cpp
    scopes.cpp
    key_effects.cpp
    key_mattes.cpp
    burn_in.cpp
    illusion_common.h
    illusion_fx.cpp
    illusion_transitions.cpp
    audio_fx.cpp
    audio_suite.cpp
    audio_input.cpp
    clap_host.cpp
    ofx_host.cpp
    ocio_colour.cpp
    movie_export.cpp
    xdcam_disk.cpp
    as11.cpp
    dcp.cpp
    imf_export.cpp
    tape_deck.cpp
    test_patterns.cpp
    subcap.cpp
    transcription.cpp
    ocio_colour.h
    link.cpp
    ltc.cpp
    tone.cpp
    time_stretch.cpp
    waveform.cpp
    waveform_sync.cpp
    playback.cpp
    managed.cpp
    batch_import.cpp
    graphic_import.cpp
    sequence_report.cpp
    audio_cd.cpp
    consolidate.cpp
    relink.cpp
    mixdown.cpp
    dynamic_media_folders.cpp
    surround.cpp
    include/lumenis/media/media.h
    include/lumenis/media/compositor.h
    include/lumenis/media/pict.h
    include/lumenis/media/psd.h
    include/lumenis/media/plasma_wipe.h
    include/lumenis/media/auto_correct.h
    include/lumenis/media/color_correction.h
    include/lumenis/media/color_match.h
    include/lumenis/media/custom_color.h
    include/lumenis/media/scopes.h
    include/lumenis/media/optical_flow.h
    include/lumenis/media/render.h
    include/lumenis/media/audio_fx.h
    include/lumenis/media/audio_suite.h
    include/lumenis/media/audio_input.h
    include/lumenis/media/dynamic_media_folders.h
    include/lumenis/media/color_transform.h
    include/lumenis/media/display_color.h
    include/lumenis/media/aces.h
    include/lumenis/media/lut_file.h
    include/lumenis/media/frame_flex.h
    include/lumenis/media/target_mask.h
    include/lumenis/media/clap_host.h
    include/lumenis/media/ofx_host.h
    include/lumenis/media/movie_export.h
    include/lumenis/media/xdcam_disk.h
    include/lumenis/media/as11.h
    include/lumenis/media/tape_deck.h
    include/lumenis/media/test_patterns.h
    include/lumenis/media/link.h
    include/lumenis/media/ltc.h
    include/lumenis/media/tone.h
    include/lumenis/media/time_stretch.h
    include/lumenis/media/waveform.h
    include/lumenis/media/waveform_sync.h
    include/lumenis/media/playback.h
    include/lumenis/media/managed.h
    include/lumenis/media/consolidate.h
    include/lumenis/media/relink.h
    include/lumenis/media/mixdown.h
)
add_library(lumenis::media ALIAS lumenis_media)

target_include_directories(lumenis_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_media
    PUBLIC lumenis::core lumenis::ext lumenis::model Qt6::Gui
    PRIVATE lumenis::miniaudio lumenis::clap lumenis::openfx FFmpeg::avformat FFmpeg::avcodec FFmpeg::swscale FFmpeg::swresample FFmpeg::avutil)
target_compile_definitions(lumenis_media PRIVATE LUMENIS_PATENT_CODECS=$<BOOL:${LUMENIS_PATENT_CODECS}>)
# The OFX host's colour conversions (ocio_colour.cpp) use OpenColorIO when it was found; ocio_colour.h is the same either way.
if(TARGET OpenColorIO::OpenColorIO)
    target_link_libraries(lumenis_media PRIVATE OpenColorIO::OpenColorIO)
    target_compile_definitions(lumenis_media PRIVATE LUMENIS_HAVE_OCIO=1)
endif()
lumenis_set_warnings(lumenis_media)
# GCC's -O2 leaves a loop with a comparison in it unvectorised while floating-point exceptions may be observed
# (-ftrapping-math, its default); nothing here reads them. Clang vectorises such loops already. With this the picture
# passes' clamps and roundings run eight values at a time (10.1b: Picture::quantize, a third of a dissolve's work).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(lumenis_media PRIVATE -fno-trapping-math)
endif()

# MSVC 14.44 (cl 14.44.35207) stops with an internal compiler error (C1001) generating code for key_effects.cpp's RGB
# Keyer (CI runs 219-321; BUILDING.md), with the optimizer off as well. The file is compiled without optimization and
# with its debug information in the object file (/Z7) rather than the shared PDB (/Zi). Its loops are in
# key_mattes.cpp, which keeps /O2, so the cost is small.
if(MSVC)
    set_source_files_properties(key_effects.cpp PROPERTIES COMPILE_OPTIONS "/Od;/Ob0;/Z7")
endif()
