#include "lumenis/media/render.h"

#include <QObject>

#include <algorithm>

// Render Effect (docs/spec/rendering.md): composing an effect's frames and writing them as a precompute.
// See render.h.
namespace lumenis::media {

QString precomputeName(const QString& sequence, const QString& effect) {
    return sequence.isEmpty() ? effect : QStringLiteral("%1 - %2").arg(sequence, effect);
}

std::optional<model::EffectRender> renderEffect(const Composition& composition, const model::RenderTarget& target,
                                                const QString& sequenceName, const Rational& editRate,
                                                const MediaCreation& creation, QString* error) {
    auto failed = [&](const QString& text) -> std::optional<model::EffectRender> {
        if (error) *error = text;
        return std::nullopt;
    };
    if (!composition.sequence) return failed(QObject::tr("There is no sequence."));
    const model::Effect* effect = model::effectOf(*composition.sequence, target);
    if (!effect || target.length <= 0) return failed(QObject::tr("There is no effect to render there."));
    const auto ref = model::parseTrackName(target.track);
    if (!ref) return failed(QObject::tr("There is no effect to render there."));

    // The sequence without the target's own render (it is being made again), and the effect's track
    // with every video track below it, bottom up.
    model::Sequence sequence = *composition.sequence;
    model::setRender(sequence, target, std::nullopt);
    Composition c = composition;
    c.sequence = &sequence;
    c.tracks.clear();
    QList<int> numbers;
    for (const model::Track& t : sequence.tracks()) {
        if (t.kind == model::TrackKind::Video && t.number <= ref->number) numbers << t.number;
    }
    std::sort(numbers.begin(), numbers.end());
    for (int n : numbers) c.tracks << model::trackName(model::TrackKind::Video, n);

    // At the project's raster, whatever size the monitors show (full quality, p.57).
    c.size = creation.raster.expandedTo(kMinimumRaster);
    MediaCreation made = creation;
    made.raster = c.size;
    const auto clip = renderedVideoClip(
        precomputeName(sequenceName, effect->name), editRate, target.length,
        [&](std::int64_t frame) { return composeFrame(c, target.start + frame).toImage(); }, made,
        model::BinItemKind::RenderedEffect, error);
    if (!clip) return std::nullopt;
    return model::EffectRender{model::binItemToJson(*clip), model::renderKey(*composition.sequence, target)};
}

} // namespace lumenis::media
