add_library(lumenis_media STATIC
    media.cpp
    audio_fx.cpp
    audio_suite.cpp
    audio_input.cpp
    clap_host.cpp
    link.cpp
    playback.cpp
    managed.cpp
    consolidate.cpp
    relink.cpp
    mixdown.cpp
    include/lumenis/media/media.h
    include/lumenis/media/audio_fx.h
    include/lumenis/media/audio_suite.h
    include/lumenis/media/audio_input.h
    include/lumenis/media/clap_host.h
    include/lumenis/media/link.h
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
    PRIVATE lumenis::miniaudio lumenis::clap FFmpeg::avformat FFmpeg::avcodec FFmpeg::swscale FFmpeg::swresample FFmpeg::avutil)
target_compile_definitions(lumenis_media PRIVATE LUMENIS_PATENT_CODECS=$<BOOL:${LUMENIS_PATENT_CODECS}>)
lumenis_set_warnings(lumenis_media)
