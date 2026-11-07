// The transcript a monitor shows (lumenis/model/transcript_view.h).
#include "lumenis/model/transcript_view.h"

#include "lumenis/model/group_clip.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace lumenis::model {

namespace {

std::int64_t frameOf(double seconds, const Rational& rate) {
    return static_cast<std::int64_t>(std::floor(seconds * rate.toDouble() + 1e-6));
}

// Collects the words of one transcript whose clip frames (master frame - base) fall in [from, to), placed at
// `at + clipFrame - from`, into lines kept by (mob, line).
class Collector {
public:
    explicit Collector(const TranscriptFor& transcriptFor) : transcriptFor_(transcriptFor) {}

    void add(const QString& mob, const QString& clipName, std::int64_t base, std::int64_t from, std::int64_t to,
             std::int64_t at, const Rational& rate) {
        const std::optional<Transcript>* t = transcript(mob);
        if (!t->has_value()) {
            if (!out.notTranscribedMobs.contains(mob)) {
                out.notTranscribedMobs << mob;
                out.notTranscribed << clipName;
            }
            return;
        }
        const Transcript& tr = **t;
        if (!out.speakers.contains(mob)) {
            QList<std::pair<int, QString>> speakers;
            QList<int> ids = speakerIds(tr);
            for (auto it = tr.speakers.cbegin(); it != tr.speakers.cend(); ++it) {
                if (!ids.contains(it.key())) ids << it.key();
            }
            std::sort(ids.begin(), ids.end());
            for (const int id : ids) speakers.append({id, tr.speakerName(id)});
            out.speakers.insert(mob, speakers);
        }
        for (int l = 0; l < tr.lines.size(); ++l) {
            const TranscriptLine& line = tr.lines[l];
            for (int w = 0; w < line.words.size(); ++w) {
                const std::int64_t clipFrame = frameOf(line.words[w].start, rate) - base;
                if (clipFrame < from || clipFrame >= to) continue;
                ShownWord sw;
                sw.text = line.words[w].text;
                sw.frame = at + clipFrame - from;
                sw.endFrame = std::max(sw.frame + 1, at + frameOf(line.words[w].end, rate) - base - from);
                sw.mob = mob;
                sw.line = l;
                sw.word = w;
                const QString key = QStringLiteral("%1/%2/%3/%4").arg(mob).arg(l).arg(w).arg(sw.frame);
                if (seen_.contains(key)) continue; // the same clip on two tracks
                seen_.insert(key);
                const QString lineKey = QStringLiteral("%1/%2/%3").arg(mob).arg(l).arg(at - from);
                auto found = lineIndex_.constFind(lineKey);
                if (found == lineIndex_.constEnd()) {
                    ShownLine sl;
                    sl.speakerId = line.speaker;
                    sl.speaker = tr.speakerName(line.speaker);
                    sl.mob = mob;
                    sl.line = l;
                    sl.frame = sw.frame;
                    found = lineIndex_.insert(lineKey, static_cast<int>(out.lines.size()));
                    out.lines << sl;
                }
                out.lines[*found].words << sw;
            }
        }
    }

    ShownTranscript finish() {
        for (ShownLine& l : out.lines) {
            std::sort(l.words.begin(), l.words.end(), [](const ShownWord& a, const ShownWord& b) { return a.frame < b.frame; });
            if (!l.words.isEmpty()) l.frame = l.words.first().frame;
        }
        std::stable_sort(out.lines.begin(), out.lines.end(), [](const ShownLine& a, const ShownLine& b) { return a.frame < b.frame; });
        return out;
    }

    ShownTranscript out;

private:
    const std::optional<Transcript>* transcript(const QString& mob) {
        auto it = cache_.find(mob);
        if (it == cache_.end()) it = cache_.insert(mob, transcriptFor_ ? transcriptFor_(mob) : std::nullopt);
        return &it.value();
    }

    const TranscriptFor& transcriptFor_;
    QHash<QString, std::optional<Transcript>> cache_;
    QHash<QString, int> lineIndex_;
    QSet<QString> seen_;
};

// The master clip and base of a group's angle (its clip's own source, moved by the angle's offset).
std::optional<std::pair<QString, std::int64_t>> angleSource(const GroupClip& group, int angle, const QList<std::int64_t>& offsets) {
    if (angle < 0 || angle >= group.angles.size()) return std::nullopt;
    const GroupAngle& a = group.angles[angle];
    const auto source = transcriptSource(a.clip);
    if (!source) return std::nullopt;
    const std::int64_t offset = angle < offsets.size() ? offsets[angle] : a.offset;
    return std::pair{source->first, source->second + offset};
}

} // namespace

std::optional<std::pair<QString, std::int64_t>> transcriptSource(const BinItem& clip) {
    if (clip.kind == BinItemKind::MasterClip || clip.kind == BinItemKind::AudioClip) {
        return std::pair{QString::fromStdString(mobIdOf(clip).toString()), std::int64_t(0)};
    }
    if (clip.kind == BinItemKind::Subclip) {
        for (const ClipSource& s : clip.sources) {
            if (s.kind == ClipSource::Kind::Clip) return std::pair{QString::fromStdString(s.mob.toString()), s.offset};
        }
    }
    return std::nullopt;
}

ShownTranscript shownTranscript(const BinItem& item, const TranscriptFor& transcriptFor) {
    Collector c(transcriptFor);
    std::optional<std::pair<QString, std::int64_t>> source;
    QString name = item.name;
    if (const auto group = groupOf(item)) {
        source = angleSource(*group, group->selected, {});
        if (source) name = group->angles[group->selected].clip.name;
    } else {
        source = transcriptSource(item);
    }
    if (source) c.add(source->first, name, source->second, 0, std::max<std::int64_t>(item.durationFrames, 1), 0, item.editRate);
    return c.finish();
}

ShownTranscript shownTranscript(const Sequence& sequence, const QStringList& tracks, const TranscriptFor& transcriptFor,
                                const ShownClipLookup& lookup) {
    Collector c(transcriptFor);
    for (const Track& t : sequence.tracks()) {
        if (t.kind == TrackKind::Audio) c.out.availableTracks << t.name();
    }
    c.out.tracks = tracks.isEmpty() ? c.out.availableTracks : tracks;
    for (const QString& name : c.out.tracks) {
        const Track* t = sequence.track(name);
        if (!t || t->kind != TrackKind::Audio) continue;
        std::int64_t at = 0;
        for (const Segment& s : t->segments) {
            const std::int64_t start = at;
            at += s.length;
            if (s.isFiller() || !lookup) continue;
            const auto clip = lookup(s.binId, s.clipId);
            if (!clip) continue;
            std::optional<std::pair<QString, std::int64_t>> source;
            QString clipName = clip->name;
            if (s.groupAngle) {
                if (const auto group = groupOf(*clip)) {
                    source = angleSource(*group, *s.groupAngle, s.groupOffsets);
                    if (source) clipName = group->angles[*s.groupAngle].clip.name;
                }
            } else {
                source = transcriptSource(*clip);
            }
            // Words to frames at the clip's own rate (ours: mixed rate segments are not adapted here).
            if (source) c.add(source->first, clipName, source->second, s.sourceIn, s.sourceIn + s.length, start, clip->editRate);
        }
    }
    return c.finish();
}

ShownTranscript groupedLines(const ShownTranscript& shown, bool groupSpeakers, std::int64_t newLineAfter) {
    if (!groupSpeakers) return shown;
    ShownTranscript out = shown;
    out.lines.clear();
    for (const ShownLine& l : shown.lines) {
        if (!out.lines.isEmpty()) {
            ShownLine& last = out.lines.last();
            const bool sameSpeaker = last.speaker == l.speaker;
            const bool fits = newLineAfter <= 0 || l.words.isEmpty() || l.words.last().endFrame - last.frame <= newLineAfter;
            if (sameSpeaker && fits) {
                last.words += l.words;
                continue;
            }
        }
        out.lines << l;
    }
    return out;
}

std::optional<std::pair<int, int>> wordAt(const ShownTranscript& shown, std::int64_t frame) {
    std::optional<std::pair<int, int>> best;
    std::int64_t bestFrame = 0;
    for (int l = 0; l < shown.lines.size(); ++l) {
        const QList<ShownWord>& words = shown.lines[l].words;
        for (int w = 0; w < words.size(); ++w) {
            if (words[w].frame <= frame && (!best || words[w].frame >= bestFrame)) {
                best = std::pair{l, w};
                bestFrame = words[w].frame;
            }
        }
    }
    return best;
}

} // namespace lumenis::model
