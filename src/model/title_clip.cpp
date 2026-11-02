// Title Effect clips (title_clip.h): the Title effect with the classic title among its parameters, its segment with the
// precomputes' nest, and the bin item.
#include "lumenis/model/title_clip.h"

#include "lumenis/model/nest.h"

#include <QJsonObject>

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
    if (const Segment* fill = first(QStringLiteral("V1"))) {
        m.fill = fill->clipId;
        m.binId = fill->binId;
    }
    if (const Segment* matte = first(QStringLiteral("V2"))) m.matte = matte->clipId;
    return m;
}

Segment classicTitleSegment(const ClassicTitle& title, std::int64_t length, const TitleMedia& media) {
    Segment s = Segment::filler(length);
    s.effect = classicTitleEffect(title, media.isEmpty());
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
