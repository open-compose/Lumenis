# Fuzzing the interchange parsers (ROADMAP Phase 3 gate 4; fuzz/README.md, BUILDING.md "Fuzzing").
#
# The targets (target_*.cpp) and the seeds our own writers make (seeds.cpp) form one library. It is
# linked two ways:
# * lumenis_fuzz_replay, a plain executable that runs saved inputs through every target. It builds
#   with any compiler, so ctest fuzz_replay replays the regression inputs (fuzz/regressions), the
#   built-in seeds and the downloaded interchange corpus on every platform.
# * fuzz_<target>, libFuzzer executables, only with LUMENIS_FUZZ (the `fuzz` preset: Clang, with
#   the whole tree compiled with -fsanitize=fuzzer-no-link,address,undefined).
add_library(lumenis_fuzz_targets STATIC
    fuzz_targets.h
    common.cpp
    seeds.cpp
    target_cfb.cpp
    target_aaf.cpp
    target_avb.cpp
    target_avb_rewrite.cpp
    target_otio.cpp
    target_fcpxml.cpp
    target_pict.cpp
    target_scc.cpp
    target_lut.cpp
    target_psd.cpp
    # The PICT, Photoshop and LUT readers are built in here rather than linked from lumenis_media, which would bring FFmpeg.
    ${PROJECT_SOURCE_DIR}/src/media/pict.cpp
    ${PROJECT_SOURCE_DIR}/src/media/psd.cpp
    ${PROJECT_SOURCE_DIR}/src/media/lut_file.cpp
    ${PROJECT_SOURCE_DIR}/src/media/color_transform.cpp
    ${PROJECT_SOURCE_DIR}/src/media/aces.cpp
    # Without OpenColorIO here: CLF files are refused.
    ${PROJECT_SOURCE_DIR}/src/media/ocio_colour.cpp
)
target_include_directories(lumenis_fuzz_targets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(lumenis_fuzz_targets PRIVATE ${PROJECT_SOURCE_DIR}/src/media/include)
target_link_libraries(lumenis_fuzz_targets PUBLIC lumenis::interop lumenis::model Qt6::Core Qt6::Gui)
lumenis_set_warnings(lumenis_fuzz_targets)

add_executable(lumenis_fuzz_replay replay_main.cpp)
target_link_libraries(lumenis_fuzz_replay PRIVATE lumenis_fuzz_targets)
lumenis_set_warnings(lumenis_fuzz_replay)

if(LUMENIS_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "LUMENIS_FUZZ needs Clang (libFuzzer); use the fuzz preset")
    endif()
    # One executable per target; the function each calls is fuzz<Name> in fuzz_targets.h.
    foreach(pair "cfb:fuzzCfb" "aaf:fuzzAaf" "avb:fuzzAvb" "avb_rewrite:fuzzAvbRewrite" "otio:fuzzOtio" "fcpxml:fuzzFcpXml" "pict:fuzzPict" "scc:fuzzScc" "lut:fuzzLut" "psd:fuzzPsd")
        string(REPLACE ":" ";" pair "${pair}")
        list(GET pair 0 name)
        list(GET pair 1 function)
        add_executable(fuzz_${name} libfuzzer_main.cpp)
        target_compile_definitions(fuzz_${name} PRIVATE LUMENIS_FUZZ_TARGET=${function})
        target_link_libraries(fuzz_${name} PRIVATE lumenis_fuzz_targets)
        # libFuzzer's main(); the libraries are already instrumented (fuzzer-no-link).
        target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
        lumenis_set_warnings(fuzz_${name})
    endforeach()
endif()

if(LUMENIS_BUILD_TESTS)
    # Every regression input and built-in seed, and the interchange corpus when it has been downloaded
    # (tests/interop/CMakeLists.txt), through every target. Quick: no mutation, one run each.
    add_test(NAME fuzz_replay
        COMMAND lumenis_fuzz_replay --seeds ${CMAKE_CURRENT_SOURCE_DIR}/regressions ${LUMENIS_INTEROP_CORPUS})
    # DEPENDS on the download only orders the two (it is ignored when Python is missing).
    set_tests_properties(fuzz_replay PROPERTIES TIMEOUT 600 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                         DEPENDS interop_corpus_fetch)
endif()
