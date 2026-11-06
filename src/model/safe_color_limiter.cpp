// The Safe Color Limiter effect's parameters and limiting (FX Guide p.545-546, p.727-730). See safe_color_limiter.h.

#include "lumenis/model/safe_color_limiter.h"

#include <algorithm>
#include <cmath>

namespace lumenis::model {

namespace {

const QString kCompositeLuma = QStringLiteral("Composite/Luma Levels Parameters: Enable");
const QString kRgb = QStringLiteral("RGB Levels Parameters: Enable");

constexpr double kBlack = 16.0;
constexpr double kRange = 219.0;

// Rec. 601 luma and the colour difference signals (as safe_colors.cpp's safeLevels) of normalised R'G'B' (0 at reference
// black, 1 at reference white).
double luma601(const std::array<double, 3>& c) {
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
}
double chromaAmplitude(const std::array<double, 3>& c, double y) {
    const double u = 0.492 * (c[2] - y), v = 0.877 * (c[0] - y);
    return std::sqrt(u * u + v * v);
}

// The limits on the normalised scale.
double norm(double level8) {
    return (level8 - kBlack) / kRange;
}

// Each step's tolerance: a hundredth of an 8-bit level, so values on a limit count as within it.
constexpr double kEpsilon = 0.01 / kRange;

// One pass of the three steps. True when nothing had to change.
bool limitOnce(std::array<double, 3>& c, const SafeLimiting& l) {
    bool within = true;
    if (l.compositeLuma) {
        // Luma into its limits, chroma kept: every channel moves by the same amount.
        const double yLow = norm(l.limits.luma.low8()), yHigh = norm(l.limits.luma.high8());
        double y = luma601(c);
        const double target = std::clamp(y, std::min(yLow, yHigh), std::max(yLow, yHigh));
        if (std::abs(target - y) > kEpsilon) {
            for (double& v : c) v += target - y;
            y = target;
            within = false;
        }
        // The composite signal's extremes, y plus and minus the chroma's amplitude, into theirs: luma first when it is
        // itself outside them, then the chroma scaled down towards grey.
        const double cLow = std::min(norm(l.limits.composite.low8()), norm(l.limits.composite.high8()));
        const double cHigh = std::max(norm(l.limits.composite.low8()), norm(l.limits.composite.high8()));
        const double inside = std::clamp(y, cLow, cHigh);
        if (std::abs(inside - y) > kEpsilon) {
            for (double& v : c) v += inside - y;
            y = inside;
            within = false;
        }
        const double amplitude = chromaAmplitude(c, y);
        const double allowed = std::min(cHigh - y, y - cLow);
        if (amplitude > allowed + kEpsilon && amplitude > 0.0) {
            const double k = std::max(allowed, 0.0) / amplitude;
            for (double& v : c) v = y + k * (v - y);
            within = false;
        }
    }
    if (l.rgb) {
        const double low = std::min(norm(l.limits.rgb.low8()), norm(l.limits.rgb.high8()));
        const double high = std::max(norm(l.limits.rgb.low8()), norm(l.limits.rgb.high8()));
        for (double& v : c) {
            const double clamped = std::clamp(v, low, high);
            if (std::abs(clamped - v) > kEpsilon) within = false;
            v = clamped;
        }
    }
    return within;
}

double number(const Effect& e, const QString& name, double t, const EffectSpan& span, double fallback) {
    return numberAt(e, kSafeColorLimiterCategory, name, t, span).value_or(fallback);
}

QString text(const Effect& e, const QString& name, const QString& fallback) {
    const QJsonValue v = e.parameters.value(kSafeColorLimiterCategory).toObject().value(name);
    return v.isString() ? v.toString() : fallback;
}

bool flag(const Effect& e, const QString& name, bool fallback) {
    const QJsonValue v = e.parameters.value(kSafeColorLimiterCategory).toObject().value(name);
    return v.isBool() ? v.toBool() : fallback;
}

} // namespace

bool isSafeColorLimiter(const Effect& effect) {
    return effect.name == QLatin1String("Safe Color Limiter");
}

QString safeUnitMenuText(SafeUnit unit) {
    switch (unit) {
    case SafeUnit::Bit8: return QStringLiteral("8 Bit");
    case SafeUnit::Percent: return QStringLiteral("%");
    case SafeUnit::Ire: return QStringLiteral("IRE");
    case SafeUnit::Millivolts: return QStringLiteral("mVolts");
    }
    return QStringLiteral("8 Bit");
}

SafeUnit safeUnitFromMenu(const QString& text) {
    if (text == QLatin1String("%")) return SafeUnit::Percent;
    if (text == QLatin1String("IRE")) return SafeUnit::Ire;
    if (text == QLatin1String("mVolts")) return SafeUnit::Millivolts;
    return SafeUnit::Bit8;
}

SafeLimiting safeColorLimiting(const Effect& effect, double t, const EffectSpan& span) {
    SafeLimiting out;
    const SafeColorLimits d;
    SafeColorLimits& l = out.limits;
    l.composite = {number(effect, QStringLiteral("Composite L"), t, span, d.composite.low), number(effect, QStringLiteral("Composite H"), t, span, d.composite.high),
                   safeUnitFromMenu(text(effect, QStringLiteral("Composite units"), safeUnitMenuText(d.composite.unit)))};
    l.luma = {number(effect, QStringLiteral("Luma L"), t, span, d.luma.low), number(effect, QStringLiteral("Luma H"), t, span, d.luma.high),
              safeUnitFromMenu(text(effect, QStringLiteral("Luminance units"), safeUnitMenuText(d.luma.unit)))};
    l.rgb = {number(effect, QStringLiteral("RGB Gamut L"), t, span, d.rgb.low), number(effect, QStringLiteral("RGB Gamut H"), t, span, d.rgb.high),
             safeUnitFromMenu(text(effect, QStringLiteral("RGB Gamut units"), safeUnitMenuText(d.rgb.unit)))};
    out.compositeLuma = flag(effect, kCompositeLuma, true);
    out.rgb = flag(effect, kRgb, true);
    out.exact = flag(effect, QStringLiteral("422 Safe"), false);
    out.analysis = text(effect, QStringLiteral("Source Monitor Analysis"), QStringLiteral("Highlight Out of Range Colors")) ==
                   QLatin1String("Highlight Out of Range Colors");
    return out;
}

void seedSafeColorLimiter(Effect& effect, const SafeColorLimits& limits) {
    QJsonObject p = effect.parameters.value(kSafeColorLimiterCategory).toObject();
    p.insert(QStringLiteral("Composite L"), limits.composite.low);
    p.insert(QStringLiteral("Composite H"), limits.composite.high);
    p.insert(QStringLiteral("Composite units"), safeUnitMenuText(limits.composite.unit));
    p.insert(QStringLiteral("Luma L"), limits.luma.low);
    p.insert(QStringLiteral("Luma H"), limits.luma.high);
    p.insert(QStringLiteral("Luminance units"), safeUnitMenuText(limits.luma.unit));
    p.insert(QStringLiteral("RGB Gamut L"), limits.rgb.low);
    p.insert(QStringLiteral("RGB Gamut H"), limits.rgb.high);
    p.insert(QStringLiteral("RGB Gamut units"), safeUnitMenuText(limits.rgb.unit));
    effect.parameters.insert(kSafeColorLimiterCategory, p);
}

std::array<double, 3> limitSafeLevels(const std::array<double, 3>& levels, const SafeLimiting& limiting) {
    std::array<double, 3> c{norm(levels[0]), norm(levels[1]), norm(levels[2])};
    // 422 Safe repeats until every enabled limit holds (a few passes at most; the RGB clamp comes last, so the gamut
    // always holds); off, one pass.
    const int passes = limiting.exact ? 16 : 1;
    for (int i = 0; i < passes; ++i) {
        if (limitOnce(c, limiting)) break;
    }
    return {kBlack + kRange * c[0], kBlack + kRange * c[1], kBlack + kRange * c[2]};
}

} // namespace lumenis::model
