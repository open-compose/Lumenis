#include "lumenis/model/clip_coloring.h"

// Clip colours in the Timeline (clip_coloring.h). The default colours are ours, except offline red
// (p.1113).
namespace lumenis::model {

namespace {

const char* const kRates[] = {"23.976", "24", "25", "29.97", "30", "47.952", "48", "50", "59.94", "60"};
const char* const kRateColors[] = {"#d27828", "#d2c832", "#8cc83c", "#2ca096", "#3cbed2",
                                   "#7846c8", "#be3cb4", "#e68cb4", "#8c5a32", "#e6e6e6"};

} // namespace

QStringList mixedRates() {
    QStringList out;
    for (const char* r : kRates) out << QString::fromLatin1(r);
    return out;
}

ClipColorOptions ClipColorOptions::defaults() {
    ClipColorOptions o;
    o.resetColors();
    return o;
}

void ClipColorOptions::resetColors() {
    offlineColor = QStringLiteral("#c83232");
    linkedColor = QStringLiteral("#3264c8");
    sdHdColor = QStringLiteral("#7846c8");
    rateColors.clear();
    for (std::size_t i = 0; i < std::size(kRates); ++i) {
        rateColors.insert(QString::fromLatin1(kRates[i]), QString::fromLatin1(kRateColors[i]));
    }
}

QJsonObject ClipColorOptions::toJson() const {
    QJsonObject rates;
    for (auto it = rateColors.begin(); it != rateColors.end(); ++it) rates.insert(it.key(), it.value());
    return QJsonObject{{QStringLiteral("proxy"), proxy},
                       {QStringLiteral("offline"), offline},
                       {QStringLiteral("linked"), linked},
                       {QStringLiteral("mixedRates"), mixedRates},
                       {QStringLiteral("sdHd"), sdHd},
                       {QStringLiteral("local"), local},
                       {QStringLiteral("source"), source},
                       {QStringLiteral("offlineColor"), offlineColor},
                       {QStringLiteral("linkedColor"), linkedColor},
                       {QStringLiteral("sdHdColor"), sdHdColor},
                       {QStringLiteral("rateColors"), rates}};
}

ClipColorOptions ClipColorOptions::fromJson(const QJsonObject& o) {
    ClipColorOptions c = defaults();
    auto flag = [&o](const char* key, bool& into) {
        if (o.contains(QLatin1String(key))) into = o.value(QLatin1String(key)).toBool(into);
    };
    auto color = [&o](const char* key, QString& into) {
        const QString v = o.value(QLatin1String(key)).toString();
        if (!v.isEmpty()) into = v;
    };
    flag("proxy", c.proxy);
    flag("offline", c.offline);
    flag("linked", c.linked);
    flag("mixedRates", c.mixedRates);
    flag("sdHd", c.sdHd);
    flag("local", c.local);
    flag("source", c.source);
    color("offlineColor", c.offlineColor);
    color("linkedColor", c.linkedColor);
    color("sdHdColor", c.sdHdColor);
    const QJsonObject rates = o.value(QStringLiteral("rateColors")).toObject();
    for (auto it = rates.begin(); it != rates.end(); ++it) {
        if (c.rateColors.contains(it.key()) && !it.value().toString().isEmpty()) c.rateColors[it.key()] = it.value().toString();
    }
    return c;
}

// The dialog's order (p.595-596): Resolution Tracking, Offline, Proxy, Linked Clips, Mixed Rates,
// SD/HD, Timeline Local, Source. Resolution Tracking and Proxy never apply yet.
QString timelineClipColor(const ClipColorOptions& o, const Segment& s, const std::optional<ClipFacts>& clip) {
    if (s.isFiller()) return {};
    const bool offline = !clip || clip->offline;
    if (o.offline && offline) return o.offlineColor;
    if (clip) {
        if (o.linked && clip->linked) return o.linkedColor;
        if (o.mixedRates && !clip->rate.isEmpty() && o.rateColors.contains(clip->rate)) return o.rateColors.value(clip->rate);
        if (o.sdHd && clip->otherDefinition) return o.sdHdColor;
    }
    if (o.local && !s.localColor.isEmpty()) return s.localColor;
    if (o.source && clip && !clip->sourceColor.isEmpty()) {
        switch (clip->kind) {
        case BinItemKind::Sequence:
        case BinItemKind::Group:
        case BinItemKind::MotionEffect:
        case BinItemKind::Title: return {};
        default: return clip->sourceColor;
        }
    }
    return {};
}

} // namespace lumenis::model
