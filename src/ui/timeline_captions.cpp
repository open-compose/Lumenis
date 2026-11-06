// Closed captions in the Timeline's D1 row (Phase 7.4b; docs/spec/closed-captions.md; Editing Guide p.983-984): "The
// D-track provides visual information on when the caption is loading, displaying and switching to the next caption."
// The data comes from model::sequenceCaptionData over the clips MainWindow resolves (Timeline::setCaptionClipData).
#include "lumenis/model/closed_captions.h"
#include "lumenis/ui/timeline.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace lumenis::ui {

const Timeline::CaptionTrack& Timeline::captionTrack() const {
    const QByteArray now = captionClipData_ ? model::sequenceCaptionData(shown_, captionClipData_) : QByteArray();
    if (now != captionCache_.data || (now.isEmpty() && !captionCache_.shown.isEmpty())) {
        captionCache_.data = now;
        captionCache_.shown = model::decodeCaptions(now);
        captionCache_.loading = model::captionLoadingSpans(now);
    }
    // Cuts move without the data changing (filler, trims of null frames), so these are found every time.
    captionCache_.broken = captionClipData_ ? model::brokenCaptionCuts(shown_, captionClipData_) : QList<std::int64_t>{};
    return captionCache_;
}

void TimelineView::paintCaptions(QPainter& p, const Row& r) const {
    if (r.track != QLatin1String("D1")) return;
    const Timeline::CaptionTrack& cc = timeline_.captionTrack();
    if (cc.data.isEmpty()) return;
    const double left = headerWidth();
    const double right = width();
    const bool tall = r.height >= 30;
    // The phases' line: "The caption loading phase (black solid line) ... The caption playing phase (gray line)
    // contains mostly filler" (p.984). With the row too short for text, the line runs through its middle.
    const double lineY = tall ? r.top + r.height * 0.45 : r.top + r.height / 2.0;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const model::ShownCaption& c : cc.shown) {
        const double x0 = std::max(xFor(c.start), left);
        const double x1 = std::min(xFor(c.end), right);
        if (x1 <= x0) continue;
        p.setPen(QPen(QColor(0x9a, 0x9a, 0x9a), 1));
        p.drawLine(QPointF(x0, lineY), QPointF(x1, lineY));
        if (tall && x1 - x0 > 16) {
            // The caption's text above its line, as far as it fits (palette text, as custom-painted text is).
            QFont f = p.font();
            f.setPointSizeF(std::max(6.0, f.pointSizeF() - 2));
            p.setFont(f);
            p.setPen(palette().color(QPalette::WindowText));
            const QRectF box(x0 + 3, r.top + 1, x1 - x0 - 6, lineY - r.top - 2);
            const QString text = c.screen.text().replace(QLatin1Char('\n'), QLatin1Char(' '));
            p.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(text, Qt::ElideRight, static_cast<int>(box.width())));
        }
    }
    for (const auto& [start, end] : cc.loading) {
        const double x0 = xFor(start);
        const double x1 = std::max(xFor(end), x0 + 2);
        if (x1 < left || x0 > right) continue;
        p.setPen(QPen(Qt::black, 2));
        p.drawLine(QPointF(std::max(x0, left), lineY), QPointF(std::min(x1, right), lineY));
        // The bracket's ends.
        p.setPen(QPen(Qt::black, 1));
        if (x0 >= left) p.drawLine(QPointF(x0, lineY - 4), QPointF(x0, lineY + 4));
        if (x1 <= right) p.drawLine(QPointF(x1, lineY - 4), QPointF(x1, lineY + 4));
    }
    // "If you do break a caption, an orange triangle icon appears in the Timeline" (p.984).
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const std::int64_t cut : cc.broken) {
        const double x = xFor(cut);
        if (x < left || x > right) continue;
        QPainterPath triangle;
        triangle.moveTo(x, r.top + 2);
        triangle.lineTo(x + 5, r.top + 10);
        triangle.lineTo(x - 5, r.top + 10);
        triangle.closeSubpath();
        p.fillPath(triangle, QColor(0xff, 0x8c, 0x1a));
        p.setPen(QPen(QColor(0x80, 0x40, 0x00), 1));
        p.drawPath(triangle);
    }
    p.restore();
}

} // namespace lumenis::ui
