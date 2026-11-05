// Safe colour limits and the levels they are checked against (FX Guide ch.23). See lumenis/model/safe_colors.h.

#include "lumenis/model/safe_colors.h"

#include <algorithm>
#include <cmath>

namespace lumenis::model {

namespace {

constexpr double kBlack = 16.0;
constexpr double kRange = 219.0; // reference white 235 minus reference black 16

SafeRange rangeFrom(const QJsonObject& v, const QString& prefix, const SafeRange& fallback) {
    SafeRange r = fallback;
    r.low = v.value(prefix + QStringLiteral("Low")).toDouble(fallback.low);
    r.high = v.value(prefix + QStringLiteral("High")).toDouble(fallback.high);
    if (v.contains(prefix + QStringLiteral("Units"))) r.unit = safeUnitFrom(v.value(prefix + QStringLiteral("Units")).toString());
    return r;
}

void rangeTo(QJsonObject& v, const QString& prefix, const SafeRange& r) {
    v.insert(prefix + QStringLiteral("Low"), r.low);
    v.insert(prefix + QStringLiteral("High"), r.high);
    v.insert(prefix + QStringLiteral("Units"), safeUnitKey(r.unit));
}

} // namespace

QString safeUnitKey(SafeUnit unit) {
    switch (unit) {
    case SafeUnit::Bit8: return QStringLiteral("bit8");
    case SafeUnit::Percent: return QStringLiteral("percent");
    case SafeUnit::Ire: return QStringLiteral("ire");
    case SafeUnit::Millivolts: return QStringLiteral("mvolts");
    }
    return {};
}

SafeUnit safeUnitFrom(const QString& key) {
    if (key == QLatin1String("percent")) return SafeUnit::Percent;
    if (key == QLatin1String("ire")) return SafeUnit::Ire;
    if (key == QLatin1String("mvolts")) return SafeUnit::Millivolts;
    return SafeUnit::Bit8;
}

double toSafeUnit(double level8, SafeUnit unit) {
    switch (unit) {
    case SafeUnit::Bit8: return level8;
    case SafeUnit::Percent:
    case SafeUnit::Ire: return (level8 - kBlack) * 100.0 / kRange;
    case SafeUnit::Millivolts: return (level8 - kBlack) * 700.0 / kRange;
    }
    return level8;
}

double fromSafeUnit(double value, SafeUnit unit) {
    switch (unit) {
    case SafeUnit::Bit8: return value;
    case SafeUnit::Percent:
    case SafeUnit::Ire: return kBlack + value * kRange / 100.0;
    case SafeUnit::Millivolts: return kBlack + value * kRange / 700.0;
    }
    return value;
}

SafeColorLimits SafeColorLimits::fromValues(const QJsonObject& v) {
    SafeColorLimits l;
    l.composite = rangeFrom(v, QStringLiteral("composite"), l.composite);
    l.luma = rangeFrom(v, QStringLiteral("luma"), l.luma);
    l.rgb = rangeFrom(v, QStringLiteral("rgb"), l.rgb);
    l.warnCompositeLuma = v.value(QStringLiteral("compositeLumaAction")).toString() == QLatin1String("warn");
    l.warnRgb = v.value(QStringLiteral("rgbAction")).toString() == QLatin1String("warn");
    return l;
}

QJsonObject SafeColorLimits::toValues() const {
    QJsonObject v;
    rangeTo(v, QStringLiteral("composite"), composite);
    rangeTo(v, QStringLiteral("luma"), luma);
    rangeTo(v, QStringLiteral("rgb"), rgb);
    v.insert(QStringLiteral("compositeLumaAction"), warnCompositeLuma ? QStringLiteral("warn") : QStringLiteral("ignore"));
    v.insert(QStringLiteral("rgbAction"), warnRgb ? QStringLiteral("warn") : QStringLiteral("ignore"));
    return v;
}

bool SafeViolations::any() const {
    if (compositeLow || compositeHigh || lumaLow || lumaHigh) return true;
    for (int c = 0; c < 3; ++c) {
        if (rgbLow[static_cast<std::size_t>(c)] || rgbHigh[static_cast<std::size_t>(c)]) return true;
    }
    return false;
}

void SafeViolations::add(const SafeViolations& o) {
    compositeLow = compositeLow || o.compositeLow;
    compositeHigh = compositeHigh || o.compositeHigh;
    lumaLow = lumaLow || o.lumaLow;
    lumaHigh = lumaHigh || o.lumaHigh;
    for (std::size_t c = 0; c < 3; ++c) {
        rgbLow[c] = rgbLow[c] || o.rgbLow[c];
        rgbHigh[c] = rgbHigh[c] || o.rgbHigh[c];
    }
}

SafeLevels safeLevels(int r, int g, int b) {
    // On the video scale, 0 at reference black and 1 at reference white.
    const double R = (r - kBlack) / kRange, G = (g - kBlack) / kRange, B = (b - kBlack) / kRange;
    const double y = 0.299 * R + 0.587 * G + 0.114 * B;
    const double u = 0.492 * (B - y), v = 0.877 * (R - y);
    const double c = std::sqrt(u * u + v * v);
    return {kBlack + y * kRange, kBlack + (y - c) * kRange, kBlack + (y + c) * kRange};
}

SafeViolations safeViolations(int r, int g, int b, const SafeColorLimits& limits) {
    SafeViolations out;
    const SafeLevels l = safeLevels(r, g, b);
    // Half a level of tolerance, so that limits typed in other units than 8 Bit do not catch their own edges.
    constexpr double e = 0.5;
    out.lumaLow = l.luma < limits.luma.low8() - e;
    out.lumaHigh = l.luma > limits.luma.high8() + e;
    out.compositeLow = l.compositeLow < limits.composite.low8() - e;
    out.compositeHigh = l.compositeHigh > limits.composite.high8() + e;
    const int rgb[3] = {r, g, b};
    for (std::size_t c = 0; c < 3; ++c) {
        out.rgbLow[c] = rgb[c] < limits.rgb.low8() - e;
        out.rgbHigh[c] = rgb[c] > limits.rgb.high8() + e;
    }
    return out;
}

int pictureLevel(int value) {
    return static_cast<int>(std::lround(kBlack + kRange * std::clamp(value, 0, 255) / 255.0));
}

SafeViolations warnedViolations(const SafeViolations& violations, const SafeColorLimits& limits) {
    SafeViolations out;
    if (limits.warnCompositeLuma) {
        out.compositeLow = violations.compositeLow;
        out.compositeHigh = violations.compositeHigh;
        out.lumaLow = violations.lumaLow;
        out.lumaHigh = violations.lumaHigh;
    }
    if (limits.warnRgb) {
        out.rgbLow = violations.rgbLow;
        out.rgbHigh = violations.rgbHigh;
    }
    return out;
}

} // namespace lumenis::model
