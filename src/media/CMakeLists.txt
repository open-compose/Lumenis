add_library(lumenis_media STATIC
    media.cpp
    link.cpp
    playback.cpp
    include/lumenis/media/media.h
    include/lumenis/media/link.h
    include/lumenis/media/playback.h
)
add_library(lumenis::media ALIAS lumenis_media)

target_include_directories(lumenis_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_media
    PUBLIC lumenis::core lumenis::ext lumenis::model Qt6::Gui
    PRIVATE lumenis::miniaudio FFmpeg::avformat FFmpeg::avcodec FFmpeg::swscale FFmpeg::swresample FFmpeg::avutil)
target_compile_definitions(lumenis_media PRIVATE LUMENIS_PATENT_CODECS=$<BOOL:${LUMENIS_PATENT_CODECS}>)
lumenis_set_warnings(lumenis_media)
