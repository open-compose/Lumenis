// The LUT fuzz target (fuzz_targets.h): media::readLutData on arbitrary bytes, as installed LUT files are read
// (src/media/lut_file.h, ROADMAP 8.5d1), once as a .cube and once as a .3dl; a LUT that reads must map colours to
// finite values.
#include "fuzz_targets.h"

#include "lumenis/media/lut_file.h"

#include <cmath>

namespace lumenis::fuzz {

int fuzzLut(const std::uint8_t* data, std::size_t size) {
    const QByteArray bytes = view(data, size);
    for (const char* suffix : {"cube", "3dl"}) {
        const auto lut = media::readLutData(bytes, QString::fromLatin1(suffix));
        if (!lut) continue;
        for (const std::array<double, 3>& in : {std::array<double, 3>{0.0, 0.0, 0.0}, std::array<double, 3>{0.5, 0.25, 1.0},
                                                std::array<double, 3>{1.0, 1.0, 1.0}}) {
            const auto out = lut->apply(in);
            for (double v : out) fuzzCheck(!std::isinf(v), "lut", QStringLiteral("a colour mapped to infinity"));
        }
    }
    return 0;
}

} // namespace lumenis::fuzz
