// Group clips (Editing Guide p.1123-1131; lumenis/model/group_clip.h, docs/spec/multicamera.md): making
// a group from synced clips, keeping it in the Group item's composition and playing an angle of it.
#include "lumenis/model/group_clip.h"


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

} // namespace

std::optional<GroupClip> groupOf(const BinItem& item) {
    if (item.kind != BinItemKind::Group) return std::nullopt;
    const QJsonObject o = item.composition.value(QLatin1String(kGroupKey)).toObject();
    GroupClip g;
    for (const QJsonValue& v : o.value(QStringLiteral("angles")).toArray()) {
        if (auto a = angleFromJson(v.toObject())) g.angles << *a;
    }
    g.selected = g.angles.isEmpty() ? 0 : std::clamp(o.value(QStringLiteral("selected")).toInt(), 0, static_cast<int>(g.angles.size()) - 1);
    const QJsonObject tracks = o.value(QStringLiteral("trackAngles")).toObject();
    for (auto it = tracks.begin(); it != tracks.end(); ++it) {
        const int angle = it.value().toInt(-1);
        if (angle >= 0 && angle < g.angles.size()) g.trackAngles.insert(it.key(), angle);
    }
    const QJsonObject speakers = o.value(QStringLiteral("speakerAngles")).toObject();
    for (auto it = speakers.begin(); it != speakers.end(); ++it) {
        const int angle = it.value().toInt(-1);
        if (angle >= 0 && angle < g.angles.size()) g.speakerAngles.insert(it.key(), angle);
    }
    return g;
}

bool isGroupClip(const BinItem& item) {
    return item.kind == BinItemKind::Group && item.composition.contains(QLatin1String(kGroupKey));
}

void setGroup(BinItem& item, const GroupClip& group) {
    QJsonArray angles;
    for (const GroupAngle& a : group.angles) angles.append(angleToJson(a));
    QJsonObject o{{QStringLiteral("angles"), angles}, {QStringLiteral("selected"), group.selected}};
    if (!group.trackAngles.isEmpty()) {
        QJsonObject tracks;
        for (auto it = group.trackAngles.cbegin(); it != group.trackAngles.cend(); ++it) tracks.insert(it.key(), it.value());
        o.insert(QStringLiteral("trackAngles"), tracks);
    }
    if (!group.speakerAngles.isEmpty()) {
        QJsonObject speakers;
        for (auto it = group.speakerAngles.cbegin(); it != group.speakerAngles.cend(); ++it) speakers.insert(it.key(), it.value());
        o.insert(QStringLiteral("speakerAngles"), speakers);
    }
    item.composition.insert(QLatin1String(kGroupKey), o);
}

int groupTrackAngle(const GroupClip& group, const QString& track) {
    const auto ref = parseTrackName(track);
    if (ref && ref->kind == TrackKind::Audio) {
        const int angle = group.trackAngles.value(track, -1);
        if (angle >= 0 && angle < group.angles.size()) return angle;
    }
    return group.selected;
}

QList<int> anglesWithTrack(const GroupClip& group, const QString& track) {
    QList<int> out;
    for (int i = 0; i < group.angles.size(); ++i) {
        if (sourceTracksOf(group.angles[i].clip).contains(track)) out << i;
    }
    return out;
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
        const auto point = groupSyncPoint(clip, options, &why);
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
    storeGroup(item, group, latest - earliest);
    return item;
}

void storeGroup(BinItem& item, const GroupClip& group, std::int64_t duration) {
    item.durationFrames = std::max<std::int64_t>(duration, 1);
    item.fields.insert(QStringLiteral("Tracks"), groupTracks(group).join(QLatin1Char(' ')));
    // Its timecode is the first clip's at the group's first frame (ours).
    if (!group.angles.isEmpty()) {
        const BinItem& first = group.angles.first().clip;
        const TimecodeFormat format = timecodeFormatOf(first);
        const std::int64_t start = startFrameOf(first) + group.angles.first().offset;
        item.fields.insert(QStringLiteral("Start"), QString::fromStdString(Timecode(start, format).toString()));
        item.fields.insert(QStringLiteral("End"), QString::fromStdString(Timecode(start + item.durationFrames, format).toString()));
    }
    setGroup(item, group);
}

namespace {

// The first track of `kind` a clip offers (its V1 or A1); empty when it has none.
QString firstTrack(const BinItem& clip, TrackKind kind) {
    for (const QString& t : sourceTracksOf(clip)) {
        if (const auto ref = parseTrackName(t); ref && ref->kind == kind) return t;
    }
    return {};
}

} // namespace

// The clip's sync point as a frame of the clip counted the same way for every clip: for marks the mark
// itself, for timecode the negative of where its timecode starts (so frame f is at f + start).
std::optional<std::int64_t> groupSyncPoint(const BinItem& clip, const GroupOptions& options, QString* why) {
    QString ignored;
    if (!why) why = &ignored;
    switch (options.sync) {
    case GroupSync::Inpoints:
    case GroupSync::Outpoints: {
        const bool in = options.sync == GroupSync::Inpoints;
        if (options.marks) {
            if (const auto mark = options.marks(clip, in)) return mark;
        }
        // The bin's Mark IN / Mark OUT columns, as timecode of the clip.
        const QString text = clip.fields.value(in ? QStringLiteral("Mark IN") : QStringLiteral("Mark OUT"));
        if (const auto tc = timecodeFrames(text, clip.editRate)) return *tc - startFrameOf(clip);
        *why = in ? QStringLiteral("\"%1\" has no IN point.").arg(clip.name) : QStringLiteral("\"%1\" has no OUT point.").arg(clip.name);
        return std::nullopt;
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
    case GroupSync::Waveform:
        if (options.waveform) {
            if (const auto point = options.waveform(clip)) return *point;
        }
        *why = QStringLiteral("\"%1\" could not be synced by its sound.").arg(clip.name);
        return std::nullopt;
    case GroupSync::FilmTc:
    case GroupSync::Ink:
    case GroupSync::AuxInk: break;
    }
    *why = QStringLiteral("Grouping by this option is not available.");
    return std::nullopt;
}


Sequence groupEditSequence(const QString& binId, const BinItem& group) {
    Sequence seq;
    const auto g = groupOf(group);
    if (!g) return seq;
    int video = 0;
    int audio = 0;
    for (const GroupAngle& a : g->angles) {
        // Where the angle has material within the group: group frames [from, to).
        const std::int64_t from = std::max<std::int64_t>(0, -a.offset);
        const std::int64_t to = std::min(group.durationFrames, a.clip.durationFrames - a.offset);
        if (to <= from) continue;
        for (const TrackKind kind : {TrackKind::Video, TrackKind::Audio}) {
            const QString source = firstTrack(a.clip, kind);
            if (source.isEmpty()) continue;
            const QString name = trackName(kind, kind == TrackKind::Video ? ++video : ++audio);
            Track* t = seq.ensureTrack(name);
            if (!t) continue;
            Segment s;
            s.clipId = a.clipId;
            s.binId = a.binId.isEmpty() ? binId : a.binId;
            s.sourceMob = a.clip.mobId;
            s.name = a.clip.name;
            s.sourceTrack = source;
            s.sourceIn = from + a.offset;
            s.length = to - from;
            t->segments.clear();
            if (from > 0) t->segments << Segment::filler(from);
            t->segments << s;
        }
    }
    seq.setEditedGroup(binId, group.id);
    return seq;
}

std::optional<std::pair<GroupClip, std::int64_t>> groupFromSequence(const Sequence& sequence, const Rational& rate,
                                                                   const GroupClipLookup& lookup, std::int64_t atLeast,
                                                                   QString* error) {
    const auto fail = [&](const QString& why) -> std::optional<std::pair<GroupClip, std::int64_t>> {
        if (error) *error = why;
        return std::nullopt;
    };
    GroupClip group;
    QStringList seen;
    std::int64_t end = 0;
    for (const TrackKind kind : {TrackKind::Video, TrackKind::Audio}) {
        for (const Track& t : sequence.tracks()) {
            if (t.kind != kind) continue;
            std::int64_t at = 0;
            for (const Segment& s : t.segments) {
                const std::int64_t start = at;
                at += s.length;
                if (s.isFiller()) continue;
                end = std::max(end, at);
                if (seen.contains(s.clipId)) continue;
                const auto clip = lookup ? lookup(s.binId, s.clipId) : std::nullopt;
                if (!clip || !canBeGroupAngle(*clip)) continue;
                if (!(clip->editRate == rate)) return fail(QStringLiteral("\"%1\" is at another frame rate than the sequence.").arg(clip->name));
                seen << s.clipId;
                GroupAngle a;
                a.binId = s.binId;
                a.clipId = s.clipId;
                a.clip = *clip;
                // The segment shows clip frame sourceIn at sequence frame start.
                a.offset = s.sourceIn - start;
                group.angles << a;
            }
        }
    }
    if (group.angles.size() < 2) return fail(QStringLiteral("A group needs two or more clips in the sequence."));
    return std::pair{group, std::max(end, atLeast)};
}

int refreshGroupEdits(Sequence& sequence, const GroupClipLookup& lookup) {
    int changed = 0;
    QStringList names;
    for (const Track& t : sequence.tracks()) names << t.name();
    for (const QString& name : names) {
        Track* t = sequence.track(name);
        for (Segment& s : t->segments) {
            if (s.isFiller() || !s.groupAngle || !lookup) continue;
            const auto clip = lookup(s.binId, s.clipId);
            const auto g = clip ? groupOf(*clip) : std::nullopt;
            if (!g || g->angles.isEmpty()) continue;
            QList<std::int64_t> current;
            for (const GroupAngle& a : g->angles) current << a.offset;
            if (current == s.groupOffsets) continue;
            const int k = std::clamp(*s.groupAngle, 0, static_cast<int>(g->angles.size()) - 1);
            // The shown angle keeps showing the same material.
            if (k < s.groupOffsets.size()) s.sourceIn += s.groupOffsets[k] - current[k];
            s.groupOffsets = current;
            ++changed;
        }
    }
    return changed;
}

BinItem groupAngleClip(const BinItem& group, int angle, const QList<std::int64_t>& offsets) {
    const auto g = groupOf(group);
    if (!g || g->angles.isEmpty()) return group;
    if (angle < 0 || angle >= g->angles.size()) angle = g->selected;
    GroupAngle a = g->angles[angle];
    if (angle < offsets.size()) a.offset = offsets[angle];
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

namespace {

constexpr char kMultigroupKey[] = "multigroup";

} // namespace

bool isMultigroup(const BinItem& item) {
    return item.kind == BinItemKind::Group && item.composition.contains(QLatin1String(kMultigroupKey));
}

std::optional<BinItem> multigroupSequence(const BinItem& item) {
    if (!isMultigroup(item)) return std::nullopt;
    BinItem out = item;
    out.kind = BinItemKind::Sequence;
    out.composition = item.composition.value(QLatin1String(kMultigroupKey)).toObject();
    return out;
}

void setMultigroupSequence(BinItem& item, const Sequence& sequence) {
    item.composition = QJsonObject{{QLatin1String(kMultigroupKey), sequence.toJson()}};
    item.durationFrames = std::max<std::int64_t>(sequence.duration(), 1);
}

std::optional<MultiGroupResult> multiGroup(const Bin& bin, const QString& binId, QList<std::pair<QString, BinItem>> clips,
                                           const GroupOptions& options, QString* error) {
    const auto fail = [&](const QString& why) -> std::optional<MultiGroupResult> {
        if (error) *error = why;
        return std::nullopt;
    };
    // "Sort the clips by name in the bin" (p.1125, step 1).
    std::stable_sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) {
        return QString::localeAwareCompare(a.second.name, b.second.name) < 0;
    });
    // Each clip's span on the common timecode.
    GroupOptions timecode = options;
    timecode.sync = options.sync == GroupSync::AuxTimecode ? GroupSync::AuxTimecode : GroupSync::SourceTimecode;
    struct Span {
        std::int64_t from;
        std::int64_t to;
        std::pair<QString, BinItem> clip;
    };
    QList<Span> spans;
    QString why;
    for (const auto& c : clips) {
        if (!canBeGroupAngle(c.second)) continue;
        const auto point = groupSyncPoint(c.second, timecode, &why);
        if (!point) return fail(why);
        spans.append({-*point, c.second.durationFrames - *point, c});
    }
    std::stable_sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.from < b.from; });
    // Takes: runs of spans that overlap.
    QList<QList<std::pair<QString, BinItem>>> takes;
    std::int64_t end = std::numeric_limits<std::int64_t>::min();
    for (const Span& sp : spans) {
        if (takes.isEmpty() || sp.from >= end) takes.append(QList<std::pair<QString, BinItem>>());
        takes.last().append(sp.clip);
        end = std::max(end, sp.to);
    }
    MultiGroupResult result;
    Bin names = bin; // names taken by the groups made so far
    Sequence seq;
    std::int64_t at = 0;
    for (QList<std::pair<QString, BinItem>> take : takes) {
        if (take.size() < 2) {
            result.skipped += static_cast<int>(take.size());
            continue;
        }
        // In the bin's (name) order within the take.
        std::stable_sort(take.begin(), take.end(), [](const auto& a, const auto& b) {
            return QString::localeAwareCompare(a.second.name, b.second.name) < 0;
        });
        auto group = groupClips(names, take, options, &why);
        if (!group) return fail(why);
        names.items << *group;
        TrackPatch patch;
        patch.setSourceTracks(sourceTracksOf(*group));
        applyEdit(seq, EditKind::Overwrite, EditPlan{0, at, group->durationFrames, {}}, binId, *group, patch);
        at += group->durationFrames;
        result.groups << *group;
    }
    if (result.groups.isEmpty()) return fail(QStringLiteral("No take has two or more clips with common timecode."));
    BinItem& mg = result.multigroup;
    mg.id = newItemId();
    mg.kind = BinItemKind::Group;
    const QString base = groupOf(result.groups.first())->angles.first().clip.name;
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1.MGrp.%2").arg(base).arg(n);
        if (std::none_of(names.items.cbegin(), names.items.cend(), [&](const BinItem& i) { return i.name == candidate; })) {
            mg.name = candidate;
            break;
        }
    }
    mg.created = options.when.isValid() ? options.when : QDateTime::currentDateTimeUtc();
    mg.modified = mg.created;
    mg.editRate = result.groups.first().editRate;
    mg.fields.insert(QStringLiteral("Tracks"), seq.trackNames().join(QLatin1Char(' ')));
    mg.fields.insert(QStringLiteral("Start"), result.groups.first().fields.value(QStringLiteral("Start")));
    setMultigroupSequence(mg, seq);
    return result;
}

int commitMulticamEdits(Sequence& sequence, const GroupClipLookup& lookup) {
    int replaced = 0;
    QStringList names;
    for (const Track& t : sequence.tracks()) names << t.name();
    for (const QString& name : names) {
        for (Segment& s : sequence.track(name)->segments) {
            if (s.isFiller() || !s.groupAngle || !lookup) continue;
            const auto clip = lookup(s.binId, s.clipId);
            const auto g = clip ? groupOf(*clip) : std::nullopt;
            if (!g || *s.groupAngle < 0 || *s.groupAngle >= g->angles.size()) continue;
            const int k = *s.groupAngle;
            const GroupAngle& a = g->angles[k];
            const std::int64_t offset = k < s.groupOffsets.size() ? s.groupOffsets[k] : a.offset;
            if (s.sourceIn + offset < 0) continue;
            s.clipId = a.clipId;
            s.binId = a.binId;
            s.sourceMob = a.clip.mobId;
            s.name = a.clip.name;
            s.sourceIn += offset;
            s.groupAngle.reset();
            s.groupOffsets.clear();
            ++replaced;
        }
    }
    return replaced;
}

std::optional<GroupSegmentAt> groupSegmentAt(const Sequence& sequence, std::int64_t frame, const QStringList& tracks) {
    for (const QString& name : tracks) {
        const Track* t = sequence.track(name);
        if (!t) continue;
        std::int64_t start = 0;
        const int i = t->segmentAt(frame, &start);
        if (i < 0 || !t->segments[i].groupAngle) continue;
        GroupSegmentAt at;
        at.track = name;
        at.start = start;
        at.segment = t->segments[i];
        at.groupFrame = at.segment.sourceIn + (frame - start);
        return at;
    }
    return std::nullopt;
}

bool setGroupAngleAt(Sequence& sequence, const QString& track, std::int64_t frame, int angle, bool cut, const GroupClip& group) {
    if (angle < 0 || angle >= group.angles.size()) return false;
    Track* t = sequence.track(track);
    if (!t) return false;
    std::int64_t start = 0;
    int i = t->segmentAt(frame, &start);
    if (i < 0 || !t->segments[i].groupAngle || *t->segments[i].groupAngle == angle) return false;
    if (cut && frame > start) {
        if (!sequence.addEdit(track, frame)) return false;
        t = sequence.track(track);
        i = t->segmentAt(frame, &start);
        if (i < 0) return false;
    }
    Segment& s = t->segments[i];
    s.groupAngle = angle;
    s.name = group.angles[angle].clip.name;
    return true;
}

std::optional<int> keypadSplitPosition(int digit, int split) {
    if (digit < 1 || digit > 9) return std::nullopt;
    if (split == 9) {
        // 7 8 9 on the top row, 1 2 3 on the bottom.
        const int row = 2 - (digit - 1) / 3;
        return row * 3 + (digit - 1) % 3;
    }
    switch (digit) {
    case 7: return 0;
    case 8:
    case 9: return 1;
    case 4:
    case 1: return 2;
    default: return 3; // 2, 3, 5, 6
    }
}

int autoCut(Sequence& sequence, const QString& track, std::int64_t segmentStart, const GroupClip& group,
            const QList<std::pair<std::int64_t, QString>>& lines) {
    const Track* t = sequence.track(track);
    if (!t) return 0;
    std::int64_t start = 0;
    const int i = t->segmentAt(segmentStart, &start);
    if (i < 0 || start != segmentStart || !t->segments[i].groupAngle) return 0;
    const std::int64_t sourceIn = t->segments[i].sourceIn;
    const std::int64_t length = t->segments[i].length;
    int switches = 0;
    for (const auto& [groupFrame, speaker] : lines) {
        const auto angle = group.speakerAngles.constFind(speaker);
        if (angle == group.speakerAngles.cend()) continue;
        const std::int64_t at = segmentStart + groupFrame - sourceIn;
        if (at < segmentStart || at >= segmentStart + length) continue;
        if (setGroupAngleAt(sequence, track, at, *angle, true, group)) ++switches;
    }
    return switches;
}

int stepAngle(int angle, int step, int count) {
    if (count <= 0) return 0;
    return ((angle + step) % count + count) % count;
}

} // namespace lumenis::model
