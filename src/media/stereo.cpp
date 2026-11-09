// Stereoscopic pictures (stereo.h; Editing Guide p.66, p.1162-1163, p.1190-1191, p.1226-1227).
#include "lumenis/media/stereo.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

QImage frameCompatible(const QImage& left, const QImage& right, bool sideBySide, QSize size) {
    if (!size.isValid()) size = !left.isNull() ? left.size() : right.size();
    if (!size.isValid()) return {};
    QImage out(size, QImage::Format_RGB32);
    out.fill(Qt::black);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRect first = sideBySide ? QRect(0, 0, size.width() / 2, size.height()) : QRect(0, 0, size.width(), size.height() / 2);
    const QRect second = sideBySide ? QRect(size.width() / 2, 0, size.width() - size.width() / 2, size.height())
                                    : QRect(0, size.height() / 2, size.width(), size.height() - size.height() / 2);
    // Each eye squeezed to half the width or height, as frame-compatible formats carry them.
    if (!left.isNull()) p.drawImage(first, left);
    if (!right.isNull()) p.drawImage(second, right);
    p.end();
    return out;
}

namespace {

int luma(QRgb c) {
    return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
}

// Both eyes as RGB32 at one size: the left eye's, or the right's when the left is missing; a missing eye is black.
std::pair<QImage, QImage> sameSize(const QImage& left, const QImage& right) {
    const QSize size = !left.isNull() ? left.size() : right.size();
    auto prepared = [&size](const QImage& eye) {
        if (eye.isNull()) {
            QImage black(size, QImage::Format_RGB32);
            black.fill(Qt::black);
            return black;
        }
        const QImage scaled = eye.size() == size ? eye : eye.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return scaled.convertToFormat(QImage::Format_RGB32);
    };
    return {prepared(left), prepared(right)};
}

} // namespace

QImage stereoView(const QImage& left, const QImage& right, const model::S3DView& view, bool sideBySide, model::Eye leading) {
    using K = model::S3DView::Kind;
    if (left.isNull() && right.isNull()) return {};
    switch (view.kind) {
    case K::FollowProject:
    case K::FollowSourceRecord:
    case K::FrameCompatible:
        return frameCompatible(left, right, sideBySide);
    case K::Mono: {
        const bool rightEye = view.mono == model::S3DView::MonoEye::Right ||
                              (view.mono == model::S3DView::MonoEye::Leading && leading == model::Eye::Right);
        return rightEye ? right : left;
    }
    case K::FullFrame: {
        const auto [l, r] = sameSize(left, right);
        QImage out(l.width() * 2, l.height(), QImage::Format_RGB32);
        QPainter p(&out);
        p.drawImage(0, 0, l);
        p.drawImage(l.width(), 0, r);
        p.end();
        return out;
    }
    default:
        break;
    }
    const auto [l, r] = sameSize(left, right);
    QImage out(l.size(), QImage::Format_RGB32);
    const int w = l.width();
    const int h = l.height();
    for (int y = 0; y < h; ++y) {
        const auto* a = reinterpret_cast<const QRgb*>(l.constScanLine(y));
        const auto* b = reinterpret_cast<const QRgb*>(r.constScanLine(y));
        auto* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = a[x];
            const QRgb q = b[x];
            switch (view.kind) {
            case K::Mix:
                o[x] = qRgb((qRed(p) + qRed(q)) / 2, (qGreen(p) + qGreen(q)) / 2, (qBlue(p) + qBlue(q)) / 2);
                break;
            case K::Difference: {
                const int g = std::clamp(128 + (luma(p) - luma(q)) / 2, 0, 255);
                o[x] = qRgb(g, g, g);
                break;
            }
            case K::Anaglyph:
            case K::BwAnaglyph: {
                const bool bw = view.kind == K::BwAnaglyph;
                const int lr = bw ? luma(p) : qRed(p), lg = bw ? luma(p) : qGreen(p);
                const int rr = bw ? luma(q) : qRed(q), rg = bw ? luma(q) : qGreen(q), rb = bw ? luma(q) : qBlue(q);
                if (view.colors == model::S3DView::Colors::GreenMagenta) o[x] = qRgb(rr, lg, rb);
                else if (view.colors == model::S3DView::Colors::AmberBlue) o[x] = qRgb(lr, lg, rb);
                else o[x] = qRgb(lr, rg, rb);
                break;
            }
            case K::Checkerboard: {
                const int n = std::max(1, view.block);
                o[x] = ((x / n + y / n) % 2 == 0) ? p : q;
                break;
            }
            case K::Compare: {
                // Along the top-left to bottom-right diagonal, x/w + y/h runs from 0 to 2.
                const double along = (w > 1 ? double(x) / (w - 1) : 0.0) + (h > 1 ? double(y) / (h - 1) : 0.0);
                o[x] = along < 2.0 * view.percent / 100.0 ? p : q;
                break;
            }
            default:
                o[x] = p;
                break;
            }
        }
    }
    return out;
}

void drawParallaxGuides(QImage& image, model::S3DOverlay overlay, double nearPixels, double farPixels, int rasterWidth) {
    if (image.isNull() || overlay == model::S3DOverlay::Off) return;
    const double scale = rasterWidth > 0 ? double(image.width()) / rasterWidth : 1.0;
    QPainter p(&image);
    auto guides = [&](double parallax, QColor color) {
        const double half = std::max(std::abs(parallax) * scale / 2.0, 0.5);
        const double centre = image.width() / 2.0;
        p.setPen(QPen(color, 1));
        for (const double x : {centre - half, centre + half}) {
            const int at = std::clamp(static_cast<int>(std::lround(x)), 0, image.width() - 1);
            p.drawLine(at, 0, at, image.height() - 1);
        }
    };
    if (overlay == model::S3DOverlay::Near || overlay == model::S3DOverlay::NearFar) guides(nearPixels, QColor(0, 255, 0));
    if (overlay == model::S3DOverlay::Far || overlay == model::S3DOverlay::NearFar) guides(farPixels, QColor(0, 0, 255));
}

} // namespace lumenis::media
