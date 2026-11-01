// The Avid Pan & Zoom effect (ROADMAP 6.7b2; FX Guide p.239-247, p.468, p.528-531; docs/spec/pan-zoom.md): a still
// image, imported by the Effect Editor, seen through a field of view that pans (Position) and zooms (Zoom Factor)
// over it, with a background outside it. Images are kept in a small cache of downsized copies (mip levels) so
// that zooming out stays clean and fast. The keyframe interpolation of Velocity and Path is 6.7b2b; here the
// values come from the usual keyframes.
//
// Geometry (ours; the guide gives the controls, not the arithmetic): at Zoom Factor 1 the whole image is fitted
// into the frame (the largest rectangle of its shape, centred); a larger factor magnifies around the field of
// view's centre. Position X and Y are the centre's offset from the image's centre in percent of its half-width
// and half-height (100: its right or bottom edge). Frames are taken to have square pixels.
#include "lumenis/media/compositor.h"

#include "effect_drawing.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

const QString kCategory = QStringLiteral("Avid Pan & Zoom Effect Parameters");

// One image as read, and its halvings down to a few pixels ("Multi-Resolution ... original image resolution,
// half resolution, quarter resolution, and so on", p.247), premultiplied RGBA.
struct Cached {
    QDateTime modified;
    QList<QImage> levels;
    qint64 used = 0;
};

QMutex& cacheLock() {
    static QMutex m;
    return m;
}

QHash<QString, Cached>& cache() {
    static QHash<QString, Cached> c;
    return c;
}

qint64& clock() {
    static qint64 c = 0;
    return c;
}

// The cached levels of `path`, read on first use (and again when the file changed); empty when it cannot be read.
// `limit` (pixels on the long side, 0 for none) is Use Subres in Preview's smaller copy.
QList<QImage> levelsOf(const QString& path, int limit) {
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isFile()) return {};
    const QString key = path + QLatin1Char('|') + QString::number(limit);
    QMutexLocker lock(&cacheLock());
    auto it = cache().find(key);
    if (it == cache().end() || it->modified != info.lastModified()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QImage image = reader.read();
        if (image.isNull()) return {};
        if (limit > 0 && std::max(image.width(), image.height()) > limit) image = image.scaled(limit, limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        Cached c;
        c.modified = info.lastModified();
        c.levels << image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        while (c.levels.last().width() > 4 && c.levels.last().height() > 4) {
            const QImage& last = c.levels.last();
            c.levels << last.scaled(std::max(last.width() / 2, 1), std::max(last.height() / 2, 1), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        // Ours: eight images at most; the least recently used goes.
        if (cache().size() >= 8) {
            auto oldest = cache().begin();
            for (auto o = cache().begin(); o != cache().end(); ++o) {
                if (o->used < oldest->used) oldest = o;
            }
            cache().erase(oldest);
        }
        it = cache().insert(key, c);
    }
    it->used = ++clock();
    return it->levels;
}

double number(const model::Effect& e, const QString& name, double time, const model::EffectSpan& span) {
    return model::numberAt(e, kCategory, name, time, span).value_or(parameterDefault(e, kCategory, name).toDouble(0.0));
}

QString menu(const model::Effect& e, const QString& name) {
    const QJsonValue v = e.parameters.value(kCategory).toObject().value(name);
    return v.isString() ? v.toString() : parameterDefault(e, kCategory, name).toString();
}

bool flag(const model::Effect& e, const QString& name, bool fallback) {
    const QJsonValue v = e.parameters.value(kCategory).toObject().value(name);
    return v.isBool() ? v.toBool() : fallback;
}

// The weights of a cubic filter at fraction f (Catmull-Rom, a = -0.5; B-spline-like softness with `soft`).
void cubicWeights(double f, bool soft, double w[4]) {
    if (soft) {
        // Uniform cubic B-spline: smooth and soft (Triangle-to-Cubic look, ours).
        const double f2 = f * f, f3 = f2 * f;
        w[0] = (1 - 3 * f + 3 * f2 - f3) / 6.0;
        w[1] = (4 - 6 * f2 + 3 * f3) / 6.0;
        w[2] = (1 + 3 * f + 3 * f2 - 3 * f3) / 6.0;
        w[3] = f3 / 6.0;
        return;
    }
    const double a = -0.5;
    auto k = [&](double x) {
        x = std::abs(x);
        if (x < 1.0) return (a + 2) * x * x * x - (a + 3) * x * x + 1;
        if (x < 2.0) return a * x * x * x - 5 * a * x * x + 8 * a * x - 4 * a;
        return 0.0;
    };
    w[0] = k(1 + f);
    w[1] = k(f);
    w[2] = k(1 - f);
    w[3] = k(2 - f);
}

// The image at (x, y) (pixel centres at whole numbers + 0.5), premultiplied 0 to 1; transparent outside it.
// `kind`: 0 bilinear, 1 soft cubic, 2 sharp cubic.
void sampleImage(const QImage& img, double x, double y, int kind, float out[4]) {
    std::fill(out, out + 4, 0.0f);
    const int W = img.width(), H = img.height();
    if (x < 0.0 || y < 0.0 || x >= W || y >= H) return;
    const double fx = x - 0.5, fy = y - 0.5;
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const double ax = fx - x0, ay = fy - y0;
    auto at = [&](int px, int py, double weight, double acc[4]) {
        px = std::clamp(px, 0, W - 1);
        py = std::clamp(py, 0, H - 1);
        const uchar* p = img.constScanLine(py) + 4 * px;
        for (int c = 0; c < 4; ++c) acc[c] += weight * p[c] / 255.0;
    };
    double acc[4] = {0, 0, 0, 0};
    if (kind == 0) {
        at(x0, y0, (1 - ax) * (1 - ay), acc);
        at(x0 + 1, y0, ax * (1 - ay), acc);
        at(x0, y0 + 1, (1 - ax) * ay, acc);
        at(x0 + 1, y0 + 1, ax * ay, acc);
    } else {
        double wx[4], wy[4];
        cubicWeights(ax, kind == 1, wx);
        cubicWeights(ay, kind == 1, wy);
        for (int j = 0; j < 4; ++j) {
            for (int i = 0; i < 4; ++i) at(x0 - 1 + i, y0 - 1 + j, wx[i] * wy[j], acc);
        }
    }
    const double alpha = std::clamp(acc[3], 0.0, 1.0);
    for (int c = 0; c < 3; ++c) out[c] = static_cast<float>(std::clamp(acc[c], 0.0, alpha));
    out[3] = static_cast<float>(alpha);
}

// The filter a Filtering entry stands for (p.245, p.531). Ours: Real-Time and Triangle bilinear; Quadratic,
// Cubic and Gaussian a soft cubic; B-Spline Catmull and the Avid qualities a sharp (Catmull-Rom) cubic.
int filterOf(const QString& filtering) {
    if (filtering.startsWith(QLatin1String("Real")) || filtering == QLatin1String("Triangle")) return 0;
    if (filtering == QLatin1String("Quadratic") || filtering == QLatin1String("Cubic") || filtering == QLatin1String("Gaussian")) return 1;
    return 2;
}

} // namespace

bool isPanAndZoom(const model::Effect& effect) {
    return effect.category == QLatin1String("Image") && effect.name == QLatin1String("Avid Pan & Zoom");
}

QString panZoomImagePath(const model::Effect& effect) {
    return effect.parameters.value(kCategory).toObject().value(QStringLiteral("Image")).toString();
}

QSize panZoomImageSize(const QString& path) {
    const QList<QImage> levels = levelsOf(path, 0);
    return levels.isEmpty() ? QSize() : levels.first().size();
}

void freePanZoomCache(const QString& path) {
    QMutexLocker lock(&cacheLock());
    if (path.isEmpty()) {
        cache().clear();
        return;
    }
    for (auto it = cache().begin(); it != cache().end();) {
        if (it.key().startsWith(path + QLatin1Char('|'))) it = cache().erase(it);
        else ++it;
    }
}

std::optional<PanZoomView> panZoomView(const model::Effect& e, double time, const model::EffectSpan& span, const QSizeF& size) {
    if (!isPanAndZoom(e) || size.width() <= 0.0 || size.height() <= 0.0) return std::nullopt;
    const QSize image = panZoomImageSize(panZoomImagePath(e));
    if (image.isEmpty()) return std::nullopt;
    const double W = size.width(), H = size.height();
    PanZoomView v;
    v.imageSize = image;
    const double ratio = static_cast<double>(image.width()) / image.height();
    if (ratio >= W / H) v.fitted = QRectF(0.0, (H - W / ratio) / 2.0, W, W / ratio);
    else v.fitted = QRectF((W - H * ratio) / 2.0, 0.0, H * ratio, H);
    // "Zoom Factor ranges from 0.1 to 20" (p.242).
    v.zoom = std::clamp(number(e, QStringLiteral("Zoom Factor"), time, span), 0.1, 20.0);
    const QPointF centre = v.fitted.center() + QPointF(number(e, QStringLiteral("X"), time, span) / 100.0 * v.fitted.width() / 2.0,
                                                      number(e, QStringLiteral("Y"), time, span) / 100.0 * v.fitted.height() / 2.0);
    v.field = QRectF(centre.x() - W / v.zoom / 2.0, centre.y() - H / v.zoom / 2.0, W / v.zoom, H / v.zoom);
    v.sourceDisplay = menu(e, QStringLiteral("Display")) == QLatin1String("Source");
    v.safeAction = flag(e, QStringLiteral("Show Safe Action"), false);
    return v;
}

Picture panZoomPicture(const model::Effect& e, const Picture& segment, double time, const model::EffectSpan& span, bool preview) {
    const int width = segment.width(), height = segment.height();
    if (width <= 0 || height <= 0) return segment;
    const QSizeF size(width, height);
    auto view = panZoomView(e, time, span, size);
    // No image yet (or it is missing): the placeholder segment as it is.
    if (!view) return segment;
    // Display Source in the Effect Preview: "the entire image scaled to fit inside the Effect Preview monitor"
    // (p.241); otherwise the field of view fills the frame.
    const bool whole = preview && view->sourceDisplay;
    if (whole) {
        view->zoom = 1.0;
        view->field = QRectF(0.0, 0.0, width, height);
    }
    // "Video — Uses the video from the segment upon which you placed the Avid Pan & Zoom effect" (p.531).
    Picture out = menu(e, QStringLiteral("Background")) == QLatin1String("Video")
                      ? segment
                      : Picture::solid(width, height, [&] {
                            const auto rgb = model::colorAt(e, kCategory, QStringLiteral("Color controls"), time, span);
                            return rgb ? QColor::fromRgbF(static_cast<float>(rgb->red), static_cast<float>(rgb->green), static_cast<float>(rgb->blue))
                                       : QColor(Qt::black);
                        }());
    // Use Subres in Preview (p.529): ours, at most twice the frame's size on the long side.
    const int limit = flag(e, QStringLiteral("Use Subres in Preview"), false) ? 2 * std::max(width, height) : 0;
    const QList<QImage> levels = levelsOf(panZoomImagePath(e), limit);
    if (levels.isEmpty()) return segment;
    // Output pixels per image pixel, and the level that keeps at least one image pixel per output pixel.
    const double scale = view->fitted.width() / levels.first().width() * view->zoom;
    int level = 0;
    while (level + 1 < levels.size() && scale * std::pow(2.0, level + 1) <= 1.0 + 1e-9) ++level;
    const QImage& img = levels.at(level);
    const double sx = static_cast<double>(img.width()) / view->fitted.width();
    const double sy = static_cast<double>(img.height()) / view->fitted.height();
    const int kind = filterOf(menu(e, QStringLiteral("Filtering")));
    // "Source has 601 Levels ... the effect makes no adjustment" to video levels; ours works in full-range RGB,
    // so 601 images are stretched from 16-235 to the full range (and RGB images are left as they are).
    const bool video601 = menu(e, QStringLiteral("Source Levels")) == QLatin1String("601");
    const QPointF frameCentre(width / 2.0, height / 2.0);
    float in[4];
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // The point of the fitted image this output pixel shows.
            const QPointF q = view->field.center() + (QPointF(x + 0.5, y + 0.5) - frameCentre) / view->zoom;
            sampleImage(img, (q.x() - view->fitted.left()) * sx, (q.y() - view->fitted.top()) * sy, kind, in);
            if (in[3] <= 0.0f) continue;
            if (video601) {
                for (int c = 0; c < 3; ++c) {
                    in[c] = std::clamp((in[c] - 16.0f / 255.0f * in[3]) * (255.0f / 219.0f), 0.0f, in[3]);
                }
            }
            float* o = out.pixel(x, y);
            for (int c = 0; c < 4; ++c) o[c] = in[c] + o[c] * (1.0f - in[3]);
        }
    }
    return out;
}

} // namespace lumenis::media
