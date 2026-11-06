add_library(lumenis_ext STATIC
    provider.cpp
    module_loader.cpp
    include/lumenis/ext/provider.h
    include/lumenis/ext/module_abi.h
    include/lumenis/ext/module_loader.h
    include/lumenis/ext/codec.h
    include/lumenis/ext/transcription.h
    include/lumenis/ext/plugin_host.h
    include/lumenis/ext/export_device.h
    include/lumenis/ext/deck.h
    include/lumenis/ext/video_output.h
)
add_library(lumenis::ext ALIAS lumenis_ext)

target_include_directories(lumenis_ext PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_ext PUBLIC lumenis::core PRIVATE Qt6::Core)
lumenis_set_warnings(lumenis_ext)
set_target_properties(lumenis_ext PROPERTIES POSITION_INDEPENDENT_CODE ON)

# What a separately built module compiles against: the provider interfaces
# and the core value types they use, but no host code.
add_library(lumenis_module_sdk INTERFACE)
add_library(lumenis::module_sdk ALIAS lumenis_module_sdk)
target_include_directories(lumenis_module_sdk INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    $<TARGET_PROPERTY:lumenis_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_features(lumenis_module_sdk INTERFACE cxx_std_20)
