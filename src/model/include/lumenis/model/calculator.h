#pragma once

#include "lumenis/core/rational.h"

#include <QList>
#include <QString>

#include <cstdint>

namespace lumenis::model {

// A format of the Avid Calculator's Format menu (guide p.100): timecode at a
// frame rate, or film feet and frames. The guide names no list; ours covers
// the usual video rates and 35mm and 16mm film.
struct CalculatorFormat {
    QString name;
    Rational rate;          // frames per second
    int nominalFps = 0;     // timecode: frames per labelled second
    bool dropFrame = false;
    int framesPerFoot = 0;  // film: 16 (35mm, 4 perf) or 40 (16mm); 0 for timecode
    bool isFilm() const { return framesPerFoot > 0; }
};

const QList<CalculatorFormat>& calculatorFormats();

// The Avid Calculator (p.100): durations typed without colons or leading
// zeros, added and subtracted, and converted between formats. Values are
// frame counts in the current format.
class Calculator {
public:
    Calculator();

    int format() const { return format_; }
    // Converts the total (and anything being typed) to another format:
    // drop- and non-drop-frame timecode at the same rate keep the frame
    // count; other formats keep the duration in time (p.100).
    void setFormat(int index);

    void digit(int d);   // 0-9, typed from the right
    void doubleZero();   // the "00" key
    void backspace();
    void add();
    void subtract();
    void equals();
    void clear();

    // What the display shows: the entry being typed, or the total.
    std::int64_t value() const;
    QString display() const;
    // A frame count as the current format writes it.
    QString text(std::int64_t frames) const;

    // Typed digits as frames: the last two are frames, then seconds,
    // minutes and hours (or feet, for film).
    std::int64_t parse(const QString& digits) const;
    static std::int64_t convert(std::int64_t frames, const CalculatorFormat& from, const CalculatorFormat& to);

private:
    void commit();

    int format_ = 0;
    std::int64_t total_ = 0;
    QString entry_;
    char op_ = 0; // '+', '-' or none
};

} // namespace lumenis::model
