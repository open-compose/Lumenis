# The performance benchmarks (ROADMAP 10.1; docs/spec/performance.md). lumenis_bench is not a Qt Test: it times work
# and prints the times. ctest `perf_benchmarks_run` runs each benchmark once (--quick), so a change that breaks one
# fails the ordinary test run; the timed runs and the comparison with tests/data/perf_baseline.json are
# scripts/perf.py's (ctest -L perf, ctest `perf_compare`, not in the default runs).
add_executable(lumenis_bench bench.cpp)
target_link_libraries(lumenis_bench PRIVATE lumenis::ui Qt6::Widgets)
lumenis_set_warnings(lumenis_bench)

add_test(NAME perf_benchmarks_run COMMAND lumenis_bench --quick)
set_tests_properties(perf_benchmarks_run PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME perf_compare
             COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/scripts/perf.py --bench $<TARGET_FILE:lumenis_bench>)
    set_tests_properties(perf_compare PROPERTIES LABELS perf ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 1800)
endif()

lumenis_isolate_media_drives()
