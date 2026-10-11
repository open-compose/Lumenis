#pragma once

#include "lumenis/core/rational.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lumenis {

// How frame counts are labelled as SMPTE ST 12-1 timecode.
struct TimecodeFormat {
    int nominalFps = 30;    // the integer frame count per labelled second
    bool dropFrame = false; // only valid when nominalFps is a multiple of 30

    // The format a given edit rate uses: 24000/1001 -> 24 NDF,
    // 30000/1001 -> 30 DF (when dropFrame), 25/1 -> 25, ...
    static TimecodeFormat forRate(const Rational& editRate, bool dropFrame);

    bool isValid() const;
    // Frames skipped at the start of each non-tenth minute (2 for 29.97, 4 for 59.94).
    int framesDroppedPerMinute() const;
    std::int64_t framesPerDay() const;

    friend bool operator==(const TimecodeFormat&, const TimecodeFormat&) = default;
};

struct TimecodeFields {
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int frames = 0;
};

// A position expressed as a frame count since 00:00:00:00, with the format
// used to label it. Frame counts outside one day are wrapped for display only.
class Timecode {
public:
    Timecode(std::int64_t frameCount, TimecodeFormat format);

    static Timecode fromFields(const TimecodeFields& fields, TimecodeFormat format);

    // Accepts "HH:MM:SS:FF", "HH:MM:SS;FF", "HH;MM;SS;FF" and "HH.MM.SS.FF".
    // Returns nullopt for malformed text, out-of-range fields, or a frame
    // label that does not exist in drop-frame timecode (e.g. 00:01:00;00).
    static std::optional<Timecode> parse(std::string_view text, TimecodeFormat format);

    std::int64_t frameCount() const { return frames_; }
    const TimecodeFormat& format() const { return format_; }

    TimecodeFields fields() const;
    // Drop-frame timecode uses ';' before the frames field, as Avid displays it.
    std::string toString() const;

private:
    std::int64_t frames_;
    TimecodeFormat format_;
};

} // namespace lumenis
