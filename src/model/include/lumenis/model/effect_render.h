#pragma once

#include "lumenis/model/sequence.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Rendering video effects (FX Guide p.57-61, p.135-156; ROADMAP 6.4; docs/spec/rendering.md): which
// effects in a sequence can be rendered, what a render was made from, and keeping or clearing renders.
// A render is a precompute master clip (EffectRender in sequence.h) kept with the segment effect or
// transition it was made for. The pictures are made by media::renderEffect (lumenis/media/render.h);
// the Composer plays a render in place of the effect while its key matches.
namespace lumenis::model {

// One video effect that can be rendered: a segment effect, or the transition at the start of a
// segment (`transition`), on video track `track`, the `segment`th segment there, covering sequence
// frames [start, start + length).
struct RenderTarget {
    QString track;
    int segment = -1;
    bool transition = false;
    std::int64_t start = 0;
    std::int64_t length = 0;
    std::int64_t end() const { return start + length; }
    bool operator==(const RenderTarget&) const = default;
};

// The effects on the named video tracks that overlap sequence frames [from, to), track by track from
// the lowest and in order along each track. Audio and unknown tracks are left out.
QList<RenderTarget> renderTargets(const Sequence& sequence, const QStringList& tracks, std::int64_t from, std::int64_t to);
// The effect that shows at `frame` on `track`: a transition when the frame is in one, else the
// segment's own effect; nothing where there is neither.
std::optional<RenderTarget> renderTargetAt(const Sequence& sequence, const QString& track, std::int64_t frame);

// The target's effect, or null when the sequence no longer has it.
const Effect* effectOf(const Sequence& sequence, const RenderTarget& target);
// The target's render, if it has one (in date or not).
std::optional<EffectRender> renderOf(const Sequence& sequence, const RenderTarget& target);
// What a render of the target is made from: the effect and everything on its track and the video
// tracks below it over the effect's frames (their clips, source frames, lengths, effects and
// transitions, placed relative to the effect's start), as a hash. Moving the whole effect keeps the
// key; changing a parameter, trimming or editing under it changes it.
QString renderKey(const Sequence& sequence, const RenderTarget& target);
// How many of the effect's frames the render's clips hold, from its first.
std::int64_t renderedFrames(const EffectRender& render);
// The render's clip holding the effect's frame `frame`, and that frame in the clip; nothing past them.
std::optional<std::pair<QJsonObject, std::int64_t>> renderedClipAt(const EffectRender& render, std::int64_t frame);
// Whether the target has a render made from what it is now that covers all its frames.
bool isRendered(const Sequence& sequence, const RenderTarget& target);
// Whether it has one in date that covers only some of its frames (partial render, p.150).
bool isPartlyRendered(const Sequence& sequence, const RenderTarget& target);
// Whether its frame `frame` (counted from its start) has a render in date.
bool isRenderedAt(const Sequence& sequence, const RenderTarget& target, std::int64_t frame);
// Sets (or with nothing, removes) the target's render. False when the sequence has no such effect.
bool setRender(Sequence& sequence, const RenderTarget& target, const std::optional<EffectRender>& render);

// Clear Renders (p.60-61): the renders of `targets` removed, except, when asked, motion effects ("Skip
// Motion Effects") and effects `known` says are not installed ("Skip Unknown Effects"; every effect is
// known without `known`). The precomputes themselves are kept ("does not delete the effects or their
// precomputes", p.60). Returns how many renders were removed.
struct ClearRenders {
    bool skipMotionEffects = true;
    bool skipUnknownEffects = true;
    std::function<bool(const Effect&)> known;
};
int clearRenders(Sequence& sequence, const QList<RenderTarget>& targets, const ClearRenders& options);

} // namespace lumenis::model
