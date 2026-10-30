#pragma once

#include "lumenis/media/compositor.h"
#include "lumenis/media/managed.h"
#include "lumenis/model/effect_render.h"

#include <QString>

#include <optional>

// Render Effect (FX Guide p.57-61; ROADMAP 6.4; docs/spec/rendering.md): an effect's frames computed
// and stored as a precompute master clip, so that it plays from media rather than being computed. The
// model of what is rendered is lumenis/model/effect_render.h; the Composer plays renders through
// Composition::rendered.
namespace lumenis::media {

// The precompute's name: "<sequence> - <effect>" (ours, for "The clips are named with the sequence name
// and effect type", p.155).
QString precomputeName(const QString& sequence, const QString& effect);

// Renders `target` of `composition.sequence`: the effect's track and every video track below it
// composited over the effect's frames at the creation's raster (the project's), written as managed media
// in the creation's resolution on its video drive at `editRate`. Renders of effects on lower
// tracks that are in date are used; the target's own is not. The result is the render to keep with the
// effect (model::setRender), its clip a RenderedEffect item named precomputeName(sequenceName, effect).
// MediaCreation::progress can cancel; on failure nothing is kept and `error` says why. Partial render
// (p.150): `from` renders only the effect's frames from that one on (the rest of a partial render; the
// result is that one clip, for the caller to add to the earlier ones), and with `keepOnCancel` a cancel
// returns what was rendered so far.
std::optional<model::EffectRender> renderEffect(const Composition& composition, const model::RenderTarget& target,
                                                const QString& sequenceName, const Rational& editRate,
                                                const MediaCreation& creation, QString* error = nullptr,
                                                std::int64_t from = 0, bool keepOnCancel = false);

} // namespace lumenis::media
