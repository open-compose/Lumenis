lumenis_add_test(tst_cfb SOURCES tst_cfb.cpp LIBS lumenis::interop)
target_compile_definitions(tst_cfb PRIVATE LUMENIS_TEST_DATA="${PROJECT_SOURCE_DIR}/tests/data")

# The interchange corpus (scripts/interop-corpus.py): AAF files and bins from
# pyaaf2 and pyavb, downloaded at pinned commits. Without a network the
# download is skipped, and so are the tests that read it.
if(DEFINED ENV{CUTTHULHU_INTEROP_CORPUS})
    set(LUMENIS_INTEROP_CORPUS_DEFAULT "$ENV{CUTTHULHU_INTEROP_CORPUS}")
else()
    set(LUMENIS_INTEROP_CORPUS_DEFAULT "${CMAKE_BINARY_DIR}/interop-corpus")
endif()
set(LUMENIS_INTEROP_CORPUS "${LUMENIS_INTEROP_CORPUS_DEFAULT}" CACHE PATH "Where the interchange corpus is kept")
set_tests_properties(tst_cfb PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CUTTHULHU_INTEROP_CORPUS=${LUMENIS_INTEROP_CORPUS}")

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME interop_corpus_fetch
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py fetch
                --dir ${LUMENIS_INTEROP_CORPUS})
    set_tests_properties(interop_corpus_fetch PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS corpus)
    set_tests_properties(tst_cfb PROPERTIES DEPENDS interop_corpus_fetch)

    # What the Writer makes, read by another implementation (olefile).
    # Skipped without olefile.
    add_test(NAME cfb_olefile
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/interop-corpus.py check-written
                --test $<TARGET_FILE:tst_cfb>)
    set_tests_properties(cfb_olefile PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
