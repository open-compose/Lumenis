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
struct Link {
    QString path;
    // The file's video frame rate; a clip runs at the project's rate, and
    // its frames are taken from the file at the same time.
    Rational rate{25, 1};
    std::int64_t frames = 0; // the file's video frames; 0 for audio only
    int videoStream = -1;    // -1: no picture
    QList<int> audioStreams;
    int channels = 0; // audio channels, all streams together

    // The file's frame for frame `clipFrame` of a clip at `clipRate`.
    std::int64_t fileFrame(std::int64_t clipFrame, const Rational& clipRate) const;

    QJsonObject toJson() const;
    static std::optional<Link> fromJson(const QJsonObject& json);
};

std::optional<Link> linkOf(const model::BinItem& item);
bool isLinked(const model::BinItem& item);

struct LinkOptions {
    Rational editRate{24000, 1001}; // the project's
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

// Probes and links every file; the ones that are not media are skipped and
// counted in `skipped`.
QList<model::BinItem> linkFiles(const QStringList& paths, const LinkOptions& options, int* skipped = nullptr);

// Frames of linked clips, decoded on demand. Keeps a few files open and the
// frames last asked for; safe to use from several threads.
class FrameCache {
public:
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

    QMutex mutex_;
    int openFiles_;
    int frames_;
    std::list<std::shared_ptr<Reader>> readers_; // most recently used first
    QHash<QString, QImage> images_;
    QList<QString> imageOrder_;
};

} // namespace lumenis::media
