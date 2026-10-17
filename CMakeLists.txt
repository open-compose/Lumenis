cmake_minimum_required(VERSION 3.24)

# Everything user-visible (product name "Cutthulhu") derives from
# cmake/Branding.cmake. "lumenis" remains the internal code name used
# for targets, namespaces and source paths.
include(cmake/Branding.cmake)

project(lumenis
    VERSION ${PRODUCT_VERSION}
    DESCRIPTION "${PRODUCT_DESCRIPTION}"
    HOMEPAGE_URL "${PRODUCT_HOMEPAGE}"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(LUMENIS_BUILD_TESTS "Build unit tests" ON)
option(LUMENIS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Qt 6.4 is the oldest supported (Ubuntu 24.04 system Qt). Release builds use
# the Qt 6.8 LTS series.
set(LUMENIS_QT_MIN_VERSION 6.4)
find_package(Qt6 ${LUMENIS_QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets)
if(LUMENIS_BUILD_TESTS)
    find_package(Qt6 ${LUMENIS_QT_MIN_VERSION} REQUIRED COMPONENTS Test)
endif()

# Linux desktops run Wayland (and so does WSLg). Without Qt's Wayland
# platform plugin the app still runs, through XWayland, but more slowly and
# with the startup warning 'Could not find the Qt platform plugin "wayland"'.
# Its parts are separate packages (qt6-wayland pulls them in), so say so at
# configure time; see BUILDING.md.
if(UNIX AND NOT APPLE AND QT6_INSTALL_PREFIX AND QT6_INSTALL_PLUGINS)
    set(_qt_plugins "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
    # The platform plugin (libqwayland-egl/-generic.so up to Qt 6.9,
    # libqwayland.so from 6.10) and the xdg-shell integration windows need.
    # Ubuntu 26.04 installs the first with the Qt development packages but
    # not the second (src/ui/platform_check.h).
    file(GLOB _wayland_platform "${_qt_plugins}/platforms/libqwayland*.so")
    if(EXISTS "${_qt_plugins}/platforms"
       AND (NOT _wayland_platform OR NOT EXISTS "${_qt_plugins}/wayland-shell-integration/libxdg-shell.so"))
        message(WARNING
            "Qt's Wayland support is incomplete (platform plugin or xdg-shell missing in ${_qt_plugins}). "
            "Cutthulhu will run through XWayland. Install it with: sudo apt install qt6-wayland "
            "(Fedora: sudo dnf install qt6-qtwayland), or run scripts/bootstrap-linux.sh.")
    endif()
endif()

# Qt (6.8.3 and earlier) links the long-deprecated AGL framework through
# WrapOpenGL, but the macOS 26 SDK (Xcode 26) no longer ships it, so linking
# fails with "framework 'AGL' not found". Nothing uses AGL; drop it.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_wrap_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_wrap_gl_libs)
        list(FILTER _wrap_gl_libs EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wrap_gl_libs}")
    endif()
endif()

if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    qt_standard_project_setup()
else()
    set(CMAKE_AUTOMOC ON)
    include(GNUInstallDirs)
    # moc from Qt 6.4 cannot parse GCC 13's C++20 standard headers when it is
    # given the compiler's predefined macros. Our moc'd code never depends on
    # them, so leave them out.
    set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)
endif()

include(cmake/CompilerWarnings.cmake)

# FFmpeg decodes and encodes media (Phase 2). Development builds use the
# platform's FFmpeg; BUILDING.md says how to install it.
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
find_package(FFmpeg 60.0)
if(NOT FFmpeg_FOUND)
    message(FATAL_ERROR
        "FFmpeg 6.0 or later was not found (libavcodec, libavformat, libavutil, libswscale, libswresample). "
        "Linux: sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev. "
        "macOS: brew install ffmpeg pkg-config. Windows: scripts\\get-ffmpeg.ps1, then set FFMPEG_ROOT. "
        "Or set FFMPEG_ROOT to a folder with include/ and lib/.")
endif()
# Codecs whose patents or licences need counsel's review (docs/EXTENSIONS.md,
# marked with a dagger): development and test builds include them; official
# release builds turn this off until counsel signs off.
option(LUMENIS_PATENT_CODECS "Allow the codecs that await legal review (DNxHD, ProRes, H.264, HEVC, AAC, ...)" ON)

# Put every runtime artefact (executables, shared libraries, loadable
# modules) in one directory so the app finds its modules in the build tree
# exactly as it does after installation.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src/core)
add_subdirectory(src/ext)
add_subdirectory(src/model)
add_subdirectory(src/media)
add_subdirectory(src/ui)
add_subdirectory(app)

if(LUMENIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
