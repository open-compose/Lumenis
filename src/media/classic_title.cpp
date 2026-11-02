// The classic Title Tool's picture (classic_title.h). Text objects become Titler+ text objects (titler.h) placed
// from their top-left corner, so that both titlers share one text layout; the other objects are drawn here.
#include "lumenis/media/classic_title.h"

#include "lumenis/media/titler.h"

#include <QColor>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <numbers>

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
    t.fill.color.color = o.fill.color;
    t.fill.opacity = 100.0 - std::clamp(o.fill.transparency, 0.0, 100.0);
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

QColor colorFrom(const QString& name) {
    QColor c(name);
    return c.isValid() ? c : QColor(Qt::white);
}

// How far along a blend a point is (0 at the first value, 1 at the second): across `bounds` in the direction `degrees`
// (clockwise from left to right), the corners furthest back and ahead at 0 and 1.
double blendAt(const QPointF& p, const QRectF& bounds, double degrees) {
    const double a = degrees * std::numbers::pi / 180.0;
    const QPointF d(std::cos(a), std::sin(a));
    const double half = (std::abs(bounds.width() * d.x()) + std::abs(bounds.height() * d.y())) / 2.0;
    if (half <= 1e-9) return 0.0;
    return std::clamp((QPointF::dotProduct(p - bounds.center(), d) / half + 1.0) / 2.0, 0.0, 1.0);
}

// Coverage of `path` (title pixels, drawn through `m`) in a layer of `area`'s size whose top-left is `area`'s.
QImage maskOf(const QPainterPath& path, const QTransform& m, const QRect& area, bool antiAliased) {
    QImage mask(area.size(), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    if (path.isEmpty() || area.isEmpty()) return mask;
    QPainter p(&mask);
    p.setRenderHint(QPainter::Antialiasing, antiAliased);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.setTransform(m * QTransform::fromTranslate(-area.left(), -area.top()));
    p.drawPath(path);
    return mask;
}

// Paints a coverage mask in place with a paint's colour and transparency, each perhaps blended across `bounds`
// (picture pixels; the mask's top-left at `area`'s).
void coat(QImage& mask, const QPoint& area, const model::ClassicPaint& paint, const QRectF& bounds) {
    const QColor c1 = colorFrom(paint.color);
    const QColor c2 = paint.color2.isEmpty() ? c1 : colorFrom(paint.color2);
    const double o1 = 1.0 - std::clamp(paint.transparency, 0.0, 100.0) / 100.0;
    const double o2 = paint.transparency2 ? 1.0 - std::clamp(*paint.transparency2, 0.0, 100.0) / 100.0 : o1;
    const bool colorBlend = c1 != c2, opacityBlend = o1 != o2;
    for (int y = 0; y < mask.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(mask.scanLine(y));
        for (int x = 0; x < mask.width(); ++x) {
            const int cover = qAlpha(line[x]);
            if (cover == 0) continue;
            const QPointF at(area.x() + x + 0.5, area.y() + y + 0.5);
            const double t = colorBlend ? blendAt(at, bounds, paint.colorAngle) : 0.0;
            const double u = opacityBlend ? blendAt(at, bounds, paint.transparencyAngle) : 0.0;
            const double alpha = (o1 + (o2 - o1) * u) * cover / 255.0;
            auto channel = [&](float a, float b) {
                const double v = static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * t;
                return std::clamp(static_cast<int>(std::lround(v * alpha * 255.0)), 0, 255);
            };
            line[x] = qRgba(channel(c1.redF(), c2.redF()), channel(c1.greenF(), c2.greenF()), channel(c1.blueF(), c2.blueF()),
                            std::clamp(static_cast<int>(std::lround(alpha * 255.0)), 0, 255));
        }
    }
}

// A line's arrowhead at `tip`, pointing along `u` (a unit vector), `length` long and `width` wide.
QPolygonF arrowHead(const QPointF& tip, const QPointF& u, double length, double width) {
    const QPointF n(-u.y(), u.x());
    const QPointF base = tip - u * length;
    return QPolygonF{tip, base + n * (width / 2.0), base - n * (width / 2.0), tip};
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

QPainterPath classicObjectShape(const model::ClassicObject& o) {
    QPainterPath path;
    switch (o.kind) {
    case model::ClassicKind::Text: {
        if (o.text.isEmpty()) return path;
        const model::TitleObject t = unplacedText(o);
        path = titleTextPath(t).translated(originOf(o, t));
        break;
    }
    case model::ClassicKind::Box: {
        const QRectF r = o.rect.normalized();
        const double radius = std::clamp(o.cornerRadius, 0.0, std::min(r.width(), r.height()) / 2.0);
        if (radius > 0.0) path.addRoundedRect(r, radius, radius);
        else path.addRect(r);
        break;
    }
    case model::ClassicKind::Oval: path.addEllipse(o.rect.normalized()); break;
    case model::ClassicKind::Line: {
        QPointF a = o.rect.topLeft(), b = o.rect.bottomRight();
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        const double width = std::max(0.5, o.lineWidth);
        if (length <= 1e-9) return path;
        const QPointF u = (b - a) / length;
        const bool atEnd = o.arrows == model::ClassicArrows::End || o.arrows == model::ClassicArrows::Both;
        const bool atStart = o.arrows == model::ClassicArrows::Start || o.arrows == model::ClassicArrows::Both;
        // The arrowheads' tips at the line's ends, the line itself stopping at their bases.
        const double head = std::min(std::max(0.0, o.arrowSize.width()) * width, length / ((atEnd && atStart) ? 2.0 : 1.0));
        const double across = std::max(width, std::max(0.0, o.arrowSize.height()) * width);
        if (atEnd) path.addPolygon(arrowHead(b, u, head, across));
        if (atStart) path.addPolygon(arrowHead(a, -u, head, across));
        const QPointF from = atStart ? a + u * head : a, to = atEnd ? b - u * head : b;
        const QPointF n(-u.y() * width / 2.0, u.x() * width / 2.0);
        if (QLineF(from, to).length() > 1e-9 || !(atEnd || atStart)) path.addPolygon(QPolygonF{from + n, to + n, to - n, from - n, from + n});
        path.setFillRule(Qt::WindingFill);
        break;
    }
    }
    return path;
}

QPainterPath classicBorderShape(const model::ClassicObject& o) {
    if (o.kind == model::ClassicKind::Line || o.borderWidth <= 0.0) return {};
    const QPainterPath shape = classicObjectShape(o);
    if (shape.isEmpty()) return {};
    // Outside the shape (ours: "a border outline", p.347, round the edge, as Titler+'s Outer outline).
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * o.borderWidth);
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    QPainterPath ring = stroker.createStroke(shape);
    ring.setFillRule(Qt::WindingFill);
    return ring;
}

QImage classicTitleImage(const model::ClassicTitle& title, const QSize& size, bool antiAliased, const QPointF& origin) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (size.isEmpty()) return image;
    const QTransform m = QTransform::fromTranslate(-origin.x(), -origin.y()) * classicToPicture(size);
    const double scale = size.height() / model::kClassicHeight;
    QPainter canvas(&image);
    for (const model::ClassicObject& o : title.objects) {
        const QPainterPath shape = classicObjectShape(o);
        if (shape.isEmpty()) continue;
        const QPainterPath ring = classicBorderShape(o);
        const QRectF shapeBounds = m.mapRect(shape.boundingRect());
        const QRectF ringBounds = ring.isEmpty() ? shapeBounds : m.mapRect(ring.boundingRect()).united(shapeBounds);
        // The layers' area: the object, its border and its shadow with its softening, within the picture.
        const bool shadowed = o.shadowDepth > 0.0 || o.shadowSoftness > 0;
        const QPointF offset = o.shadowOffset() * scale;
        const double blur = o.shadowSoftness * scale;
        QRectF reach = ringBounds;
        if (shadowed) reach = reach.united(ringBounds.translated(offset)).adjusted(-3 * blur - 2, -3 * blur - 2, 3 * blur + 2, 3 * blur + 2);
        const QRect area = reach.toAlignedRect().adjusted(-1, -1, 1, 1) & image.rect();
        if (area.isEmpty()) continue;
        QImage fill = maskOf(shape, m, area, antiAliased);
        QImage border;
        if (!ring.isEmpty()) {
            // The ring outside the fill's own coverage.
            border = maskOf(ring, m, area, antiAliased);
            QPainter p(&border);
            p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
            p.drawImage(0, 0, fill);
        }
        if (shadowed) {
            // "A depth shadow extends from the edges of the object, as though the object were three-dimensional. A drop
            // shadow is offset from the object itself" (p.353): the silhouette drawn along the way to the offset, or once
            // there; then softened. With no depth and some softness, a glow (p.355).
            QImage silhouette = fill;
            if (!border.isNull()) {
                QPainter p(&silhouette);
                p.drawImage(0, 0, border);
            }
            QImage shadow(area.size(), QImage::Format_ARGB32_Premultiplied);
            shadow.fill(Qt::transparent);
            {
                QPainter p(&shadow);
                const double length = std::hypot(offset.x(), offset.y());
                const int steps = o.depthShadow ? std::max(1, static_cast<int>(std::ceil(length * 2.0))) : 1;
                for (int k = o.depthShadow ? 1 : steps; k <= steps; ++k) p.drawImage(offset * (static_cast<double>(k) / steps), silhouette);
            }
            model::ClassicPaint paint;
            paint.color = o.shadowColor;
            paint.transparency = o.shadowTransparency;
            coat(shadow, area.topLeft(), paint, ringBounds);
            softenImage(shadow, blur);
            canvas.drawImage(area.topLeft(), shadow);
        }
        if (!border.isNull()) {
            coat(border, area.topLeft(), o.border, ringBounds);
            canvas.drawImage(area.topLeft(), border);
        }
        coat(fill, area.topLeft(), o.fill, shapeBounds);
        canvas.drawImage(area.topLeft(), fill);
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

// ---------------------------------------------------------------- pages

namespace {

// Along the title's motion: an object's start and end, and a page's length.
std::pair<double, double> span(const model::ClassicTitle& t, const model::ClassicObject& o) {
    const QRectF b = classicObjectBounds(o);
    return t.motion == model::ClassicMotion::Crawling ? std::pair{b.left(), b.right()} : std::pair{b.top(), b.bottom()};
}

double pageLength(const model::ClassicTitle& t) {
    return t.motion == model::ClassicMotion::Crawling ? model::kClassicWidth : model::kClassicHeight;
}

void shift(model::ClassicObject& o, const model::ClassicTitle& t, double by) {
    if (t.motion == model::ClassicMotion::Crawling) o.rect.translate(by, 0.0);
    else o.rect.translate(0.0, by);
}

} // namespace

int classicContentPages(const model::ClassicTitle& title) {
    if (title.motion == model::ClassicMotion::Still) return 1;
    double end = 0.0;
    for (const model::ClassicObject& o : title.objects) end = std::max(end, span(title, o).second);
    // An end on a page's very edge belongs to the page before.
    return std::max(1, static_cast<int>(std::ceil(end / pageLength(title) - 1e-9)));
}

bool insertClassicPage(model::ClassicTitle& title, int page, int lines) {
    if (title.motion == model::ClassicMotion::Still || page < 1 || page > title.pages) return false;
    if (title.pages + 1 > model::classicPageLimit(title.motion, lines)) return false;
    const double at = (page - 1) * pageLength(title);
    for (const model::ClassicObject& o : title.objects) {
        const auto [a, b] = span(title, o);
        if (a < at && b > at) return false;
    }
    for (model::ClassicObject& o : title.objects) {
        if (span(title, o).first >= at) shift(o, title, pageLength(title));
    }
    ++title.pages;
    return true;
}

bool removeClassicPage(model::ClassicTitle& title, int page) {
    if (title.motion == model::ClassicMotion::Still || page < 1 || page > title.pages || title.pages <= 1) return false;
    const double from = (page - 1) * pageLength(title), to = page * pageLength(title);
    for (const model::ClassicObject& o : title.objects) {
        const auto [a, b] = span(title, o);
        if (a < to && b > from) return false; // not empty
    }
    for (model::ClassicObject& o : title.objects) {
        if (span(title, o).first >= to) shift(o, title, -pageLength(title));
    }
    --title.pages;
    return true;
}

bool placeOnClassicPage(model::ClassicTitle& title, const QString& id, int page, bool copy, int lines) {
    const model::ClassicObject* found = title.object(id);
    if (!found || title.motion == model::ClassicMotion::Still || page < 1) return false;
    if (page > model::classicPageLimit(title.motion, lines)) return false;
    model::ClassicObject o = *found;
    const int now = model::classicPageAt(title.motion, classicObjectBounds(o).topLeft());
    shift(o, title, (page - now) * pageLength(title));
    if (copy) {
        o.id = title.newId(o.kind);
        title.objects << o;
    } else {
        *title.object(id) = o;
    }
    title.pages = std::max(title.pages, page);
    return true;
}

// ---------------------------------------------------------------- alignment

namespace {

// The units alignment moves: each group (its members) or lone object among `ids`, in the order first selected, with
// their bounds; locked objects are left out.
struct Unit {
    QStringList ids;
    QRectF bounds;
};

QList<Unit> unitsOf(const model::ClassicTitle& title, const QStringList& ids) {
    QList<Unit> units;
    QStringList seen;
    for (const QString& id : ids) {
        const model::ClassicObject* o = title.object(id);
        if (!o || seen.contains(id)) continue;
        Unit u;
        u.ids = title.withGroups({id});
        seen += u.ids;
        for (const QString& member : u.ids) {
            const model::ClassicObject* m = title.object(member);
            if (m->locked) continue;
            u.bounds = u.bounds.isNull() ? classicObjectBounds(*m) : u.bounds.united(classicObjectBounds(*m));
        }
        u.ids.erase(std::remove_if(u.ids.begin(), u.ids.end(), [&](const QString& m) { return title.object(m)->locked; }), u.ids.end());
        if (!u.ids.isEmpty()) units << u;
    }
    return units;
}

bool moveUnit(model::ClassicTitle& title, const Unit& u, const QPointF& by) {
    if (std::abs(by.x()) < 1e-9 && std::abs(by.y()) < 1e-9) return false;
    for (const QString& id : u.ids) title.object(id)->rect.translate(by);
    return true;
}

} // namespace

bool alignClassicObjects(model::ClassicTitle& title, const QStringList& ids, ClassicAlign align, const QRectF& area) {
    const QList<Unit> units = unitsOf(title, ids);
    if (units.isEmpty()) return false;
    const bool toObjects = align == ClassicAlign::ObjectsLeft || align == ClassicAlign::ObjectsRight || align == ClassicAlign::ObjectsTop ||
                           align == ClassicAlign::ObjectsBottom;
    if (toObjects && units.size() < 2) return false;
    // The reference: the safe title area, or the unit of the last object selected.
    const QRectF ref = toObjects ? units.last().bounds : area;
    bool any = false;
    for (const Unit& u : units) {
        const QRectF b = u.bounds;
        QPointF by;
        switch (align) {
        case ClassicAlign::FrameLeft:
        case ClassicAlign::ObjectsLeft: by.setX(ref.left() - b.left()); break;
        case ClassicAlign::FrameRight:
        case ClassicAlign::ObjectsRight: by.setX(ref.right() - b.right()); break;
        case ClassicAlign::FrameTop:
        case ClassicAlign::ObjectsTop: by.setY(ref.top() - b.top()); break;
        case ClassicAlign::FrameBottom:
        case ClassicAlign::ObjectsBottom: by.setY(ref.bottom() - b.bottom()); break;
        case ClassicAlign::FrameCenterH: by.setX(ref.center().x() - b.center().x()); break;
        case ClassicAlign::FrameCenterV: by.setY(ref.center().y() - b.center().y()); break;
        }
        any = moveUnit(title, u, by) || any;
    }
    return any;
}

bool distributeClassicObjects(model::ClassicTitle& title, const QStringList& ids, ClassicDistribute how, const QRectF& area) {
    QList<Unit> units = unitsOf(title, ids);
    if (units.size() < 3) return false;
    bool any = false;
    if (how == ClassicDistribute::FirstToLast) {
        const QPointF from = units.first().bounds.center(), to = units.last().bounds.center();
        const auto n = static_cast<double>(units.size() - 1);
        for (qsizetype i = 1; i + 1 < units.size(); ++i) {
            const QPointF want = from + (to - from) * (static_cast<double>(i) / n);
            any = moveUnit(title, units.at(i), want - units.at(i).bounds.center()) || any;
        }
        return any;
    }
    const bool across = how == ClassicDistribute::LeftToRight;
    auto start = [&](const QRectF& r) { return across ? r.left() : r.top(); };
    auto length = [&](const QRectF& r) { return across ? r.width() : r.height(); };
    std::stable_sort(units.begin(), units.end(), [&](const Unit& a, const Unit& b) { return start(a.bounds) < start(b.bounds); });
    double total = 0.0;
    for (const Unit& u : units) total += length(u.bounds);
    const double gap = ((across ? area.width() : area.height()) - total) / static_cast<double>(units.size() - 1);
    double at = across ? area.left() : area.top();
    for (const Unit& u : units) {
        const double by = at - start(u.bounds);
        any = moveUnit(title, u, across ? QPointF(by, 0.0) : QPointF(0.0, by)) || any;
        at += length(u.bounds) + gap;
    }
    return any;
}

bool makeClassicCrawl(model::ClassicObject& object) {
    if (object.kind != model::ClassicKind::Text || object.text.isEmpty()) return false;
    const model::ClassicObject before = object;
    object.text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    // As wide as the one line: laid out without wrapping, then the box made that wide.
    object.rect.setWidth(1.0e7);
    double right = object.rect.left();
    for (const QPolygonF& p : classicTextRange(object, 0, object.text.size())) right = std::max(right, p.boundingRect().right());
    object.rect.setWidth(std::max(10.0, right - object.rect.left() + 2.0));
    object.rect.moveTop(std::floor(model::kClassicHeight * 2.0 / 3.0));
    return !(object == before);
}

} // namespace lumenis::media
