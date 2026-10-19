#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/media.h"
#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QSize>
#include <QString>

#include <cstdint>
#include <list>
#include <memory>
#include <optional>

// Linking file-based media (Editing Guide ch.11, p.310-330): master clips
// that point at media files where they are, with the files' metadata in the
// bin columns.
namespace lumenis::media {

// The "Plug-In" column of linked clips (p.277, "the plug-in name used for
// linking the media"): ours is FFmpeg.
QString linkPluginName();

// Where a linked clip's media is, kept with the clip (model::BinItem::link).
// Kept in the bin as JSON (toJson(), fromJson()); clip frames are at the clip's edit rate, file
// frames at `rate`.
struct Link {
    QString path;
    // The file's video frame rate; a clip runs at the project's rate, and
    // its frames are taken from the file at the same time.
    Rational rate{25, 1};
    std::int64_t frames = 0; // the file's video frames; 0 for audio only
    int videoStream = -1;    // -1: no picture
    // The file's audio streams the clip uses, in track order (A1 is the first channel of the first).
    QList<int> audioStreams;
    QList<int> streamChannels; // channels in each of audioStreams
    int channels = 0; // audio channels, all streams together
    // Clip > Modify > Unlink Media (p.368): the clip shows Media Offline
    // until it is relinked.
    bool unlinked = false;
    // Managed media (Avid MediaFiles, Editing Guide ch.12; media/managed.h):
    // every track its own file. `path` is the picture's (or, without one,
    // A1's); audioPaths holds each of audioStreams' files. The id is the
    // clip's in the media files, by which the media database finds them.
    QStringList audioPaths;
    QString mediaId;
    // The file frame (at `rate`) clip frame 0 shows: managed clips relinked
    // to media that starts earlier (Relink > Managed Media, p.361).
    std::int64_t first = 0;
    bool isManaged() const { return !mediaId.isEmpty(); }

    // The file to look for: the path, or a sequence's first file.
    QString file() const;
    // Every file the clip's media is in.
    QStringList files() const;
    // The stream and channel of the clip's audio track `track` (0 for A1).
    std::optional<std::pair<int, int>> channelOf(int track) const;
    // The file that track is in.
    QString audioFile(int track) const;

    // The file's frame for frame `clipFrame` of a clip at `clipRate`.
    std::int64_t fileFrame(std::int64_t clipFrame, const Rational& clipRate) const;

    QJsonObject toJson() const;
    static std::optional<Link> fromJson(const QJsonObject& json);
};

// The item's link; nothing if it has none or it cannot be read.
std::optional<Link> linkOf(const model::BinItem& item);
// Whether the item points at media (unlinked clips too).
bool isLinked(const model::BinItem& item);
// Whether the clip was unlinked (Clip > Modify > Unlink Media).
bool isUnlinked(const model::BinItem& item);
// Clip > Modify > Unlink Media (p.368): master clips only. False if the
// item is not a linked clip.
bool unlinkClip(model::BinItem& item);

// How files are linked: the project's rate and the Link settings.
struct LinkOptions {
    Rational editRate{24000, 1001}; // the project's
    // The clips' creation time; now (UTC) if invalid.
    QDateTime when;
    // Link settings, Link Options tab: "Always use 16:9 aspect ratio for SD
    // clips" (p.1204).
    bool sdWidescreen = false;
};

// Whether a probed file has picture or sound to link to.
bool isLinkable(const MediaInfo& info);

// A master clip (an audio clip for files without picture) linked to a
// probed file, named after the file without its extension (p.318), with
// the columns its metadata fills: Start, End, Tracks, Tape, Video, Format,
// FPS, Image Aspect Ratio, Color Space, Audio SR, Audio Bit Depth, Audio
// Format, Source File, Source Path, Drive and Plug-In (p.274-278).
std::optional<model::BinItem> linkClip(const MediaInfo& info, const LinkOptions& options, QString* error = nullptr);

// A subclip of frames [in, out) of a clip with media (model::subclipItem,
// p.407), its media the master's from `in` on.
std::optional<model::BinItem> makeSubclip(const model::Bin& bin, const model::BinItem& master, std::int64_t in,
                                          std::int64_t out, const QDateTime& when);

// Gathers numbered stills into image sequences (as specs, ImageSequence)
// as p.325-326 describe, leaving other paths as they are, in order.
QStringList groupStills(const QStringList& paths, const Rational& rate);

// Probes and links every file, numbered stills as sequences; the ones that
// are not media are skipped and counted in `skipped`.
QList<model::BinItem> linkFiles(const QStringList& paths, const LinkOptions& options, int* skipped = nullptr);

// Relink > Linked Media (p.364-365): the files under a folder by name, where
// a clip's media is among them (its source file; for a sequence, its first
// file), and pointing the clip at it. The media must fit the clip: the same
// tracks and duration (p.320). Relinking ends an Unlink.
// File name to path for every file under `folder` (recursively); the first found wins a name.
QHash<QString, QString> indexFolder(const QString& folder);
// Where the item's media is in such an index (a sequence spec for an image sequence); empty if not.
QString locateMedia(const model::BinItem& item, const QHash<QString, QString>& index);
// Points the clip at `path` if its media fits; false with the reason in *error otherwise.
bool relinkClip(model::BinItem& item, const QString& path, QString* error = nullptr);

// Frames of linked clips, decoded on demand. Keeps a few files open and the
// frames last asked for; safe to use from several threads.
class FrameCache {
public:
    // Keeps up to `openFiles` files open and `frames` frames (each at the size it was asked for).
    explicit FrameCache(int openFiles = 8, int frames = 64);
    ~FrameCache();
    FrameCache(const FrameCache&) = delete;
    FrameCache& operator=(const FrameCache&) = delete;

    // The clip's frame `clipFrame` (at the clip's edit rate), scaled to fit
    // `size` if given. Null if the media is offline or has no picture.
    QImage frame(const model::BinItem& item, std::int64_t clipFrame, QSize size = {});
    // Whether the clip's media file can be read.
    bool isOnline(const model::BinItem& item);
    // Forgets everything (files changed on disk).
    void clear();

private:
    struct Reader;
    std::shared_ptr<Reader> reader(const Link& link);

    // Guards readers_ and the image cache; each Reader has its own lock for decoding.
    QMutex mutex_;
    int openFiles_;
    int frames_;
    std::list<std::shared_ptr<Reader>> readers_; // most recently used first
    QHash<QString, QImage> images_;
    QList<QString> imageOrder_;
};

} // namespace lumenis::media
