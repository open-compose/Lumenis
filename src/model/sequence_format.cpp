// Set Format on sequences (sequence_format.h; Editing Guide p.1294).
#include "lumenis/model/sequence_format.h"

#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"

#include <QObject>

#include <cmath>

namespace lumenis::model {

std::int64_t convertFrames(std::int64_t frames, const Rational& from, const Rational& to) {
    if (from == to || from.num() == 0) return frames;
    return static_cast<std::int64_t>(std::llround(static_cast<double>(frames) * to.toDouble() / from.toDouble()));
}

namespace {

// Every segment at the new rate, its clip unlinked: a clip id no bin has (so the segment stays a clip, offline, not
// filler) and the MobID that id gives, so that nothing relinks it to the old media.
void convertTracks(Sequence& sequence, const Rational& from, const Rational& to) {
    QStringList names;
    for (const Track& t : sequence.tracks()) names << t.name();
    for (const QString& name : names) {
        Track* track = sequence.track(name);
        if (!track) continue;
        for (Segment& seg : track->segments) {
            const std::int64_t start = seg.sourceIn;
            seg.length = std::max<std::int64_t>(1, convertFrames(seg.length, from, to));
            if (seg.isFiller()) continue;
            seg.sourceIn = convertFrames(start, from, to);
            seg.clipId = QStringLiteral("offline-") + newItemId();
            seg.binId.clear();
            seg.sourceMob = MobId();
        }
    }
}

} // namespace

std::optional<SequenceFormatChange> setSequenceFormat(const BinItem& sequence, const ProjectFormat& format, QString* error) {
    if (sequence.kind != BinItemKind::Sequence) {
        if (error) *error = QObject::tr("%1 is not a sequence; Set Format changes sequences.").arg(sequence.name);
        return std::nullopt;
    }
    const QString label = QString::fromStdString(format.label());
    SequenceFormatChange out;
    out.item = sequence;
    out.original = sequence.name;
    out.item.fields.insert(QStringLiteral("Format"), label);
    if (format.editRate == sequence.editRate) return out;

    // Another edit rate: timecode is converted, so a new sequence.
    out.duplicated = true;
    BinItem& item = out.item;
    item.id = newItemId();
    item.mobId = MobId::generate();
    item.name = QStringLiteral("%1 %2 fps").arg(sequence.name, QString::fromStdString(frameRateText(format.editRate)));
    Sequence s = Sequence::fromItem(sequence);
    convertTracks(s, sequence.editRate, format.editRate);
    item.editRate = format.editRate;
    s.storeIn(item); // and its duration
    QList<Marker> markers = markersOf(sequence);
    for (Marker& m : markers) m.frame = convertFrames(m.frame, sequence.editRate, format.editRate);
    setMarkers(item, markers);
    return out;
}

} // namespace lumenis::model
