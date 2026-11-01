// Avid Titler+'s picture (ROADMAP 6.8a; FX Guide p.302-324; docs/spec/titler.md): each object of a title
// (model::Title) is made a path in its own coordinates (text through QPainterPath::addText, a piece for each style
// of its formatted characters, rectangles and ellipses), placed by its Transform about its Anchor Point, and drawn as its own layer — Shadow, then Fill,
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
#include <limits>
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

// A text object laid out: each shown line with where it starts, the index in the object's text of each of its
// characters and the x of each character boundary, so that the caret and selections find their places (6.8b), and
// its pieces: runs of shown characters drawn with one style (6.8c2's formatted characters).
struct LaidText {
    QFont font; // the object's own
    struct Piece {
        qsizetype from = 0, to = 0; // shown positions
        int style = 0;              // index in styles
    };
    struct Line {
        QString shown;
        QList<qsizetype> source; // per shown character
        qsizetype start = 0, end = 0; // the text indexes the line covers, [start, end]
        double x = 0.0, baseline = 0.0;
        double ascent = 0.0, descent = 0.0, height = 0.0;
        QList<double> xs; // boundaries from the line's x, one more than the characters
        QList<Piece> pieces;
    };
    QList<Line> lines;
    QList<model::TitleCharStyle> styles;
    QList<QFont> fonts; // per style
    QList<int> styleOf; // per text index
    QTransform skew;
};

// The x of each boundary of shown characters `source` (text indexes in shown order), and the pieces of one style.
void measure(const LaidText& laid, const QList<qsizetype>& source, const QString& text, QList<double>& xs, QList<LaidText::Piece>* pieces) {
    xs = {0.0};
    qsizetype k = 0;
    while (k < source.size()) {
        const int style = laid.styleOf.value(source.at(k));
        qsizetype end = k + 1;
        while (end < source.size() && laid.styleOf.value(source.at(end)) == style) ++end;
        QString piece;
        for (qsizetype m = k; m < end; ++m) piece += text.at(source.at(m));
        const QFontMetricsF metrics(laid.fonts.at(style));
        const double start = xs.last();
        for (qsizetype m = 1; m <= piece.size(); ++m) xs << start + metrics.horizontalAdvance(piece.left(m));
        if (pieces) *pieces << LaidText::Piece{k, end, style};
        k = end;
    }
}

// The lines: the text's own lines, and with a box width words wrapped to it ("Changing the size of the text box wraps
// the text appropriately", p.304). The first line's baseline is at y 0, each line placed by the justification
// ("left justified text ... its anchor point will be at the baseline left", p.307, p.313); Vertical Center and
// Bottom move the lines up so that the middle or the last baseline is at 0 (ours). Right To Left shows each line's
// characters from the right; Top To Bottom one character under the other (ours). A line is as tall as its tallest
// character (ours), and lines are spaced by the taller of two neighbours plus the Leading.
LaidText layoutText(const TitleObject& o) {
    LaidText laid;
    laid.font = fontOf(o.font);
    for (qsizetype i = 0; i < o.text.size(); ++i) {
        const model::TitleCharStyle style = model::styleAt(o, i);
        qsizetype at = laid.styles.indexOf(style);
        if (at < 0) {
            at = laid.styles.size();
            laid.styles << style;
            laid.fonts << fontOf(style.font);
        }
        laid.styleOf << static_cast<int>(at);
    }
    auto width = [&](const QList<qsizetype>& source) {
        QList<double> xs;
        measure(laid, source, o.text, xs, nullptr);
        return xs.last();
    };
    QList<LaidText::Line> lines;
    qsizetype offset = 0;
    for (const QString& paragraph : o.text.split(QLatin1Char('\n'))) {
        LaidText::Line line;
        line.start = offset;
        auto add = [&](qsizetype from, qsizetype to) {
            for (qsizetype i = from; i < to; ++i) {
                line.shown += o.text.at(i);
                line.source << i;
            }
        };
        if (o.size.width() <= 0.0) {
            add(offset, offset + paragraph.size());
        } else {
            qsizetype at = offset;
            for (const QString& word : paragraph.split(QLatin1Char(' '))) {
                QList<qsizetype> longer = line.source;
                if (!longer.isEmpty()) longer << at - 1;
                for (qsizetype i = at; i < at + word.size(); ++i) longer << i;
                if (!line.shown.isEmpty() && width(longer) > o.size.width()) {
                    line.end = at - 1;
                    lines << line;
                    line = LaidText::Line{};
                    line.start = at;
                } else if (!line.shown.isEmpty()) {
                    add(at - 1, at); // the space between
                }
                add(at, at + word.size());
                at += word.size() + 1;
            }
        }
        line.end = offset + paragraph.size();
        lines << line;
        offset += paragraph.size() + 1;
    }
    if (o.font.orientation == model::FontOrientation::TopToBottom) {
        QList<LaidText::Line> stacked;
        for (const LaidText::Line& line : lines) {
            for (qsizetype k = 0; k < line.shown.size(); ++k) {
                LaidText::Line one;
                one.shown = line.shown.at(k);
                one.source = {line.source.at(k)};
                one.start = line.source.at(k);
                one.end = line.source.at(k) + 1;
                stacked << one;
            }
            if (line.shown.isEmpty()) stacked << line;
        }
        lines = stacked;
    } else if (o.font.orientation == model::FontOrientation::RightToLeft) {
        for (LaidText::Line& line : lines) {
            std::reverse(line.shown.begin(), line.shown.end());
            std::reverse(line.source.begin(), line.source.end());
        }
    }
    // Each line's boundaries, pieces and height (an empty line the object's own font's).
    for (LaidText::Line& line : lines) {
        measure(laid, line.source, o.text, line.xs, &line.pieces);
        QList<int> styles;
        for (const qsizetype i : line.source) {
            if (!styles.contains(laid.styleOf.value(i))) styles << laid.styleOf.value(i);
        }
        QList<QFont> fonts;
        for (const int st : styles) fonts << laid.fonts.at(st);
        if (fonts.isEmpty()) fonts << laid.font;
        for (const QFont& font : fonts) {
            const QFontMetricsF metrics(font);
            line.ascent = std::max(line.ascent, metrics.ascent());
            line.descent = std::max(line.descent, metrics.descent());
            line.height = std::max(line.height, metrics.height());
        }
    }
    QList<double> steps;
    for (qsizetype i = 1; i < lines.size(); ++i) steps << std::max(lines.at(i - 1).height, lines.at(i).height) + o.font.leading;
    double total = 0.0;
    for (const double step : steps) total += step;
    double top = 0.0;
    if (o.font.vertical == model::VerticalJustify::Center) top = -total / 2.0;
    if (o.font.vertical == model::VerticalJustify::Bottom) top = -total;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const double w = lines.at(i).xs.last();
        double x = 0.0;
        if (o.font.horizontal == model::HorizontalJustify::Center) x = -w / 2.0;
        if (o.font.horizontal == model::HorizontalJustify::Right) x = -w;
        lines[i].x = x;
        lines[i].baseline = top;
        if (i < steps.size()) top += steps.at(i);
    }
    laid.lines = lines;
    // Skew "slants an object along its horizontal axis" (p.310), about the baseline.
    if (o.font.skew != 0.0) laid.skew = QTransform().shear(-std::tan(o.font.skew * std::numbers::pi / 180.0), 0.0);
    return laid;
}

// A text's paths, one for each style of its characters (6.8c2), in its own coordinates.
QList<std::pair<QPainterPath, int>> textPieces(const LaidText& laid) {
    QList<std::pair<QPainterPath, int>> out;
    for (qsizetype st = 0; st < laid.styles.size(); ++st) out << std::pair{QPainterPath(), static_cast<int>(st)};
    for (const LaidText::Line& line : laid.lines) {
        for (const LaidText::Piece& piece : line.pieces) {
            out[piece.style].first.addText(QPointF(line.x + line.xs.at(piece.from), line.baseline), laid.fonts.at(piece.style),
                                           line.shown.mid(piece.from, piece.to - piece.from));
        }
    }
    for (auto& [path, style] : out) path = laid.skew.map(path);
    return out;
}

QPainterPath textPath(const TitleObject& o) {
    QPainterPath path;
    for (const auto& piece : textPieces(layoutText(o))) path.addPath(piece.first);
    return path;
}

// The x where the caret before text index `index` stands on `line`, if the line holds it.
std::optional<double> caretX(const LaidText::Line& line, qsizetype index, bool rightToLeft) {
    if (index < line.start || index > line.end) return std::nullopt;
    for (qsizetype k = 0; k < line.source.size(); ++k) {
        if (line.source.at(k) != index) continue;
        // Before a character: its left edge, or its right edge when the line runs from the right.
        return line.x + line.xs.at(rightToLeft ? k + 1 : k);
    }
    // After the line's last character.
    return rightToLeft ? line.x : line.x + line.xs.last();
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
    // The object's parts: a text's characters of each style (6.8c2) with their Fill and Outline colours, else its
    // one shape.
    struct Part {
        QPainterPath shape, ring;
        TitleColor fill, outline;
    };
    QList<Part> parts;
    if (o.kind == TitleObjectKind::Text) {
        const LaidText laid = layoutText(o);
        for (const auto& [path, style] : textPieces(laid)) {
            if (!path.isEmpty()) parts << Part{m.map(path), {}, laid.styles.at(style).fill, laid.styles.at(style).outline};
        }
    } else {
        const QPainterPath local = shapeOf(o);
        if (!local.isEmpty()) parts << Part{m.map(local), {}, o.fill.color, o.outline.color};
    }
    if (parts.isEmpty()) return;
    QImage layer(size, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    QPainter p(&layer);
    // Outline (p.316, p.323): Outer around the shape, Inner within it, Border astride its edge; Round or Square.
    const bool square = o.outline.shape == model::OutlineShape::Square;
    if (o.outline.on && o.outline.size > 0.0) {
        QPainterPathStroker stroker;
        stroker.setJoinStyle(square ? Qt::MiterJoin : Qt::RoundJoin);
        stroker.setCapStyle(square ? Qt::SquareCap : Qt::RoundCap);
        const double width = o.outline.size * scale;
        stroker.setWidth(o.outline.type == model::OutlineType::Border ? width : 2.0 * width);
        for (Part& part : parts) {
            const QPainterPath stroke = stroker.createStroke(part.shape);
            switch (o.outline.type) {
            case model::OutlineType::Outer: part.ring = stroke.united(part.shape).subtracted(part.shape); break;
            case model::OutlineType::Inner: part.ring = stroke.intersected(part.shape); break;
            case model::OutlineType::Border: part.ring = stroke; break;
            }
        }
    }
    // Shadow (p.316-317): the whole object's shape moved by Distance at Angle (in the frame, ours).
    if (o.shadow.on) {
        const double a = o.shadow.angle * std::numbers::pi / 180.0;
        const QPointF by(std::cos(a) * o.shadow.distance * scale, std::sin(a) * o.shadow.distance * scale);
        QPainterPath whole;
        for (const Part& part : parts) whole = whole.united(part.ring.isEmpty() ? part.shape : part.shape.united(part.ring));
        p.drawImage(0, 0, coat(whole.translated(by), o.shadow.color, o.shadow.opacity, o.shadow.softness * scale, size, false));
    }
    // The fill, then the outline over it (an Inner or Border outline lies on the fill).
    if (o.fill.on) {
        for (const Part& part : parts) p.drawImage(0, 0, coat(part.shape, part.fill, o.fill.opacity, o.fill.softness * scale, size, false));
    }
    for (const Part& part : parts) {
        if (!part.ring.isEmpty()) p.drawImage(0, 0, coat(part.ring, part.outline, o.outline.opacity, o.outline.softness * scale, size, square));
    }
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

QRectF titleObjectBounds(const TitleObject& object) {
    if (object.kind != TitleObjectKind::Group) return shapeOf(object).boundingRect();
    QRectF bounds;
    for (const TitleObject& child : object.children) bounds = bounds.united(placementOf(child.transform).map(shapeOf(child)).boundingRect());
    return bounds;
}

std::optional<QTransform> titleObjectPlacement(const Title& title, const QString& id) {
    std::function<std::optional<QTransform>(const QList<TitleObject>&, const QTransform&)> find =
        [&](const QList<TitleObject>& list, const QTransform& outer) -> std::optional<QTransform> {
        for (const TitleObject& o : list) {
            const QTransform m = placementOf(o.transform) * outer;
            if (o.id == id) return m;
            if (const auto inner = find(o.children, m)) return inner;
        }
        return std::nullopt;
    };
    return find(title.objects, QTransform());
}

QLineF titleTextCaret(const TitleObject& object, qsizetype index) {
    const LaidText laid = layoutText(object);
    const bool rtl = object.font.orientation == model::FontOrientation::RightToLeft;
    for (const LaidText::Line& line : laid.lines) {
        if (const auto x = caretX(line, index, rtl)) {
            return laid.skew.map(QLineF(*x, line.baseline - line.ascent, *x, line.baseline + line.descent));
        }
    }
    if (laid.lines.isEmpty()) {
        const QFontMetricsF metrics(laid.font);
        return QLineF(0, -metrics.ascent(), 0, metrics.descent());
    }
    const LaidText::Line& last = laid.lines.last();
    return laid.skew.map(QLineF(last.x, last.baseline - last.ascent, last.x, last.baseline + last.descent));
}

qsizetype titleTextIndexAt(const TitleObject& object, const QPointF& point) {
    const LaidText laid = layoutText(object);
    if (laid.lines.isEmpty()) return 0;
    const QPointF p = laid.skew.inverted().map(point);
    // The line nearest the point up and down, then the character boundary nearest it across.
    const LaidText::Line* best = &laid.lines.first();
    double far = std::numeric_limits<double>::max();
    for (const LaidText::Line& line : laid.lines) {
        const double top = line.baseline - line.ascent, bottom = line.baseline + line.descent;
        const double d = p.y() < top ? top - p.y() : p.y() > bottom ? p.y() - bottom : 0.0;
        if (d < far) {
            far = d;
            best = &line;
        }
    }
    const bool rtl = object.font.orientation == model::FontOrientation::RightToLeft;
    qsizetype k = 0;
    for (qsizetype m = 1; m < best->xs.size(); ++m) {
        if (std::abs(best->x + best->xs.at(m) - p.x()) < std::abs(best->x + best->xs.at(k) - p.x())) k = m;
    }
    if (rtl) return k == 0 ? best->end : best->source.at(k - 1);
    return k < best->source.size() ? best->source.at(k) : best->end;
}

QList<QPolygonF> titleTextRange(const TitleObject& object, qsizetype from, qsizetype to) {
    QList<QPolygonF> out;
    if (to < from) std::swap(from, to);
    if (to == from) return out;
    const LaidText laid = layoutText(object);
    for (const LaidText::Line& line : laid.lines) {
        // The line's characters in the range, as one span of their shown positions.
        double left = 0.0, right = 0.0;
        bool any = false;
        for (qsizetype k = 0; k < line.source.size(); ++k) {
            if (line.source.at(k) < from || line.source.at(k) >= to) continue;
            const double a = line.x + line.xs.at(k);
            const double b = line.x + line.xs.at(k + 1);
            left = any ? std::min(left, a) : a;
            right = any ? std::max(right, b) : b;
            any = true;
        }
        if (!any) continue;
        out << laid.skew.map(QPolygonF(QRectF(QPointF(left, line.baseline - line.ascent), QPointF(right, line.baseline + line.descent))));
    }
    return out;
}

Picture titlePicture(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span) {
    const Title title = model::titleAt(effect, time, span);
    if (layer.isNull()) return layer;
    // "the background can be either frames of your video (utilizing an alpha channel that is automatically applied
    // with the Avid Titler+ effect), a solid color, or some combination of both" (p.317): the title over the segment.
    const QSize size(layer.width(), layer.height());
    return over(Picture::fromImage(titleImage(title, size), size.width(), size.height()), layer);
}

} // namespace lumenis::media
