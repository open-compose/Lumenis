// Frame-compatible stereoscopic pictures (stereo.h; Editing Guide p.66).
#include "lumenis/media/stereo.h"

#include <QPainter>

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

} // namespace lumenis::media
