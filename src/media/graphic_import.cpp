// Graphics with alpha on import (graphic_import.h; Editing Guide p.217-218, p.1196, p.1231).
#include "lumenis/media/graphic_import.h"

#include "lumenis/media/media.h"
#include "lumenis/media/pict.h"

#include <QFileInfo>

#include <memory>

namespace lumenis::media {

AlphaHandling alphaHandlingOf(const QString& value) {
    if (value == QLatin1String("invert")) return AlphaHandling::Invert;
    if (value == QLatin1String("noInvert")) return AlphaHandling::NoInvert;
    return AlphaHandling::Ignore;
}

bool isPictFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("pic") || suffix == QLatin1String("pct") || suffix == QLatin1String("pict");
}

std::optional<AlphaSource> alphaSourceOf(const QString& path) {
    if (isPictFile(path)) {
        const QImage picture = readPict(path);
        if (picture.isNull() || !picture.hasAlphaChannel()) return std::nullopt;
        const QImage rgba = picture.convertToFormat(QImage::Format_RGBA8888);
        // readPict() gives ARGB32 for every picture, opaque when the file has no alpha: alpha only where some pixel is
        // not opaque.
        bool transparent = false;
        for (int y = 0; y < rgba.height() && !transparent; ++y) {
            const uchar* line = rgba.constScanLine(y);
            for (int x = 0; x < rgba.width() && !transparent; ++x) transparent = line[4 * x + 3] != 255;
        }
        if (!transparent) return std::nullopt;
        AlphaSource s;
        s.frames = 1;
        s.size = rgba.size();
        s.frame = [rgba](std::int64_t) { return rgba; };
        return s;
    }
    const auto info = probe(path);
    if (!info) return std::nullopt;
    const StreamInfo* video = info->firstVideo();
    if (!video || !pixelFormatHasAlpha(video->pixelFormat)) return std::nullopt;
    auto reader = std::make_shared<VideoReader>();
    if (!reader->open(path, video->index)) return std::nullopt;
    AlphaSource s;
    s.frames = std::max<std::int64_t>(reader->frameCount(), 1);
    s.rate = video->frameRate;
    s.size = QSize(video->width, video->height);
    s.frame = [reader](std::int64_t f) { return reader->frame(f); };
    return s;
}

QImage fillPicture(const QImage& source, bool dilate) {
    const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
    QImage out(rgba.size(), QImage::Format_RGB32);
    const int w = rgba.width(), h = rgba.height();
    for (int y = 0; y < h; ++y) {
        const uchar* in = rgba.constScanLine(y);
        auto* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const uchar* p = in + 4 * x;
            o[x] = qRgb(p[0], p[1], p[2]);
            if (!dilate || p[3] != 0) continue;
            // A transparent pixel at the edge takes its visible neighbours' colour.
            int r = 0, g = 0, b = 0, n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const uchar* q = rgba.constScanLine(ny) + 4 * nx;
                    if (q[3] == 0) continue;
                    r += q[0];
                    g += q[1];
                    b += q[2];
                    ++n;
                }
            }
            if (n > 0) o[x] = qRgb(r / n, g / n, b / n);
        }
    }
    return out;
}

QImage mattePicture(const QImage& source, AlphaHandling handling) {
    const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
    QImage out(rgba.size(), QImage::Format_RGB32);
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* in = rgba.constScanLine(y);
        auto* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < rgba.width(); ++x) {
            const int a = in[4 * x + 3];
            const int v = handling == AlphaHandling::NoInvert ? 255 - a : a;
            o[x] = qRgb(v, v, v);
        }
    }
    return out;
}

} // namespace lumenis::media
