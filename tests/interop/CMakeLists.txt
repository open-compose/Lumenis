lumenis_add_test(tst_cfb SOURCES tst_cfb.cpp LIBS lumenis::interop)
lumenis_add_test(tst_aaf SOURCES tst_aaf.cpp LIBS lumenis::interop)
lumenis_add_test(tst_avb SOURCES tst_avb.cpp LIBS lumenis::interop)
foreach(t tst_cfb tst_aaf tst_avb)
    target_compile_definitions(${t} PRIVATE LUMENIS_TEST_DATA="${PROJECT_SOURCE_DIR}/tests/data")
endforeach()

# The interchange corpus (scripts/interop-corpus.py): AAF files and bins from
# pyaaf2 and pyavb, downloaded at pinned commits. Without a network the
# download is skipped, and so are the tests that read it.
if(DEFINED ENV{CUTTHULHU_INTEROP_CORPUS})
    set(LUMENIS_INTEROP_CORPUS_DEFAULT "$ENV{CUTTHULHU_INTEROP_CORPUS}")
else()
    set(LUMENIS_INTEROP_CORPUS_DEFAULT "${CMAKE_BINARY_DIR}/interop-corpus")
endif()
set(LUMENIS_INTEROP_CORPUS "${LUMENIS_INTEROP_CORPUS_DEFAULT}" CACHE PATH "Where the interchange corpus is kept")
set_tests_properties(tst_cfb tst_aaf tst_avb PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CUTTHULHU_INTEROP_CORPUS=${LUMENIS_INTEROP_CORPUS}")

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME interop_corpus_fetch
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py fetch
                --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(interop_corpus_fetch PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS corpus)
    set_tests_properties(tst_cfb tst_aaf tst_avb PROPERTIES DEPENDS interop_corpus_fetch)

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
