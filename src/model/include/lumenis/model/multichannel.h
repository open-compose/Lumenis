#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QString>

#include <optional>

// Multichannel audio tracks (Editing Guide p.670-672, p.734-736; docs/spec/multichannel.md): stereo,
// 5.1 and 7.1 tracks that carry several channels in one track.
//
// - A clip's channels are its audio tracks as captured or linked, A1 to An (its Tracks column). Its
//   Track Formats group consecutive channels into multichannel tracks ("Stereo A1A2", p.671). The
//   source tracks a clip offers are these groups and the channels left alone, numbered in order:
//   with "Stereo A1A2", A1 is the stereo pair and A2 the third channel (ours: the guide does not say
//   how the source tracks are numbered).
// - A segment on a multichannel sequence track names its first channel (Segment::sourceTrack, "A1");
//   the track's format says how many channels follow. A clip whose formats change later keeps its
//   segments as they are ("This does not cause a problem with editing, playback, or any other
//   operation", p.671).
// - A multichannel track's channels are in Pro Tools order (L C R Ls Rs Lfe; 7.1: L C R Lss Rss Lsr
//   Rsr Lfe), the default the guide gives for surround (p.685).
namespace lumenis::model {

// Channels in a track of each format: 1, 2, 6 and 8.
int channelCount(AudioFormat format);
// "Mono", "Stereo", "5.1", "7.1": the names the Track Formats column uses.
QString formatName(AudioFormat format);
std::optional<AudioFormat> formatFromName(const QString& name);

// One multichannel group of a clip's channels: its format and first channel (1 for A1).
struct ChannelGroup {
    AudioFormat format = AudioFormat::Stereo;
    int first = 1;
    bool operator==(const ChannelGroup&) const = default;
};
// The Track Formats column (p.671): "Stereo A1A2", several groups separated by "; " (ours), in
// channel order. Mono channels are not listed.
QString trackFormatsText(const QList<ChannelGroup>& groups);
// Reads it back; nothing for text that is not a list of groups of consecutive channels.
std::optional<QList<ChannelGroup>> parseTrackFormats(const QString& text);
// The clip's groups (its Track Formats field); empty for sequences and clips without one.
QList<ChannelGroup> trackFormatsOf(const BinItem& item);
// How many audio channels a clip has: its Tracks column's audio tracks.
int audioChannelsOf(const BinItem& item);
// Set Multichannel Audio (p.671-672): `groups` as the clip's Track Formats. Groups must not overlap,
// must fit the clip's channels, and stereo groups must start on an odd channel (the Modify dialog's
// format buttons sit under paired tracks, p.671). "You can modify audio formats for master clips
// only" (p.670): master clips and audio clips. False with `error` otherwise, the clip unchanged.
bool setTrackFormats(BinItem& clip, const QList<ChannelGroup>& groups, QString* error = nullptr);

// One source track a clip offers: its name, format and first channel (1 for A1).
struct SourceAudioTrack {
    QString name;
    AudioFormat format = AudioFormat::Mono;
    int first = 1;
};
// A clip's audio source tracks, in order (see above). For a sequence, its audio tracks, each
// standing for itself.
QList<SourceAudioTrack> sourceAudioTracks(const BinItem& item);
// The format of a source track of `item` (a clip's group or a sequence's track); Mono for video, an
// unknown track or a mono channel.
AudioFormat sourceFormat(const BinItem& item, const QString& sourceTrack);
// What a segment edited from `sourceTrack` of a clip names as its sourceTrack: the first channel
// ("A3"); other tracks, and sequences, keep their name.
QString sourceChannel(const BinItem& item, const QString& sourceTrack);

// Split Track to Mono (p.734-736): the multichannel track `track` becomes a mono track with its first
// channel, and a mono track for each other channel is added below it, the tracks after it
// renumbered. Each new track's segments name their own channel. As the guide lists: the track's
// Audio Track Effects are removed; AudioSuite effects stay but lose their renders; volume
// automation stays; the tracks are panned odd left, even right (their pan automation is dropped,
// ours). Locks are kept. False with `error` for a mono track, or when the tracks would pass the
// limit.
bool splitToMono(Sequence& sequence, const QString& track, QString* error = nullptr);
// Split All Tracks to Mono (p.736): every multichannel track, top to bottom. Returns how many were
// split; on failure nothing changes and `error` says why.
int splitAllToMono(Sequence& sequence, QString* error = nullptr);

} // namespace lumenis::model
