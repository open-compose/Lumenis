# miniaudio 0.11.22 (https://miniaud.io, public domain or MIT-0): audio
# output on Core Audio, WASAPI, PulseAudio/ALSA and a null device. The
# platform libraries are loaded at run time, so nothing needs installing.
add_library(lumenis_miniaudio STATIC miniaudio.cpp)
add_library(lumenis::miniaudio ALIAS lumenis_miniaudio)
# SYSTEM: the header's own warnings are not ours.
target_include_directories(lumenis_miniaudio SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lumenis_miniaudio PUBLIC MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION
                           MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE)
set_target_properties(lumenis_miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(lumenis_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(APPLE)
    target_link_libraries(lumenis_miniaudio PUBLIC "-framework CoreFoundation" "-framework CoreAudio"
                          "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(lumenis_miniaudio PUBLIC m)
endif()
