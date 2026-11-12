#pragma once

// Work on whole pictures shared out among the processor's threads (ROADMAP 10.1b; docs/spec/performance.md): the
// compositor's per-pixel passes (conversion, rounding to the effects' precision, blends) and colour correction. Private
// to lumenis_media.

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace lumenis::media::detail {

// Calls `work(from, to)` over [0, count) in bands, one per thread, when there is enough work to be worth the threads
// (`grain` items or more per band), else once here. `work` must only write what its band owns. Returns when every
// band is done.
template <typename Work>
void parallelRange(std::size_t count, Work&& work, std::size_t grain = 1 << 16) {
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t bands = std::min<std::size_t>({std::size_t{hardware}, std::size_t{16}, std::max<std::size_t>(1, count / grain)});
    if (bands <= 1) {
        work(std::size_t{0}, count);
        return;
    }
    const std::size_t band = (count + bands - 1) / bands;
    std::vector<std::thread> threads;
    threads.reserve(bands - 1);
    for (std::size_t n = 1; n < bands; ++n) {
        const std::size_t from = n * band, to = std::min(count, from + band);
        if (from < to) threads.emplace_back([&work, from, to] { work(from, to); });
    }
    work(std::size_t{0}, std::min(count, band)); // the first band on this thread
    for (std::thread& t : threads) t.join();
}

} // namespace lumenis::media::detail
