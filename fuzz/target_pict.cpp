// The PICT fuzz target (fuzz_targets.h): media::readPictData on arbitrary bytes, as Export Matte PICT's files are read
// back (src/media/pict.h, ROADMAP 6.9c2); a picture that reads is written again and must read back the same.
#include "fuzz_targets.h"

#include "lumenis/media/pict.h"

namespace lumenis::fuzz {

int fuzzPict(const std::uint8_t* data, std::size_t size) {
    const QImage image = media::readPictData(view(data, size));
    if (image.isNull()) return 0;
    const QImage again = media::readPictData(media::pictData(image));
    fuzzCheck(again == image, "pict", QStringLiteral("a picture written again did not read back the same"));
    return 0;
}

} // namespace lumenis::fuzz
