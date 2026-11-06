# Finds OpenColorIO (BSD-3), the optional colour management library behind the OFX host's colour suite (6.11c3,
# docs/spec/ofx-host.md "Colour management"). Without it the host still names the project's colour space to plug-ins
# but converts nothing.
#
# OpenColorIO's own CMake package is tried first (Homebrew, vcpkg, a build from source). Ubuntu's libopencolorio-dev
# installs that package's files straight into /usr/share/cmake, where find_package does not look, so the header and
# library are also looked for directly. OpenColorIO_ROOT (a CMake or environment variable) points at another install.
#
# Defines the imported target OpenColorIO::OpenColorIO and OpenColorIO_FOUND.

find_package(OpenColorIO CONFIG QUIET)
if(TARGET OpenColorIO::OpenColorIO)
    set(OpenColorIO_FOUND TRUE)
    return()
endif()

find_path(OpenColorIO_INCLUDE_DIR OpenColorIO/OpenColorIO.h)
find_library(OpenColorIO_LIBRARY NAMES OpenColorIO OpenColorIO_2_4 OpenColorIO_2_3 OpenColorIO_2_2 OpenColorIO_2_1)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenColorIO REQUIRED_VARS OpenColorIO_LIBRARY OpenColorIO_INCLUDE_DIR)
if(OpenColorIO_FOUND AND NOT TARGET OpenColorIO::OpenColorIO)
    add_library(OpenColorIO::OpenColorIO UNKNOWN IMPORTED)
    set_target_properties(OpenColorIO::OpenColorIO PROPERTIES
        IMPORTED_LOCATION "${OpenColorIO_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OpenColorIO_INCLUDE_DIR}")
endif()
mark_as_advanced(OpenColorIO_INCLUDE_DIR OpenColorIO_LIBRARY)
