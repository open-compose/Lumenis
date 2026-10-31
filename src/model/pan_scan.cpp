// Pan and Scan's origin and Subdivide Effect (FX Guide p.235-238, p.564; docs/spec/pan-scan.md).
#include "lumenis/model/pan_scan.h"

#include <QJsonObject>

#include <algorithm>
#include <set>

namespace lumenis::model {

namespace {

QString category() {
    return QLatin1String(kPanScanCategory);
}

} // namespace

bool isPanAndScanEffect(const Effect& effect) {
    return effect.category == QLatin1String("Reformat") && effect.name == QLatin1String("Pan and Scan");
}

PanScanOrigin panScanOrigin(const Effect& effect) {
    PanScanOrigin o;
    const QJsonObject kept = effect.parameters.value(category()).toObject().value(QStringLiteral("Origin")).toObject();
    if (kept.isEmpty()) return o;
    o.scaleX = kept.value(QStringLiteral("Scaling X")).toDouble(100.0);
    o.scaleY = kept.value(QStringLiteral("Scaling Y")).toDouble(100.0);
    o.x = kept.value(QStringLiteral("Position X")).toDouble(0.0);
    o.y = kept.value(QStringLiteral("Position Y")).toDouble(0.0);
    return o;
}

void establishOrigin(Effect& effect, double t, const EffectSpan& span) {
    auto at = [&](const char* group, const char* name, double fallback) {
        return numberAt(effect, QLatin1String(group), QLatin1String(name), t, span).value_or(fallback);
    };
    QJsonObject group = effect.parameters.value(category()).toObject();
    group.insert(QStringLiteral("Origin"), QJsonObject{{QStringLiteral("Scaling X"), at("Scaling", "X", 100.0)},
                                                       {QStringLiteral("Scaling Y"), at("Scaling", "Y", 100.0)},
                                                       {QStringLiteral("Position X"), at("Position", "X", 0.0)},
                                                       {QStringLiteral("Position Y"), at("Position", "Y", 0.0)}});
    effect.parameters.insert(category(), group);
}

void clearOrigin(Effect& effect) {
    QJsonObject group = effect.parameters.value(category()).toObject();
    if (!group.contains(QStringLiteral("Origin"))) return;
    group.remove(QStringLiteral("Origin"));
    effect.parameters.insert(category(), group);
}

bool subdivideEffect(Sequence& sequence, const QString& track, std::int64_t frame, const QStringList& guides) {
    const Track* own = sequence.track(track);
    if (!own) return false;
    std::int64_t start = 0;
    const int i = own->segmentAt(frame, &start);
    if (i < 0 || !own->segments.at(i).effect || !isPanAndScanEffect(*own->segments.at(i).effect)) return false;
    const std::int64_t end = start + own->segments.at(i).length;
    // The guide tracks below the effect's own: their cuts inside the effect, but not those with a transition.
    const QList<Track>& tracks = sequence.tracks();
    const auto ownAt = std::find_if(tracks.begin(), tracks.end(), [&](const Track& t) { return t.name() == track; });
    if (ownAt == tracks.end()) return false;
    std::set<std::int64_t> cuts;
    for (const Track& t : tracks) {
        if (t.kind != own->kind || !guides.contains(t.name()) || t.number >= ownAt->number) continue;
        std::int64_t at = 0;
        for (const Segment& s : t.segments) {
            if (at > start && at < end && !s.transition) cuts.insert(at);
            at += s.length;
        }
    }
    bool split = false;
    for (std::int64_t cut : cuts) split = sequence.addEdit(track, cut) || split;
    return split;
}

} // namespace lumenis::model
