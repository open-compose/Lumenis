// Custom colours in bins (custom_color.h; FX Guide p.630-631, p.608).

#include "lumenis/media/custom_color.h"

#include <QStringList>

#include <cmath>
#include <limits>

namespace lumenis::media {

SavedColorLabels savedColorLabelsFromKey(const QString& key) {
    if (key == QLatin1String("none")) return SavedColorLabels::None;
    if (key == QLatin1String("rgb")) return SavedColorLabels::Rgb;
    if (key == QLatin1String("name")) return SavedColorLabels::Name;
    return SavedColorLabels::NameAndRgb;
}

QString nearestHtmlColorName(const QColor& color) {
    QString best;
    double bestDistance = std::numeric_limits<double>::max();
    for (const QString& name : QColor::colorNames()) {
        if (name == QLatin1String("transparent")) continue;
        const QColor c = QColor::fromString(name);
        const double d = std::pow(c.red() - color.red(), 2) + std::pow(c.green() - color.green(), 2) + std::pow(c.blue() - color.blue(), 2);
        // The first of equal names (grey/gray) stays.
        if (d < bestDistance) {
            bestDistance = d;
            best = name;
        }
    }
    if (!best.isEmpty()) best[0] = best[0].toUpper();
    return best;
}

QString customColorName(const QColor& color, SavedColorLabels labels) {
    auto level = [](float v) { return static_cast<int>(std::lround(16.0 + 219.0 * static_cast<double>(v))); };
    const QString rgb = QStringLiteral("R:%1 G:%2 B:%3").arg(level(color.redF())).arg(level(color.greenF())).arg(level(color.blueF()));
    switch (labels) {
    case SavedColorLabels::None: return {};
    case SavedColorLabels::Rgb: return rgb;
    case SavedColorLabels::Name: return nearestHtmlColorName(color);
    case SavedColorLabels::NameAndRgb: return nearestHtmlColorName(color) + QLatin1Char(' ') + rgb;
    }
    return rgb;
}

model::BinItem customColorItem(const QColor& color, const QString& name, const QDateTime& when) {
    model::BinItem item;
    item.id = model::newItemId();
    item.kind = model::BinItemKind::Effect;
    item.name = name;
    item.created = when;
    item.modified = when;
    item.composition.insert(QStringLiteral("customColor"), color.name(QColor::HexRgb));
    return item;
}

std::optional<QColor> customColorOf(const model::BinItem& item) {
    if (item.kind != model::BinItemKind::Effect) return std::nullopt;
    const QColor c = QColor::fromString(item.composition.value(QStringLiteral("customColor")).toString());
    if (!c.isValid()) return std::nullopt;
    return c;
}

} // namespace lumenis::media
