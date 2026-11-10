// Font replacement for classic titles (font_replacement.h; Editing Guide p.993-994).
#include "lumenis/model/font_replacement.h"

namespace lumenis::model {

QJsonObject fontMapToJson(const FontMap& map) {
    QJsonObject o;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) o.insert(it.key(), it.value());
    return o;
}

FontMap fontMapFromJson(const QJsonObject& o) {
    FontMap map;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        const QString to = it.value().toString();
        if (!it.key().isEmpty() && !to.isEmpty()) map.insert(it.key(), to);
    }
    return map;
}

QStringList titleFamilies(const ClassicTitle& title) {
    QStringList out;
    auto add = [&out](const QString& family) {
        if (!family.isEmpty() && !out.contains(family)) out << family;
    };
    for (const ClassicObject& o : title.objects) {
        if (o.kind != ClassicKind::Text) continue;
        add(o.family);
        for (const TitleRun& r : o.runs) {
            if (r.format.family) add(*r.format.family);
        }
    }
    return out;
}

QStringList unknownFonts(const ClassicTitle& title, const QStringList& installed) {
    QStringList out;
    for (const QString& family : titleFamilies(title)) {
        if (!installed.contains(family, Qt::CaseInsensitive)) out << family;
    }
    return out;
}

bool replaceFont(ClassicTitle& title, const QString& original, const QString& replacement) {
    if (original.isEmpty() || replacement.isEmpty()) return false;
    // What the title shows now in place of `original`: itself, or an earlier replacement.
    QStringList from{original};
    if (const auto it = title.fontReplacements.constFind(original); it != title.fontReplacements.constEnd() && *it != replacement)
        from << *it;
    const ClassicTitle before = title;
    for (ClassicObject& o : title.objects) {
        if (o.kind != ClassicKind::Text) continue;
        if (from.contains(o.family)) o.family = replacement;
        for (TitleRun& r : o.runs) {
            if (r.format.family && from.contains(*r.format.family)) r.format.family = replacement;
        }
    }
    title.fontReplacements.insert(original, replacement);
    return title != before;
}

QStringList applyFontMap(ClassicTitle& title, const FontMap& map, const QStringList& installed) {
    QStringList left;
    for (const QString& family : unknownFonts(title, installed)) {
        // A mapping onto a font this system lacks too does not help.
        const auto it = map.constFind(family);
        if (it != map.constEnd() && installed.contains(*it, Qt::CaseInsensitive)) {
            replaceFont(title, family, *it);
        } else {
            left << family;
        }
    }
    return left;
}

} // namespace lumenis::model
