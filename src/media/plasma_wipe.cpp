// PlasmaWipe effects (plasma_wipe.h; FX Guide ch.16). Our own gradient images are made here; the user's come from
// PlasmaWipes folders. Everything not in the guide (our images, how Soft and Width map onto the gradient, the frame's
// colouring) is ours and docs/spec/plasma-wipe.md says so.

#include "lumenis/media/plasma_wipe.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcessEnvironment>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <numbers>
#include <vector>

namespace lumenis::media {

namespace {

constexpr int kWidth = 720;  // "720x576 pixel image for PAL" (p.445)
constexpr int kHeight = 576;

std::mutex& lock() {
    static std::mutex m;
    return m;
}

QStringList& extraFolders() {
    static QStringList folders;
    return folders;
}

// Our own effects: a few gradients of each kind the guide's categories describe (p.478), named for what they do. The
// Borders folder and the frame's name put "border" and "frame" in its path (p.453).
struct Builtin {
    const char* folder;
    const char* name;
};
constexpr Builtin kBuiltins[] = {
    {"Horiz", "Linear Ramp"}, {"Horiz", "Twirl"},  {"Center", "Radial"},           {"Center", "Diamond"},
    {"Center", "Clock"},      {"Techno", "Clouds"}, {"Techno", "Ripples"},         {"Borders", "Soft Edge Border"},
    {"Borders", "Square Frame"},
};

// Spreads the values over the whole range, "to ensure that the image uses all the available gradient values and that
// the effect plays smoothly" (p.445, p.448): histogram equalization.
void equalize(std::vector<double>& v) {
    std::vector<std::size_t> order(v.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return v[a] < v[b]; });
    std::vector<double> out(v.size());
    const double last = std::max<double>(1.0, static_cast<double>(v.size()) - 1.0);
    for (std::size_t r = 0; r < order.size(); ++r) out[order[r]] = static_cast<double>(r) / last;
    v = std::move(out);
}

// Smooth value noise for Clouds: a few octaves of interpolated random lattices, the same every time.
double lattice(int x, int y, int seed) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + static_cast<std::uint32_t>(seed) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<double>((h ^ (h >> 16)) & 0xffffu) / 65535.0;
}

double valueNoise(double x, double y, int seed) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    const double sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const double a = lattice(x0, y0, seed), b = lattice(x0 + 1, y0, seed), c = lattice(x0, y0 + 1, seed), d = lattice(x0 + 1, y0 + 1, seed);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}

QImage builtinGradient(const QString& name) {
    std::vector<double> v(static_cast<std::size_t>(kWidth) * kHeight);
    const double cx = kWidth / 2.0, cy = kHeight / 2.0;
    const bool frame = name == QLatin1String("Square Frame");
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            // Square pixels in a 4:3 picture (ours): x as y in units of half the height.
            const double dx = (x + 0.5 - cx) / cy, dy = (y + 0.5 - cy) / cy;
            const double r = std::hypot(dx, dy);
            const double angle = std::atan2(dx, -dy); // 0 at the top, clockwise
            double g = 0.0;
            if (name == QLatin1String("Linear Ramp")) g = (x + 0.5) / kWidth;
            else if (name == QLatin1String("Twirl")) g = std::fmod(angle / (2 * std::numbers::pi) + 1.0 + r * 0.75, 1.0) * 0.35 + r * 0.65;
            else if (name == QLatin1String("Radial")) g = r;
            else if (name == QLatin1String("Diamond")) g = std::abs(dx) + std::abs(dy);
            else if (name == QLatin1String("Clock")) g = angle < 0 ? angle + 2 * std::numbers::pi : angle;
            else if (name == QLatin1String("Clouds")) {
                double sum = 0.0, amp = 1.0, freq = 3.0;
                for (int o = 0; o < 5; ++o, amp *= 0.5, freq *= 2.0) sum += amp * valueNoise((x + 0.5) / kWidth * freq * 4.0 / 3.0, (y + 0.5) / kHeight * freq, o + 1);
                g = sum;
            } else if (name == QLatin1String("Ripples")) g = r + 0.06 * std::sin(angle * 8.0) * r;
            else if (name == QLatin1String("Soft Edge Border")) g = std::pow(std::max(std::abs(dx) / (4.0 / 3.0), std::abs(dy)), 4.0);
            else if (frame) {
                // A 4:3 frame (p.454): black inside, a grey bevelled band, white outside.
                const double e = std::max(std::abs(dx) / (4.0 / 3.0), std::abs(dy)); // 0 centre, 1 edge
                g = e < 0.78 ? 0.0 : e < 0.96 ? 0.35 + 0.4 * (e - 0.78) / 0.18 : 1.0;
            }
            v[static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)] = g;
        }
    }
    // A frame keeps "distinct boundaries between the black, white, and gray portions" (p.454); the others use the
    // whole range.
    if (!frame) equalize(v);
    QImage image(kWidth, kHeight, QImage::Format_Grayscale8);
    for (int y = 0; y < kHeight; ++y) {
        uchar* line = image.scanLine(y);
        for (int x = 0; x < kWidth; ++x) line[x] = static_cast<uchar>(std::lround(std::clamp(v[static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)], 0.0, 1.0) * 255.0));
    }
    return image;
}

bool frameDefaults(const QString& path) {
    return path.contains(QLatin1String("border"), Qt::CaseInsensitive) && path.contains(QLatin1String("frame"), Qt::CaseInsensitive);
}

QList<PlasmaWipe> scan() {
    QList<PlasmaWipe> out;
    for (const Builtin& b : kBuiltins) {
        PlasmaWipe w;
        w.category = QStringLiteral("PlasmaWipe Basic ") + QLatin1String(b.folder);
        w.name = QLatin1String(b.name);
        w.frame = frameDefaults(QLatin1String(b.folder) + QLatin1Char('/') + QLatin1String(b.name));
        out << w;
    }
    QStringList roots;
    if (QCoreApplication::instance()) roots << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("SupportingFiles/PlasmaWipes"));
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("LUMENIS_PLASMAWIPES"));
    if (!env.isEmpty()) roots << env.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    roots << extraFolders();
    for (const QString& root : roots) {
        // "There are parallel folders for NTSC (720x486) and PAL (720x576) resolutions" (p.443): the PAL one when there,
        // its higher resolution being the better start (p.445).
        QString base;
        for (const char* res : {"720x576", "720x486"}) {
            const QString d = QDir(root).filePath(QLatin1String(res));
            if (QFileInfo(d).isDir()) {
                base = d;
                break;
            }
        }
        if (base.isEmpty()) continue;
        QStringList files;
        QDirIterator it(base, {QStringLiteral("*.raw"), QStringLiteral("*.RAW")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << it.next();
        files.sort();
        for (const QString& f : files) {
            const QString relative = QDir(base).relativeFilePath(f);
            QStringList dirs = relative.split(QLatin1Char('/'));
            dirs.removeLast();
            if (dirs.isEmpty()) continue; // "Create folders within the 720x486 folder and the 720x576 folder" (p.451)
            PlasmaWipe w;
            // "Media Composer system displays the full path as part of the name in the effect category" (p.451).
            w.category = QStringLiteral("PlasmaWipe ") + dirs.join(QLatin1Char(' '));
            w.name = QFileInfo(f).completeBaseName();
            w.path = f;
            w.frame = frameDefaults(relative);
            const bool taken = std::any_of(out.cbegin(), out.cend(), [&](const PlasmaWipe& o) { return o.category == w.category && o.name == w.name; });
            if (!taken) out << w;
        }
    }
    return out;
}

QList<PlasmaWipe>& wipes() {
    static QList<PlasmaWipe> list = scan();
    return list;
}

QHash<QString, QImage>& gradients() {
    static QHash<QString, QImage> cache;
    return cache;
}

double param(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span, double fallback) {
    return model::numberAt(e, category, name, time, span).value_or(fallback);
}

std::array<float, 3> colourParam(const model::Effect& e, const QString& name, double time, const model::EffectSpan& span, const QColor& fallback) {
    if (const auto rgb = model::colorAt(e, QStringLiteral("Border"), name, time, span)) {
        return {static_cast<float>(rgb->red), static_cast<float>(rgb->green), static_cast<float>(rgb->blue)};
    }
    return {fallback.redF(), fallback.greenF(), fallback.blueF()};
}

float ramp(double v) {
    return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

} // namespace

void setPlasmaWipeFolders(const QStringList& folders) {
    std::lock_guard guard(lock());
    extraFolders() = folders;
    wipes() = scan();
    gradients().clear();
}

const QList<PlasmaWipe>& plasmaWipes() {
    std::lock_guard guard(lock());
    return wipes();
}

const PlasmaWipe* plasmaWipeOf(const model::Effect& effect) {
    if (!effect.category.startsWith(QLatin1String("PlasmaWipe "))) return nullptr;
    for (const PlasmaWipe& w : plasmaWipes()) {
        if (w.category == effect.category && w.name == effect.name) return &w;
    }
    return nullptr;
}

bool isPlasmaWipe(const model::Effect& effect) {
    return plasmaWipeOf(effect) != nullptr;
}

QImage readPlasmaRaw(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray data = f.readAll();
    // "save the image as a raw image file" (p.445): one byte a pixel, rows of 720, no header.
    if (data.isEmpty() || data.size() % kWidth != 0) return {};
    const int height = static_cast<int>(data.size() / kWidth);
    if (height != 576 && height != 486 && height != 540) return {};
    QImage image(kWidth, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) std::copy_n(data.constData() + static_cast<qsizetype>(y) * kWidth, kWidth, reinterpret_cast<char*>(image.scanLine(y)));
    return image;
}

QImage plasmaWipeGradient(const PlasmaWipe& wipe, const QSize& size) {
    if (size.isEmpty()) return {};
    const QString key = wipe.category + QLatin1Char('/') + wipe.name + QLatin1Char('@') + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height());
    {
        std::lock_guard guard(lock());
        if (const auto it = gradients().constFind(key); it != gradients().constEnd()) return *it;
    }
    const QImage source = wipe.path.isEmpty() ? builtinGradient(wipe.name) : readPlasmaRaw(wipe.path);
    if (source.isNull()) return {};
    const QImage scaled = source.scaled(size, Qt::IgnoreAspectRatio, wipe.frame ? Qt::FastTransformation : Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_Grayscale8);
    std::lock_guard guard(lock());
    if (gradients().size() > 64) gradients().clear();
    gradients().insert(key, scaled);
    return scaled;
}

Picture plasmaWipePicture(const model::Effect& effect, const Picture& outgoing, const Picture& incoming, double level, double time,
                          const model::EffectSpan& span) {
    const PlasmaWipe* wipe = plasmaWipeOf(effect);
    const int w = outgoing.width(), h = outgoing.height();
    if (!wipe || w <= 0 || h <= 0 || incoming.width() != w || incoming.height() != h) return incoming;
    const QImage gradient = plasmaWipeGradient(*wipe, QSize(w, h));
    if (gradient.isNull()) return incoming;
    const QString border = QStringLiteral("Border");
    // Ours: Soft (0 to 255, p.493) spreads the edge over up to a quarter of the gradient's range, and Width (0 to 100)
    // makes a band of up to a quarter of it in the Border colour, blended towards the Blend Color across the band.
    const double soft = std::clamp(param(effect, border, QStringLiteral("Soft"), time, span, wipe->frame ? 0.0 : 40.0), 0.0, 255.0) / 255.0 * 0.25;
    const double band = std::clamp(param(effect, border, QStringLiteral("Width"), time, span, 0.0), 0.0, 100.0) / 100.0 * 0.25;
    const auto colour = colourParam(effect, QStringLiteral("Color"), time, span, wipe->frame ? QColor(128, 128, 128) : QColor(Qt::black));
    const auto blend = colourParam(effect, QStringLiteral("Blend Color"), time, span, wipe->frame ? QColor(Qt::white) : QColor(Qt::black));
    const double t = std::clamp(level, 0.0, 1.0);
    Picture out(w, h);
    for (int y = 0; y < h; ++y) {
        const uchar* line = gradient.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const double g = line[x] / 255.0;
            const float* o = outgoing.pixel(x, y);
            const float* in = incoming.pixel(x, y);
            float* px = out.pixel(x, y);
            if (wipe->frame) {
                // A frame effect: "Foreground video displays through the black area and background video displays through
                // the white area" (p.454); the grey frame between "Border adjusts the base color of the frame. Blend Color
                // adjusts the highlight color" (p.453), by its grey (ours). Level is the black's threshold.
                const double edge = std::max(t, 0.002);
                const float aFg = soft > 0 ? ramp((edge - g) / soft + 0.5) : (g < edge ? 1.0f : 0.0f);
                const float aBg = soft > 0 ? ramp((g - 0.98) / soft + 0.5) : (g >= 0.98 ? 1.0f : 0.0f);
                const float k = ramp((g - edge) / std::max(0.98 - edge, 1e-6));
                for (int c = 0; c < 3; ++c) {
                    const float frameColour = colour[static_cast<std::size_t>(c)] + (blend[static_cast<std::size_t>(c)] - colour[static_cast<std::size_t>(c)]) * k;
                    const float behind = o[c] * aBg + frameColour * (1.0f - aBg);
                    px[c] = behind + (in[c] - behind) * aFg;
                }
                px[3] = (o[3] * aBg + (1.0f - aBg)) * (1.0f - aFg) + in[3] * aFg;
                continue;
            }
            // The threshold runs from just below black to just above white, the band and softness included, so that
            // Level 0 shows only the outgoing video and 100 only the incoming.
            const double edge = t * (1.0 + band + 2.0 * soft) - soft;
            auto coverage = [&](double threshold) {
                if (soft <= 0.0) return g < threshold ? 1.0f : 0.0f;
                return ramp((threshold - g) / (2.0 * soft) + 0.5);
            };
            const float aIn = coverage(edge - band);
            const float aBand = band > 0.0 ? coverage(edge) : aIn;
            const float k = band > 0.0 ? ramp((edge - g) / band) : 0.0f;
            for (int c = 0; c < 4; ++c) {
                const float bc = c == 3 ? 1.0f : colour[static_cast<std::size_t>(c)] + (blend[static_cast<std::size_t>(c)] - colour[static_cast<std::size_t>(c)]) * k;
                const float withBand = band > 0.0 ? o[c] + (bc - o[c]) * aBand : o[c];
                px[c] = withBand + (in[c] - withBand) * aIn;
            }
        }
    }
    return out;
}

Picture plasmaWipeSegmentPicture(const model::Effect& effect, const Picture& layer, const Picture& below, double time, const model::EffectSpan& span) {
    const PlasmaWipe* wipe = plasmaWipeOf(effect);
    if (!wipe) return over(layer, below);
    // "segments as static wipes" (p.443): the Foreground Level as the threshold, 50 until set (ours; a frame's 5, its
    // black only).
    const double level = param(effect, QStringLiteral("Foreground"), QStringLiteral("Level"), time, span, wipe->frame ? 5.0 : 50.0) / 100.0;
    return plasmaWipePicture(effect, below, layer, level, time, span);
}

} // namespace lumenis::media
