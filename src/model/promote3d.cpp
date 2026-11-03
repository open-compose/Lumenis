// Promoting 2D effects to the 3D Warp effect (promote3d.h): each 2D parameter that has a 3D counterpart is
// moved over with a linear change of units, its keyframes (and their Bézier handles) with it.
#include "lumenis/model/promote3d.h"

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/title_clip.h"
#include "lumenis/model/tracking.h"

#include <QJsonObject>

namespace lumenis::model {

namespace {

// One 2D parameter and where it goes: value3D = offset + scale * value2D. `fallback` is the 2D value until
// set (stored explicitly when the 3D one would differ).
struct Mapping {
    const char* category;
    const char* name;
    const char* category3D;
    const char* name3D;
    double offset;
    double scale;
    double fallback;
};

// Units: 2D Position is in pixels of a 1920-wide picture with Y down; 3D's the same with Y up (p.577). 2D
// Crop's T and L are distances from the top and left edges, 0 to 999 of the picture, and B and R -999 to 0
// from the bottom and right (p.494); 3D's are edge positions from -999 to 999 (p.572).
constexpr Mapping kPip[] = {
    {"Position", "X", "Position (3D)", "X", 0, 1, 0},
    {"Position", "Y", "Position (3D)", "Y", 0, -1, 0},
    {"Scaling", "X", "Scaling (3D)", "X", 0, 1, 50},
    {"Scaling", "Y", "Scaling (3D)", "Y", 0, 1, 50},
    {"Scaling", "Fixed Aspect", "Scaling (3D)", "Fixed Aspect", 0, 1, 0},
    {"Crop", "T", "Crop (3D)", "T", 999, -2, 0},
    {"Crop", "B", "Crop (3D)", "B", -999, -2, 0},
    {"Crop", "L", "Crop (3D)", "L", -999, 2, 0},
    {"Crop", "R", "Crop (3D)", "R", 999, 2, 0},
    {"Foreground", "Level", "Foreground (3D)", "Opac.", 0, 1, 100},
};

} // namespace

bool canPromoteTo3D(const Effect& effect) {
    // "You can promote the Stabilize effect to 3D" (p.277).
    return (effect.category == QLatin1String("Blend") && effect.name == QLatin1String("Picture-in-Picture")) || isStabilizeEffect(effect) ||
           (isClassicTitleEffect(effect) && !is3DClassicTitle(effect));
}

std::optional<Effect> promoteTo3D(const Effect& effect) {
    if (!canPromoteTo3D(effect)) return std::nullopt;
    const bool stabilize = isStabilizeEffect(effect);
    // A title's Scaling starts at 100 too (it fills the frame); the title and its media come along (p.371).
    const bool title = isClassicTitleEffect(effect);
    Effect out{QStringLiteral("Blend"), QStringLiteral("3D Warp"), {}};
    if (title) {
        out.parameters.insert(kClassicTitleParameters, effect.parameters.value(kClassicTitleParameters));
        const QString scroll = QStringLiteral("Rolling Title and Crawling Title Effect Parameters");
        if (effect.parameters.contains(scroll)) out.parameters.insert(scroll, effect.parameters.value(scroll));
    }
    for (const Mapping& m : kPip) {
        const QString cat = QLatin1String(m.category);
        const QString name = QLatin1String(m.name);
        const QString cat3 = QLatin1String(m.category3D);
        const QString name3 = QLatin1String(m.name3D);
        // Stabilize has no Foreground, and its Scaling starts at 100.
        if (stabilize && cat == QLatin1String("Foreground")) continue;
        const double fallback = (stabilize || title) && cat == QLatin1String("Scaling") && m.fallback != 0.0 ? 100.0 : m.fallback;
        const QJsonValue stored = effect.parameters.value(cat).toObject().value(name);
        QJsonObject group = out.parameters.value(cat3).toObject();
        if (stored.isBool()) {
            group.insert(name3, stored);
        } else if (stored.isDouble() || fallback != 0.0) {
            const double v = stored.isDouble() ? stored.toDouble() : fallback;
            group.insert(name3, m.offset + m.scale * v);
        }
        if (!group.isEmpty()) out.parameters.insert(cat3, group);
        ParameterKeyframes keys = keyframesOf(effect, cat, name);
        if (keys.keys.isEmpty()) continue;
        for (EffectKeyframe& k : keys.keys) {
            if (k.value.isDouble()) k.value = m.offset + m.scale * k.value.toDouble();
            k.inValue *= m.scale;
            k.outValue *= m.scale;
        }
        setKeyframes(out, cat3, name3, keys);
    }
    // The tracking data comes along ("promote an effect that uses tracking data to 3D", p.277). "When you promote
    // the Stabilize effect to 3D, Stabilize Tracking is automatically activated for all effect parameters that use
    // tracking data. The Run Auto-Zoom option is not available after you promote" (p.277).
    if (effect.parameters.contains(QStringLiteral("Trackers"))) {
        out.parameters.insert(QStringLiteral("Trackers"), effect.parameters.value(QStringLiteral("Trackers")));
        if (stabilize) {
            for (TrackedParameter p : {TrackedParameter::Position, TrackedParameter::Scaling, TrackedParameter::Rotation, TrackedParameter::Tracking}) {
                TrackerMapping m = trackerMapping(out, p);
                if (!m.on) continue;
                m.stabilize = true;
                setTrackerMapping(out, p, m);
            }
            TrackingSetup setup = trackingOf(out);
            setup.autoZoom = false;
            setTracking(out, setup);
        }
    }
    return out;
}

} // namespace lumenis::model
