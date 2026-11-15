#include "lumenis/model/multichannel.h"

#include <QObject>
#include <QRegularExpression>

#include <algorithm>

// Multichannel audio tracks (multichannel.h).
namespace lumenis::model {

int channelCount(AudioFormat format) {
    switch (format) {
    case AudioFormat::Mono: return 1;
    case AudioFormat::Stereo: return 2;
    case AudioFormat::Surround51: return 6;
    case AudioFormat::Surround71: return 8;
    }
    return 1;
}

QString formatName(AudioFormat format) {
    switch (format) {
    case AudioFormat::Mono: return QStringLiteral("Mono");
    case AudioFormat::Stereo: return QStringLiteral("Stereo");
    case AudioFormat::Surround51: return QStringLiteral("5.1");
    case AudioFormat::Surround71: return QStringLiteral("7.1");
    }
    return {};
}

std::optional<AudioFormat> formatFromName(const QString& name) {
    for (AudioFormat f : {AudioFormat::Mono, AudioFormat::Stereo, AudioFormat::Surround51, AudioFormat::Surround71}) {
        if (formatName(f).compare(name.trimmed(), Qt::CaseInsensitive) == 0) return f;
    }
    return std::nullopt;
}

QString trackFormatsText(const QList<ChannelGroup>& groups) {
    QStringList parts;
    for (const ChannelGroup& g : groups) {
        QString channels;
        for (int k = 0; k < channelCount(g.format); ++k) channels += QStringLiteral("A%1").arg(g.first + k);
        parts << formatName(g.format) + QLatin1Char(' ') + channels;
    }
    return parts.join(QStringLiteral("; "));
}

std::optional<QList<ChannelGroup>> parseTrackFormats(const QString& text) {
    QList<ChannelGroup> out;
    static const QRegularExpression group(QStringLiteral(R"(^\s*(Stereo|5\.1|7\.1)\s+((?:A\d+)+)\s*$)"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression channel(QStringLiteral(R"(A(\d+))"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        if (part.trimmed().isEmpty()) continue;
        const auto m = group.match(part);
        if (!m.hasMatch()) return std::nullopt;
        ChannelGroup g;
        g.format = *formatFromName(m.captured(1));
        QList<int> numbers;
        for (auto it = channel.globalMatch(m.captured(2)); it.hasNext();) numbers << it.next().captured(1).toInt();
        if (numbers.size() != channelCount(g.format)) return std::nullopt;
        for (int k = 1; k < numbers.size(); ++k) {
            if (numbers.at(k) != numbers.at(0) + k) return std::nullopt;
        }
        g.first = numbers.first();
        out << g;
    }
    std::sort(out.begin(), out.end(), [](const ChannelGroup& a, const ChannelGroup& b) { return a.first < b.first; });
    return out;
}

QList<ChannelGroup> trackFormatsOf(const BinItem& item) {
    if (item.kind == BinItemKind::Sequence) return {};
    return parseTrackFormats(item.fields.value(QStringLiteral("Track Formats"))).value_or(QList<ChannelGroup>{});
}

int audioChannelsOf(const BinItem& item) {
    int n = 0;
    for (const QString& t : parseTrackList(item.fields.value(QStringLiteral("Tracks")))) {
        if (const auto ref = parseTrackName(t); ref && ref->kind == TrackKind::Audio) n = std::max(n, ref->number);
    }
    if (n == 0 && item.kind == BinItemKind::AudioClip && item.fields.value(QStringLiteral("Tracks")).isEmpty()) n = 2;
    return n;
}

bool setTrackFormats(BinItem& clip, const QList<ChannelGroup>& groups, QString* error) {
    auto failed = [&](const QString& text) {
        if (error) *error = text;
        return false;
    };
    if (clip.kind != BinItemKind::MasterClip && clip.kind != BinItemKind::AudioClip) {
        return failed(QObject::tr("Track formats can be set on master clips only."));
    }
    QList<ChannelGroup> sorted;
    for (const ChannelGroup& g : groups) {
        if (g.format != AudioFormat::Mono) sorted << g;
    }
    std::sort(sorted.begin(), sorted.end(), [](const ChannelGroup& a, const ChannelGroup& b) { return a.first < b.first; });
    const int channels = audioChannelsOf(clip);
    int next = 1;
    for (const ChannelGroup& g : sorted) {
        const int last = g.first + channelCount(g.format) - 1;
        if (g.first < next) return failed(QObject::tr("The track formats overlap."));
        if (g.first < 1 || last > channels) {
            return failed(QObject::tr("%1 has no A%2.").arg(clip.name).arg(std::max(last, g.first)));
        }
        if (g.format == AudioFormat::Stereo && g.first % 2 == 0) {
            return failed(QObject::tr("A stereo track pairs an odd track with the next one."));
        }
        next = last + 1;
    }
    if (sorted.isEmpty()) {
        clip.fields.remove(QStringLiteral("Track Formats"));
    } else {
        clip.fields.insert(QStringLiteral("Track Formats"), trackFormatsText(sorted));
    }
    return true;
}

QList<SourceAudioTrack> sourceAudioTracks(const BinItem& item) {
    QList<SourceAudioTrack> out;
    if (item.kind == BinItemKind::Sequence) {
        const Sequence seq = Sequence::fromItem(item);
        for (const Track& t : seq.tracks()) {
            if (t.kind == TrackKind::Audio) out.append({t.name(), t.format, t.number});
        }
        return out;
    }
    QList<ChannelGroup> groups = trackFormatsOf(item);
    int channel = 1;
    int number = 1;
    const int channels = audioChannelsOf(item);
    while (channel <= channels) {
        const auto g = std::find_if(groups.begin(), groups.end(), [&](const ChannelGroup& x) { return x.first == channel; });
        if (g != groups.end() && g->first + channelCount(g->format) - 1 <= channels) {
            out.append({trackName(TrackKind::Audio, number), g->format, channel});
            channel += channelCount(g->format);
        } else {
            out.append({trackName(TrackKind::Audio, number), AudioFormat::Mono, channel});
            ++channel;
        }
        ++number;
    }
    return out;
}

AudioFormat sourceFormat(const BinItem& item, const QString& sourceTrack) {
    for (const SourceAudioTrack& t : sourceAudioTracks(item)) {
        if (t.name == sourceTrack) return t.format;
    }
    return AudioFormat::Mono;
}

QString sourceChannel(const BinItem& item, const QString& sourceTrack) {
    if (item.kind == BinItemKind::Sequence) return sourceTrack;
    for (const SourceAudioTrack& t : sourceAudioTracks(item)) {
        if (t.name == sourceTrack) return trackName(TrackKind::Audio, t.first);
    }
    return sourceTrack;
}

namespace {

// The channel `k` after a segment's first channel: "A1" and 1 give "A2".
QString nextChannel(const QString& first, int k) {
    const auto ref = parseTrackName(first);
    if (!ref || ref->kind != TrackKind::Audio) return first;
    return trackName(TrackKind::Audio, ref->number + k);
}

} // namespace

bool splitToMono(Sequence& sequence, const QString& name, QString* error) {
    auto failed = [&](const QString& text) {
        if (error) *error = text;
        return false;
    };
    const Track* found = sequence.track(name);
    if (!found || found->kind != TrackKind::Audio) return failed(QObject::tr("%1 is not an audio track.").arg(name));
    const int n = channelCount(found->format);
    if (n < 2) return failed(QObject::tr("%1 is already a mono track.").arg(name));
    const Track original = *found;
    // Room for the new tracks: the audio tracks after it move down.
    int highest = 0;
    for (const Track& t : sequence.tracks()) {
        if (t.kind == TrackKind::Audio) highest = std::max(highest, t.number);
    }
    if (highest + n - 1 > kMaxTracks) return failed(QObject::tr("Splitting %1 would make more than %2 audio tracks.").arg(name).arg(kMaxTracks));
    // Rebuild the audio tracks in order.
    QList<Track> audio;
    for (const Track& t : sequence.tracks()) {
        if (t.kind == TrackKind::Audio) audio << t;
    }
    for (const Track& t : audio) sequence.removeTrack(t.name());
    int number = 1;
    for (const Track& t : audio) {
        if (t.number != original.number) {
            Track moved = t;
            moved.number = number++;
            sequence.restoreTrack(moved);
            continue;
        }
        for (int k = 0; k < n; ++k) {
            Track mono;
            mono.kind = TrackKind::Audio;
            mono.number = number++;
            mono.format = AudioFormat::Mono;
            mono.locked = original.locked;
            mono.syncLocked = original.syncLocked;
            // "Using Split Tracks to Mono on a multichannel audio track also designates the created LFE
            // track as LFE only" (p.710): the last channel of a 5.1 or 7.1 track (Pro Tools order).
            mono.lfeOnly = (original.format == AudioFormat::Surround51 || original.format == AudioFormat::Surround71) && k == n - 1;
            // "Removes stereo track effects such as Audio Track Effect plug-in effects" (p.735).
            mono.segments = original.segments;
            for (Segment& s : mono.segments) {
                if (s.isFiller()) continue;
                s.sourceTrack = nextChannel(s.sourceTrack, k);
                // "Converts stereo AudioSuite plug-in effects to mono effects ... Clears rendered
                // effects" (p.735).
                if (s.audioSuite) {
                    s.audioSuite->rendered.clear();
                    s.audioSuite->renderedKey.clear();
                }
                // "Panning odd-numbered tracks to the left and even-numbered tracks to the right".
                s.pan = mono.number % 2 == 1 ? -kPanMax : kPanMax;
                s.panKeys.clear();
                s.surround.clear(); // a stereo segment's pans were for both channels
                s.surroundKeys.clear();
            }
            sequence.restoreTrack(mono);
        }
    }
    return true;
}

int splitAllToMono(Sequence& sequence, QString* error) {
    Sequence work = sequence;
    int split = 0;
    // Top to bottom; each split renumbers the tracks below, so look again from the start.
    for (bool again = true; again;) {
        again = false;
        for (const Track& t : work.tracks()) {
            if (t.kind != TrackKind::Audio || channelCount(t.format) < 2) continue;
            if (!splitToMono(work, t.name(), error)) return 0;
            ++split;
            again = true;
            break;
        }
    }
    if (split == 0) {
        if (error) *error = QObject::tr("There are no multichannel tracks to split.");
        return 0;
    }
    sequence = work;
    return split;
}

} // namespace lumenis::model
