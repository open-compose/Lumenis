#include "lumenis/core/timecode.h"

#include <array>
#include <charconv>
#include <cstdio>

namespace lumenis {

namespace {

std::int64_t floorMod(std::int64_t a, std::int64_t b) {
    const std::int64_t r = a % b;
    return r < 0 ? r + b : r;
}

} // namespace

TimecodeFormat TimecodeFormat::forRate(const Rational& editRate, bool dropFrame) {
    TimecodeFormat format;
    format.nominalFps = static_cast<int>(editRate.round());
    format.dropFrame = dropFrame && (format.nominalFps % 30 == 0);
    return format;
}

bool TimecodeFormat::isValid() const {
    return nominalFps > 0 && (!dropFrame || nominalFps % 30 == 0);
}

int TimecodeFormat::framesDroppedPerMinute() const {
    return dropFrame ? nominalFps / 15 : 0;
}

std::int64_t TimecodeFormat::framesPerDay() const {
    const std::int64_t perMinute = std::int64_t{nominalFps} * 60;
    const std::int64_t dropped = framesDroppedPerMinute();
    // 24 hours = 144 ten-minute blocks; each drops on 9 of its 10 minutes.
    return 144 * (10 * perMinute - 9 * dropped);
}

Timecode::Timecode(std::int64_t frameCount, TimecodeFormat format)
    : frames_(frameCount), format_(format) {}

Timecode Timecode::fromFields(const TimecodeFields& f, TimecodeFormat format) {
    const std::int64_t fps = format.nominalFps;
    const std::int64_t totalMinutes = std::int64_t{f.hours} * 60 + f.minutes;
    std::int64_t frames = (totalMinutes * 60 + f.seconds) * fps + f.frames;
    frames -= format.framesDroppedPerMinute() * (totalMinutes - totalMinutes / 10);
    return {frames, format};
}

TimecodeFields Timecode::fields() const {
    const std::int64_t fps = format_.nominalFps;
    std::int64_t frames = floorMod(frames_, format_.framesPerDay());

    if (const std::int64_t drop = format_.framesDroppedPerMinute()) {
        const std::int64_t perMinute = fps * 60 - drop;
        const std::int64_t perTenMinutes = fps * 600 - 9 * drop;
        const std::int64_t tens = frames / perTenMinutes;
        const std::int64_t rem = frames % perTenMinutes;
        frames += 9 * drop * tens;
        if (rem > drop) {
            frames += drop * ((rem - drop) / perMinute);
        }
    }

    TimecodeFields f;
    f.frames = static_cast<int>(frames % fps);
    f.seconds = static_cast<int>((frames / fps) % 60);
    f.minutes = static_cast<int>((frames / (fps * 60)) % 60);
    f.hours = static_cast<int>(frames / (fps * 3600));
    return f;
}

std::optional<Timecode> Timecode::parse(std::string_view text, TimecodeFormat format) {
    if (!format.isValid()) {
        return std::nullopt;
    }
    std::array<int, 4> values{};
    std::size_t field = 0;
    const char* p = text.data();
    const char* end = p + text.size();
    while (field < values.size()) {
        const auto [next, ec] = std::from_chars(p, end, values[field]);
        if (ec != std::errc{} || next == p || values[field] < 0) {
            return std::nullopt;
        }
        p = next;
        ++field;
        if (field < values.size()) {
            if (p == end || (*p != ':' && *p != ';' && *p != '.')) {
                return std::nullopt;
            }
            ++p;
        }
    }
    if (p != end) {
        return std::nullopt;
    }

    const TimecodeFields f{values[0], values[1], values[2], values[3]};
    if (f.hours > 23 || f.minutes > 59 || f.seconds > 59 || f.frames >= format.nominalFps) {
        return std::nullopt;
    }
    if (format.dropFrame && f.seconds == 0 && f.minutes % 10 != 0
        && f.frames < format.framesDroppedPerMinute()) {
        return std::nullopt;
    }
    return fromFields(f, format);
}

std::string Timecode::toString() const {
    const TimecodeFields f = fields();
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%02d:%02d:%02d%c%02d", f.hours, f.minutes, f.seconds,
                  format_.dropFrame ? ';' : ':', f.frames);
    return buf.data();
}

} // namespace lumenis
