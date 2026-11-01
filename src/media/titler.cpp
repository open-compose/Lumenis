// Avid Titler+'s picture (ROADMAP 6.8a; FX Guide p.302-324; docs/spec/titler.md): each object of a title
// (model::Title) is made a path in its own coordinates (text through QPainterPath::addText, rectangles and
// ellipses), placed by its Transform about its Anchor Point, and drawn as its own layer — Shadow, then Fill,
// then Outline, each softened by blurring its coverage — at its Opacity; the Background goes under all of them and
// the Foreground's Opacity and Crop over the whole. Coordinates are pixels of a 1920 x 1080 frame from its centre
// (titler.h), scaled to the picture's height.
#include "lumenis/media/compositor.h"
#include "lumenis/media/titler.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>

#include <algorithm>
#include <functional>
#include <cmath>
#include <numbers>
#include <vector>

namespace lumenis::media {

namespace {

using model::Title;
using model::TitleColor;
using model::TitleObject;
using model::TitleObjectKind;

QColor colorOf(const QString& name) {
    const QColor c(name);
    return c.isValid() ? c : QColor(Qt::white);
}

QFont fontOf(const model::TitleFont& f) {
    QFont font(f.family.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::GeneralFont).family() : f.family);
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(f.size))));
    // "Options for Font Style will differ depending on the selected Font Family" (p.307): a named style when the
    // family has it, else bold and italic from the name.
    if (!f.style.isEmpty()) {
        font.setStyleName(f.style);
        const QString s = f.style.toLower();
        font.setBold(s.contains(QLatin1String("bold")));
        font.setItalic(s.contains(QLatin1String("italic")) || s.contains(QLatin1String("oblique")));
    }
    font.setUnderline(f.underline);
    // Kerning: "the amount of space between characters" (p.309), added in pixels.
    font.setLetterSpacing(QFont::AbsoluteSpacing, f.kerning);
    return font;
}

// A paragraph's lines: its own lines, and with a box width words wrapped to it ("Changing the size of the text box
// wraps the text appropriately", p.304).
QStringList textLines(const TitleObject& o, const QFontMetricsF& metrics) {
    QStringList lines;
    for (const QString& paragraph : o.text.split(QLatin1Char('\n'))) {
        if (o.size.width() <= 0.0) {
            lines << paragraph;
            continue;
        }
        QString line;
        for (const QString& word : paragraph.split(QLatin1Char(' '))) {
            const QString longer = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
            if (!line.isEmpty() && metrics.horizontalAdvance(longer) > o.size.width()) {
                lines << line;
                line = word;
            } else {
                line = longer;
            }
        }
        lines << line;
    }
    return lines;
}

// A text object's shape: its first line's baseline at y 0, each line placed by the justification ("left justified
// text ... its anchor point will be at the baseline left", p.307, p.313); Vertical Center and Bottom move the lines
// up so that the middle or the last baseline is at 0 (ours). Right To Left puts each line's characters from the
// right; Top To Bottom one character under the other (ours).
QPainterPath textPath(const TitleObject& o) {
    QPainterPath path;
    const QFont font = fontOf(o.font);
    const QFontMetricsF metrics(font);
    QStringList lines = textLines(o, metrics);
    model::HorizontalJustify horizontal = o.font.horizontal;
    if (o.font.orientation == model::FontOrientation::TopToBottom) {
        QStringList stacked;
        for (const QString& line : lines) {
            for (const QChar c : line) stacked << QString(c);
        }
        lines = stacked;
    } else if (o.font.orientation == model::FontOrientation::RightToLeft) {
        for (QString& line : lines) std::reverse(line.begin(), line.end());
    }
    const double lineHeight = metrics.height() + o.font.leading;
    const auto n = static_cast<double>(std::max<qsizetype>(lines.size(), 1));
    double top = 0.0;
    if (o.font.vertical == model::VerticalJustify::Center) top = -(n - 1.0) * lineHeight / 2.0;
    if (o.font.vertical == model::VerticalJustify::Bottom) top = -(n - 1.0) * lineHeight;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const double width = metrics.horizontalAdvance(lines.at(i));
        double x = 0.0;
        if (horizontal == model::HorizontalJustify::Center) x = -width / 2.0;
        if (horizontal == model::HorizontalJustify::Right) x = -width;
        path.addText(QPointF(x, top + static_cast<double>(i) * lineHeight), font, lines.at(i));
    }
    // Skew "slants an object along its horizontal axis" (p.310), about the baseline.
    if (o.font.skew != 0.0) path = QTransform().shear(-std::tan(o.font.skew * std::numbers::pi / 180.0), 0.0).map(path);
    return path;
}

// The object's own shape, in its coordinates; empty for groups.
QPainterPath shapeOf(const TitleObject& o) {
    QPainterPath path;
    const QRectF box(-o.size.width() / 2.0, -o.size.height() / 2.0, o.size.width(), o.size.height());
    switch (o.kind) {
    case TitleObjectKind::Text: return textPath(o);
    case TitleObjectKind::Rectangle: path.addRect(box); break;
    case TitleObjectKind::Ellipse: path.addEllipse(box); break;
    case TitleObjectKind::Group: break;
    }
    return path;
}

// Where an object's coordinates go: "Transformations for an object are relative to its anchor point position. For
// example, when rotating an object, it will rotate around its anchor point. The object will also scale from its
// anchor point" (p.313).
QTransform placementOf(const model::TitleTransform& t) {
    QTransform m;
    m.translate(t.position.x() + t.anchor.x(), t.position.y() + t.anchor.y());
    m.rotate(t.rotation);
    m.scale(t.scaleX / 100.0, t.scaleY / 100.0);
    m.translate(-t.anchor.x(), -t.anchor.y());
    return m;
}

// A box blur of an image's every channel, three times (close to a Gaussian), radius in pixels.
void soften(QImage& image, double radius) {
    const int r = static_cast<int>(std::lround(radius));
    if (r <= 0 || image.isNull()) return;
    const int w = image.width(), h = image.height();
    std::vector<int> sums(static_cast<std::size_t>(std::max(w, h) + 1) * 4);
    for (int pass = 0; pass < 3; ++pass) {
        for (int horizontal = 0; horizontal < 2; ++horizontal) {
            const int lines = horizontal ? h : w;
            const int length = horizontal ? w : h;
            for (int line = 0; line < lines; ++line) {
                auto at = [&](int i) -> uchar* {
                    const int x = horizontal ? i : line;
                    const int y = horizontal ? line : i;
                    return image.scanLine(y) + static_cast<std::ptrdiff_t>(x) * 4;
                };
                // Running sums of the line, then each pixel the mean of its window.
                for (int c = 0; c < 4; ++c) sums[static_cast<std::size_t>(c)] = 0;
                for (int i = 0; i < length; ++i) {
                    const uchar* p = at(i);
                    for (int c = 0; c < 4; ++c) sums[static_cast<std::size_t>((i + 1) * 4 + c)] = sums[static_cast<std::size_t>(i * 4 + c)] + p[c];
                }
                for (int i = 0; i < length; ++i) {
                    const int a = std::max(0, i - r), b = std::min(length, i + r + 1);
                    uchar* p = at(i);
                    for (int c = 0; c < 4; ++c) {
                        const int sum = sums[static_cast<std::size_t>(b * 4 + c)] - sums[static_cast<std::size_t>(a * 4 + c)];
                        p[c] = static_cast<uchar>(sum / (2 * r + 1));
                    }
                }
            }
        }
    }
}

// One coat (fill, outline or shadow) of `shape` (picture pixels): its colour or four-corner gradient over the
// shape's bounds, at `opacity` (0-100), softened by `softness` picture pixels.
QImage coat(const QPainterPath& shape, const TitleColor& color, double opacity, double softness, const QSize& size, bool square) {
    QImage layer(size, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    if (shape.isEmpty()) return layer;
    {
        QPainter p(&layer);
        p.setRenderHint(QPainter::Antialiasing, !square);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawPath(shape);
    }
    // Colour the coverage: a solid colour, or bilinearly between the four corners of the shape's bounds.
    const QRectF bounds = shape.boundingRect();
    const QColor tl = colorOf(color.gradient ? color.topLeft : color.color);
    const QColor tr = colorOf(color.gradient ? color.topRight : color.color);
    const QColor bl = colorOf(color.gradient ? color.bottomLeft : color.color);
    const QColor br = colorOf(color.gradient ? color.bottomRight : color.color);
    const double level = std::clamp(opacity, 0.0, 100.0) / 100.0;
    for (int y = 0; y < size.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(layer.scanLine(y));
        const double v = bounds.height() > 0 ? std::clamp((y + 0.5 - bounds.top()) / bounds.height(), 0.0, 1.0) : 0.0;
        for (int x = 0; x < size.width(); ++x) {
            const int cover = qAlpha(line[x]);
            if (cover == 0) continue;
            const double u = bounds.width() > 0 ? std::clamp((x + 0.5 - bounds.left()) / bounds.width(), 0.0, 1.0) : 0.0;
            // Each channel 0-1 between the corners (QColor's float channels made double first).
            auto mix = [&](float a, float b, float c, float d) {
                return (static_cast<double>(a) * (1 - u) + static_cast<double>(b) * u) * (1 - v) +
                       (static_cast<double>(c) * (1 - u) + static_cast<double>(d) * u) * v;
            };
            const double alpha = mix(tl.alphaF(), tr.alphaF(), bl.alphaF(), br.alphaF()) * level * cover / 255.0;
            const double red = mix(tl.redF(), tr.redF(), bl.redF(), br.redF());
            const double green = mix(tl.greenF(), tr.greenF(), bl.greenF(), br.greenF());
            const double blue = mix(tl.blueF(), tr.blueF(), bl.blueF(), br.blueF());
            auto byte = [](double value) { return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255); };
            line[x] = qRgba(byte(red * alpha), byte(green * alpha), byte(blue * alpha), byte(alpha));
        }
    }
    soften(layer, softness);
    return layer;
}

// Draws an object (and a group's children) placed by `outer` (title pixels to picture pixels) onto `canvas`.
void drawObject(QPainter& canvas, const TitleObject& o, const QTransform& outer, double scale, const QSize& size, double opacity) {
    const QTransform m = placementOf(o.transform) * outer;
    const double level = opacity * std::clamp(o.transform.opacity, 0.0, 100.0) / 100.0;
    if (o.kind == TitleObjectKind::Group) {
        // A group's opacity covers its children (ours, as each child's own).
        for (const TitleObject& child : o.children) drawObject(canvas, child, m, scale, size, level);
        return;
    }
    const QPainterPath local = shapeOf(o);
    if (local.isEmpty()) return;
    const QPainterPath shape = m.map(local);
    QImage layer(size, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    QPainter p(&layer);
    // Outline (p.316, p.323): Outer around the shape, Inner within it, Border astride its edge; Round or Square.
    QPainterPath ring;
    const bool square = o.outline.shape == model::OutlineShape::Square;
    if (o.outline.on && o.outline.size > 0.0) {
        QPainterPathStroker stroker;
        stroker.setJoinStyle(square ? Qt::MiterJoin : Qt::RoundJoin);
        stroker.setCapStyle(square ? Qt::SquareCap : Qt::RoundCap);
        const double width = o.outline.size * scale;
        stroker.setWidth(o.outline.type == model::OutlineType::Border ? width : 2.0 * width);
        const QPainterPath stroke = stroker.createStroke(shape);
        switch (o.outline.type) {
        case model::OutlineType::Outer: ring = stroke.united(shape).subtracted(shape); break;
        case model::OutlineType::Inner: ring = stroke.intersected(shape); break;
        case model::OutlineType::Border: ring = stroke; break;
        }
    }
    // Shadow (p.316-317): the whole object's shape moved by Distance at Angle (in the frame, ours).
    if (o.shadow.on) {
        const double a = o.shadow.angle * std::numbers::pi / 180.0;
        const QPointF by(std::cos(a) * o.shadow.distance * scale, std::sin(a) * o.shadow.distance * scale);
        const QPainterPath whole = ring.isEmpty() ? shape : shape.united(ring);
        p.drawImage(0, 0, coat(whole.translated(by), o.shadow.color, o.shadow.opacity, o.shadow.softness * scale, size, false));
    }
    // The fill, then the outline over it (an Inner or Border outline lies on the fill).
    if (o.fill.on) p.drawImage(0, 0, coat(shape, o.fill.color, o.fill.opacity, o.fill.softness * scale, size, false));
    if (!ring.isEmpty()) p.drawImage(0, 0, coat(ring, o.outline.color, o.outline.opacity, o.outline.softness * scale, size, square));
    p.end();
    canvas.setOpacity(level);
    canvas.drawImage(0, 0, layer);
    canvas.setOpacity(1.0);
}

} // namespace

QTransform titleToPicture(const QSize& size) {
    // Title pixels (a 1080-line frame, its centre at 0, 0) to the picture's, by its height (ours).
    const double scale = size.height() / 1080.0;
    QTransform m;
    m.translate(size.width() / 2.0, size.height() / 2.0);
    m.scale(scale, scale);
    return m;
}

QImage titleImage(const Title& title, const QSize& size) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (size.isEmpty()) return image;
    const double scale = size.height() / 1080.0;
    {
        QPainter canvas(&image);
        canvas.setRenderHint(QPainter::Antialiasing);
        // "Adding a background in Avid Titler+ creates a solid color or color gradient behind all of your text and
        // graphic objects" (p.317).
        if (title.background.on) {
            QPainterPath all;
            all.addRect(QRectF(QPointF(0, 0), QSizeF(size)));
            canvas.drawImage(0, 0, coat(all, title.background.color, title.background.opacity, 0.0, size, true));
        }
        const QTransform outer = titleToPicture(size);
        for (const TitleObject& o : title.objects) drawObject(canvas, o, outer, scale, size, 1.0);
    }
    // Foreground: "change the opacity and crop the entire composition" (p.324); crop in percent of the frame.
    const model::TitleForeground& fg = title.foreground;
    const double level = std::clamp(fg.opacity, 0.0, 100.0) / 100.0;
    const QRectF kept(size.width() * fg.cropLeft / 100.0, size.height() * fg.cropTop / 100.0,
                      size.width() * (1.0 - (fg.cropLeft + fg.cropRight) / 100.0), size.height() * (1.0 - (fg.cropTop + fg.cropBottom) / 100.0));
    if (level < 1.0 || kept != QRectF(QPointF(0, 0), QSizeF(size))) {
        QImage out(size, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setClipRect(kept);
        p.setOpacity(level);
        p.drawImage(0, 0, image);
        p.end();
        return out;
    }
    return image;
}

std::optional<QPolygonF> titleObjectOutline(const Title& title, const QString& id) {
    // The object's bounds where it is drawn, in title pixels: its own shape's bounds placed by its transform and
    // its groups'.
    std::function<std::optional<QPolygonF>(const QList<TitleObject>&, const QTransform&)> find =
        [&](const QList<TitleObject>& list, const QTransform& outer) -> std::optional<QPolygonF> {
        for (const TitleObject& o : list) {
            const QTransform m = placementOf(o.transform) * outer;
            if (o.id == id) {
                QRectF bounds;
                if (o.kind == TitleObjectKind::Group) {
                    for (const TitleObject& child : o.children) bounds = bounds.united(placementOf(child.transform).map(shapeOf(child)).boundingRect());
                    return outer.map(placementOf(o.transform).map(QPolygonF(bounds)));
                }
                return m.map(QPolygonF(shapeOf(o).boundingRect()));
            }
            if (const auto inner = find(o.children, m)) return inner;
        }
        return std::nullopt;
    };
    return find(title.objects, QTransform());
}

Picture titlePicture(const model::Effect& effect, const Picture& layer) {
    const Title title = model::titleOf(effect);
    if (layer.isNull()) return layer;
    // "the background can be either frames of your video (utilizing an alpha channel that is automatically applied
    // with the Avid Titler+ effect), a solid color, or some combination of both" (p.317): the title over the segment.
    const QSize size(layer.width(), layer.height());
    return over(Picture::fromImage(titleImage(title, size), size.width(), size.height()), layer);
}

} // namespace lumenis::media
