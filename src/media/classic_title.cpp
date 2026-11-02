// The classic Title Tool's picture (classic_title.h). Text objects become Titler+ text objects (titler.h) placed
// from their top-left corner, so that both titlers share one text layout; the other objects are drawn here.
#include "lumenis/media/classic_title.h"

#include "lumenis/media/titler.h"

#include <QColor>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

// A text object's Titler+ twin with its origin at (0, 0): the object's font, justification and runs (their kerning made
// pixels), the box width for wrapping.
model::TitleObject unplacedText(const model::ClassicObject& o) {
    model::TitleObject t;
    t.kind = model::TitleObjectKind::Text;
    t.id = o.id;
    t.text = o.text;
    t.size = QSizeF(std::max(1.0, o.rect.width()), 0.0);
    t.font.family = o.family;
    t.font.style = model::classicStyle(o.bold, o.italic);
    t.font.size = std::clamp(o.size, 1.0, 999.0);
    t.font.kerning = o.kerning * t.font.size / 100.0;
    t.font.leading = o.leading;
    t.font.horizontal = o.justify == model::ClassicJustify::Center  ? model::HorizontalJustify::Center
                        : o.justify == model::ClassicJustify::Right ? model::HorizontalJustify::Right
                                                                    : model::HorizontalJustify::Left;
    t.fill.color.color = o.color;
    t.fill.color.topLeft = t.fill.color.topRight = t.fill.color.bottomLeft = t.fill.color.bottomRight = o.color;
    t.fill.opacity = 100.0 - std::clamp(o.transparency, 0.0, 100.0);
    t.runs = o.runs;
    for (model::TitleRun& r : t.runs) {
        const double size = r.format.size.value_or(t.font.size);
        if (r.format.kerning) r.format.kerning = *r.format.kerning * size / 100.0;
    }
    model::normalizeRuns(t);
    return t;
}

// Where the twin's origin goes: its first line's top at the object's top; the justification's x within the width.
QPointF originOf(const model::ClassicObject& o, const model::TitleObject& t) {
    const double top = titleTextCaret(t, 0).p1().y(); // the first line's top, above its baseline
    double x = o.rect.left();
    if (o.justify == model::ClassicJustify::Center) x += o.rect.width() / 2.0;
    if (o.justify == model::ClassicJustify::Right) x += o.rect.width();
    return {x, o.rect.top() - top};
}

QColor colorOf(const model::ClassicObject& o) {
    QColor c(o.color);
    if (!c.isValid()) c = Qt::white;
    c.setAlphaF(static_cast<float>(1.0 - std::clamp(o.transparency, 0.0, 100.0) / 100.0));
    return c;
}

} // namespace

QTransform classicToPicture(const QSize& size) {
    const double scale = size.height() / model::kClassicHeight;
    QTransform m;
    m.translate((size.width() - model::kClassicWidth * scale) / 2.0, 0.0);
    m.scale(scale, scale);
    return m;
}

model::TitleObject classicTextObject(const model::ClassicObject& object) {
    model::TitleObject t = unplacedText(object);
    // Titler+ positions are from the frame's centre.
    t.transform.position = originOf(object, t) - QPointF(model::kClassicWidth / 2.0, model::kClassicHeight / 2.0);
    return t;
}

QRectF classicObjectBounds(const model::ClassicObject& object) {
    if (object.kind != model::ClassicKind::Text) return object.rect.normalized();
    const model::TitleObject t = unplacedText(object);
    const QPointF origin = originOf(object, t);
    const QRectF laid = titleObjectBounds(t).translated(origin);
    // The text's own lines' height; at least one line's for an empty object.
    const QLineF caret = titleTextCaret(t, 0);
    const double bottom = std::max(laid.isEmpty() ? 0.0 : laid.bottom(), origin.y() + caret.p2().y());
    return QRectF(object.rect.left(), object.rect.top(), object.rect.width(), std::max(1.0, bottom - object.rect.top()));
}

QImage classicTitleImage(const model::ClassicTitle& title, const QSize& size, bool antiAliased) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (size.isEmpty()) return image;
    const QTransform m = classicToPicture(size);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, antiAliased);
    p.setRenderHint(QPainter::TextAntialiasing, antiAliased);
    for (const model::ClassicObject& o : title.objects) {
        if (o.kind == model::ClassicKind::Text) {
            if (o.text.isEmpty()) continue;
            model::Title one;
            one.objects << classicTextObject(o);
            // Titler+ draws centred on the picture's own centre; the frame's centre is the same point.
            QImage text = titleImage(one, size);
            if (!antiAliased) {
                // Hard edges: coverage to all or nothing.
                for (int y = 0; y < text.height(); ++y) {
                    auto* line = reinterpret_cast<QRgb*>(text.scanLine(y));
                    for (int x = 0; x < text.width(); ++x) {
                        const int a = qAlpha(line[x]);
                        if (a == 0 || a == 255) continue;
                        if (a < 128) {
                            line[x] = 0;
                        } else {
                            const double k = 255.0 / a;
                            line[x] = qRgba(std::min(255, static_cast<int>(qRed(line[x]) * k)), std::min(255, static_cast<int>(qGreen(line[x]) * k)),
                                            std::min(255, static_cast<int>(qBlue(line[x]) * k)), 255);
                        }
                    }
                }
            }
            p.drawImage(0, 0, text);
            continue;
        }
        const QColor c = colorOf(o);
        p.save();
        p.setTransform(m);
        if (o.kind == model::ClassicKind::Line) {
            // Ours until the line attributes (6.8d3): two pixels of the frame wide.
            QPen pen(c, 2.0);
            pen.setCapStyle(Qt::FlatCap);
            p.setPen(pen);
            p.drawLine(o.rect.topLeft(), o.rect.bottomRight());
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            if (o.kind == model::ClassicKind::Box) p.drawRect(o.rect.normalized());
            else p.drawEllipse(o.rect.normalized());
        }
        p.restore();
    }
    return image;
}

qsizetype classicTextIndexAt(const model::ClassicObject& object, const QPointF& point) {
    const model::TitleObject t = unplacedText(object);
    return titleTextIndexAt(t, point - originOf(object, t));
}

QLineF classicTextCaret(const model::ClassicObject& object, qsizetype index) {
    const model::TitleObject t = unplacedText(object);
    return titleTextCaret(t, index).translated(originOf(object, t));
}

QList<QPolygonF> classicTextRange(const model::ClassicObject& object, qsizetype from, qsizetype to) {
    const model::TitleObject t = unplacedText(object);
    const QPointF origin = originOf(object, t);
    QList<QPolygonF> out;
    for (const QPolygonF& p : titleTextRange(t, from, to)) out << p.translated(origin);
    return out;
}

} // namespace lumenis::media
