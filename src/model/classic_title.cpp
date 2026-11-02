// The classic Title Tool's titles (classic_title.h): their JSON, lookups and the Safe Colors limit.
#include "lumenis/model/classic_title.h"

#include <QJsonArray>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace lumenis::model {

namespace {

constexpr const char* kKinds[] = {"Text", "Box", "Oval", "Line"};
constexpr const char* kJustify[] = {"Left", "Center", "Right"};

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

QString hex(int r, int g, int b) {
    return QStringLiteral("#%1%2%3").arg(r, 2, 16, QLatin1Char('0')).arg(g, 2, 16, QLatin1Char('0')).arg(b, 2, 16, QLatin1Char('0'));
}

} // namespace

QJsonObject ClassicTitle::toJson() const {
    QJsonArray list;
    for (const ClassicObject& o : objects) {
        QJsonObject j{{QStringLiteral("id"), o.id},
                      {QStringLiteral("kind"), QLatin1String(kKinds[static_cast<int>(o.kind)])},
                      {QStringLiteral("rect"), QJsonArray{o.rect.x(), o.rect.y(), o.rect.width(), o.rect.height()}},
                      {QStringLiteral("color"), o.color},
                      {QStringLiteral("transparency"), o.transparency}};
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
                       {QStringLiteral("backgroundColor"), backgroundColor}};
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
        c.color = j.value(QStringLiteral("color")).toString(c.color);
        c.transparency = j.value(QStringLiteral("transparency")).toDouble();
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
