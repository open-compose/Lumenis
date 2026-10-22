// libFuzzer's entry points for one target (fuzz/CMakeLists.txt compiles this file once per target,
// with LUMENIS_FUZZ_TARGET naming the function of fuzz_targets.h). Built only by the `fuzz` preset,
// whose Clang links libFuzzer's main() with -fsanitize=fuzzer; see fuzz/README.md for running it.
#include "fuzz_targets.h"

#ifndef LUMENIS_FUZZ_TARGET
#error "LUMENIS_FUZZ_TARGET names the target function"
#endif

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    lumenis::fuzz::quietQt();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    return lumenis::fuzz::LUMENIS_FUZZ_TARGET(data, size);
}
