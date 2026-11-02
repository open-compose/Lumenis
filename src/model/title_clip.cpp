// Title Effect clips (title_clip.h): the Title effect with the classic title among its parameters, its segment with the
// precomputes' nest, and the bin item.
#include "lumenis/model/title_clip.h"

#include "lumenis/model/nest.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace lumenis::model {

namespace {

const QString kTitleKey = QStringLiteral("Title");
const QString kUnrenderedKey = QStringLiteral("Unrendered");

// A nest track's one segment of a precompute, `length` frames long.
Segment precomputeSegment(const QString& clipId, const QString& binId, std::int64_t length) {
    Segment s;
    s.clipId = clipId;
    s.binId = binId;
    s.sourceTrack = QStringLiteral("V1");
    s.length = length;
    return s;
}

} // namespace

QString classicTitleEffectName(ClassicMotion motion) {
    switch (motion) {
    case ClassicMotion::Rolling: return QStringLiteral("Rolling Title");
    case ClassicMotion::Crawling: return QStringLiteral("Crawling Title");
    case ClassicMotion::Still: break;
    }
    return QStringLiteral("Title");
}

bool isClassicTitleEffect(const Effect& effect) {
    return effect.category == kTitleEffectCategory &&
           (effect.name == QLatin1String("Title") || effect.name == QLatin1String("Rolling Title") || effect.name == QLatin1String("Crawling Title"));
}

Effect classicTitleEffect(const ClassicTitle& title, bool unrendered) {
    Effect e;
    e.category = kTitleEffectCategory;
    e.name = classicTitleEffectName(title.motion);
    QJsonObject tool{{kTitleKey, title.toJson()}};
    if (unrendered) tool.insert(kUnrenderedKey, true);
    e.parameters.insert(kClassicTitleParameters, tool);
    return e;
}

std::optional<ClassicTitle> classicTitleOf(const Effect& effect) {
    if (!isClassicTitleEffect(effect)) return std::nullopt;
    const QJsonValue v = effect.parameters.value(kClassicTitleParameters).toObject().value(kTitleKey);
    if (!v.isObject()) return std::nullopt;
    return ClassicTitle::fromJson(v.toObject());
}

bool isUnrenderedTitle(const Effect& effect) {
    return isClassicTitleEffect(effect) && effect.parameters.value(kClassicTitleParameters).toObject().value(kUnrenderedKey).toBool();
}

double titleRollStart(const Effect& effect) {
    return std::clamp(effect.parameters.value(kClassicTitleParameters).toObject().value(QStringLiteral("Roll Start")).toDouble(), 0.0, 1.0);
}

void setTitleRollStart(Effect& effect, double start) {
    QJsonObject tool = effect.parameters.value(kClassicTitleParameters).toObject();
    if (start > 0.0) tool.insert(QStringLiteral("Roll Start"), std::clamp(start, 0.0, 1.0));
    else tool.remove(QStringLiteral("Roll Start"));
    effect.parameters.insert(kClassicTitleParameters, tool);
}

TitleSource titleSourceOf(const Effect& effect) {
    const QJsonObject tool = effect.parameters.value(kClassicTitleParameters).toObject();
    return {tool.value(QStringLiteral("Bin")).toString(), tool.value(QStringLiteral("Clip")).toString()};
}

void setTitleSource(Effect& effect, const TitleSource& source) {
    QJsonObject tool = effect.parameters.value(kClassicTitleParameters).toObject();
    tool.insert(QStringLiteral("Bin"), source.bin);
    tool.insert(QStringLiteral("Clip"), source.clip);
    effect.parameters.insert(kClassicTitleParameters, tool);
}

void reviseTitleSegment(Segment& segment, const BinItem& clip, const QString& bin) {
    const auto revised = titleSegmentOf(clip);
    if (!revised || !segment.effect) return;
    const double start = titleRollStart(*segment.effect);
    Effect e = *segment.effect;
    // The revised title and its fast-save state; the rest of the effect stays.
    e.name = revised->effect->name;
    e.parameters.insert(kClassicTitleParameters, revised->effect->parameters.value(kClassicTitleParameters));
    setTitleRollStart(e, start);
    setTitleSource(e, {bin, clip.id});
    segment.effect = e;
    // The new precomputes, as long as the segment.
    Sequence nest = nestOf(*revised);
    for (const Track& t : nest.tracks()) {
        Track* mine = nest.track(t.name());
        for (Segment& s : mine->segments) s.length = segment.length;
    }
    setNest(segment, nest);
    segment.nestIn = 0;
    segment.render.reset();
}

std::int64_t titleClipLength(const Rational& rate) {
    const double fps = rate.den() != 0 ? rate.toDouble() : 25.0;
    return std::max<std::int64_t>(1, std::llround(120.0 * fps));
}

TitleMedia titleMediaOf(const Segment& segment) {
    TitleMedia m;
    if (!segment.hasNest()) return m;
    const Sequence nest = nestOf(segment);
    auto first = [&](const QString& track) -> const Segment* {
        const Track* t = nest.track(track);
        if (!t) return nullptr;
        for (const Segment& s : t->segments) {
            if (!s.isFiller()) return &s;
        }
        return nullptr;
    };
    // The matte names the bin; the fill is the one the effect names (titles saved before 6.8e2: the nest's first
    // V1 clip), whatever was edited onto V1 since.
    const QString stored = segment.effect ? segment.effect->parameters.value(kClassicTitleParameters).toObject().value(QStringLiteral("Fill")).toString()
                                          : QString();
    if (const Segment* fill = first(QStringLiteral("V1"))) {
        m.fill = fill->clipId;
        m.binId = fill->binId;
    }
    if (!stored.isEmpty()) m.fill = stored;
    if (const Segment* matte = first(QStringLiteral("V2"))) {
        m.matte = matte->clipId;
        m.binId = matte->binId;
    }
    return m;
}

Segment classicTitleSegment(const ClassicTitle& title, std::int64_t length, const TitleMedia& media) {
    Segment s = Segment::filler(length);
    s.effect = classicTitleEffect(title, media.isEmpty());
    if (!media.isEmpty()) {
        // The fill precompute by name, so that a fill track replaced in the nest (FX Guide p.373) is told from it.
        QJsonObject tool = s.effect->parameters.value(kClassicTitleParameters).toObject();
        tool.insert(QStringLiteral("Fill"), media.fill);
        s.effect->parameters.insert(kClassicTitleParameters, tool);
    }
    // The graphic (fill) and alpha (matte) tracks, filler for a fast-saved title.
    Sequence nest;
    // (Track pointers last only until the next track is added.)
    nest.ensureTrack(QStringLiteral("V1"));
    nest.ensureTrack(QStringLiteral("V2"));
    nest.track(QStringLiteral("V1"))->segments = {media.isEmpty() ? Segment::filler(length) : precomputeSegment(media.fill, media.binId, length)};
    nest.track(QStringLiteral("V2"))->segments = {media.isEmpty() ? Segment::filler(length) : precomputeSegment(media.matte, media.binId, length)};
    nest.setTimecodeTrack(false);
    setNest(s, nest);
    return s;
}

BinItem classicTitleClip(const ClassicTitle& title, const QString& name, const Rational& rate, const TitleMedia& media,
                         const QDateTime& when) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::Title;
    item.name = name;
    item.created = when;
    item.modified = when;
    item.editRate = rate;
    reviseClassicTitleClip(item, title, media, when);
    return item;
}

void reviseClassicTitleClip(BinItem& clip, const ClassicTitle& title, const TitleMedia& media, const QDateTime& when) {
    const std::int64_t length = clip.durationFrames > 0 ? clip.durationFrames : titleClipLength(clip.editRate);
    Sequence seq;
    seq.setTimecodeTrack(false);
    Track* v1 = seq.ensureTrack(QStringLiteral("V1"));
    v1->segments = {classicTitleSegment(title, length, media)};
    seq.storeIn(clip);
    clip.kind = BinItemKind::Title;
    clip.durationFrames = length;
    clip.modified = when;
    clip.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1"));
}

std::optional<Segment> titleSegmentOf(const BinItem& item) {
    if (item.kind != BinItemKind::Title || item.composition.isEmpty()) return std::nullopt;
    const Sequence seq = Sequence::fromItem(item);
    const Track* v1 = seq.track(QStringLiteral("V1"));
    if (!v1) return std::nullopt;
    for (const Segment& s : v1->segments) {
        if (s.effect && isClassicTitleEffect(*s.effect)) return s;
    }
    return std::nullopt;
}

std::optional<ClassicTitle> classicTitleOf(const BinItem& item) {
    const auto s = titleSegmentOf(item);
    return s ? classicTitleOf(*s->effect) : std::nullopt;
}

bool isUnrenderedTitle(const BinItem& item) {
    const auto s = titleSegmentOf(item);
    return s && isUnrenderedTitle(*s->effect);
}

QString unrenderedTitleName(const QString& name) {
    const QString prefix = QStringLiteral("unrendered.");
    return name.startsWith(prefix) ? name : prefix + name;
}

QString revisionTitleName(const Bin& bin, const QString& name) {
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1.%2").arg(name).arg(n, 2, 10, QLatin1Char('0'));
        bool taken = false;
        for (const BinItem& i : bin.items) taken = taken || i.name == candidate;
        if (!taken) return candidate;
    }
}

} // namespace lumenis::model
