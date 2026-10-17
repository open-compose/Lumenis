# Finds FFmpeg's libraries: avcodec, avformat, avutil, swscale, swresample.
#
# Development builds use the FFmpeg the platform provides (BUILDING.md):
#   Linux    the distribution's packages (libavcodec-dev ...), via pkg-config
#   macOS    Homebrew's ffmpeg, via pkg-config or /opt/homebrew
#   Windows  the LGPL shared build that scripts/get-ffmpeg.ps1 unpacks
# FFMPEG_ROOT (a CMake or environment variable) points at another install:
# a folder with include/ and lib/ (and bin/ for Windows DLLs).
#
# Defines the imported targets FFmpeg::avcodec, FFmpeg::avformat,
# FFmpeg::avutil, FFmpeg::swscale and FFmpeg::swresample, FFmpeg_FOUND,
# FFmpeg_VERSION (libavcodec's) and, on Windows, FFmpeg_RUNTIME_DLLS.

set(_ffmpeg_libs avcodec avformat avutil swscale swresample)
set(_ffmpeg_root "${FFMPEG_ROOT}")
if(NOT _ffmpeg_root AND DEFINED ENV{FFMPEG_ROOT})
    set(_ffmpeg_root "$ENV{FFMPEG_ROOT}")
endif()
# Windows paths come with backslashes, which break the generated install
# scripts ("Invalid character escape '\U'").
if(_ffmpeg_root)
    file(TO_CMAKE_PATH "${_ffmpeg_root}" _ffmpeg_root)
endif()

find_package(PkgConfig QUIET)
set(FFmpeg_RUNTIME_DLLS "")
foreach(lib IN LISTS _ffmpeg_libs)
    if(TARGET FFmpeg::${lib})
        continue()
    endif()
    set(_inc "")
    set(_lib "")
    if(PKG_CONFIG_FOUND AND NOT _ffmpeg_root)
        pkg_check_modules(PC_${lib} QUIET lib${lib})
    endif()
    find_path(FFmpeg_${lib}_INCLUDE_DIR lib${lib}/${lib}.h
        HINTS ${_ffmpeg_root}/include ${PC_${lib}_INCLUDEDIRS} /opt/homebrew/include /usr/local/include)
    find_library(FFmpeg_${lib}_LIBRARY NAMES ${lib}
        HINTS ${_ffmpeg_root}/lib ${PC_${lib}_LIBRARY_DIRS} /opt/homebrew/lib /usr/local/lib)
    if(FFmpeg_${lib}_INCLUDE_DIR AND FFmpeg_${lib}_LIBRARY)
        add_library(FFmpeg::${lib} UNKNOWN IMPORTED)
        set_target_properties(FFmpeg::${lib} PROPERTIES
            IMPORTED_LOCATION "${FFmpeg_${lib}_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_${lib}_INCLUDE_DIR}")
        if(WIN32 AND _ffmpeg_root)
            file(GLOB _dll "${_ffmpeg_root}/bin/${lib}-*.dll")
            list(APPEND FFmpeg_RUNTIME_DLLS ${_dll})
        endif()
    endif()
    mark_as_advanced(FFmpeg_${lib}_INCLUDE_DIR FFmpeg_${lib}_LIBRARY)
endforeach()

# libavcodec's version, from its headers (version_major.h since FFmpeg 6).
if(FFmpeg_avcodec_INCLUDE_DIR)
    foreach(_h version_major.h version.h)
        set(_file "${FFmpeg_avcodec_INCLUDE_DIR}/libavcodec/${_h}")
        if(EXISTS "${_file}")
            file(STRINGS "${_file}" _major REGEX "#define LIBAVCODEC_VERSION_MAJOR")
            if(_major)
                string(REGEX REPLACE ".*MAJOR +([0-9]+).*" "\\1" _major "${_major}")
                break()
            endif()
        endif()
    endforeach()
    file(STRINGS "${FFmpeg_avcodec_INCLUDE_DIR}/libavcodec/version.h" _minor REGEX "#define LIBAVCODEC_VERSION_MINOR")
    string(REGEX REPLACE ".*MINOR +([0-9]+).*" "\\1" _minor "${_minor}")
    set(FFmpeg_VERSION "${_major}.${_minor}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFmpeg_avcodec_LIBRARY FFmpeg_avformat_LIBRARY FFmpeg_avutil_LIBRARY
                  FFmpeg_swscale_LIBRARY FFmpeg_swresample_LIBRARY FFmpeg_avcodec_INCLUDE_DIR
    VERSION_VAR FFmpeg_VERSION)
