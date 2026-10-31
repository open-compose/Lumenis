// Promoting 2D effects to the 3D Warp effect (promote3d.h): each 2D parameter that has a 3D counterpart is
// moved over with a linear change of units, its keyframes (and their Bézier handles) with it.
#include "lumenis/model/promote3d.h"

#include "lumenis/model/effect_keyframes.h"

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
    return effect.category == QLatin1String("Blend") && effect.name == QLatin1String("Picture-in-Picture");
}

std::optional<Effect> promoteTo3D(const Effect& effect) {
    if (!canPromoteTo3D(effect)) return std::nullopt;
    Effect out{QStringLiteral("Blend"), QStringLiteral("3D Warp"), {}};
    for (const Mapping& m : kPip) {
        const QString cat = QLatin1String(m.category);
        const QString name = QLatin1String(m.name);
        const QString cat3 = QLatin1String(m.category3D);
        const QString name3 = QLatin1String(m.name3D);
        const QJsonValue stored = effect.parameters.value(cat).toObject().value(name);
        QJsonObject group = out.parameters.value(cat3).toObject();
        if (stored.isBool()) {
            group.insert(name3, stored);
        } else if (stored.isDouble() || m.fallback != 0.0) {
            const double v = stored.isDouble() ? stored.toDouble() : m.fallback;
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
    return out;
}

} // namespace lumenis::model
