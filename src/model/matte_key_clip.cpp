// Matte Key clips (matte_key_clip.h; Editing Guide p.218, p.1196; FX Guide p.474).
#include "lumenis/model/matte_key_clip.h"

#include "lumenis/model/nest.h"

#include <QDateTime>
#include <QJsonObject>

namespace lumenis::model {

namespace {

Segment precompute(const QString& clipId, const QString& binId, std::int64_t length) {
    Segment s;
    s.clipId = clipId;
    s.binId = binId;
    s.sourceTrack = QStringLiteral("V1");
    s.length = length;
    return s;
}

} // namespace

BinItem matteKeyClip(const QString& name, const Rational& rate, std::int64_t length, const MatteKeyMedia& media, double level) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::Title;
    item.name = name;
    item.created = QDateTime::currentDateTimeUtc();
    item.modified = item.created;
    item.editRate = rate;
    item.durationFrames = std::max<std::int64_t>(length, 1);
    Segment s = Segment::filler(item.durationFrames);
    Effect e;
    e.category = QStringLiteral("Key");
    e.name = QStringLiteral("Matte Key");
    QJsonObject own;
    own.insert(QStringLiteral("Fill"), media.fill);
    own.insert(QStringLiteral("Matte"), media.matte);
    own.insert(QStringLiteral("Bin"), media.binId);
    own.insert(QStringLiteral("Frames"), static_cast<qint64>(std::max<std::int64_t>(media.frames, 1)));
    e.parameters.insert(kMatteKeyClipParameters, own);
    if (level < 100.0) {
        QJsonObject fg;
        fg.insert(QStringLiteral("Level"), level);
        e.parameters.insert(QStringLiteral("Foreground"), fg);
    }
    s.effect = e;
    // The fill and alpha tracks, as a Title Effect clip's.
    Sequence nest;
    nest.ensureTrack(QStringLiteral("V1"));
    nest.ensureTrack(QStringLiteral("V2"));
    nest.track(QStringLiteral("V1"))->segments = {precompute(media.fill, media.binId, item.durationFrames)};
    nest.track(QStringLiteral("V2"))->segments = {precompute(media.matte, media.binId, item.durationFrames)};
    nest.setTimecodeTrack(false);
    setNest(s, nest);
    Sequence seq;
    seq.setTimecodeTrack(false);
    seq.ensureTrack(QStringLiteral("V1"))->segments = {s};
    seq.storeIn(item);
    item.kind = BinItemKind::Title;
    item.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1"));
    return item;
}

bool isMatteKeyClipEffect(const Effect& effect) {
    return effect.category == QLatin1String("Key") && effect.name == QLatin1String("Matte Key") &&
           effect.parameters.contains(kMatteKeyClipParameters);
}

MatteKeyMedia matteKeyMediaOf(const Effect& effect) {
    MatteKeyMedia m;
    if (!isMatteKeyClipEffect(effect)) return m;
    const QJsonObject own = effect.parameters.value(kMatteKeyClipParameters).toObject();
    m.fill = own.value(QStringLiteral("Fill")).toString();
    m.matte = own.value(QStringLiteral("Matte")).toString();
    m.binId = own.value(QStringLiteral("Bin")).toString();
    m.frames = own.value(QStringLiteral("Frames")).toInteger(1);
    return m;
}

std::optional<Segment> matteKeyClipSegment(const BinItem& item) {
    if (item.kind != BinItemKind::Title || item.composition.isEmpty()) return std::nullopt;
    const Sequence seq = Sequence::fromItem(item);
    const Track* v1 = seq.track(QStringLiteral("V1"));
    if (!v1) return std::nullopt;
    for (const Segment& s : v1->segments) {
        if (s.effect && isMatteKeyClipEffect(*s.effect)) return s;
    }
    return std::nullopt;
}

bool isMatteKeyClip(const BinItem& item) { return matteKeyClipSegment(item).has_value(); }

} // namespace lumenis::model
