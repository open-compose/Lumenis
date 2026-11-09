// Target Mask drawing and mask burn-in (8.5e3); see the header.
#include "lumenis/media/target_mask.h"

#include <QPainterPath>

namespace lumenis::media {

void paintTargetMask(QPainter& painter, const QRectF& picture, const model::MaskMargins& margins, model::TargetMask mask) {
    if (mask == model::TargetMask::None || margins.isEmpty() || picture.isEmpty()) return;
    QPainterPath outside;
    outside.addRect(picture);
    QPainterPath inside;
    inside.addRect(model::maskedArea(picture, margins));
    QColor colour(Qt::black);
    if (mask == model::TargetMask::MixToWhite) colour = QColor(255, 255, 255, 128);
    if (mask == model::TargetMask::MixToBlack) colour = QColor(0, 0, 0, 128);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.fillPath(outside.subtracted(inside), colour);
    painter.restore();
}

QImage burnMaskMargins(const QImage& frame, const model::MaskMargins& margins) {
    if (frame.isNull() || margins.isEmpty()) return frame;
    QImage burned = frame.convertToFormat(QImage::Format_ARGB32);
    QPainter p(&burned);
    paintTargetMask(p, QRectF(QPointF(0, 0), QSizeF(burned.size())), margins, model::TargetMask::BlackMask);
    p.end();
    return burned.convertToFormat(frame.format());
}

} // namespace lumenis::media
