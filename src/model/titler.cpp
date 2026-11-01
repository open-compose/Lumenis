// Avid Titler+ titles (titler.h; FX Guide p.302-324; docs/spec/titler.md): the JSON a Titler+ effect keeps under
// "Titler", object lookup and layer order, groups, and applying the effect to segments.
#include "lumenis/model/titler.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <functional>

namespace lumenis::model {

namespace {

const QString kKey = QStringLiteral("Titler");

// ---- JSON (only what differs from the defaults is written, ours, to keep effects small)

QString colorFrom(const QJsonValue& v, const QString& fallback) {
    const QString c = v.toString();
    return c.startsWith(QLatin1Char('#')) ? c : fallback;
}

QJsonObject colorJson(const TitleColor& c) {
    QJsonObject o{{QStringLiteral("color"), c.color}};
    // The corners are kept even while the gradient is off, so turning it on again finds them.
    {
        if (c.gradient) o.insert(QStringLiteral("gradient"), true);
        o.insert(QStringLiteral("topLeft"), c.topLeft);
        o.insert(QStringLiteral("topRight"), c.topRight);
        o.insert(QStringLiteral("bottomLeft"), c.bottomLeft);
        o.insert(QStringLiteral("bottomRight"), c.bottomRight);
    }
    return o;
}

TitleColor colorFromJson(const QJsonValue& v, const TitleColor& fallback) {
    if (!v.isObject()) return fallback;
    const QJsonObject o = v.toObject();
    TitleColor c;
    c.color = colorFrom(o.value(QStringLiteral("color")), fallback.color);
    c.gradient = o.value(QStringLiteral("gradient")).toBool();
    c.topLeft = colorFrom(o.value(QStringLiteral("topLeft")), fallback.topLeft);
    c.topRight = colorFrom(o.value(QStringLiteral("topRight")), fallback.topRight);
    c.bottomLeft = colorFrom(o.value(QStringLiteral("bottomLeft")), fallback.bottomLeft);
    c.bottomRight = colorFrom(o.value(QStringLiteral("bottomRight")), fallback.bottomRight);
    return c;
}

template <typename E>
QString enumName(E value, std::initializer_list<const char*> names) {
    return QLatin1String(*(names.begin() + static_cast<int>(value)));
}

template <typename E>
E enumFrom(const QJsonValue& v, std::initializer_list<const char*> names, E fallback) {
    int i = 0;
    for (const char* n : names) {
        if (v.toString() == QLatin1String(n)) return static_cast<E>(i);
        ++i;
    }
    return fallback;
}

constexpr std::initializer_list<const char*> kKinds = {"Text", "Rectangle", "Ellipse", "Group"};
constexpr std::initializer_list<const char*> kHorizontal = {"Left", "Center", "Right"};
constexpr std::initializer_list<const char*> kVertical = {"Top", "Center", "Bottom"};
constexpr std::initializer_list<const char*> kOrientation = {"Left To Right", "Right To Left", "Top To Bottom"};
constexpr std::initializer_list<const char*> kOutlineType = {"Outer", "Inner", "Border"};
constexpr std::initializer_list<const char*> kOutlineShape = {"Round", "Square"};

QJsonObject objectJson(const TitleObject& t) {
    QJsonObject o{{QStringLiteral("id"), t.id}, {QStringLiteral("kind"), enumName(t.kind, kKinds)}};
    if (!t.text.isEmpty()) o.insert(QStringLiteral("text"), t.text);
    if (!t.size.isEmpty()) o.insert(QStringLiteral("size"), QJsonArray{t.size.width(), t.size.height()});
    if (t.kind == TitleObjectKind::Text) {
        const TitleFont& f = t.font;
        o.insert(QStringLiteral("font"), QJsonObject{{QStringLiteral("family"), f.family},
                                                     {QStringLiteral("style"), f.style},
                                                     {QStringLiteral("size"), f.size},
                                                     {QStringLiteral("underline"), f.underline},
                                                     {QStringLiteral("horizontal"), enumName(f.horizontal, kHorizontal)},
                                                     {QStringLiteral("vertical"), enumName(f.vertical, kVertical)},
                                                     {QStringLiteral("orientation"), enumName(f.orientation, kOrientation)},
                                                     {QStringLiteral("kerning"), f.kerning},
                                                     {QStringLiteral("leading"), f.leading},
                                                     {QStringLiteral("skew"), f.skew}});
    }
    const TitleTransform& x = t.transform;
    o.insert(QStringLiteral("transform"), QJsonObject{{QStringLiteral("opacity"), x.opacity},
                                                      {QStringLiteral("position"), QJsonArray{x.position.x(), x.position.y()}},
                                                      {QStringLiteral("scale"), QJsonArray{x.scaleX, x.scaleY}},
                                                      {QStringLiteral("fixedAspect"), x.fixedAspect},
                                                      {QStringLiteral("rotation"), x.rotation},
                                                      {QStringLiteral("anchor"), QJsonArray{x.anchor.x(), x.anchor.y()}}});
    if (t.kind != TitleObjectKind::Group) {
        o.insert(QStringLiteral("fill"), QJsonObject{{QStringLiteral("on"), t.fill.on},
                                                     {QStringLiteral("color"), colorJson(t.fill.color)},
                                                     {QStringLiteral("opacity"), t.fill.opacity},
                                                     {QStringLiteral("softness"), t.fill.softness}});
        o.insert(QStringLiteral("outline"), QJsonObject{{QStringLiteral("on"), t.outline.on},
                                                        {QStringLiteral("color"), colorJson(t.outline.color)},
                                                        {QStringLiteral("opacity"), t.outline.opacity},
                                                        {QStringLiteral("size"), t.outline.size},
                                                        {QStringLiteral("softness"), t.outline.softness},
                                                        {QStringLiteral("type"), enumName(t.outline.type, kOutlineType)},
                                                        {QStringLiteral("shape"), enumName(t.outline.shape, kOutlineShape)}});
        o.insert(QStringLiteral("shadow"), QJsonObject{{QStringLiteral("on"), t.shadow.on},
                                                       {QStringLiteral("color"), colorJson(t.shadow.color)},
                                                       {QStringLiteral("opacity"), t.shadow.opacity},
                                                       {QStringLiteral("distance"), t.shadow.distance},
                                                       {QStringLiteral("angle"), t.shadow.angle},
                                                       {QStringLiteral("softness"), t.shadow.softness}});
    }
    if (!t.children.isEmpty()) {
        QJsonArray children;
        for (const TitleObject& c : t.children) children.append(objectJson(c));
        o.insert(QStringLiteral("children"), children);
    }
    return o;
}

QPointF pointFrom(const QJsonValue& v, const QPointF& fallback) {
    const QJsonArray a = v.toArray();
    return a.size() == 2 ? QPointF(a.at(0).toDouble(), a.at(1).toDouble()) : fallback;
}

TitleObject objectFrom(const QJsonObject& o) {
    TitleObject t;
    t.id = o.value(QStringLiteral("id")).toString();
    t.kind = enumFrom(o.value(QStringLiteral("kind")), kKinds, TitleObjectKind::Text);
    t.text = o.value(QStringLiteral("text")).toString();
    const QJsonArray size = o.value(QStringLiteral("size")).toArray();
    if (size.size() == 2) t.size = QSizeF(size.at(0).toDouble(), size.at(1).toDouble());
    const QJsonObject f = o.value(QStringLiteral("font")).toObject();
    const TitleFont df;
    t.font.family = f.value(QStringLiteral("family")).toString();
    t.font.style = f.value(QStringLiteral("style")).toString();
    t.font.size = f.value(QStringLiteral("size")).toDouble(df.size);
    t.font.underline = f.value(QStringLiteral("underline")).toBool();
    t.font.horizontal = enumFrom(f.value(QStringLiteral("horizontal")), kHorizontal, df.horizontal);
    t.font.vertical = enumFrom(f.value(QStringLiteral("vertical")), kVertical, df.vertical);
    t.font.orientation = enumFrom(f.value(QStringLiteral("orientation")), kOrientation, df.orientation);
    t.font.kerning = f.value(QStringLiteral("kerning")).toDouble();
    t.font.leading = f.value(QStringLiteral("leading")).toDouble();
    t.font.skew = f.value(QStringLiteral("skew")).toDouble();
    const QJsonObject x = o.value(QStringLiteral("transform")).toObject();
    const TitleTransform dx;
    t.transform.opacity = x.value(QStringLiteral("opacity")).toDouble(dx.opacity);
    t.transform.position = pointFrom(x.value(QStringLiteral("position")), dx.position);
    const QPointF scale = pointFrom(x.value(QStringLiteral("scale")), QPointF(dx.scaleX, dx.scaleY));
    t.transform.scaleX = scale.x();
    t.transform.scaleY = scale.y();
    t.transform.fixedAspect = x.value(QStringLiteral("fixedAspect")).toBool(dx.fixedAspect);
    t.transform.rotation = x.value(QStringLiteral("rotation")).toDouble();
    t.transform.anchor = pointFrom(x.value(QStringLiteral("anchor")), dx.anchor);
    const QJsonObject fill = o.value(QStringLiteral("fill")).toObject();
    const TitleFill dfill;
    t.fill.on = fill.value(QStringLiteral("on")).toBool(dfill.on);
    t.fill.color = colorFromJson(fill.value(QStringLiteral("color")), dfill.color);
    t.fill.opacity = fill.value(QStringLiteral("opacity")).toDouble(dfill.opacity);
    t.fill.softness = fill.value(QStringLiteral("softness")).toDouble(dfill.softness);
    const QJsonObject line = o.value(QStringLiteral("outline")).toObject();
    const TitleOutline dline;
    t.outline.on = line.value(QStringLiteral("on")).toBool(dline.on);
    t.outline.color = colorFromJson(line.value(QStringLiteral("color")), dline.color);
    t.outline.opacity = line.value(QStringLiteral("opacity")).toDouble(dline.opacity);
    t.outline.size = line.value(QStringLiteral("size")).toDouble(dline.size);
    t.outline.softness = line.value(QStringLiteral("softness")).toDouble(dline.softness);
    t.outline.type = enumFrom(line.value(QStringLiteral("type")), kOutlineType, dline.type);
    t.outline.shape = enumFrom(line.value(QStringLiteral("shape")), kOutlineShape, dline.shape);
    const QJsonObject shadow = o.value(QStringLiteral("shadow")).toObject();
    const TitleShadow dshadow;
    t.shadow.on = shadow.value(QStringLiteral("on")).toBool(dshadow.on);
    t.shadow.color = colorFromJson(shadow.value(QStringLiteral("color")), dshadow.color);
    t.shadow.opacity = shadow.value(QStringLiteral("opacity")).toDouble(dshadow.opacity);
    t.shadow.distance = shadow.value(QStringLiteral("distance")).toDouble(dshadow.distance);
    t.shadow.angle = shadow.value(QStringLiteral("angle")).toDouble(dshadow.angle);
    t.shadow.softness = shadow.value(QStringLiteral("softness")).toDouble(dshadow.softness);
    for (const QJsonValue& c : o.value(QStringLiteral("children")).toArray()) t.children << objectFrom(c.toObject());
    return t;
}

// ---- lookup

const TitleObject* find(const QList<TitleObject>& list, const QString& id) {
    for (const TitleObject& o : list) {
        if (o.id == id) return &o;
        if (const TitleObject* inner = find(o.children, id)) return inner;
    }
    return nullptr;
}

// The list holding `id` and its index there.
std::optional<std::pair<QList<TitleObject>*, qsizetype>> place(QList<TitleObject>& list, const QString& id) {
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (list.at(i).id == id) return std::pair{&list, i};
        if (const auto inner = place(list[i].children, id)) return inner;
    }
    return std::nullopt;
}

void collectIds(const QList<TitleObject>& list, QStringList& ids) {
    for (const TitleObject& o : list) {
        ids << o.id;
        collectIds(o.children, ids);
    }
}

} // namespace

bool TitleObject::operator==(const TitleObject& o) const {
    return id == o.id && kind == o.kind && text == o.text && size == o.size && font == o.font && transform == o.transform && fill == o.fill &&
           outline == o.outline && shadow == o.shadow && children == o.children;
}

bool isTitlerEffect(const Effect& effect) {
    return effect.category == QLatin1String(kTitlerCategory) && effect.name == QLatin1String(kTitlerName);
}

const TitleObject* Title::object(const QString& id) const {
    return find(objects, id);
}

TitleObject* Title::object(const QString& id) {
    // Searched without const, so that the lists detach: a pointer into data shared with another Title's copy (QList
    // is implicitly shared) would change that copy too.
    std::function<TitleObject*(QList<TitleObject>&)> search = [&](QList<TitleObject>& list) -> TitleObject* {
        for (TitleObject& o : list) {
            if (o.id == id) return &o;
            if (TitleObject* inner = search(o.children)) return inner;
        }
        return nullptr;
    };
    return search(objects);
}

QString Title::newId(TitleObjectKind kind) const {
    QStringList ids;
    collectIds(objects, ids);
    const QString base = enumName(kind, kKinds);
    for (int n = 1;; ++n) {
        const QString id = QStringLiteral("%1 %2").arg(base).arg(n);
        if (!ids.contains(id)) return id;
    }
}

QString Title::topText() const {
    // From the front: the first text object found, groups searched front to back too.
    std::function<QString(const QList<TitleObject>&)> top = [&](const QList<TitleObject>& list) -> QString {
        for (auto it = list.crbegin(); it != list.crend(); ++it) {
            if (it->kind == TitleObjectKind::Text && !it->text.trimmed().isEmpty()) return it->text.section(QLatin1Char('\n'), 0, 0).trimmed();
            const QString inner = top(it->children);
            if (!inner.isEmpty()) return inner;
        }
        return {};
    };
    return top(objects);
}

Title titleOf(const Effect& effect) {
    const QJsonObject o = effect.parameters.value(kKey).toObject();
    Title t;
    for (const QJsonValue& v : o.value(QStringLiteral("objects")).toArray()) t.objects << objectFrom(v.toObject());
    const QJsonObject bg = o.value(QStringLiteral("background")).toObject();
    t.background.on = bg.value(QStringLiteral("on")).toBool();
    t.background.color = colorFromJson(bg.value(QStringLiteral("color")), t.background.color);
    t.background.opacity = bg.value(QStringLiteral("opacity")).toDouble(100.0);
    const QJsonObject fg = o.value(QStringLiteral("foreground")).toObject();
    t.foreground.fillOverlay = fg.value(QStringLiteral("fillOverlay")).toDouble();
    t.foreground.opacity = fg.value(QStringLiteral("opacity")).toDouble(100.0);
    t.foreground.cropTop = fg.value(QStringLiteral("cropTop")).toDouble();
    t.foreground.cropBottom = fg.value(QStringLiteral("cropBottom")).toDouble();
    t.foreground.cropLeft = fg.value(QStringLiteral("cropLeft")).toDouble();
    t.foreground.cropRight = fg.value(QStringLiteral("cropRight")).toDouble();
    return t;
}

void setTitle(Effect& effect, const Title& title) {
    // The keyframes of objects that are gone go too.
    {
        QStringList ids;
        collectIds(title.objects, ids);
        QJsonObject keys = effect.parameters.value(QStringLiteral("_keyframes")).toObject();
        bool pruned = false;
        for (const QString& k : keys.keys()) {
            if (!k.startsWith(kKey + QLatin1Char(':'))) continue;
            const QString id = k.section(QLatin1Char('/'), 0, 0).mid(kKey.size() + 1);
            if (!ids.contains(id)) {
                keys.remove(k);
                pruned = true;
            }
        }
        if (pruned) effect.parameters.insert(QStringLiteral("_keyframes"), keys);
    }
    QJsonArray objects;
    for (const TitleObject& o : title.objects) objects.append(objectJson(o));
    const TitleForeground& fg = title.foreground;
    effect.parameters.insert(kKey, QJsonObject{{QStringLiteral("objects"), objects},
                                               {QStringLiteral("background"), QJsonObject{{QStringLiteral("on"), title.background.on},
                                                                                          {QStringLiteral("color"), colorJson(title.background.color)},
                                                                                          {QStringLiteral("opacity"), title.background.opacity}}},
                                               {QStringLiteral("foreground"), QJsonObject{{QStringLiteral("fillOverlay"), fg.fillOverlay},
                                                                                          {QStringLiteral("opacity"), fg.opacity},
                                                                                          {QStringLiteral("cropTop"), fg.cropTop},
                                                                                          {QStringLiteral("cropBottom"), fg.cropBottom},
                                                                                          {QStringLiteral("cropLeft"), fg.cropLeft},
                                                                                          {QStringLiteral("cropRight"), fg.cropRight}}}});
}

Effect titlerEffect() {
    Effect e{QLatin1String(kTitlerCategory), QLatin1String(kTitlerName), {}};
    setTitle(e, Title{});
    return e;
}

TitleObject textObject(const Title& title, const QString& text, const QPointF& position) {
    TitleObject o;
    o.kind = TitleObjectKind::Text;
    o.id = title.newId(o.kind);
    o.text = text;
    o.transform.position = position;
    return o;
}

TitleObject shapeObject(const Title& title, TitleObjectKind kind, const QPointF& position, const QSizeF& size) {
    TitleObject o;
    o.kind = kind == TitleObjectKind::Ellipse ? TitleObjectKind::Ellipse : TitleObjectKind::Rectangle;
    o.id = title.newId(o.kind);
    o.size = size;
    o.transform.position = position;
    return o;
}

bool bringForward(Title& title, const QString& id) {
    const auto at = place(title.objects, id);
    if (!at || at->second + 1 >= at->first->size()) return false;
    at->first->swapItemsAt(at->second, at->second + 1);
    return true;
}

bool sendBackward(Title& title, const QString& id) {
    const auto at = place(title.objects, id);
    if (!at || at->second == 0) return false;
    at->first->swapItemsAt(at->second, at->second - 1);
    return true;
}

std::optional<QString> groupObjects(Title& title, const QStringList& ids) {
    QList<qsizetype> indexes;
    for (const QString& id : ids) {
        const auto it = std::find_if(title.objects.cbegin(), title.objects.cend(), [&](const TitleObject& o) { return o.id == id; });
        if (it == title.objects.cend()) return std::nullopt;
        indexes << (it - title.objects.cbegin());
    }
    if (indexes.size() < 2) return std::nullopt;
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    TitleObject group;
    group.kind = TitleObjectKind::Group;
    group.id = title.newId(group.kind);
    for (qsizetype i : indexes) group.children << title.objects.at(i);
    // Where the topmost of them was.
    const qsizetype top = indexes.last() - (indexes.size() - 1);
    for (auto it = indexes.crbegin(); it != indexes.crend(); ++it) title.objects.removeAt(*it);
    title.objects.insert(top, group);
    return group.id;
}

bool ungroupObject(Title& title, const QString& id) {
    const auto at = place(title.objects, id);
    if (!at) return false;
    const TitleObject group = at->first->at(at->second);
    if (group.kind != TitleObjectKind::Group) return false;
    at->first->removeAt(at->second);
    for (qsizetype k = 0; k < group.children.size(); ++k) at->first->insert(at->second + k, group.children.at(k));
    return true;
}

bool removeObject(Title& title, const QString& id) {
    const auto at = place(title.objects, id);
    if (!at) return false;
    at->first->removeAt(at->second);
    return true;
}

// ---------------------------------------------------------------- keyframes

namespace {

// The keyframeable numbers and where they are kept.
struct TitleNumber {
    const char* name;
    double (*get)(const TitleObject&);
    void (*set)(TitleObject&, double);
    bool textOnly;
};

constexpr TitleNumber kObjectNumbers[] = {
    {"Opacity", [](const TitleObject& o) { return o.transform.opacity; }, [](TitleObject& o, double v) { o.transform.opacity = v; }, false},
    {"Position X", [](const TitleObject& o) { return o.transform.position.x(); }, [](TitleObject& o, double v) { o.transform.position.setX(v); }, false},
    {"Position Y", [](const TitleObject& o) { return o.transform.position.y(); }, [](TitleObject& o, double v) { o.transform.position.setY(v); }, false},
    {"Scale X", [](const TitleObject& o) { return o.transform.scaleX; }, [](TitleObject& o, double v) { o.transform.scaleX = v; }, false},
    {"Scale Y", [](const TitleObject& o) { return o.transform.scaleY; }, [](TitleObject& o, double v) { o.transform.scaleY = v; }, false},
    {"Rotation", [](const TitleObject& o) { return o.transform.rotation; }, [](TitleObject& o, double v) { o.transform.rotation = v; }, false},
    {"Anchor X", [](const TitleObject& o) { return o.transform.anchor.x(); }, [](TitleObject& o, double v) { o.transform.anchor.setX(v); }, false},
    {"Anchor Y", [](const TitleObject& o) { return o.transform.anchor.y(); }, [](TitleObject& o, double v) { o.transform.anchor.setY(v); }, false},
    {"Fill Opacity", [](const TitleObject& o) { return o.fill.opacity; }, [](TitleObject& o, double v) { o.fill.opacity = v; }, false},
    {"Fill Softness", [](const TitleObject& o) { return o.fill.softness; }, [](TitleObject& o, double v) { o.fill.softness = v; }, false},
    {"Outline Opacity", [](const TitleObject& o) { return o.outline.opacity; }, [](TitleObject& o, double v) { o.outline.opacity = v; }, false},
    {"Outline Size", [](const TitleObject& o) { return o.outline.size; }, [](TitleObject& o, double v) { o.outline.size = v; }, false},
    {"Outline Softness", [](const TitleObject& o) { return o.outline.softness; }, [](TitleObject& o, double v) { o.outline.softness = v; }, false},
    {"Shadow Opacity", [](const TitleObject& o) { return o.shadow.opacity; }, [](TitleObject& o, double v) { o.shadow.opacity = v; }, false},
    {"Shadow Distance", [](const TitleObject& o) { return o.shadow.distance; }, [](TitleObject& o, double v) { o.shadow.distance = v; }, false},
    {"Shadow Angle", [](const TitleObject& o) { return o.shadow.angle; }, [](TitleObject& o, double v) { o.shadow.angle = v; }, false},
    {"Shadow Softness", [](const TitleObject& o) { return o.shadow.softness; }, [](TitleObject& o, double v) { o.shadow.softness = v; }, false},
    {"Size", [](const TitleObject& o) { return o.font.size; }, [](TitleObject& o, double v) { o.font.size = v; }, true},
    {"Kerning", [](const TitleObject& o) { return o.font.kerning; }, [](TitleObject& o, double v) { o.font.kerning = v; }, true},
    {"Leading", [](const TitleObject& o) { return o.font.leading; }, [](TitleObject& o, double v) { o.font.leading = v; }, true},
    {"Skew", [](const TitleObject& o) { return o.font.skew; }, [](TitleObject& o, double v) { o.font.skew = v; }, true},
};

struct TitleWideNumber {
    const char* name;
    double (*get)(const Title&);
    void (*set)(Title&, double);
};

constexpr TitleWideNumber kTitleNumbers[] = {
    {"Background Opacity", [](const Title& t) { return t.background.opacity; }, [](Title& t, double v) { t.background.opacity = v; }},
    {"Foreground Opacity", [](const Title& t) { return t.foreground.opacity; }, [](Title& t, double v) { t.foreground.opacity = v; }},
    {"Fill Overlay", [](const Title& t) { return t.foreground.fillOverlay; }, [](Title& t, double v) { t.foreground.fillOverlay = v; }},
    {"Crop Top", [](const Title& t) { return t.foreground.cropTop; }, [](Title& t, double v) { t.foreground.cropTop = v; }},
    {"Crop Bottom", [](const Title& t) { return t.foreground.cropBottom; }, [](Title& t, double v) { t.foreground.cropBottom = v; }},
    {"Crop Left", [](const Title& t) { return t.foreground.cropLeft; }, [](Title& t, double v) { t.foreground.cropLeft = v; }},
    {"Crop Right", [](const Title& t) { return t.foreground.cropRight; }, [](Title& t, double v) { t.foreground.cropRight = v; }},
};

// Every (object id, name) pair a title has, the title's own with an empty id.
QList<std::pair<QString, QString>> allNumbers(const Title& title) {
    QList<std::pair<QString, QString>> out;
    for (const QString& n : titleParameters()) out << std::pair{QString(), n};
    std::function<void(const QList<TitleObject>&)> walk = [&](const QList<TitleObject>& list) {
        for (const TitleObject& o : list) {
            for (const QString& n : titleParameters(o.kind)) out << std::pair{o.id, n};
            walk(o.children);
        }
    };
    walk(title.objects);
    return out;
}

} // namespace

QString titleCategory(const QString& objectId) {
    return objectId.isEmpty() ? kKey : kKey + QLatin1Char(':') + objectId;
}

QStringList titleParameters(TitleObjectKind kind) {
    QStringList out;
    for (const TitleNumber& n : kObjectNumbers) {
        if (!n.textOnly || kind == TitleObjectKind::Text) out << QLatin1String(n.name);
    }
    return out;
}

QStringList titleParameters() {
    QStringList out;
    for (const TitleWideNumber& n : kTitleNumbers) out << QLatin1String(n.name);
    return out;
}

std::optional<double> titleValue(const Title& title, const QString& objectId, const QString& name) {
    if (objectId.isEmpty()) {
        for (const TitleWideNumber& n : kTitleNumbers) {
            if (name == QLatin1String(n.name)) return n.get(title);
        }
        return std::nullopt;
    }
    const TitleObject* o = title.object(objectId);
    if (!o) return std::nullopt;
    for (const TitleNumber& n : kObjectNumbers) {
        if (name == QLatin1String(n.name)) return n.get(*o);
    }
    return std::nullopt;
}

bool setTitleValue(Title& title, const QString& objectId, const QString& name, double value) {
    if (objectId.isEmpty()) {
        for (const TitleWideNumber& n : kTitleNumbers) {
            if (name == QLatin1String(n.name)) {
                n.set(title, value);
                return true;
            }
        }
        return false;
    }
    TitleObject* o = title.object(objectId);
    if (!o) return false;
    for (const TitleNumber& n : kObjectNumbers) {
        if (name == QLatin1String(n.name)) {
            n.set(*o, value);
            return true;
        }
    }
    return false;
}

Title titleAt(const Effect& effect, double t, const EffectSpan& span) {
    Title title = titleOf(effect);
    for (const auto& [id, name] : allNumbers(title)) {
        if (keyframesOf(effect, titleCategory(id), name).keys.isEmpty()) continue;
        if (const auto v = numberAt(effect, titleCategory(id), name, t, span)) setTitleValue(title, id, name, *v);
    }
    return title;
}

void storeTitleAt(Effect& effect, const Title& shown, double t, const EffectSpan& span) {
    const Title before = titleAt(effect, t, span);
    setTitle(effect, shown);
    for (const auto& [id, name] : allNumbers(shown)) {
        const ParameterKeyframes keys = keyframesOf(effect, titleCategory(id), name);
        if (keys.keys.isEmpty()) continue;
        const auto now = titleValue(shown, id, name);
        const auto was = titleValue(before, id, name);
        if (now && (!was || std::abs(*now - *was) > 1e-9)) setKeyframe(effect, titleCategory(id), name, t, span, *now);
    }
}

bool addTitleKeyframes(Effect& effect, const QStringList& ids, double t, const EffectSpan& span) {
    const Title at = titleAt(effect, t, span);
    const Effect before = effect;
    for (const QString& id : ids.isEmpty() ? QStringList{QString()} : ids) {
        const TitleObject* o = id.isEmpty() ? nullptr : at.object(id);
        if (!id.isEmpty() && !o) continue;
        for (const QString& name : id.isEmpty() ? titleParameters() : titleParameters(o->kind)) {
            const auto v = titleValue(at, id, name);
            if (!v) continue;
            ParameterKeyframes keys = keyframesOf(effect, titleCategory(id), name);
            const bool first = keys.keys.isEmpty();
            setKeyframe(effect, titleCategory(id), name, t, span, *v);
            if (first) {
                // Fixed, as the title's keyframes always are (titleCategory's note).
                keys = keyframesOf(effect, titleCategory(id), name);
                setFixed(keys, true, span);
                setKeyframes(effect, titleCategory(id), name, keys);
            }
        }
    }
    return effect.parameters != before.parameters;
}

bool removeTitleKeyframes(Effect& effect, const QStringList& ids, double t, const EffectSpan& span) {
    bool any = false;
    const Title title = titleOf(effect);
    for (const QString& id : ids.isEmpty() ? QStringList{QString()} : ids) {
        const TitleObject* o = id.isEmpty() ? nullptr : title.object(id);
        if (!id.isEmpty() && !o) continue;
        for (const QString& name : id.isEmpty() ? titleParameters() : titleParameters(o->kind)) {
            any = removeKeyframe(effect, titleCategory(id), name, t, span) || any;
        }
    }
    return any;
}

int applyTitler(Sequence& sequence, const QString& track, std::optional<std::pair<std::int64_t, std::int64_t>> marks,
                const QList<SegmentRef>& targets) {
    QList<SegmentRef> where = targets;
    if (marks && marks->first < marks->second) {
        Track* t = sequence.track(track);
        if (!t || t->kind != TrackKind::Video || t->locked) return 0;
        // "set in and out marks in the Timeline to highlight a portion of the track": the marked part becomes its own
        // segments (filler where the track is empty or short).
        if (t->length() < marks->second) {
            Segment pad = Segment::filler(marks->second - t->length());
            t->segments << pad;
        }
        sequence.addEdit(track, marks->first);
        sequence.addEdit(track, marks->second);
        where.clear();
        std::int64_t start = 0;
        for (const Segment& s : sequence.track(track)->segments) {
            if (start >= marks->first && start < marks->second) where << SegmentRef{track, start};
            start += s.length;
        }
    }
    int applied = 0;
    for (const SegmentRef& ref : where) {
        const auto segment = segmentFor(sequence, ref);
        if (!segment) continue;
        if (applySegmentEffect(sequence, ref.track, ref.start, titlerEffect(), segment->effect.has_value())) ++applied;
    }
    return applied;
}

} // namespace lumenis::model
