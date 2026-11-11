lumenis_add_test(tst_cfb SOURCES tst_cfb.cpp LIBS lumenis::interop)
lumenis_add_test(tst_aaf SOURCES tst_aaf.cpp LIBS lumenis::interop)
lumenis_add_test(tst_avb SOURCES tst_avb.cpp LIBS lumenis::interop)
lumenis_add_test(tst_edl SOURCES tst_edl.cpp LIBS lumenis::interop)
lumenis_add_test(tst_cut_list SOURCES tst_cut_list.cpp LIBS lumenis::interop)
lumenis_add_test(tst_ale SOURCES tst_ale.cpp LIBS lumenis::interop)
lumenis_add_test(tst_otio SOURCES tst_otio.cpp LIBS lumenis::interop)
lumenis_add_test(tst_fcpxml SOURCES tst_fcpxml.cpp LIBS lumenis::interop)
foreach(t tst_cfb tst_aaf tst_avb tst_edl tst_ale tst_otio tst_fcpxml)
    target_compile_definitions(${t} PRIVATE LUMENIS_TEST_DATA="${PROJECT_SOURCE_DIR}/tests/data")
endforeach()

# The interchange corpus (scripts/interop-corpus.py): AAF files and bins from
# pyaaf2 and pyavb, downloaded at pinned commits. Without a network the
# download is skipped, and so are the tests that read it (they read the corpus
# only once the fetch has marked it complete, tests/interop/corpus.h).
if(DEFINED ENV{CUTTHULHU_INTEROP_CORPUS})
    set(LUMENIS_INTEROP_CORPUS_DEFAULT "$ENV{CUTTHULHU_INTEROP_CORPUS}")
else()
    # The user's cache folder, as interop-corpus.py's own default: one download for every build folder,
    # and kept when a self-hosted runner's checkout is cleaned (git clean -ffdx removes the build folder,
    # and a download each run fails whenever the network drops; BUILDING.md "Troubleshooting").
    if(WIN32 AND DEFINED ENV{LOCALAPPDATA})
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _cache)
    elseif(APPLE AND DEFINED ENV{HOME})
        set(_cache "$ENV{HOME}/Library/Caches")
    elseif(DEFINED ENV{XDG_CACHE_HOME})
        set(_cache "$ENV{XDG_CACHE_HOME}")
    elseif(DEFINED ENV{HOME})
        set(_cache "$ENV{HOME}/.cache")
    endif()
    if(DEFINED _cache)
        set(LUMENIS_INTEROP_CORPUS_DEFAULT "${_cache}/cutthulhu/interop-corpus")
    else()
        set(LUMENIS_INTEROP_CORPUS_DEFAULT "${CMAKE_BINARY_DIR}/interop-corpus")
    endif()
endif()
set(LUMENIS_INTEROP_CORPUS "${LUMENIS_INTEROP_CORPUS_DEFAULT}" CACHE PATH "Where the interchange corpus is kept")
set_tests_properties(tst_cfb tst_aaf tst_avb tst_edl tst_ale tst_otio tst_fcpxml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CUTTHULHU_INTEROP_CORPUS=${LUMENIS_INTEROP_CORPUS}")

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME interop_corpus_fetch
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py fetch
                --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(interop_corpus_fetch PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS corpus)
    set_tests_properties(tst_cfb tst_aaf tst_avb tst_edl tst_ale tst_otio tst_fcpxml PROPERTIES DEPENDS interop_corpus_fetch)

    # The checks against other implementations are skipped without their Python packages, and
    # ctest says only "Skipped": name the missing ones here, where they can be seen.
    set(_missing "")
    foreach(module olefile aaf2 avb opentimelineio otio_fcp_adapter)
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import ${module}"
            RESULT_VARIABLE _status OUTPUT_QUIET ERROR_QUIET)
        if(NOT _status EQUAL 0)
            string(REPLACE "aaf2" "pyaaf2" _package "${module}")
            string(REPLACE "avb" "pyavb" _package "${_package}")
            string(REPLACE "otio_fcp_adapter" "otio-fcp-adapter" _package "${_package}")
            list(APPEND _missing ${_package})
        endif()
    endforeach()
    if(_missing)
        list(JOIN _missing " " _missing)
        message(STATUS "Interchange checks against other readers will be skipped: ${Python3_EXECUTABLE} "
            "lacks ${_missing}: run scripts/check-python.py (BUILDING.md, \"Python packages for the checks\")")
    endif()

    # What the Writer makes, read by another implementation (olefile).
    # Skipped without olefile.
    add_test(NAME cfb_olefile
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-written
                --test $<TARGET_FILE:tst_cfb>)
    set_tests_properties(cfb_olefile PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

    # Every corpus AAF read by Cutthulhu and by pyaaf2, dumped and compared.
    # Skipped without pyaaf2 (pip install pyaaf2).
    add_test(NAME aaf_pyaaf2
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-aaf
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(aaf_pyaaf2 PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Every corpus bin read by Cutthulhu and by pyavb, dumped and compared
    # byte for byte. Skipped without pyavb (pip install pyavb).
    add_test(NAME avb_pyavb
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-avb
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(avb_pyavb PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The writer's round trip: every corpus bin read and written back
    # (--rewrite-avb) is the same file, byte for byte. Needs only the corpus.
    add_test(NAME avb_round_trip
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-avb-rewrite
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(avb_round_trip PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # What the writer makes, read by pyavb: a bin built from scratch, and the
    # corpus written big-endian (tst_avb forPyavb). Skipped without pyavb or
    # the corpus.
    add_test(NAME avb_write_pyavb
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-avb-write
                --test $<TARGET_FILE:tst_avb> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(avb_write_pyavb PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch)

    # Native bins out as Media Composer bins (3.4b): each corpus bin with a
    # reference read (--import-avb), written (--export-avb) and read again is
    # the same, columns, sources and effects included. Needs only the corpus.
    add_test(NAME avb_export_round_trip
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-avb-export
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(avb_export_round_trip PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The same bins written, read by pyavb: every object as Cutthulhu reads
    # it, and the bin (avb-structure.py) as the one exported. Skipped without
    # pyavb or the corpus.
    add_test(NAME avb_export_pyavb
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-avb-export-pyavb
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(avb_export_pyavb PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Phase 3 gate 1 on the corpus: each AAF imported into a bin is the same
    # as Media Composer's own bin of the material (tests/data/aaf_reference).
    add_test(NAME aaf_import_bins
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-import
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(aaf_import_bins PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Phase 3 gate 1 on the bins themselves: each corpus bin read into a bin
    # (--import-avb) is the same as pyavb's reading of it (the same
    # reference dumps). Needs only the corpus, not pyavb.
    add_test(NAME avb_import_bins
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-avb-import
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(avb_import_bins PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # OpenTimelineIO (3.6b): every corpus timeline exported, imported and
    # exported again is the same; and OpenTimelineIO's own Python package
    # reads each export with the clips, gaps and transitions it finds in the
    # original. The second is skipped without opentimelineio.
    add_test(NAME otio_round_trip
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-otio-round-trip
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    add_test(NAME otio_python
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-otio
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(otio_round_trip otio_python PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # FCP 7 XML (docs/spec/fcp-xml.md): the same two checks with the export written as FCP XML, from
    # the corpus's FCP XML and .otio files; fcpxml_python reads them with OpenTimelineIO's FCP adapter.
    add_test(NAME fcpxml_round_trip
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-fcpxml-round-trip
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    add_test(NAME fcpxml_python
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-fcpxml
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(fcpxml_round_trip fcpxml_python PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 600 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Phase 3 gate 3, Cutthulhu's half: every corpus AAF imported, exported
    # and imported again is the same; pyaaf2, when installed, reads each
    # export as Cutthulhu does.
    add_test(NAME aaf_round_trip
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-round-trip
                --app $<TARGET_FILE:lumenis_app> --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(aaf_round_trip PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 900 DEPENDS interop_corpus_fetch
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

lumenis_isolate_media_drives()
