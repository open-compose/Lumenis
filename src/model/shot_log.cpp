// The Shot Log tab's events (shot_log.h; Editing Guide p.1197-1198, p.131, the merge steps of "Merging Additional
// Metadata for Clips"). Events are clips with a Start, an End and a source (a tape or a source file); combining makes
// one master clip over a group of events and a subclip for each (subclipItem, so each stays linked to it); merging
// finds the selected clip of the same source that holds the event.
#include "lumenis/model/shot_log.h"

#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace lumenis::model {

namespace {

// The source an event names: its Tape, else its Source File (the merge steps match "the matching tape name or source
// file name", p.1198 and the merge steps).
QString sourceKey(const BinItem& c) {
    const QString tape = c.fields.value(QStringLiteral("Tape"));
    return tape.isEmpty() ? QStringLiteral("file:") + c.fields.value(QStringLiteral("Source File")) : QStringLiteral("tape:") + tape;
}

bool isClip(const BinItem& c) { return c.kind == BinItemKind::MasterClip || c.kind == BinItemKind::AudioClip; }

// The frames [first, last) an event covers, by its Start and its length.
std::pair<std::int64_t, std::int64_t> spanOf(const BinItem& c) {
    const std::int64_t first = startFrameOf(c);
    return {first, first + c.durationFrames};
}

// Track names as a Tracks column: "V1 A1-2", runs of numbers joined with a dash.
QString tracksText(const QStringList& names) {
    std::map<int, std::set<int>> byKind; // 0 video, 1 audio, 2 data
    for (const QString& n : names) {
        const auto ref = parseTrackName(n);
        if (!ref) continue;
        byKind[ref->kind == TrackKind::Video ? 0 : ref->kind == TrackKind::Audio ? 1 : 2].insert(ref->number);
    }
    QStringList parts;
    for (const auto& [kind, numbers] : byKind) {
        const QString letter = kind == 0 ? QStringLiteral("V") : kind == 1 ? QStringLiteral("A") : QStringLiteral("D");
        const std::vector<int> n(numbers.begin(), numbers.end());
        for (std::size_t i = 0; i < n.size();) {
            std::size_t j = i;
            while (j + 1 < n.size() && n[j + 1] == n[j] + 1) ++j;
            parts << (j == i ? QStringLiteral("%1%2").arg(letter).arg(n[i]) : QStringLiteral("%1%2-%3").arg(letter).arg(n[i]).arg(n[j]));
            i = j + 1;
        }
    }
    return parts.join(QLatin1Char(' '));
}

// A subclip of `master` over the event's frames, carrying the event's name and columns: "subclips that represent the
// original events" (p.1197).
std::optional<BinItem> eventSubclip(const Bin& bin, const BinItem& master, const BinItem& event, const QDateTime& when) {
    const std::int64_t in = spanOf(event).first - startFrameOf(master);
    auto sub = subclipItem(bin, master, in, in + event.durationFrames, when);
    if (!sub) return std::nullopt;
    sub->name = event.name;
    for (auto it = event.fields.constBegin(); it != event.fields.constEnd(); ++it) {
        if (it.key() == QLatin1String("Start") || it.key() == QLatin1String("End")) continue;
        sub->fields.insert(it.key(), it.value());
    }
    return sub;
}

// The master clip over a group of events (all of one source and rate), named `name`: the first event's columns, its
// first Start to its last End, every event's tracks.
BinItem combinedMaster(const QList<BinItem>& group, const QString& name, const QDateTime& when) {
    BinItem m = group.first();
    m.id = newItemId();
    m.mobId = MobId::generate();
    m.name = name;
    m.created = when;
    m.modified = when;
    std::int64_t first = spanOf(group.first()).first;
    std::int64_t last = spanOf(group.first()).second;
    QStringList tracks;
    for (const BinItem& e : group) {
        first = std::min(first, spanOf(e).first);
        last = std::max(last, spanOf(e).second);
        tracks << parseTrackList(e.fields.value(QStringLiteral("Tracks")));
    }
    m.durationFrames = last - first;
    const TimecodeFormat format = timecodeFormatOf(m);
    const QString start = QString::fromStdString(Timecode(first, format).toString());
    m.fields.insert(QStringLiteral("Start"), start);
    m.fields.insert(QStringLiteral("End"), QString::fromStdString(Timecode(last, format).toString()));
    m.fields.insert(QStringLiteral("Tracks"), tracksText(tracks));
    m.fields.remove(QStringLiteral("Duration"));
    m.kind = m.fields.value(QStringLiteral("Tracks")).contains(QLatin1Char('V')) ? BinItemKind::MasterClip : BinItemKind::AudioClip;
    // A new source of its own for the tape or file, starting where the clip does.
    for (ClipSource& s : m.sources) {
        s.mob = MobId::generate();
        s.startTimecode = start;
    }
    return m;
}

} // namespace

ShotLogEvents shotLogEventsOf(const QString& value) {
    if (value == QLatin1String("scene")) return ShotLogEvents::Scene;
    if (value == QLatin1String("camroll")) return ShotLogEvents::CameraRoll;
    if (value == QLatin1String("sources")) return ShotLogEvents::KnownSources;
    if (value == QLatin1String("masters")) return ShotLogEvents::KnownMasterClips;
    return ShotLogEvents::AsLogged;
}

ShotLogResult shotLogEvents(ShotLogEvents mode, const QList<BinItem>& events, const QList<BinItem>& known, const Bin& bin,
                            const QDateTime& when) {
    ShotLogResult out;
    auto warn = [&](const QString& text) {
        if (!out.warnings.contains(text)) out.warnings << text;
    };
    if (mode == ShotLogEvents::AsLogged) {
        out.added = events;
        return out;
    }
    if (mode == ShotLogEvents::Scene || mode == ShotLogEvents::CameraRoll) {
        const QString column = mode == ShotLogEvents::Scene ? QStringLiteral("Scene") : QStringLiteral("Camroll");
        // Groups in the order their first event comes, each keyed by source, rate and the column's value.
        using Key = std::tuple<QString, std::int64_t, std::int64_t, QString>;
        std::map<Key, int> groupOf;
        QList<QList<BinItem>> groups;
        QList<QString> values;
        // The events kept as logged, by place, so the bin gets them in the log's order with the groups.
        QList<std::pair<int, BinItem>> order; // group index (or -1) and the event kept as logged
        for (const BinItem& e : events) {
            const QString value = e.fields.value(column).trimmed();
            if (value.isEmpty() || !isClip(e)) {
                warn(QStringLiteral("events without a %1 column kept as logged").arg(column));
                order.append({-1, e});
                continue;
            }
            const Key key{sourceKey(e), e.editRate.num(), e.editRate.den(), value};
            auto it = groupOf.find(key);
            if (it == groupOf.end()) {
                it = groupOf.emplace(key, static_cast<int>(groups.size())).first;
                groups.append(QList<BinItem>{});
                values << value;
                order.append({it->second, BinItem{}});
            }
            groups[it->second] << e;
        }
        for (const auto& [group, kept] : order) {
            if (group < 0) {
                out.added << kept;
                continue;
            }
            const BinItem master = combinedMaster(groups[group], values[group], when);
            out.added << master;
            for (const BinItem& e : groups[group]) {
                if (auto sub = eventSubclip(bin, master, e, when)) out.added << *sub;
            }
        }
        return out;
    }
    // The merge modes: the selected clip of the event's source and rate that holds it (KnownSources) or has exactly
    // its Start and End ("This must be an exact match and so should the START and END timecodes", KnownMasterClips).
    QList<BinItem> updated;
    for (const BinItem& e : events) {
        const auto [first, last] = spanOf(e);
        const BinItem* match = nullptr;
        for (const BinItem& k : known) {
            if (!isClip(k) || sourceKey(k) != sourceKey(e) || k.editRate != e.editRate) continue;
            const auto [kFirst, kLast] = spanOf(k);
            const bool fits = mode == ShotLogEvents::KnownSources ? kFirst <= first && last <= kLast : kFirst == first && kLast == last;
            if (fits) {
                match = &k;
                break;
            }
        }
        if (!match) {
            warn(mode == ShotLogEvents::KnownSources ? QStringLiteral("events outside the selected clips kept as logged")
                                                     : QStringLiteral("events matching no selected master clip kept as logged"));
            out.added << e;
            continue;
        }
        if (mode == ShotLogEvents::KnownSources) {
            if (auto sub = eventSubclip(bin, *match, e, when)) out.added << *sub;
            continue;
        }
        // Merged onto the master clip: the log's columns over the clip's, its name, media and sources kept. A clip
        // matched by two events takes both, the later last.
        auto it = std::find_if(updated.begin(), updated.end(), [&](const BinItem& u) { return u.id == match->id; });
        BinItem merged = it == updated.end() ? *match : *it;
        for (auto f = e.fields.constBegin(); f != e.fields.constEnd(); ++f) {
            if (f.key() == QLatin1String("Name")) continue;
            merged.fields.insert(f.key(), f.value());
        }
        merged.modified = when;
        if (it == updated.end()) updated << merged;
        else *it = merged;
    }
    out.updated = updated;
    return out;
}

} // namespace lumenis::model
