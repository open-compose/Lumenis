// Color Correction effect templates (FX Guide p.610-615): what a template keeps and how its values go into one tab. See
// lumenis/model/correction_template.h.

#include "lumenis/model/correction_template.h"


namespace lumenis::model {

namespace {

// The composition key of a Color Correction effect template (video effect templates use "videoEffect").
constexpr const char* kCorrectionKey = "colorCorrection";

bool under(const QString& key, const QString& path) {
    return key == path || key.startsWith(path + QLatin1Char('/'));
}

} // namespace

ColorCorrection CorrectionTemplate::merged() const {
    return mergedCorrection(source, program);
}

QJsonObject CorrectionTemplate::toJson() const {
    QJsonObject o;
    if (!source.isEmpty()) {
        o.insert(QStringLiteral("source"), source.toJson());
        o.insert(QStringLiteral("sourceRelationship"), relationshipKey(sourceRelationship));
    }
    if (!program.isEmpty()) {
        o.insert(QStringLiteral("program"), program.toJson());
        o.insert(QStringLiteral("programRelationship"), relationshipKey(programRelationship));
    }
    return o;
}

CorrectionTemplate CorrectionTemplate::fromJson(const QJsonObject& o) {
    CorrectionTemplate t;
    t.source = ColorCorrection::fromJson(o.value(QStringLiteral("source")).toObject());
    t.program = ColorCorrection::fromJson(o.value(QStringLiteral("program")).toObject());
    // A relationship of the other side, or none, is the side's segment relationship.
    const auto s = relationshipFromKey(o.value(QStringLiteral("sourceRelationship")).toString());
    if (s && sideOf(*s) == CorrectionSide::Source) t.sourceRelationship = *s;
    const auto p = relationshipFromKey(o.value(QStringLiteral("programRelationship")).toString());
    if (p && sideOf(*p) == CorrectionSide::Program) t.programRelationship = *p;
    return t;
}

CorrectionTemplate correctionTemplateOf(const Segment& segment) {
    CorrectionTemplate t;
    if (segment.isFiller()) return t;
    t.source = segment.sourceCorrection;
    t.program = segment.programCorrection;
    if (!t.source.isEmpty()) t.sourceRelationship = segment.sourceRelationship;
    if (!t.program.isEmpty()) t.programRelationship = segment.programRelationship;
    return t;
}

BinItem correctionTemplateItem(const CorrectionTemplate& t, const QString& name, const QDateTime& when) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::Effect;
    item.name = name.isEmpty() ? QStringLiteral("Color Correction") : name;
    item.created = when;
    item.modified = when;
    item.composition.insert(QLatin1String(kCorrectionKey), t.toJson());
    return item;
}

std::optional<CorrectionTemplate> correctionTemplateOf(const BinItem& item) {
    if (item.kind != BinItemKind::Effect || !item.composition.value(QLatin1String(kCorrectionKey)).isObject()) return std::nullopt;
    return CorrectionTemplate::fromJson(item.composition.value(QLatin1String(kCorrectionKey)).toObject());
}

bool applyCorrectionGroup(ColorCorrection& target, const ColorCorrection& from, const QString& path) {
    ColorCorrection out = target;
    for (const QString& key : target.values.keys()) {
        if (under(key, path)) out.values.remove(key);
    }
    for (auto it = from.values.begin(); it != from.values.end(); ++it) {
        if (under(it.key(), path)) out.values.insert(it.key(), it.value());
    }
    out.disabled.clear();
    for (const QString& d : target.disabled) {
        if (!under(d, path)) out.disabled << d;
    }
    for (const QString& d : from.disabled) {
        if (under(d, path) && !out.disabled.contains(d)) out.disabled << d;
    }
    if (out == target) return false;
    target = out;
    return true;
}

} // namespace lumenis::model
