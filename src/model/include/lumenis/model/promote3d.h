#pragma once

#include "lumenis/model/sequence.h"

#include <optional>

// Promoting 2D effects to 3D (FX Guide p.288-289; ROADMAP 6.7a; docs/spec/3d-effects.md): "if you have a 2D
// Picture-in-Picture effect, you can promote the effect to 3D and add 3D features" (p.289); the 3D Promote
// button "Promotes the effect to a 3D effect. Available for some effects, including titles,
// Picture-in-Picture, and imported matte keys" (p.86). "You cannot demote a 3D effect to a 2D effect" (p.289).
namespace lumenis::model {

// Whether the 3D Promote button promotes `effect`. Ours, for now: Picture-in-Picture (titles and imported
// matte keys come with them, 6.8 and Phase 9).
bool canPromoteTo3D(const Effect& effect);
// The 3D Warp effect (Blend) that looks as `effect` does: Position (Y turned up, p.577), Scaling (a
// Picture-in-Picture's 50 until set made explicit), Crop (2D's distances from the edges as 3D's edge
// positions, p.572) and the Foreground Level as the 3D Opac., with their keyframes. Nothing when it cannot
// be promoted.
std::optional<Effect> promoteTo3D(const Effect& effect);

} // namespace lumenis::model
