// The Waveform and Vectorscope monitors' pictures (scopes.h; FX Guide p.686-692). Each scope counts the frame's pixels
// into a grid the size of its panel and lights each cell by how many fell in it, in the trace colours the guide names:
// green for Y (p.686), cyan for the YC Waveform's chroma envelope (p.690), white for the Y of YCbCr Parade, red, green
// and blue for the RGB displays, blue/yellow and red/cyan for Cb and Cr (p.691). The graticules and scales follow the
// guide's descriptions; their exact marks, the brightness curve and the vectorscope's size are ours
// (docs/spec/color-correction.md).

#include "lumenis/media/scopes.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lumenis::media {

namespace {

constexpr double kR = 0.2126, kG = 0.7152, kB = 0.0722;

// The frame cut down to at most this many rows (the scopes need no more to show its distribution).
constexpr int kSampleRows = 240;

// The video level (8-bit, 16-235) a full-range 0-1 value stands for.
double video(double v) {
    return 16.0 + 219.0 * v;
}

// The safe colour limits a sample (full-range 0-1) exceeds, read as the video levels it stands for; none without limits.
model::SafeViolations violations(const std::array<double, 3>& c, const model::SafeColorLimits* limits) {
    if (!limits) return {};
    const auto level = [](double v) { return model::pictureLevel(static_cast<int>(std::lround(v * 255.0))); };
    return model::safeViolations(level(c[0]), level(c[1]), level(c[2]), *limits);
}

// The legal and illegal trace colours of p.727's table.
const QColor kLumaLegal(40, 220, 60);      // green
const QColor kIllegal(235, 235, 235);       // white
const QColor kCompositeLegal(30, 170, 190); // cyan
const QColor kCompositeIllegal(230, 210, 40); // yellow

// Counts in a grid of cells, one per pixel of a panel.
class Plot {
public:
    Plot(const QRect& area) : area_(area), cells_(static_cast<std::size_t>(std::max(area.width(), 1) * std::max(area.height(), 1)), 0.0f) {}
    // Adds a hit at fractions (0-1) across and up the panel; outside is clipped to the edge.
    void add(double across, double up, float weight = 1.0f) {
        const int w = area_.width(), h = area_.height();
        if (w <= 0 || h <= 0) return;
        const int x = std::clamp(static_cast<int>(across * (w - 1) + 0.5), 0, w - 1);
        const int y = std::clamp(static_cast<int>((1.0 - up) * (h - 1) + 0.5), 0, h - 1);
        cells_[static_cast<std::size_t>(y * w + x)] += weight;
    }
    // Lights the image's panel: each cell in `color` by 1 - e^(-count / norm), added to what is there.
    void draw(QImage& image, const QColor& color, double norm) const {
        const int w = area_.width(), h = area_.height();
        for (int y = 0; y < h; ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(area_.top() + y));
            for (int x = 0; x < w; ++x) {
                const float c = cells_[static_cast<std::size_t>(y * w + x)];
                if (c <= 0.0f) continue;
                const double a = 1.0 - std::exp(-static_cast<double>(c) / std::max(norm, 0.001));
                QRgb& px = line[area_.left() + x];
                px = qRgb(std::min(255, qRed(px) + static_cast<int>(color.red() * a)), std::min(255, qGreen(px) + static_cast<int>(color.green() * a)),
                          std::min(255, qBlue(px) + static_cast<int>(color.blue() * a)));
            }
        }
    }

private:
    QRect area_;
    std::vector<float> cells_;
};

// The frame as rows of full-range R'G'B' (0-1), at most kSampleRows high and `columns` wide.
struct Samples {
    int width = 0, height = 0;
    std::vector<std::array<double, 3>> rgb;
    const std::array<double, 3>& at(int x, int y) const { return rgb[static_cast<std::size_t>(y * width + x)]; }
};

Samples sample(const QImage& frame, int columns) {
    Samples s;
    if (frame.isNull()) return s;
    const int w = std::clamp(columns, 1, frame.width());
    const int h = std::clamp(frame.height() * w / std::max(frame.width(), 1), 1, kSampleRows);
    const QImage small = frame.scaled(w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    s.width = small.width();
    s.height = small.height();
    s.rgb.resize(static_cast<std::size_t>(s.width * s.height));
    for (int y = 0; y < s.height; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(small.constScanLine(y));
        for (int x = 0; x < s.width; ++x) s.rgb[static_cast<std::size_t>(y * s.width + x)] = {qRed(line[x]) / 255.0, qGreen(line[x]) / 255.0, qBlue(line[x]) / 255.0};
    }
    return s;
}

// How many hits make a cell bright: about the hits a column gets spread over a tenth of the panel.
double columnNorm(const Samples& s, const QRect& area) {
    return std::max(1.0, s.height * s.width / std::max(1.0, static_cast<double>(area.width())) / std::max(1.0, area.height() / 10.0));
}

QColor grid() {
    return {70, 70, 70};
}

QColor scaleText() {
    return {150, 150, 150};
}

// A horizontal graticule line at an 8-bit level with labels at both ends.
void levelLine(QPainter& p, const QRect& area, double up, const QString& left, const QString& right) {
    const double y = area.top() + (1.0 - up) * (area.height() - 1);
    p.setPen(QPen(grid(), 1, Qt::DashLine));
    p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    p.setPen(scaleText());
    if (!left.isEmpty()) p.drawText(QPointF(area.left() + 2, y - 2), left);
    if (!right.isEmpty()) p.drawText(QPointF(area.right() - p.fontMetrics().horizontalAdvance(right) - 2, y - 2), right);
}

// A painter for a panel's graticule and scale, with a font sized to the panel. Made after the traces are lit, which
// writes the image's pixels directly.
void setUp(QPainter& p, const QRect& area) {
    QFont f = p.font();
    f.setPixelSize(std::clamp(area.height() / 18, 8, 12));
    p.setFont(f);
}

double levelUp(double level) {
    // 0 at the bottom, 255 at the top, a margin of 8 levels at both ends.
    return (level + 8.0) / (255.0 + 16.0);
}

void waveformScale(QPainter& p, const QRect& area) {
    // "The scale on the left is a digital level scale using a 256-step (eight-bit) range; 16 is the level for black, and
    // 235 is the level for white. The scale on the right shows ... a percentage" (p.689).
    for (const auto& [level, percent] : {std::pair{16.0, 0}, std::pair{75.0, 25}, std::pair{126.0, 50}, std::pair{180.0, 75}, std::pair{235.0, 100}}) {
        levelLine(p, area, levelUp(level), QString::number(static_cast<int>(level)), QStringLiteral("%1%").arg(percent));
    }
}

// Y Waveform: luma by column, legal luma green and illegal white (p.727).
void yWaveform(QImage& image, const QRect& area, const Samples& s, const model::SafeColorLimits* limits) {
    Plot plot(area), illegal(area);
    for (int y = 0; y < s.height; ++y) {
        for (int x = 0; x < s.width; ++x) {
            const auto& c = s.at(x, y);
            const model::SafeViolations v = violations(c, limits);
            (v.lumaLow || v.lumaHigh ? illegal : plot).add((x + 0.5) / s.width, levelUp(video(kR * c[0] + kG * c[1] + kB * c[2])));
        }
    }
    plot.draw(image, kLumaLegal, columnNorm(s, area));
    illegal.draw(image, kIllegal, columnNorm(s, area));
}

// The HDR waveforms (p.689-690): Y' as a PQ code value up to the code of 1000 or 10000 nits, with a scale in nits.
void hdrWaveform(QImage& image, const QRect& area, const Samples& s, double maxNits) {
    const double top = pqCode(maxNits);
    Plot plot(area);
    for (int y = 0; y < s.height; ++y) {
        for (int x = 0; x < s.width; ++x) {
            const auto& c = s.at(x, y);
            plot.add((x + 0.5) / s.width, (kR * c[0] + kG * c[1] + kB * c[2]) / top);
        }
    }
    plot.draw(image, QColor(40, 220, 60), columnNorm(s, area));
    QPainter p(&image);
    setUp(p, area);
    const double step = maxNits / 10.0;
    for (int i = 0; i <= 10; ++i) {
        const double nits = step * i;
        levelLine(p, area, pqCode(nits) / top, i % 2 == 0 ? QString::number(static_cast<int>(nits)) : QString(), QString());
    }
}

// YC Waveform (p.690): Y in green and the composite Y ± C in cyan, a red edge where the composite leaves its range.
// With limits, illegal luma white and illegal composite excursions yellow (p.727).
void ycWaveform(QImage& image, const QRect& area, const Samples& s, bool pal, const model::SafeColorLimits* limits) {
    Plot luma(area), chroma(area), lumaIllegal(area), chromaIllegal(area);
    bool over = false, under = false;
    for (int y = 0; y < s.height; ++y) {
        for (int x = 0; x < s.width; ++x) {
            const auto& c = s.at(x, y);
            const YCbCr v = videoYCbCr(c[0], c[1], c[2]);
            // The chroma's amplitude on the 8-bit scale (224 levels across Cb or Cr's range).
            const double amplitude = 224.0 * std::hypot(v.cb, v.cr);
            const double across = (x + 0.5) / s.width;
            const model::SafeViolations bad = violations(c, limits);
            (bad.lumaLow || bad.lumaHigh ? lumaIllegal : luma).add(across, levelUp(v.y));
            (bad.compositeHigh ? chromaIllegal : chroma).add(across, levelUp(v.y + amplitude), 0.5f);
            (bad.compositeLow ? chromaIllegal : chroma).add(across, levelUp(v.y - amplitude), 0.5f);
            // Ours: the composite range of -20 to 120 IRE (8-bit -28 to 279).
            over = over || v.y + amplitude > 279.0;
            under = under || v.y - amplitude < -28.0;
        }
    }
    const double norm = columnNorm(s, area);
    chroma.draw(image, kCompositeLegal, norm);
    chromaIllegal.draw(image, kCompositeIllegal, norm);
    luma.draw(image, kLumaLegal, norm);
    lumaIllegal.draw(image, kIllegal, norm);
    QPainter p(&image);
    setUp(p, area);
    if (pal) {
        for (const auto& [level, mv] : {std::pair{16.0, 0}, std::pair{126.0, 350}, std::pair{235.0, 700}}) {
            levelLine(p, area, levelUp(level), QStringLiteral("%1mV").arg(mv), QStringLiteral("%1%").arg(mv * 100 / 700));
        }
    } else {
        // 7.5 IRE black and 100 IRE white: 16 and 235 on the 8-bit scale.
        for (const auto& [level, ire] : {std::pair{16.0, 7.5}, std::pair{126.0, 53.75}, std::pair{235.0, 100.0}}) {
            levelLine(p, area, levelUp(level), QStringLiteral("%1 IRE").arg(ire), QStringLiteral("%1%").arg(static_cast<int>(std::lround((level - 16.0) * 100.0 / 219.0))));
        }
    }
    p.setPen(QPen(QColor(230, 40, 40), 3));
    if (over) p.drawLine(area.topLeft(), area.topRight());
    if (under) p.drawLine(area.bottomLeft(), area.bottomRight());
}

// RGB and YCbCr Parade (p.688, p.691): three waveforms side by side.
// With limits, an RGB Parade channel's values past the RGB Gamut limits white (p.727).
void parade(QImage& image, const QRect& area, const Samples& s, bool ycbcr, const model::SafeColorLimits* limits) {
    const int third = area.width() / 3;
    for (int k = 0; k < 3; ++k) {
        const QRect band(area.left() + k * third, area.top(), third - 2, area.height());
        Plot plot(band), negative(band), illegal(band);
        for (int y = 0; y < s.height; ++y) {
            for (int x = 0; x < s.width; ++x) {
                const auto& c = s.at(x, y);
                const double across = (x + 0.5) / s.width;
                if (!ycbcr) {
                    const auto channel = static_cast<std::size_t>(k);
                    const model::SafeViolations bad = violations(c, limits);
                    (bad.rgbLow.at(channel) || bad.rgbHigh.at(channel) ? illegal : plot).add(across, levelUp(video(c[channel])));
                    continue;
                }
                const YCbCr v = videoYCbCr(c[0], c[1], c[2]);
                if (k == 0) {
                    plot.add(across, levelUp(v.y));
                } else {
                    // Cb and Cr centred: "a flat white line halfway up the two right bands" for black and white.
                    const double d = k == 1 ? v.cb : v.cr;
                    (d >= 0 ? plot : negative).add(across, 0.5 + d);
                }
            }
        }
        const double norm = columnNorm(s, band);
        if (!ycbcr) {
            plot.draw(image, k == 0 ? QColor(230, 50, 50) : k == 1 ? QColor(50, 210, 60) : QColor(70, 110, 240), norm);
            illegal.draw(image, kIllegal, norm);
        } else if (k == 0) {
            plot.draw(image, QColor(230, 230, 230), norm);
        } else {
            // "Positive Cb is represented by blue hues, and negative Cb is represented by yellow hues. Positive Cr is
            // represented by red hues, and negative Cr is represented by cyan hues" (p.691).
            plot.draw(image, k == 1 ? QColor(70, 110, 240) : QColor(230, 50, 50), norm);
            negative.draw(image, k == 1 ? QColor(220, 210, 60) : QColor(40, 200, 210), norm);
        }
    }
    QPainter p(&image);
    setUp(p, area);
    for (int k = 0; k < 3; ++k) {
        const QRect band(area.left() + k * third, area.top(), third - 2, area.height());
        if (ycbcr && k > 0) {
            levelLine(p, band, 0.5, QString(), QString());
        } else {
            levelLine(p, band, levelUp(16), k == 0 ? QStringLiteral("16") : QString(), QString());
            levelLine(p, band, levelUp(235), k == 0 ? QStringLiteral("235") : QString(), QString());
        }
    }
}

// The histograms (p.687-688, p.690-691): one bar each, values across, counts up.
void histograms(QImage& image, const QRect& area, const Samples& s, bool ycbcr) {
    QPainter p(&image);
    setUp(p, area);
    const int bandHeight = area.height() / 3;
    const QColor colors[3] = {ycbcr ? QColor(230, 230, 230) : QColor(230, 50, 50), ycbcr ? QColor(70, 110, 240) : QColor(50, 210, 60),
                              ycbcr ? QColor(230, 50, 50) : QColor(70, 110, 240)};
    for (int k = 0; k < 3; ++k) {
        const QRect band(area.left(), area.top() + k * bandHeight + 2, area.width(), bandHeight - 4);
        std::vector<int> counts(static_cast<std::size_t>(std::max(band.width(), 1)), 0);
        for (const auto& c : s.rgb) {
            double v = 0; // 0-1 across
            if (!ycbcr) {
                v = levelUp(video(c[static_cast<std::size_t>(k)]));
            } else {
                const YCbCr y = videoYCbCr(c[0], c[1], c[2]);
                v = k == 0 ? levelUp(y.y) : 0.5 + (k == 1 ? y.cb : y.cr);
            }
            ++counts[static_cast<std::size_t>(std::clamp(static_cast<int>(v * (band.width() - 1) + 0.5), 0, band.width() - 1))];
        }
        const int peak = std::max(1, *std::max_element(counts.begin(), counts.end()));
        p.setPen(QPen(grid(), 1));
        p.drawRect(band.adjusted(0, 0, -1, -1));
        p.setPen(colors[k]);
        for (int x = 0; x < band.width(); ++x) {
            const int h = counts[static_cast<std::size_t>(x)] * (band.height() - 1) / peak;
            if (h > 0) p.drawLine(band.left() + x, band.bottom(), band.left() + x, band.bottom() - h);
        }
    }
}

// The vectorscope (p.688-689): chroma around a circle, with targets for 75% and 100% colour bars.
void vectorscope(QImage& image, const QRect& area, const Samples& s) {
    Plot plot(area);
    for (const auto& c : s.rgb) {
        const YCbCr v = videoYCbCr(c[0], c[1], c[2]);
        const QPointF at = vectorscopePoint(v.cb, v.cr, area.size());
        plot.add(at.x() / std::max(1, area.width() - 1), 1.0 - at.y() / std::max(1, area.height() - 1));
    }
    plot.draw(image, QColor(40, 220, 60), std::max(1.0, static_cast<double>(s.rgb.size()) / 400.0));
    QPainter p(&image);
    setUp(p, area);
    const QPointF centre = area.topLeft() + vectorscopePoint(0, 0, area.size());
    const double radius = vectorscopePoint(0.5, 0, area.size()).x() - vectorscopePoint(0, 0, area.size()).x();
    p.setPen(QPen(grid(), 1));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(centre, radius, radius);
    p.drawLine(centre - QPointF(radius, 0), centre + QPointF(radius, 0));
    p.drawLine(centre - QPointF(0, radius), centre + QPointF(0, radius));
    // "Small squares mark the location of standard color bar vectors. Inner squares represent the proper values for 75%
    // color bars, and outer squares represent 100% color bars" (p.688-689).
    const std::array<std::array<int, 3>, 6> bars{{{1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {1, 1, 0}}};
    for (double level : {0.75, 1.0}) {
        for (const auto& b : bars) {
            const YCbCr v = videoYCbCr(b[0] * level, b[1] * level, b[2] * level);
            const QPointF at = area.topLeft() + vectorscopePoint(v.cb, v.cr, area.size());
            p.setPen(QPen(scaleText(), 1));
            p.drawRect(QRectF(at - QPointF(3, 3), QSizeF(6, 6)));
        }
    }
}

void draw(QImage& image, const QRect& area, const Samples& s, Scope scope, bool pal, const model::SafeColorLimits* limits) {
    switch (scope) {
    case Scope::QuadDisplay: {
        // "clockwise from the top left corner: YC Waveform, Vectorscope, RGB Histogram, RGB Parade" (p.687).
        const int w = area.width() / 2, h = area.height() / 2;
        draw(image, QRect(area.left(), area.top(), w - 1, h - 1), s, Scope::YcWaveform, pal, limits);
        draw(image, QRect(area.left() + w + 1, area.top(), w - 1, h - 1), s, Scope::Vectorscope, pal, limits);
        draw(image, QRect(area.left() + w + 1, area.top() + h + 1, w - 1, h - 1), s, Scope::RgbHistogram, pal, limits);
        draw(image, QRect(area.left(), area.top() + h + 1, w - 1, h - 1), s, Scope::RgbParade, pal, limits);
        break;
    }
    case Scope::RgbHistogram: histograms(image, area, s, false); break;
    case Scope::YcbcrHistogram: histograms(image, area, s, true); break;
    case Scope::RgbParade: parade(image, area, s, false, limits); break;
    case Scope::YcbcrParade: parade(image, area, s, true, nullptr); break;
    case Scope::Vectorscope: vectorscope(image, area, s); break;
    case Scope::YWaveform: {
        yWaveform(image, area, s, limits);
        QPainter p(&image);
        setUp(p, area);
        waveformScale(p, area);
        break;
    }
    case Scope::YWaveformHdr1000: hdrWaveform(image, area, s, 1000.0); break;
    case Scope::YWaveformHdr10000: hdrWaveform(image, area, s, 10000.0); break;
    case Scope::YcWaveform: ycWaveform(image, area, s, pal, limits); break;
    }
}

} // namespace

QString scopeName(Scope scope) {
    switch (scope) {
    case Scope::QuadDisplay: return QCoreApplication::translate("Scopes", "Quad Display");
    case Scope::RgbHistogram: return QCoreApplication::translate("Scopes", "RGB Histogram");
    case Scope::RgbParade: return QCoreApplication::translate("Scopes", "RGB Parade");
    case Scope::Vectorscope: return QCoreApplication::translate("Scopes", "Vectorscope");
    case Scope::YWaveform: return QCoreApplication::translate("Scopes", "Y Waveform");
    case Scope::YWaveformHdr1000: return QCoreApplication::translate("Scopes", "Y Waveform HDR 1000");
    case Scope::YWaveformHdr10000: return QCoreApplication::translate("Scopes", "Y Waveform HDR 10000");
    case Scope::YcWaveform: return QCoreApplication::translate("Scopes", "YC Waveform");
    case Scope::YcbcrHistogram: return QCoreApplication::translate("Scopes", "YCbCr Histogram");
    case Scope::YcbcrParade: return QCoreApplication::translate("Scopes", "YCbCr Parade");
    }
    return {};
}

YCbCr videoYCbCr(double r, double g, double b) {
    const double y = kR * r + kG * g + kB * b;
    return {video(y), (b - y) / (2.0 * (1.0 - kB)), (r - y) / (2.0 * (1.0 - kR))};
}

double waveformRow(double level, int height) {
    return (1.0 - levelUp(level)) * (height - 1);
}

QPointF vectorscopePoint(double cb, double cr, const QSize& size) {
    const double radius = std::min(size.width(), size.height()) * 0.45;
    const QPointF centre((size.width() - 1) / 2.0, (size.height() - 1) / 2.0);
    return centre + QPointF(cb / 0.5 * radius, -cr / 0.5 * radius);
}

double pqCode(double nits) {
    // ST 2084's inverse EOTF.
    constexpr double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0, c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0,
                     c3 = 2392.0 / 4096.0 * 32.0;
    const double y = std::pow(std::clamp(nits / 10000.0, 0.0, 1.0), m1);
    return std::pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

QImage scopeImage(const QImage& frame, Scope scope, const QSize& size, bool pal, const model::SafeColorLimits* limits) {
    QImage image(size.expandedTo(QSize(1, 1)), QImage::Format_RGB32);
    image.fill(Qt::black);
    const Samples s = sample(frame, std::max(size.width(), 16));
    if (s.rgb.empty()) return image;
    draw(image, image.rect().adjusted(2, 2, -2, -2), s, scope, pal, limits);
    return image;
}

model::SafeViolations pictureViolations(const QImage& frame, const model::SafeColorLimits& limits) {
    model::SafeViolations out;
    if (frame.isNull()) return out;
    const QImage image = frame.convertToFormat(QImage::Format_RGB32);
    // Neighbouring pixels are often the same colour: each new colour is measured once while it lasts.
    QRgb last = 0;
    bool measured = false;
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb px = line[x] & 0xffffff;
            if (measured && px == last) continue;
            last = px;
            measured = true;
            out.add(model::safeViolations(model::pictureLevel(qRed(px)), model::pictureLevel(qGreen(px)), model::pictureLevel(qBlue(px)), limits));
        }
    }
    return out;
}

QImage safeColorAnalysisImage(const QImage& frame, const model::SafeColorLimits& limits, bool compositeLuma, bool rgb) {
    if (frame.isNull()) return {};
    QImage out = frame.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < out.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const QRgb px = line[x];
            const model::SafeViolations v =
                model::safeViolations(model::pictureLevel(qRed(px)), model::pictureLevel(qGreen(px)), model::pictureLevel(qBlue(px)), limits);
            QRgb mark = 0;
            bool marked = true;
            if (compositeLuma && (v.compositeLow || v.compositeHigh)) {
                mark = qRgb(240, 220, 40);
            } else if (compositeLuma && (v.lumaLow || v.lumaHigh)) {
                mark = qRgb(240, 240, 240);
            } else if (rgb && (v.rgbLow[0] || v.rgbHigh[0])) {
                mark = qRgb(230, 40, 40);
            } else if (rgb && (v.rgbLow[1] || v.rgbHigh[1])) {
                mark = qRgb(40, 200, 60);
            } else if (rgb && (v.rgbLow[2] || v.rgbHigh[2])) {
                mark = qRgb(50, 90, 240);
            } else {
                marked = false;
            }
            line[x] = marked ? mark : qRgb(qRed(px) / 3, qGreen(px) / 3, qBlue(px) / 3);
        }
    }
    return out;
}

} // namespace lumenis::media
