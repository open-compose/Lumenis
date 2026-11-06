// The SCC fuzz target (fuzz_targets.h): model::parseScc on arbitrary text, as File > Input > Source Browser's import
// reads Scenarist Closed Caption files (ROADMAP 7.4b); a file that reads is laid out as caption data, decoded, written
// again with model::writeScc and must lay out the same data.
#include "fuzz_targets.h"

#include "lumenis/model/closed_captions.h"

namespace lumenis::fuzz {

int fuzzScc(const std::uint8_t* data, std::size_t size) {
    const QString text = QString::fromLatin1(view(data, size));
    const auto file = model::parseScc(text);
    if (!file) return 0;
    // Timecodes run up to a day: keep the data small enough to lay out quickly.
    for (const model::SccLine& line : file->lines) {
        if (line.frame - file->startFrame() > 100000) return 0;
    }
    const QByteArray bytes = model::sccCaptionData(*file, file->startFrame(), Rational(30000, 1001));
    (void)model::decodeCaptions(bytes);
    const TimecodeFormat format{30, file->dropFrame};
    const auto again = model::parseScc(model::writeScc(bytes, file->startFrame(), format));
    fuzzCheck(again.has_value(), "scc", QStringLiteral("a file written again did not read"));
    if (!again) return 0;
    const QByteArray back = model::sccCaptionData(*again, again->startFrame(), Rational(30000, 1001));
    // Null pairs are not written, so the data matches up to its last sent pair.
    const auto spans = model::captionLoadingSpans(bytes);
    const qsizetype used = spans.isEmpty() ? 0 : static_cast<qsizetype>(spans.last().second * 2);
    const qsizetype first = spans.isEmpty() ? 0 : static_cast<qsizetype>(spans.first().first * 2);
    fuzzCheck(back.mid(0, used - first) == bytes.mid(first, used - first), "scc", QStringLiteral("a file written again laid out other data"));
    return 0;
}

} // namespace lumenis::fuzz
