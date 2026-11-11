# Interchange: AAF and the formats other editors read and write (ROADMAP
# Phase 3, docs/spec/aaf.md, docs/spec/avb.md, docs/spec/edl.md, docs/spec/ale.md,
# docs/spec/otio.md, docs/spec/fcp-xml.md).
add_library(lumenis_interop STATIC
    cfb.cpp
    aaf.cpp
    aaf_dump.cpp
    aaf_import.cpp
    aaf_writer.cpp
    aaf_export.cpp
    aaf_model.inc
    avb.cpp
    avb_import.cpp
    avb_writer.cpp
    avb_export.cpp
    edl.cpp
    cut_list.cpp
    ale.cpp
    otio.cpp
    fcp_xml.cpp
    import_common.h
    text_common.h
    include/lumenis/interop/cfb.h
    include/lumenis/interop/aaf.h
    include/lumenis/interop/aaf_import.h
    include/lumenis/interop/aaf_writer.h
    include/lumenis/interop/aaf_export.h
    include/lumenis/interop/avb.h
    include/lumenis/interop/avb_import.h
    include/lumenis/interop/avb_writer.h
    include/lumenis/interop/avb_export.h
    include/lumenis/interop/edl.h
    include/lumenis/interop/cut_list.h
    include/lumenis/interop/ale.h
    include/lumenis/interop/otio.h
    include/lumenis/interop/fcp_xml.h
)
add_library(lumenis::interop ALIAS lumenis_interop)

target_include_directories(lumenis_interop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lumenis_interop PUBLIC lumenis::model Qt6::Core)
lumenis_set_warnings(lumenis_interop)
