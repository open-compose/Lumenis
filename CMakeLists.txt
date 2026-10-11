cmake_minimum_required(VERSION 3.24)

# The product (display) name is not final; everything user-visible derives
# from cmake/Branding.cmake. "lumenis" remains the internal code name used
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

# Put every runtime artefact (executables, shared libraries, loadable
# modules) in one directory so the app finds its modules in the build tree
# exactly as it does after installation.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src/core)
add_subdirectory(src/ext)
add_subdirectory(src/ui)
add_subdirectory(app)

if(LUMENIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
