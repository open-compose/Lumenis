# End-to-end checks on the real executable.

add_test(NAME app_version COMMAND lumenis_app --version)
set_tests_properties(app_version PROPERTIES
    PASS_REGULAR_EXPRESSION "${PROJECT_VERSION}")

add_test(NAME app_smoke COMMAND lumenis_app --smoke-test)
set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    PASS_REGULAR_EXPRESSION "smoke-test: ok"
    TIMEOUT 60)

# The application discovers a module through <PRODUCT>_MODULE_PATH.
add_test(NAME app_list_providers COMMAND lumenis_app --list-providers)
set_tests_properties(app_list_providers PROPERTIES
    ENVIRONMENT "${PRODUCT_ENV_PREFIX}_MODULE_PATH=${CMAKE_BINARY_DIR}/test-modules/good"
    PASS_REGULAR_EXPRESSION "org\\.lumenis\\.test\\.codec"
    DEPENDS tst_module_loader)

# The Phase 1 gate 5 checklist (docs/checklists/phase1-gate5.md) matches the
# menu bar; scripts/make-checklist.py rewrites it.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME checklist_current
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/make-checklist.py
                --app $<TARGET_FILE:lumenis_app> --check)
    set_tests_properties(checklist_current PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        PASS_REGULAR_EXPRESSION "checklist: current"
        TIMEOUT 60)
endif()

# The CI workflows are ones GitHub accepts (scripts/check-workflows.py); skipped without actionlint.
if(Python3_Interpreter_FOUND)
    add_test(NAME workflows_lint
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/check-workflows.py)
    set_tests_properties(workflows_lint PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

# The structural diff (ROADMAP Phase 3 gate 1): what it finds the same and
# different.
if(Python3_Interpreter_FOUND)
    add_test(NAME structdiff
        COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tests/scripts/test_structdiff.py)
    set_tests_properties(structdiff PROPERTIES TIMEOUT 60)
endif()

# The decode corpus (ROADMAP Phase 2 gate 1): every frame of about 150
# files, read by seeking, matches FFmpeg's own decode. Skipped without the
# ffmpeg command.
if(Python3_Interpreter_FOUND)
    find_program(LUMENIS_FFMPEG_COMMAND ffmpeg HINTS "${FFMPEG_ROOT}/bin" "$ENV{FFMPEG_ROOT}/bin")
    if(LUMENIS_FFMPEG_COMMAND)
        add_test(NAME decode_corpus
            COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/decode-corpus.py
                    --app $<TARGET_FILE:lumenis_app> --ffmpeg ${LUMENIS_FFMPEG_COMMAND})
        set_tests_properties(decode_corpus PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            SKIP_RETURN_CODE 77
            LABELS corpus
            TIMEOUT 3600)
    else()
        message(WARNING "The ffmpeg command was not found: the decode_corpus test is left out. "
                        "Install it (Linux: scripts/bootstrap-linux.sh; BUILDING.md, Phase 2 gates).")
    endif()
endif()

# MXF we write against bmx's mxf2raw (ROADMAP Phase 2 gate 2): every DNx
# resolution with stereo sound. Left out without mxf2raw (BUILDING.md).
if(Python3_Interpreter_FOUND AND LUMENIS_FFMPEG_COMMAND)
    find_program(LUMENIS_MXF2RAW_COMMAND mxf2raw)
    if(LUMENIS_MXF2RAW_COMMAND)
        add_test(NAME mxf_check
            COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/check-mxf.py --make
                    --app $<TARGET_FILE:lumenis_app> --ffmpeg ${LUMENIS_FFMPEG_COMMAND}
                    --mxf2raw ${LUMENIS_MXF2RAW_COMMAND})
        set_tests_properties(mxf_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
    else()
        message(STATUS "bmx's mxf2raw was not found: the mxf_check test is left out (BUILDING.md, Phase 2 gates).")
    endif()
endif()

# FFmpeg is a built-in codec provider (docs/EXTENSIONS.md).
add_test(NAME app_ffmpeg_provider COMMAND lumenis_app --list-providers)
set_tests_properties(app_ffmpeg_provider PROPERTIES
    PASS_REGULAR_EXPRESSION "org\\.ffmpeg\\.libavcodec.*available.*FFmpeg [0-9n]")
