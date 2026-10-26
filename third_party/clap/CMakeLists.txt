# CLAP 1.2.9 (https://github.com/free-audio/clap, MIT): the CLever Audio Plug-in
# API's C headers, kept unchanged in include/. Header-only: the host is
# src/media/clap_host.* (docs/spec/clap-host.md).
add_library(lumenis_clap INTERFACE)
add_library(lumenis::clap ALIAS lumenis_clap)
# SYSTEM: the headers' own warnings are not ours.
target_include_directories(lumenis_clap SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
