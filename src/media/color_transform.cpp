// The Color Encoding tab's colour transformations (Editing Guide p.539-540; 8.5c, docs/spec/color-adapters.md). Each is
// built from the published curves the guide names: Rec. 709's OETF (ITU-R BT.709), sRGB's (IEC 61966-2.1), Kodak's
// Cineon printing density, ARRI's Log-C (v3, EI 800) with ALEXA Wide Gamut to Rec. 709 primaries, and Canon's C-Log. A
// chain of curves is looked up from tables built from the 8-bit input; after a matrix the steps are computed per pixel.
#include "lumenis/media/color_transform.h"

#include <QHash>
#include <QMutex>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

double fullToVideo(double v) { return (16.0 + 219.0 * v) / 255.0; }
double videoToFull(double v) { return (255.0 * v - 16.0) / 219.0; }
double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

// ITU-R BT.709's transfer function: linear light to the 709 signal ("applies a 2.2 gamma correction (Rec.709)", p.539).
double oetf709(double l) {
    l = clamp01(l);
    return l < 0.018 ? 4.5 * l : 1.099 * std::pow(l, 0.45) - 0.099;
}

double srgbToLinear(double v) {
    v = clamp01(v);
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

// Cineon printing density (10-bit code values, black 95, white 685, 0.002 density per code, 0.6 film gamma), normalised
// so that 95 is 0 and 685 is 1; "Automatically clamps the high white values" (p.540).
double cineonToLinear(double v) {
    const double black = std::pow(10.0, (95.0 - 685.0) * 0.002 / 0.6);
    const double lin = (std::pow(10.0, (v * 1023.0 - 685.0) * 0.002 / 0.6) - black) / (1.0 - black);
    return clamp01(lin);
}

// ARRI Log-C v3 at EI 800 to scene linear.
double logCToLinear(double t) {
    const double cut = 0.010591, a = 5.555556, b = 0.052272, c = 0.247190, d = 0.385537, e = 5.367655, f = 0.092809;
    return t > e * cut + f ? (std::pow(10.0, (t - d) / c) - b) / a : (t - f) / e;
}

// Canon C-Log to scene linear.
double cLogToLinear(double v) {
    const double k = 0.0730597;
    return v >= k ? (std::pow(10.0, (v - k) / 0.529136) - 1.0) / 10.1596 : -(std::pow(10.0, (k - v) / 0.529136) - 1.0) / 10.1596;
}

ColorStep curve(std::function<double(double)> f) {
    ColorStep s;
    s.curve = std::move(f);
    return s;
}

ColorStep matrix(std::array<double, 9> m) {
    ColorStep s;
    s.matrix = m;
    s.isMatrix = true;
    return s;
}

struct Registry {
    QStringList names;
    QHash<QString, ColorTransformation> steps;
};

Registry& registry() {
    static Registry r = [] {
        Registry g;
        auto add = [&](const QString& name, ColorTransformation t) {
            g.names << name;
            g.steps.insert(name, std::move(t));
        };
        add(QStringLiteral("Levels scaling (full range to video levels)"), {curve(fullToVideo)});
        add(QStringLiteral("Levels scaling (video levels to full range)"), {curve([](double v) { return clamp01(videoToFull(v)); })});
        add(QStringLiteral("Gamma 2.6 to Linear"), {curve([](double v) { return std::pow(clamp01(v), 2.6); })});
        add(QStringLiteral("Linear full range to REC709"), {curve([](double v) { return fullToVideo(oetf709(v)); })});
        // "while keeping the same black and white points": video levels in and out.
        add(QStringLiteral("Linear video levels to REC709"),
            {curve([](double v) { return fullToVideo(oetf709(clamp01(videoToFull(v)))); })});
        add(QStringLiteral("Printing Density (Cineon) to linear"), {curve(cineonToLinear)});
        add(QStringLiteral("SRGB (IEC 61966-2.1) to linear"), {curve(srgbToLinear)});
        // ALEXA Wide Gamut to Rec. 709 primaries (ARRI's published matrix), then the 709 signal. Ours: scene-referred,
        // without a display tone curve.
        add(QStringLiteral("Arri ALEXA Log-C SUP 3.x to ITU 709"),
            {curve(logCToLinear),
             matrix({1.617523, -0.537287, -0.080237, -0.070573, 1.334613, -0.264040, -0.021102, -0.226954, 1.248056}),
             curve(oetf709)});
        add(QStringLiteral("Canon C-Log to REC709"), {curve([](double v) { return oetf709(cLogToLinear(v)); })});
        return g;
    }();
    return r;
}

QMutex& registryMutex() {
    static QMutex m;
    return m;
}

QList<ColorStep> stepsOf(const QStringList& names) {
    QMutexLocker lock(&registryMutex());
    QList<ColorStep> steps;
    for (const QString& n : names) {
        const auto it = registry().steps.constFind(n);
        if (it != registry().steps.constEnd()) steps += *it;
    }
    return steps;
}

void run(std::array<double, 3>& c, const QList<ColorStep>& steps, qsizetype from) {
    for (qsizetype i = from; i < steps.size(); ++i) {
        const ColorStep& s = steps.at(i);
        if (s.isMatrix) {
            const auto& m = s.matrix;
            c = {m[0] * c[0] + m[1] * c[1] + m[2] * c[2], m[3] * c[0] + m[4] * c[1] + m[5] * c[2],
                 m[6] * c[0] + m[7] * c[1] + m[8] * c[2]};
        } else if (s.curve) {
            for (double& v : c) v = s.curve(v);
        }
    }
}

} // namespace

QStringList colorTransformationNames() {
    QMutexLocker lock(&registryMutex());
    return registry().names;
}

void registerColorTransformation(const QString& name, const ColorTransformation& steps) {
    QMutexLocker lock(&registryMutex());
    if (!registry().steps.contains(name)) registry().names << name;
    registry().steps.insert(name, steps);
}

bool hasColorTransformation(const QString& name) {
    QMutexLocker lock(&registryMutex());
    return registry().steps.contains(name);
}

std::array<double, 3> transformColor(std::array<double, 3> rgb, const QStringList& names) {
    run(rgb, stepsOf(names), 0);
    return rgb;
}

QImage applyColorTransformations(const QImage& image, const QStringList& names) {
    const QList<ColorStep> steps = stepsOf(names);
    if (steps.isEmpty() || image.isNull()) return image;
    // The curves before the first matrix, from the 8-bit input: one table per value.
    qsizetype firstMatrix = 0;
    while (firstMatrix < steps.size() && !steps.at(firstMatrix).isMatrix) ++firstMatrix;
    std::array<double, 256> table{};
    for (int i = 0; i < 256; ++i) {
        double v = i / 255.0;
        for (qsizetype k = 0; k < firstMatrix; ++k) {
            if (steps.at(k).curve) v = steps.at(k).curve(v);
        }
        table[static_cast<std::size_t>(i)] = v;
    }
    const bool perPixel = firstMatrix < steps.size();
    auto byte = [](double v) { return static_cast<int>(std::lround(clamp01(v) * 255.0)); };
    std::array<int, 256> bytes{};
    for (std::size_t i = 0; i < 256; ++i) bytes[i] = byte(table[i]);
    QImage out = image.convertToFormat(image.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    for (int y = 0; y < out.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb p = line[x];
            const auto r = static_cast<std::size_t>(qRed(p));
            const auto g = static_cast<std::size_t>(qGreen(p));
            const auto b = static_cast<std::size_t>(qBlue(p));
            if (!perPixel) {
                line[x] = qRgba(bytes[r], bytes[g], bytes[b], qAlpha(p));
                continue;
            }
            std::array<double, 3> c{table[r], table[g], table[b]};
            run(c, steps, firstMatrix);
            line[x] = qRgba(byte(c[0]), byte(c[1]), byte(c[2]), qAlpha(p));
        }
    }
    return out;
}

} // namespace lumenis::media
