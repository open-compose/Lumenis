#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

// Effect templates (FX Guide p.52-55; ROADMAP 6.6b1; docs/spec/effect-templates.md): "Media Composer lets you
// save an effect with its parameter settings as a template. You can then apply the template to other effects
// or video clips at any time" (p.52). A template is an Effect item in a bin (BinItemKind::Effect) whose
// composition holds the effect under "videoEffect"; the Audio EQ and AudioSuite templates (audio_eq.h,
// audio_suite.h) are Effect items too, under keys of their own.
namespace lumenis::model {

struct EffectTemplate {
    // The effect with its parameters and keyframes.
    Effect effect;
    // Saved from a transition: it goes on cuts, with this duration and alignment.
    bool transition = false;
    std::int64_t duration = 0;
    TransitionAlignment alignment = TransitionAlignment::CenteredOnCut;
    std::int64_t start = 0;
    // The effect's length in frames when saved, and the sequence's edit rate then (0/1 when not known).
    // "When you apply a template, Media Composer adjusts keyframes if necessary to account for differences
    // in frame rate" (Editing Guide p.474): Fixed keyframes, which count in frames, are scaled by the
    // rates; Elastic ones are fractions of the effect and need nothing.
    std::int64_t length = 0;
    Rational editRate{0, 1};
    // "save a segment effect with its source media" (Alt+drag, p.53): the segment itself (clip, effect,
    // nest), which is edited in where the template is applied. Empty without.
    QJsonObject source;

    bool hasSource() const { return !source.isEmpty(); }
    QJsonObject toJson() const;
    static std::optional<EffectTemplate> fromJson(const QJsonObject& o);
    bool operator==(const EffectTemplate&) const = default;
};

// A segment effect's template; with `withSource` the segment goes with it. Nothing when the segment has
// no effect.
std::optional<EffectTemplate> segmentTemplate(const Segment& segment, const Rational& editRate, bool withSource);
// A transition's template.
EffectTemplate transitionTemplate(const Transition& transition, const Rational& editRate);
// The segment a template saved with its source media, its effect as the template has it now; nothing
// without.
std::optional<Segment> templateSource(const EffectTemplate& t);

// The bin item for a template: "The new effect template is identified in the bin by its effect icon"
// (p.53), named by the caller (effectTemplateName() by default).
BinItem effectTemplateItem(const EffectTemplate& t, const QString& name, const QDateTime& when);
// The template an Effect item holds; nothing for other items.
std::optional<EffectTemplate> effectTemplateOf(const BinItem& item);
// The default name: the effect's, as shown (model::shownName: the item is the user's, so it carries our name).
QString effectTemplateName(const EffectTemplate& t);

// The template's effect for a sequence at `editRate`: Fixed keyframes scaled from the template's rate
// (unchanged when either rate is unknown or they are equal).
Effect adaptedEffect(const EffectTemplate& t, const Rational& editRate);

// "You can apply an effect template with all its parameters directly to segments or transitions as you
// would any other effect" (p.53). A segment template goes on the segment under `frame` (with `nest`, inside
// its effect, as an Alt+drag of a palette effect does); one with its source media is edited in over the
// track from `frame` (an overwrite); a transition template goes on the cut `frame` with its duration and
// alignment, sized to fit if `sizeToFit` (else refused when there is not enough material). False when it
// could not be applied (nothing there, a locked or non-video track, no room).
bool applyTemplate(Sequence& sequence, const QString& track, std::int64_t frame, const EffectTemplate& t,
                   const Rational& editRate, bool nest = false, bool sizeToFit = false,
                   const SourceLength& sourceLength = {});

// "Media Composer applies the effect template only to the effect parameter group you selected" (p.54): the
// parameters, and keyframes, of `category` in `target` become the template's (its defaults where it has
// none). False when that changes nothing.
bool applyTemplateGroup(Effect& target, const EffectTemplate& t, const QString& category, const Rational& editRate);

// Promote Effects (p.54-55): "The name of the new template uses a suffix indicating that it is a promoted
// template. If the original template is named MyColorEffect, the promoted copy is named, for example,
// MyColorEffect.Promoted.01": the first free number among `taken`.
QString promotedTemplateName(const QString& name, const QStringList& taken);

} // namespace lumenis::model
