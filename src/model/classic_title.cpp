// The classic Title Tool's titles (classic_title.h): their JSON, lookups and the Safe Colors limit.
#include "lumenis/model/classic_title.h"

#include <QJsonArray>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lumenis::model {

namespace {

constexpr const char* kKinds[] = {"Text", "Box", "Oval", "Line"};
constexpr const char* kJustify[] = {"Left", "Center", "Right"};
constexpr const char* kMotions[] = {"Still", "Rolling", "Crawling"};
constexpr const char* kArrows[] = {"None", "End", "Start", "Both"};

template <typename E, std::size_t N>
E enumFrom(const QJsonValue& v, const char* const (&names)[N], E fallback) {
    for (std::size_t i = 0; i < N; ++i) {
        if (v.toString() == QLatin1String(names[i])) return static_cast<E>(i);
    }
    return fallback;
}

QJsonObject runJson(const TitleRun& r) {
    QJsonObject j{{QStringLiteral("from"), r.from}, {QStringLiteral("to"), r.to}};
    const TitleCharFormat& f = r.format;
    if (f.family) j.insert(QStringLiteral("family"), *f.family);
    if (f.style) j.insert(QStringLiteral("style"), *f.style);
    if (f.size) j.insert(QStringLiteral("size"), *f.size);
    if (f.kerning) j.insert(QStringLiteral("kerning"), *f.kerning);
    return j;
}

TitleRun runFrom(const QJsonObject& j) {
    TitleRun r;
    r.from = static_cast<qsizetype>(j.value(QStringLiteral("from")).toInteger());
    r.to = static_cast<qsizetype>(j.value(QStringLiteral("to")).toInteger());
    if (j.contains(QStringLiteral("family"))) r.format.family = j.value(QStringLiteral("family")).toString();
    if (j.contains(QStringLiteral("style"))) r.format.style = j.value(QStringLiteral("style")).toString();
    if (j.contains(QStringLiteral("size"))) r.format.size = j.value(QStringLiteral("size")).toDouble();
    if (j.contains(QStringLiteral("kerning"))) r.format.kerning = j.value(QStringLiteral("kerning")).toDouble();
    return r;
}

// A paint's key: `name`, or after a prefix with its first letter upper case ("borderColor").
QString paintKey(const QString& prefix, const char* name) {
    if (prefix.isEmpty()) return QLatin1String(name);
    return prefix + QChar(QLatin1Char(name[0])).toUpper() + QLatin1String(name + 1);
}

// A paint's keys, each with `prefix` ("" for the fill, which keeps the plain "color" and "transparency").
void paintJson(QJsonObject& j, const QString& prefix, const ClassicPaint& p) {
    auto key = [&](const char* name) { return paintKey(prefix, name); };
    j.insert(key("color"), p.color);
    j.insert(key("transparency"), p.transparency);
    if (!p.color2.isEmpty()) {
        j.insert(key("color2"), p.color2);
        j.insert(key("colorAngle"), p.colorAngle);
    }
    if (p.transparency2) {
        j.insert(key("transparency2"), *p.transparency2);
        j.insert(key("transparencyAngle"), p.transparencyAngle);
    }
}

ClassicPaint paintFrom(const QJsonObject& j, const QString& prefix, ClassicPaint p) {
    auto key = [&](const char* name) { return paintKey(prefix, name); };
    p.color = j.value(key("color")).toString(p.color);
    p.transparency = j.value(key("transparency")).toDouble(p.transparency);
    p.color2 = j.value(key("color2")).toString();
    p.colorAngle = j.value(key("colorAngle")).toDouble();
    if (j.contains(key("transparency2"))) p.transparency2 = j.value(key("transparency2")).toDouble();
    p.transparencyAngle = j.value(key("transparencyAngle")).toDouble();
    return p;
}

QString hex(int r, int g, int b) {
    return QStringLiteral("#%1%2%3").arg(r, 2, 16, QLatin1Char('0')).arg(g, 2, 16, QLatin1Char('0')).arg(b, 2, 16, QLatin1Char('0'));
}

} // namespace

QJsonObject ClassicTitle::toJson() const {
    QJsonArray list;
    for (const ClassicObject& o : objects) {
        QJsonObject j{{QStringLiteral("id"), o.id},
                      {QStringLiteral("kind"), QLatin1String(kKinds[static_cast<int>(o.kind)])},
                      {QStringLiteral("rect"), QJsonArray{o.rect.x(), o.rect.y(), o.rect.width(), o.rect.height()}}};
        paintJson(j, QString(), o.fill);
        if (o.kind == ClassicKind::Line) {
            j.insert(QStringLiteral("lineWidth"), o.lineWidth);
            if (o.arrows != ClassicArrows::None) {
                j.insert(QStringLiteral("arrows"), QLatin1String(kArrows[static_cast<int>(o.arrows)]));
                j.insert(QStringLiteral("arrowSize"), QJsonArray{o.arrowSize.width(), o.arrowSize.height()});
            }
        } else if (o.borderWidth > 0.0) {
            j.insert(QStringLiteral("borderWidth"), o.borderWidth);
            paintJson(j, QStringLiteral("border"), o.border);
        }
        if (!o.group.isEmpty()) j.insert(QStringLiteral("group"), o.group);
        if (o.locked) j.insert(QStringLiteral("locked"), true);
        if (o.kind == ClassicKind::Box && o.cornerRadius > 0.0) j.insert(QStringLiteral("cornerRadius"), o.cornerRadius);
        if (o.shadowDepth > 0.0 || o.shadowSoftness > 0) {
            j.insert(QStringLiteral("depthShadow"), o.depthShadow);
            j.insert(QStringLiteral("shadowDepth"), o.shadowDepth);
            j.insert(QStringLiteral("shadowAngle"), o.shadowAngle);
            j.insert(QStringLiteral("shadowSoftness"), o.shadowSoftness);
            j.insert(QStringLiteral("shadowColor"), o.shadowColor);
            j.insert(QStringLiteral("shadowTransparency"), o.shadowTransparency);
        }
        if (o.kind == ClassicKind::Text) {
            j.insert(QStringLiteral("text"), o.text);
            j.insert(QStringLiteral("family"), o.family);
            j.insert(QStringLiteral("size"), o.size);
            j.insert(QStringLiteral("bold"), o.bold);
            j.insert(QStringLiteral("italic"), o.italic);
            j.insert(QStringLiteral("justify"), QLatin1String(kJustify[static_cast<int>(o.justify)]));
            j.insert(QStringLiteral("kerning"), o.kerning);
            j.insert(QStringLiteral("leading"), o.leading);
            if (!o.runs.isEmpty()) {
                QJsonArray runs;
                for (const TitleRun& r : o.runs) runs.append(runJson(r));
                j.insert(QStringLiteral("runs"), runs);
            }
        }
        list.append(j);
    }
    return QJsonObject{{QStringLiteral("objects"), list},
                       {QStringLiteral("videoBackground"), videoBackground},
                       {QStringLiteral("backgroundColor"), backgroundColor},
                       {QStringLiteral("motion"), QLatin1String(kMotions[static_cast<int>(motion)])},
                       {QStringLiteral("pages"), pages},
                       {QStringLiteral("autoSize"), autoSize}};
}

ClassicTitle ClassicTitle::fromJson(const QJsonObject& o) {
    ClassicTitle t;
    for (const QJsonValue& v : o.value(QStringLiteral("objects")).toArray()) {
        const QJsonObject j = v.toObject();
        ClassicObject c;
        c.id = j.value(QStringLiteral("id")).toString();
        c.kind = enumFrom(j.value(QStringLiteral("kind")), kKinds, ClassicKind::Text);
        const QJsonArray r = j.value(QStringLiteral("rect")).toArray();
        if (r.size() == 4) c.rect = QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(), r.at(3).toDouble());
        c.fill = paintFrom(j, QString(), c.fill);
        c.lineWidth = j.value(QStringLiteral("lineWidth")).toDouble(c.lineWidth);
        c.arrows = enumFrom(j.value(QStringLiteral("arrows")), kArrows, ClassicArrows::None);
        if (const QJsonArray a = j.value(QStringLiteral("arrowSize")).toArray(); a.size() == 2) c.arrowSize = QSizeF(a.at(0).toDouble(), a.at(1).toDouble());
        c.borderWidth = j.value(QStringLiteral("borderWidth")).toDouble();
        c.border = paintFrom(j, QStringLiteral("border"), c.border);
        c.cornerRadius = j.value(QStringLiteral("cornerRadius")).toDouble();
        c.group = j.value(QStringLiteral("group")).toString();
        c.locked = j.value(QStringLiteral("locked")).toBool();
        c.depthShadow = j.value(QStringLiteral("depthShadow")).toBool();
        c.shadowDepth = j.value(QStringLiteral("shadowDepth")).toDouble();
        c.shadowAngle = j.value(QStringLiteral("shadowAngle")).toDouble(c.shadowAngle);
        c.shadowSoftness = j.value(QStringLiteral("shadowSoftness")).toInt();
        c.shadowColor = j.value(QStringLiteral("shadowColor")).toString(c.shadowColor);
        c.shadowTransparency = j.value(QStringLiteral("shadowTransparency")).toDouble();
        c.text = j.value(QStringLiteral("text")).toString();
        c.family = j.value(QStringLiteral("family")).toString();
        c.size = j.value(QStringLiteral("size")).toDouble(c.size);
        c.bold = j.value(QStringLiteral("bold")).toBool();
        c.italic = j.value(QStringLiteral("italic")).toBool();
        c.justify = enumFrom(j.value(QStringLiteral("justify")), kJustify, ClassicJustify::Left);
        c.kerning = j.value(QStringLiteral("kerning")).toDouble();
        c.leading = j.value(QStringLiteral("leading")).toDouble();
        for (const QJsonValue& rv : j.value(QStringLiteral("runs")).toArray()) c.runs << runFrom(rv.toObject());
        t.objects << c;
    }
    t.videoBackground = o.value(QStringLiteral("videoBackground")).toBool(true);
    t.backgroundColor = o.value(QStringLiteral("backgroundColor")).toString(t.backgroundColor);
    t.motion = enumFrom(o.value(QStringLiteral("motion")), kMotions, ClassicMotion::Still);
    t.pages = std::max(1, o.value(QStringLiteral("pages")).toInt(1));
    t.autoSize = o.value(QStringLiteral("autoSize")).toBool(true);
    return t;
}

const ClassicObject* ClassicTitle::object(const QString& id) const {
    for (const ClassicObject& o : objects) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

ClassicObject* ClassicTitle::object(const QString& id) {
    for (ClassicObject& o : objects) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

QString ClassicTitle::newId(ClassicKind kind) const {
    const QString base = QLatin1String(kKinds[static_cast<int>(kind)]);
    for (int n = 1;; ++n) {
        const QString id = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!object(id)) return id;
    }
}

QPointF ClassicObject::shadowOffset() const {
    const double a = shadowAngle * std::numbers::pi / 180.0;
    return {std::cos(a) * shadowDepth, std::sin(a) * shadowDepth};
}

void ClassicObject::setShadowOffset(const QPointF& offset) {
    shadowDepth = std::hypot(offset.x(), offset.y());
    if (shadowDepth > 1e-9) shadowAngle = std::atan2(offset.y(), offset.x()) * 180.0 / std::numbers::pi;
}

QString ClassicTitle::newGroupId() const {
    for (int n = 1;; ++n) {
        const QString id = QStringLiteral("Group %1").arg(n);
        if (std::none_of(objects.begin(), objects.end(), [&](const ClassicObject& o) { return o.group == id; })) return id;
    }
}

QStringList ClassicTitle::withGroups(const QStringList& ids) const {
    QStringList out;
    for (const QString& id : ids) {
        const ClassicObject* o = object(id);
        if (!o || out.contains(id)) continue;
        out << id;
        if (o->group.isEmpty()) continue;
        for (const ClassicObject& other : objects) {
            if (other.group == o->group && !out.contains(other.id)) out << other.id;
        }
    }
    return out;
}

bool reorderClassicObjects(ClassicTitle& title, const QStringList& ids, ClassicLayer layer) {
    const QList<ClassicObject> before = title.objects;
    auto chosen = [&](const ClassicObject& o) { return ids.contains(o.id); };
    QList<ClassicObject> picked, rest;
    for (const ClassicObject& o : title.objects) (chosen(o) ? picked : rest) << o;
    if (picked.isEmpty()) return false;
    switch (layer) {
    case ClassicLayer::ToFront: title.objects = rest + picked; break;
    case ClassicLayer::ToBack: title.objects = picked + rest; break;
    case ClassicLayer::Forward:
        // Each chosen object past the next unchosen one, the frontmost first.
        for (qsizetype i = title.objects.size() - 2; i >= 0; --i) {
            if (chosen(title.objects.at(i)) && !chosen(title.objects.at(i + 1))) title.objects.swapItemsAt(i, i + 1);
        }
        break;
    case ClassicLayer::Backward:
        for (qsizetype i = 1; i < title.objects.size(); ++i) {
            if (chosen(title.objects.at(i)) && !chosen(title.objects.at(i - 1))) title.objects.swapItemsAt(i, i - 1);
        }
        break;
    }
    return title.objects != before;
}

bool groupClassicObjects(ClassicTitle& title, const QStringList& ids) {
    const QStringList all = title.withGroups(ids);
    if (all.size() < 2) return false;
    const QString group = title.newGroupId();
    for (const QString& id : all) title.object(id)->group = group;
    return true;
}

bool ungroupClassicObjects(ClassicTitle& title, const QStringList& ids) {
    bool any = false;
    for (const QString& id : title.withGroups(ids)) {
        ClassicObject* o = title.object(id);
        any = any || !o->group.isEmpty();
        o->group.clear();
    }
    return any;
}

void keepLockedObjects(ClassicTitle& title, const ClassicTitle& before) {
    for (qsizetype i = 0; i < before.objects.size(); ++i) {
        const ClassicObject& was = before.objects.at(i);
        if (!was.locked) continue;
        ClassicObject* now = title.object(was.id);
        if (!now) {
            // Deleted: back at its place among the objects kept.
            title.objects.insert(std::min(i, title.objects.size()), was);
            continue;
        }
        const bool locked = now->locked;
        *now = was;
        now->locked = locked;
    }
}

int classicPageLimit(ClassicMotion motion, int lines) {
    if (motion == ClassicMotion::Still) return 1;
    const bool roll = motion == ClassicMotion::Rolling;
    if (lines <= 486) return roll ? 67 : 11;
    if (lines <= 576) return roll ? 56 : 11;
    if (lines <= 720) return roll ? 44 : 6;
    return roll ? 29 : 4;
}

QPointF classicPageOrigin(ClassicMotion motion, int page) {
    const double n = std::max(0, page - 1);
    if (motion == ClassicMotion::Rolling) return {0.0, n * kClassicHeight};
    if (motion == ClassicMotion::Crawling) return {n * kClassicWidth, 0.0};
    return {};
}

int classicPageAt(ClassicMotion motion, const QPointF& point) {
    if (motion == ClassicMotion::Rolling) return std::max(1, static_cast<int>(std::floor(point.y() / kClassicHeight)) + 1);
    if (motion == ClassicMotion::Crawling) return std::max(1, static_cast<int>(std::floor(point.x() / kClassicWidth)) + 1);
    return 1;
}

QString classicStyle(bool bold, bool italic) {
    if (bold && italic) return QStringLiteral("Bold Italic");
    if (bold) return QStringLiteral("Bold");
    if (italic) return QStringLiteral("Italic");
    return QStringLiteral("Regular");
}

bool styleIsBold(const QString& style) {
    return style.contains(QLatin1String("bold"), Qt::CaseInsensitive);
}

bool styleIsItalic(const QString& style) {
    return style.contains(QLatin1String("italic"), Qt::CaseInsensitive) || style.contains(QLatin1String("oblique"), Qt::CaseInsensitive);
}

QString safeColor(const QString& color) {
    bool ok = false;
    const uint rgb = color.mid(1).toUInt(&ok, 16);
    if (!color.startsWith(QLatin1Char('#')) || color.size() != 7 || !ok) return color;
    const double r = ((rgb >> 16) & 0xff) / 255.0, g = ((rgb >> 8) & 0xff) / 255.0, b = (rgb & 0xff) / 255.0;
    // In HSV terms: the value is the largest channel, the saturation its share above the smallest.
    const double hi = std::max({r, g, b}), lo = std::min({r, g, b});
    const double value = std::min(hi, 0.9);
    const double saturation = hi > 0.0 ? std::min((hi - lo) / hi, 0.75) : 0.0;
    if (hi <= 0.9 && (hi <= 0.0 || (hi - lo) / hi <= 0.75)) return color;
    // Each channel kept at its place between the smallest and the largest, the largest at `value`.
    auto channel = [&](double c) {
        const double t = hi > lo ? (c - lo) / (hi - lo) : 0.0;
        const double low = value * (1.0 - saturation);
        return static_cast<int>(std::lround((low + t * (value - low)) * 255.0));
    };
    return hex(channel(r), channel(g), channel(b));
}

} // namespace lumenis::model
