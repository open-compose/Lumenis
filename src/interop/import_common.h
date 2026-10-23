#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/core/timecode.h"

#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

// What the importers into bins share (aaf_import.cpp, avb_import.cpp): the rules that make an AAF
// file and a Media Composer bin of the same material read into the same bin items, so that both are
// compared with one reference (tests/data/aaf_reference, docs/spec/structure.md). The exporters
// (aaf_export.cpp, avb_export.cpp) use the same rules in reverse (timecodeFrames). Internal to
// lumenis_interop: not installed with the public headers.
namespace lumenis::interop::detail {

// Media Composer stores NTSC rates rounded (23976/1000 in AAF, 23.976 in bins): every rate within
// 1/1000 of n×1000/1001 is that exact rate (24000/1001), as its bins show it. Whole rates and
// others are returned as they are.
inline Rational exactNtscRate(const Rational& r) {
    if (r.den() != 1) {
        const std::int64_t whole = (r * Rational(1001, 1000)).round();
        if (whole > 0) {
            const Rational ntsc(whole * 1000, 1001);
            const Rational off = r > ntsc ? r - ntsc : ntsc - r;
            if (off < Rational(1, 1000)) return ntsc;
        }
    }
    return r;
}

// Frames at one rate as frames at another, rounded down.
inline std::int64_t rescale(std::int64_t frames, const Rational& from, const Rational& to) {
    if (from == to || from.num() <= 0 || to.num() <= 0) return frames;
    const Rational r = Rational(frames) * to / from;
    return r.floor();
}

// Frames as timecode text at the rate, wrapped into one day; drop frame only where the rate has it.
// Empty for a rate with no timecode (under one frame a second, from a damaged file): its frames per
// day are 0, and the wrap divided by zero (found by fuzz_aaf, tst_aaf slowEditRate).
inline QString timecodeText(std::int64_t frames, const Rational& rate, bool drop) {
    TimecodeFormat format = TimecodeFormat::forRate(rate, drop);
    if (!format.isValid()) format = TimecodeFormat::forRate(rate, false);
    if (!format.isValid() || format.framesPerDay() <= 0) return {};
    const std::int64_t day = format.framesPerDay();
    return QString::fromStdString(Timecode(((frames % day) + day) % day, format).toString());
}

// A timecode column's text ("01:00:00:00", ";" before the frames for drop frame) as frames at the
// rate's nominal frames per second; nullopt when it is empty or not a timecode. `drop` says whether it
// was drop frame (false where the rate has none).
inline std::optional<std::int64_t> timecodeFrames(const QString& text, const Rational& rate, bool* drop = nullptr) {
    bool isDrop = text.contains(QLatin1Char(';'));
    if (drop) *drop = false;
    if (text.isEmpty()) return std::nullopt;
    const TimecodeFormat format = TimecodeFormat::forRate(rate, isDrop);
    if (!format.isValid()) return std::nullopt;
    isDrop = format.dropFrame; // only where the rate has drop frame
    const auto tc = Timecode::parse(text.toStdString(), format);
    if (!tc) return std::nullopt;
    if (drop) *drop = isDrop;
    return tc->frameCount();
}

// The picture size a clip's Format column gives ("1080i", "720p"): its height, 1080 when there is
// none; 16:9 from 720 lines up, else 720 wide (4:3). For the descriptors the exporters write.
struct PictureSize {
    int width = 1920;
    int height = 1080;
    bool wide = true; // 16:9, else 4:3
};
inline PictureSize pictureSizeOf(const QString& format) {
    PictureSize p;
    int height = 0;
    for (const QChar c : format) {
        if (!c.isDigit()) break;
        height = height * 10 + c.digitValue();
        if (height > 100000) break;
    }
    p.height = height > 0 ? height : 1080;
    p.width = p.height >= 1080 ? p.height * 16 / 9 : p.height >= 720 ? 1280 : 720;
    p.wide = p.height >= 720;
    return p;
}

// a + b, held at the limits of std::int64_t instead of overflowing: timecodes and offsets come from the
// file, and a damaged one can hold any 64-bit value (found by fuzz_aaf: a start near 2^62).
inline std::int64_t saturatingAdd(std::int64_t a, std::int64_t b) {
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
    if (b > 0 && a > kMax - b) return kMax;
    if (b < 0 && a < kMin - b) return kMin;
    return a + b;
}

// A start timecode counted at `fps` frames per second as frames at the nominal frames per second of
// `rate`, rounded toward zero (30 fps 00:00:30;00 is 24 fps 00:00:30:00). Worked out as whole seconds
// and the frames left over, so that no product overflows before the division; a start whose result is
// past std::int64_t is held at its limits.
inline std::int64_t startAtRate(std::int64_t start, int fps, const Rational& rate) {
    const int nominal = TimecodeFormat::forRate(rate, false).nominalFps;
    if (fps <= 0 || nominal <= 0 || fps == nominal) return start;
    const std::int64_t seconds = start / fps;
    const std::int64_t frames = (start % fps) * nominal / fps; // |start % fps| < fps: no overflow
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    if (seconds > kMax / nominal) return kMax;
    if (seconds < -(kMax / nominal)) return std::numeric_limits<std::int64_t>::min();
    return saturatingAdd(seconds * nominal, frames);
}

// The Tracks column: runs of track numbers, video first ("V1 A1-2 A4").
inline QString trackList(QList<int> video, QList<int> audio) {
    QStringList parts;
    auto runs = [&](QChar letter, QList<int>& numbers) {
        std::sort(numbers.begin(), numbers.end());
        numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
        for (qsizetype i = 0; i < numbers.size();) {
            qsizetype j = i;
            while (j + 1 < numbers.size() && numbers[j + 1] == numbers[j] + 1) ++j;
            parts << (j == i ? QStringLiteral("%1%2").arg(letter).arg(numbers[i])
                             : QStringLiteral("%1%2-%3").arg(letter).arg(numbers[i]).arg(numbers[j]));
            i = j + 1;
        }
    };
    runs(QLatin1Char('V'), video);
    runs(QLatin1Char('A'), audio);
    return parts.join(QLatin1Char(' '));
}

// A source's timecode: its start, frames per second and drop frame flag.
struct SourceTimecode {
    std::int64_t start = 0; // frames at fps
    int fps = 0;
    bool drop = false;
};

} // namespace lumenis::interop::detail
