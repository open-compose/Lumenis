// The monitors' Display Color Space conversion (Editing Guide p.544; 8.5e1). The header says what the spaces are; this
// file works out the primaries matrices from the chromaticities (SMPTE RP 177) with a Bradford white adaptation, and
// converts pictures through lookup tables: 256 entries to decode 8-bit code values, 4096 to encode.
#include "lumenis/media/display_color.h"

#include "lumenis/media/aces.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace lumenis::media {

namespace {

using Mat = std::array<double, 9>;
using Vec = std::array<double, 3>;

Vec mul(const Mat& m, const Vec& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

Mat mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r[static_cast<std::size_t>(i * 3 + j)] = a[static_cast<std::size_t>(i * 3)] * b[static_cast<std::size_t>(j)]
                                                     + a[static_cast<std::size_t>(i * 3 + 1)] * b[static_cast<std::size_t>(3 + j)]
                                                     + a[static_cast<std::size_t>(i * 3 + 2)] * b[static_cast<std::size_t>(6 + j)];
        }
    }
    return r;
}

Mat inverse(const Mat& m) {
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    const double k = std::abs(det) > 1e-12 ? 1.0 / det : 0.0;
    return {(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k,
            (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k,
            (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k};
}

Vec xyzOf(double x, double y) { return {x / y, 1.0, (1.0 - x - y) / y}; }

// RGB to XYZ for primaries and a white point given as xy chromaticities (SMPTE RP 177).
Mat rgbToXyz(const std::array<double, 8>& xy) {
    const Vec r = xyzOf(xy[0], xy[1]), g = xyzOf(xy[2], xy[3]), b = xyzOf(xy[4], xy[5]), w = xyzOf(xy[6], xy[7]);
    const Mat p{r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
    const Vec s = mul(inverse(p), w);
    return {p[0] * s[0], p[1] * s[1], p[2] * s[2], p[3] * s[0], p[4] * s[1], p[5] * s[2], p[6] * s[0], p[7] * s[1], p[8] * s[2]};
}

// The Bradford adaptation from one white to another.
Mat bradford(const Vec& from, const Vec& to) {
    const Mat m{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    const Vec a = mul(m, from), b = mul(m, to);
    const Mat scale{b[0] / a[0], 0, 0, 0, b[1] / a[1], 0, 0, 0, b[2] / a[2]};
    return mul(inverse(m), mul(scale, m));
}

enum class Transfer { Gamma24, Gamma26, Srgb, Pq, Hlg };

struct Space {
    std::array<double, 8> xy; // red, green, blue, white
    Transfer transfer;
};

constexpr std::array<double, 8> kRec709{0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290};
constexpr std::array<double, 8> kRec2020{0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290};
constexpr std::array<double, 8> kDciP3{0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.314, 0.351};

std::optional<Space> displaySpace(const QString& display) {
    // Rec709 (full range) is shown as Rec709: our pictures are full-range RGB already (ours).
    if (display == QLatin1String("Rec709") || display == QLatin1String("Rec709 (full range)")) return Space{kRec709, Transfer::Gamma24};
    if (display == QLatin1String("SRGB")) return Space{kRec709, Transfer::Srgb};
    if (display == QLatin1String("Rec2020")) return Space{kRec2020, Transfer::Gamma24};
    if (display == QLatin1String("DCI P3")) return Space{kDciP3, Transfer::Gamma26};
    return std::nullopt;
}

// The space a project's pictures are in.
std::optional<Space> projectSpace(const QString& project) {
    if (project.contains(QLatin1String("2084"))) return Space{kRec2020, Transfer::Pq};
    if (project.contains(QLatin1String("HLG"))) return Space{kRec2020, Transfer::Hlg};
    const QString display = displaySpaceOfProject(project);
    return display.isEmpty() ? std::nullopt : displaySpace(display);
}

// Code value to display light, 1.0 the display's white.
double decode(Transfer t, double v) {
    v = std::clamp(v, 0.0, 1.0);
    switch (t) {
    case Transfer::Gamma24: return std::pow(v, 2.4);
    case Transfer::Gamma26: return std::pow(v, 2.6);
    case Transfer::Srgb: return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    case Transfer::Pq: {
        // SMPTE ST 2084's EOTF, in cd/m², over the 203 cd/m² reference white.
        const double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0, c1 = 3424.0 / 4096.0,
                     c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
        const double p = std::pow(v, 1.0 / m2);
        const double nits = 10000.0 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
        return nits / 203.0;
    }
    case Transfer::Hlg: {
        // BT.2100's inverse OETF, then the OOTF of a 1000 cd/m² display (system gamma 1.2, on each channel: ours), over
        // the 203 cd/m² reference white.
        const double a = 0.17883277, b = 1.0 - 4.0 * a, c = 0.5 - a * std::log(4.0 * a);
        const double scene = v <= 0.5 ? v * v / 3.0 : (std::exp((v - c) / a) + b) / 12.0;
        return 1000.0 * std::pow(scene, 1.2) / 203.0;
    }
    }
    return v;
}

double encode(Transfer t, double light) {
    light = std::clamp(light, 0.0, 1.0);
    if (t == Transfer::Srgb) return light <= 0.0031308 ? 12.92 * light : 1.055 * std::pow(light, 1.0 / 2.4) - 0.055;
    return t == Transfer::Gamma26 ? std::pow(light, 1.0 / 2.6) : std::pow(light, 1.0 / 2.4);
}

struct Conversion {
    Mat matrix;
    Transfer from;
    Transfer to;
};

std::optional<Conversion> conversion(const QString& project, const QString& display) {
    if (!convertsForDisplay(project, display)) return std::nullopt;
    const auto src = projectSpace(project);
    const auto dst = displaySpace(display);
    if (!src || !dst) return std::nullopt;
    const Vec srcWhite = xyzOf(src->xy[6], src->xy[7]), dstWhite = xyzOf(dst->xy[6], dst->xy[7]);
    const Mat m = mul(inverse(rgbToXyz(dst->xy)), mul(bradford(srcWhite, dstWhite), rgbToXyz(src->xy)));
    return Conversion{m, src->transfer, dst->transfer};
}

bool isAces(const QString& project) { return project.contains(QLatin1String("ACES")); }

// An ACES project's display (p.557-559; 8.5e4b1): the ACEScct working space decoded to linear AP1, then for ACESproxy
// its encoding; for the other displays the output tone curve (aces::outputToneMap, ours), the display's primaries and
// transfer. "Project" in an ACES project is shown as Rec709 (ours, until the Format tab's Output Color Space).
struct AcesDisplay {
    bool proxy = false;
    Mat matrix{1, 0, 0, 0, 1, 0, 0, 0, 1};
    Transfer to = Transfer::Gamma24;
};

std::optional<AcesDisplay> acesDisplay(const QString& display) {
    AcesDisplay d;
    if (display == QLatin1String("ACESproxy")) {
        d.proxy = true;
        return d;
    }
    const auto dst = displaySpace(display == QLatin1String("Project") ? QStringLiteral("Rec709") : display);
    if (!dst) return std::nullopt;
    d.matrix = aces::conversion(aces::ap1(), dst->xy);
    d.to = dst->transfer;
    return d;
}

Vec acesColor(const AcesDisplay& d, const Vec& linearAp1) {
    if (d.proxy) {
        return {aces::linearToAcesproxy(linearAp1[0]), aces::linearToAcesproxy(linearAp1[1]), aces::linearToAcesproxy(linearAp1[2])};
    }
    const Vec light = mul(d.matrix, aces::outputToneMap(linearAp1));
    return {encode(d.to, light[0]), encode(d.to, light[1]), encode(d.to, light[2])};
}

} // namespace

QStringList displayColorSpaces() {
    return {QStringLiteral("Project"), QStringLiteral("Rec709"), QStringLiteral("Rec709 (full range)"), QStringLiteral("SRGB"),
            QStringLiteral("DCI P3"),  QStringLiteral("Rec2020"), QStringLiteral("ACESproxy")};
}

QStringList displayColorSpaceValues() {
    return {QStringLiteral("project"), QStringLiteral("rec709"), QStringLiteral("rec709full"), QStringLiteral("srgb"),
            QStringLiteral("dciP3"),   QStringLiteral("rec2020"), QStringLiteral("acesProxy")};
}

QString displayColorSpaceOfValue(const QString& value) {
    const qsizetype i = displayColorSpaceValues().indexOf(value);
    return displayColorSpaces().value(i < 0 ? 0 : i);
}

QString displaySpaceOfProject(const QString& project) {
    if (project.contains(QLatin1String("ACES"))) return {};
    if (project.contains(QLatin1String("2020"))) return QStringLiteral("Rec2020");
    if (project.contains(QLatin1String("P3"))) return QStringLiteral("DCI P3");
    if (project.contains(QLatin1String("709")) || project.contains(QLatin1String("601"))) return QStringLiteral("Rec709");
    return {};
}

bool convertsForDisplay(const QString& project, const QString& display) {
    // An ACES project's ACEScct pictures are always shown through an output transform.
    if (isAces(project)) return acesDisplay(display).has_value();
    // "Project" (and ACESproxy outside ACES projects) is no display space: nothing to convert. Otherwise the picture is converted
    // when the display's primaries or transfer differ from the project's (a PQ or HLG picture even on a Rec2020 display).
    const auto src = projectSpace(project);
    const auto dst = displaySpace(display);
    if (!src || !dst) return false;
    return src->xy != dst->xy || src->transfer != dst->transfer;
}

std::array<double, 3> toDisplayColor(const std::array<double, 3>& rgb, const QString& project, const QString& display) {
    if (isAces(project)) {
        const auto d = acesDisplay(display);
        if (!d) return rgb;
        return acesColor(*d, {aces::acescctToLinear(rgb[0]), aces::acescctToLinear(rgb[1]), aces::acescctToLinear(rgb[2])});
    }
    const auto c = conversion(project, display);
    if (!c) return rgb;
    const Vec lin = mul(c->matrix, Vec{decode(c->from, rgb[0]), decode(c->from, rgb[1]), decode(c->from, rgb[2])});
    return {encode(c->to, lin[0]), encode(c->to, lin[1]), encode(c->to, lin[2])};
}

QImage toDisplayColorSpace(const QImage& image, const QString& project, const QString& display) {
    if (isAces(project)) {
        const auto d = acesDisplay(display);
        if (!d || image.isNull()) return image;
        std::array<double, 256> lin{};
        for (int i = 0; i < 256; ++i) lin[static_cast<std::size_t>(i)] = aces::acescctToLinear(i / 255.0);
        QImage result = image.convertToFormat(QImage::Format_ARGB32);
        auto byte = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
        for (int y = 0; y < result.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
            for (int x = 0; x < result.width(); ++x) {
                const QRgb p = line[x];
                const Vec c = acesColor(*d, {lin[static_cast<std::size_t>(qRed(p))], lin[static_cast<std::size_t>(qGreen(p))],
                                             lin[static_cast<std::size_t>(qBlue(p))]});
                line[x] = qRgba(byte(c[0]), byte(c[1]), byte(c[2]), qAlpha(p));
            }
        }
        return result;
    }
    const auto c = conversion(project, display);
    if (!c || image.isNull()) return image;
    std::array<double, 256> in{};
    for (int i = 0; i < 256; ++i) in[static_cast<std::size_t>(i)] = decode(c->from, i / 255.0);
    constexpr int kOut = 4096;
    std::vector<std::uint8_t> out(kOut + 1);
    for (int i = 0; i <= kOut; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::lround(encode(c->to, static_cast<double>(i) / kOut) * 255.0));
    }
    auto toCode = [&](double light) {
        const int k = static_cast<int>(std::lround(std::clamp(light, 0.0, 1.0) * kOut));
        return out[static_cast<std::size_t>(k)];
    };
    QImage result = image.convertToFormat(QImage::Format_ARGB32);
    const Mat& m = c->matrix;
    for (int y = 0; y < result.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const QRgb p = line[x];
            const double r = in[static_cast<std::size_t>(qRed(p))], g = in[static_cast<std::size_t>(qGreen(p))],
                         b = in[static_cast<std::size_t>(qBlue(p))];
            line[x] = qRgba(toCode(m[0] * r + m[1] * g + m[2] * b), toCode(m[3] * r + m[4] * g + m[5] * b),
                            toCode(m[6] * r + m[7] * g + m[8] * b), qAlpha(p));
        }
    }
    return result;
}

} // namespace lumenis::media
