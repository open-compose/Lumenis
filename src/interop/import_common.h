#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/core/timecode.h"

#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>

// What the importers into bins share (aaf_import.cpp, avb_import.cpp): the rules that make an AAF
// file and a Media Composer bin of the same material read into the same bin items, so that both are
// compared with one reference (tests/data/aaf_reference, docs/spec/structure.md). Internal to
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
inline QString timecodeText(std::int64_t frames, const Rational& rate, bool drop) {
    TimecodeFormat format = TimecodeFormat::forRate(rate, drop);
    if (!format.isValid()) format = TimecodeFormat::forRate(rate, false);
    const std::int64_t day = format.framesPerDay();
    return QString::fromStdString(Timecode(((frames % day) + day) % day, format).toString());
}

// A start timecode counted at `fps` frames per second as frames at the nominal frames per second of
// `rate`, rounded down (30 fps 00:00:30;00 is 24 fps 00:00:30:00).
inline std::int64_t startAtRate(std::int64_t start, int fps, const Rational& rate) {
    const int nominal = TimecodeFormat::forRate(rate, false).nominalFps;
    return fps > 0 && nominal > 0 && fps != nominal ? start * nominal / fps : start;
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
