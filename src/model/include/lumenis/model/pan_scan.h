#pragma once

// Pan and Scan's Actions (ROADMAP 6.7b1; FX Guide p.235-238, p.564; docs/spec/pan-scan.md): the origin that
// Establish Origin keeps and Reset to Origin returns to, and Subdivide Effect. The picture itself is media's
// (media::panScanPicture); the Effect Editor's buttons call these.

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QStringList>

namespace lumenis::model {

// The parameter category of Pan and Scan's ratios and actions (p.564); the origin is kept in it as "Origin".
inline constexpr char kPanScanCategory[] = "Pan and Scan Aspect Ratios and Actions Parameters";

bool isPanAndScanEffect(const Effect& effect);

// The wire frame's Scaling and Position: the established origin, or "the default values for the current Target
// aspect ratio" (p.564; ours: Scaling 100 and Position 0, the largest wire frame centred on the source).
struct PanScanOrigin {
    double scaleX = 100.0;
    double scaleY = 100.0;
    double x = 0.0;
    double y = 0.0;
    bool operator==(const PanScanOrigin&) const = default;
};
PanScanOrigin panScanOrigin(const Effect& effect);
// Establish Origin: "Saves the current Scaling and Position values as a customized default setting" (p.564),
// their values at time `t` of the effect.
void establishOrigin(Effect& effect, double t, const EffectSpan& span);
// Choosing a Target again forgets the origin: "You can return to these default settings at any time by
// selecting the aspect ratio again" (p.235).
void clearOrigin(Effect& effect);

// Subdivide Effect (p.235-236, p.238): the Pan and Scan effect on `track` at `frame` is split "based on the
// segment boundaries of the selected video tracks underneath" (`guides`; only tracks below `track` count), with
// an Add Edit at each boundary strictly inside it, except boundaries that carry a transition ("Subdivide Effect
// does not subdivide segments connected by transition effects"). Each piece keeps the effect as it was. False
// when there is no Pan and Scan there or no boundary to split at.
bool subdivideEffect(Sequence& sequence, const QString& track, std::int64_t frame, const QStringList& guides);

} // namespace lumenis::model
