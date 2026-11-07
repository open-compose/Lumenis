// Group clips (Editing Guide p.1123-1131; lumenis/model/group_clip.h, docs/spec/multicamera.md): making
// a group from synced clips, keeping it in the Group item's composition and playing an angle of it.
#include "lumenis/model/group_clip.h"

#include "lumenis/model/sequence.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <limits>

namespace lumenis::model {

namespace {

constexpr char kGroupKey[] = "group";
// On an angle clip made by groupAngleClip(): the frames that have material.
constexpr char kRangeKey[] = "groupRange";

QJsonObject angleToJson(const GroupAngle& a) {
    return {{QStringLiteral("bin"), a.binId},
            {QStringLiteral("clip"), a.clipId},
            {QStringLiteral("offset"), static_cast<qint64>(a.offset)},
            {QStringLiteral("item"), binItemToJson(a.clip)}};
}

std::optional<GroupAngle> angleFromJson(const QJsonObject& o) {
    const auto item = binItemFromJson(o.value(QStringLiteral("item")).toObject());
    if (!item) return std::nullopt;
    GroupAngle a;
    a.binId = o.value(QStringLiteral("bin")).toString();
    a.clipId = o.value(QStringLiteral("clip")).toString();
    a.offset = o.value(QStringLiteral("offset")).toInteger();
    a.clip = *item;
    return a;
}

// "25/1" or "24000/1001" as a rate; nothing for anything else.
std::optional<std::pair<std::int64_t, std::int64_t>> rateParts(const QString& text) {
    const QStringList parts = text.split(QLatin1Char('/'));
    if (parts.size() != 2) return std::nullopt;
    bool a = false;
    bool b = false;
    const std::int64_t num = parts[0].toLongLong(&a);
    const std::int64_t den = parts[1].toLongLong(&b);
    if (!a || !b || num <= 0 || den <= 0) return std::nullopt;
    return std::pair{num, den};
}

// `frames` clip frames at the clip's rate as frames of a file at the link's "rate" (rounded to the
// nearest), so a link's "first" can move by them.
std::int64_t fileFrames(std::int64_t frames, const Rational& clipRate, const QJsonObject& link) {
    const auto file = rateParts(link.value(QStringLiteral("rate")).toString());
    if (!file || clipRate.num() <= 0) return frames;
    const auto [fnum, fden] = *file;
    if (fnum * clipRate.den() == clipRate.num() * fden) return frames;
    const double scaled = static_cast<double>(frames) * static_cast<double>(fnum) * static_cast<double>(clipRate.den()) /
                          (static_cast<double>(fden) * static_cast<double>(clipRate.num()));
    return static_cast<std::int64_t>(std::llround(scaled));
}

// The clip's sync point as a frame of the clip counted the same way for every clip: for marks the mark
// itself, for timecode the negative of where its timecode starts (so frame f is at f + start).
std::optional<std::int64_t> syncPoint(const BinItem& clip, const GroupOptions& options, QString* why) {
    switch (options.sync) {
    case GroupSync::Inpoints:
    case GroupSync::Outpoints: {
        const bool in = options.sync == GroupSync::Inpoints;
        std::optional<std::int64_t> mark = options.marks ? options.marks(clip, in) : std::nullopt;
        if (!mark) {
            // The bin's Mark IN / Mark OUT columns, as timecode of the clip.
            const QString text = clip.fields.value(in ? QStringLiteral("Mark IN") : QStringLiteral("Mark OUT"));
            if (const auto tc = timecodeFrames(text, clip.editRate)) mark = *tc - startFrameOf(clip);
        }
        if (!mark) {
            *why = in ? QStringLiteral("\"%1\" has no IN point.").arg(clip.name) : QStringLiteral("\"%1\" has no OUT point.").arg(clip.name);
            return std::nullopt;
        }
        return *mark;
    }
    case GroupSync::SourceTimecode:
        if (!timecodeFrames(clip.fields.value(QStringLiteral("Start")), clip.editRate)) {
            *why = QStringLiteral("\"%1\" has no source timecode.").arg(clip.name);
            return std::nullopt;
        }
        return -startFrameOf(clip);
    case GroupSync::AuxTimecode: {
        const QString heading = QStringLiteral("Auxiliary TC%1").arg(std::clamp(options.auxTc, 1, 5));
        const auto tc = timecodeFrames(clip.fields.value(heading), clip.editRate);
        if (!tc) {
            *why = QStringLiteral("\"%1\" has no %2.").arg(clip.name, heading);
            return std::nullopt;
        }
        return -*tc;
    }
    case GroupSync::FilmTc:
    case GroupSync::Ink:
    case GroupSync::AuxInk:
    case GroupSync::Waveform: break;
    }
    *why = QStringLiteral("Grouping by this option is not available.");
    return std::nullopt;
}

} // namespace

std::optional<GroupClip> groupOf(const BinItem& item) {
    if (item.kind != BinItemKind::Group) return std::nullopt;
    const QJsonObject o = item.composition.value(QLatin1String(kGroupKey)).toObject();
    GroupClip g;
    for (const QJsonValue& v : o.value(QStringLiteral("angles")).toArray()) {
        if (auto a = angleFromJson(v.toObject())) g.angles << *a;
    }
    g.selected = g.angles.isEmpty() ? 0 : std::clamp(o.value(QStringLiteral("selected")).toInt(), 0, static_cast<int>(g.angles.size()) - 1);
    return g;
}

bool isGroupClip(const BinItem& item) {
    return item.kind == BinItemKind::Group && item.composition.contains(QLatin1String(kGroupKey));
}

void setGroup(BinItem& item, const GroupClip& group) {
    QJsonArray angles;
    for (const GroupAngle& a : group.angles) angles.append(angleToJson(a));
    item.composition.insert(QLatin1String(kGroupKey), QJsonObject{{QStringLiteral("angles"), angles},
                                                                  {QStringLiteral("selected"), group.selected}});
}

bool canBeGroupAngle(const BinItem& item) {
    return item.kind == BinItemKind::MasterClip || item.kind == BinItemKind::Subclip || item.kind == BinItemKind::AudioClip;
}

QString groupName(const Bin& bin, const QString& name) {
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1.Grp.%2").arg(name).arg(n);
        const bool used = std::any_of(bin.items.cbegin(), bin.items.cend(), [&](const BinItem& i) { return i.name == candidate; });
        if (!used) return candidate;
    }
}

QStringList groupTracks(const GroupClip& group) {
    bool video = false;
    int audio = 0;
    for (const GroupAngle& a : group.angles) {
        int n = 0;
        for (const QString& t : sourceTracksOf(a.clip)) {
            const auto ref = parseTrackName(t);
            if (!ref) continue;
            if (ref->kind == TrackKind::Video) video = true;
            if (ref->kind == TrackKind::Audio) ++n;
        }
        audio = std::max(audio, n);
    }
    QStringList tracks;
    if (video) tracks << QStringLiteral("V1");
    for (int i = 1; i <= audio; ++i) tracks << trackName(TrackKind::Audio, i);
    return tracks;
}

std::optional<BinItem> groupClips(const Bin& bin, const QList<std::pair<QString, BinItem>>& clips, const GroupOptions& options,
                                  QString* error) {
    QString why;
    const auto fail = [&](const QString& reason) {
        if (error) *error = reason;
        return std::nullopt;
    };
    QList<std::pair<QString, BinItem>> usable;
    for (const auto& c : clips) {
        if (canBeGroupAngle(c.second)) usable << c;
    }
    if (usable.size() < 2) return fail(QStringLiteral("Select two or more master clips, subclips or audio clips to group."));
    const Rational rate = usable.first().second.editRate;
    // Where each clip lies relative to the common sync point: from -sync to duration - sync.
    std::int64_t earliest = std::numeric_limits<std::int64_t>::max();
    std::int64_t latest = std::numeric_limits<std::int64_t>::min();
    QList<std::int64_t> points;
    for (const auto& [binId, clip] : usable) {
        if (!(clip.editRate == rate)) return fail(QStringLiteral("\"%1\" is at another frame rate; grouped clips share one.").arg(clip.name));
        const auto point = syncPoint(clip, options, &why);
        if (!point) return fail(why);
        points << *point;
        earliest = std::min(earliest, -*point);
        latest = std::max(latest, clip.durationFrames - *point);
    }
    GroupClip group;
    for (int i = 0; i < usable.size(); ++i) {
        GroupAngle a;
        a.binId = usable[i].first;
        a.clipId = usable[i].second.id;
        a.clip = usable[i].second;
        // Group frame g is at earliest + g relative to the sync point, which is clip frame
        // earliest + g + point.
        a.offset = earliest + points[i];
        group.angles << a;
    }
    const BinItem& first = usable.first().second;
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::Group;
    item.name = groupName(bin, first.name);
    item.created = options.when.isValid() ? options.when : QDateTime::currentDateTimeUtc();
    item.modified = item.created;
    item.editRate = rate;
    item.durationFrames = std::max<std::int64_t>(latest - earliest, 1);
    item.fields.insert(QStringLiteral("Tracks"), groupTracks(group).join(QLatin1Char(' ')));
    // Its timecode is the first clip's at the group's first frame (ours).
    const TimecodeFormat format = timecodeFormatOf(first);
    const std::int64_t start = startFrameOf(first) + group.angles.first().offset;
    item.fields.insert(QStringLiteral("Start"), QString::fromStdString(Timecode(start, format).toString()));
    item.fields.insert(QStringLiteral("End"), QString::fromStdString(Timecode(start + item.durationFrames, format).toString()));
    setGroup(item, group);
    return item;
}

BinItem groupAngleClip(const BinItem& group, int angle) {
    const auto g = groupOf(group);
    if (!g || g->angles.isEmpty()) return group;
    if (angle < 0 || angle >= g->angles.size()) angle = g->selected;
    const GroupAngle& a = g->angles[angle];
    BinItem out = a.clip;
    out.durationFrames = group.durationFrames;
    if (!out.link.isEmpty()) {
        const qint64 first = out.link.value(QStringLiteral("first")).toInteger();
        out.link.insert(QStringLiteral("first"), first + fileFrames(a.offset, out.editRate, out.link));
    }
    out.composition.insert(QLatin1String(kRangeKey), QJsonObject{{QStringLiteral("from"), static_cast<qint64>(-a.offset)},
                                                                 {QStringLiteral("to"), static_cast<qint64>(a.clip.durationFrames - a.offset)}});
    return out;
}

std::optional<std::pair<std::int64_t, std::int64_t>> groupAngleRange(const BinItem& clip) {
    const QJsonValue v = clip.composition.value(QLatin1String(kRangeKey));
    if (!v.isObject()) return std::nullopt;
    const QJsonObject o = v.toObject();
    return std::pair<std::int64_t, std::int64_t>{o.value(QStringLiteral("from")).toInteger(), o.value(QStringLiteral("to")).toInteger()};
}

int stepAngle(int angle, int step, int count) {
    if (count <= 0) return 0;
    return ((angle + step) % count + count) % count;
}

} // namespace lumenis::model
