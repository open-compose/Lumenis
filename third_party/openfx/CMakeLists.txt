# OpenFX 1.5.1 (https://github.com/AcademySoftwareFoundation/openfx, tag OFX_Release_1.5.1, BSD 3-Clause): the
# image effect plug-in standard's C headers, kept unchanged in include/. Header-only: the host is
# src/media/ofx_host.* (docs/spec/ofx-host.md).
add_library(lumenis_openfx INTERFACE)
add_library(lumenis::openfx ALIAS lumenis_openfx)
# SYSTEM: the headers' own warnings are not ours.
target_include_directories(lumenis_openfx SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
