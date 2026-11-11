// The Photoshop fuzz target (fuzz_targets.h): media::readPsdData on arbitrary bytes, as multilayer Photoshop files are read
// on import (src/media/psd.h, ROADMAP 9.2e); a file whose layers read is written again with media::psdData and its
// layers must read back the same.
#include "fuzz_targets.h"

#include "lumenis/media/psd.h"

#include <algorithm>

namespace lumenis::fuzz {

int fuzzPsd(const std::uint8_t* data, std::size_t size) {
    const auto file = media::readPsdData(view(data, size));
    if (!file) return 0;
    const QList<media::PsdLayer> layers = media::importableLayers(*file);
    // The writer draws the merged picture at the canvas's size: not for canvases far larger than the input could fill.
    if (layers.isEmpty() || static_cast<qint64>(file->size.width()) * file->size.height() > 4096 * 4096) return 0;
    media::PsdFile again = *file;
    again.layers = layers;
    const auto back = media::readPsdData(media::psdData(again));
    fuzzCheck(back.has_value(), "psd", QStringLiteral("a file written again did not read"));
    if (!back) return 0;
    const QList<media::PsdLayer> read = media::importableLayers(*back);
    fuzzCheck(read.size() == layers.size(), "psd", QStringLiteral("a file written again read with other layers"));
    for (qsizetype i = 0; i < std::min(read.size(), layers.size()); ++i) {
        const media::PsdLayer& a = layers.at(i);
        const media::PsdLayer& b = read.at(i);
        fuzzCheck(a.name == b.name && a.rect == b.rect && a.opacity == b.opacity && a.visible == b.visible &&
                      a.transparency == b.transparency && a.image == b.image,
                  "psd", QStringLiteral("a layer written again did not read back the same"));
    }
    return 0;
}

} // namespace lumenis::fuzz
