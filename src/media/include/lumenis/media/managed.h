#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/link.h"
#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

// Managed media (Editing Guide ch.12, p.348-359; ROADMAP Phase 2.4): media
// the application makes itself, by importing, consolidating or
// transcoding. It is kept as MXF OP-Atom ("Avid OP-Atom", p.1209), one file
// per track, in "<drive>/Avid MediaFiles/MXF/<n>/" (p.513).
namespace lumenis::media {

// A video resolution media is made in (Media Creation, p.134; the
// resolutions of p.1240-1256): Avid DNx at the guide's quality levels,
// through FFmpeg's DNxHR encoder, which takes any raster.
struct Resolution {
    QString id;          // kept in settings: "dnx_hq"
    QString name;        // "Avid DNx HQ"
    QString profile;     // FFmpeg's: "dnxhr_hq"
    QString pixelFormat; // "yuv422p", "yuv422p10le", "yuv444p10le"
    bool rgb = false;    // Avid DNx 444 is RGB only (p.1241)
};
const QList<Resolution>& resolutions();
// By id or name; nullptr if there is none.
const Resolution* findResolution(const QString& idOrName);
const Resolution& defaultResolution(); // Avid DNx HQ

// A place media can be made: a volume, or a folder the user chose. Its
// "Avid MediaFiles" folder goes at `root`.
struct MediaDrive {
    QString name;
    QString root;
    std::int64_t freeBytes = 0;
};
// The writable volumes, the home folder when its volume's top is not
// writable (macOS, Linux), and the `extra` folders. The drive with the most
// space is shown in bold (p.135); see mostSpace().
QList<MediaDrive> mediaDrives(const QStringList& extra);
// With the folders set here, and those in the environment variable
// LUMENIS_MEDIA_DRIVES (separated as PATH is): for tests, and for media
// kept in a folder rather than on a drive of its own.
QList<MediaDrive> mediaDrives();
void setExtraMediaDrives(const QStringList& folders);
QStringList extraMediaDrives();
// The drive a setting names, or the one with the most space when it names
// none or one that is not there.
QString chooseDrive(const QString& setting);
int mostSpace(const QList<MediaDrive>& drives);

// "<drive>/Avid MediaFiles".
QString mediaFilesFolder(const QString& drive);
// The MXF folder new media goes in: the highest-numbered one under
// "Avid MediaFiles/MXF", or "1" when there is none (p.513). Made if needed;
// empty on failure.
QString mxfFolder(const QString& drive, QString* error = nullptr);

// How media is made (Media Creation settings, p.1206-1209; Audio Project
// settings, p.1148).
struct MediaCreation {
    QSize raster{1920, 1080}; // the project's
    Resolution resolution = defaultResolution();
    QString videoDrive;
    QString audioDrive; // empty: the video drive
    int bitDepth = 24;  // PCM, 16 or 24
    QString project;    // the project's name, kept in the files
    QDateTime when;
    // The fraction done; returning false cancels.
    std::function<bool(double)> progress;
};
// The smallest raster media is made at (p.355; DNxHR's too).
inline const QSize kMinimumRaster{256, 120};
// MXF OP-Atom audio is 48 kHz (FFmpeg's muxer takes no other rate).
inline constexpr int kManagedSampleRate = 48000;

// Transcode (p.353-356) and import (p.213): new media for frames [in, out)
// of a clip with media, linked or managed, every track its own file: the
// picture in the resolution at the project's raster, fitted keeping its
// shape; each audio channel as PCM. `out` -1 is the clip's end. The result
// is a master clip (an audio clip without picture) of that media, named as
// the clip, with its columns.
std::optional<model::BinItem> transcodeClip(const model::BinItem& clip, std::int64_t in, std::int64_t out,
                                            const MediaCreation& creation, QString* error = nullptr);

// Consolidate (p.348-352): copies the media of frames [in, out) without
// converting the picture. That needs a picture whose frames stand alone
// and that MXF OP-Atom holds (DNx), at the clip's rate; sound is written
// as PCM, which loses nothing.
bool canConsolidate(const model::BinItem& clip);
std::optional<model::BinItem> consolidateClip(const model::BinItem& clip, std::int64_t in, std::int64_t out,
                                              const MediaCreation& creation, QString* error = nullptr);

// Which clip and track a managed media file holds, from its metadata, and
// what it holds: for relinking by source name and timecode (p.362-363).
struct MediaFileInfo {
    QString path;
    QString mediaId;
    QString track; // "V1", "A1", ...
    QString clipName;
    QString project;
    QString source;     // the source file it was made from, or the clip
    QString tape;
    QString start;      // starting timecode
    std::int64_t frames = 0;
    Rational rate{25, 1};
    QString resolution; // "Avid DNx HQ", or the copied picture's codec
    std::int64_t bytes = 0;
    QDateTime created;
    // The master clip's MobID: the file's material package
    // (docs/spec/native-model.md). How a clip read from AAF finds its media.
    MobId material;

    QJsonObject toJson() const;
    static MediaFileInfo fromJson(const QJsonObject& o);
};
std::optional<MediaFileInfo> readMediaFile(const QString& path);

// The files that make one clip's media, by track.
struct MediaSet {
    QString mediaId;
    std::map<QString, MediaFileInfo> tracks; // "A1", "A2", ..., "V1"; never empty
    const MediaFileInfo& any() const { return tracks.begin()->second; }
    // The track's file; empty if there is none.
    MediaFileInfo track(const QString& name) const;
    bool hasVideo() const { return tracks.contains(QStringLiteral("V1")); }
    int audioTracks() const;
    // A link to it, starting at file frame `first`.
    Link link(std::int64_t first = 0) const;
};

// The media database (p.358-359): the managed media files on the drives,
// by clip. Each MXF folder keeps an index of its files, rebuilt for the
// files that changed (Refresh Media Directories).
class MediaDatabase {
public:
    static QString indexName() { return QStringLiteral("MediaDatabase.json"); }

    // Reads the Avid MediaFiles folders of the drives (their roots).
    void scan(const QStringList& drives);
    void clear();
    // Track ("V1", "A1") to file, for a clip's id.
    QHash<QString, QString> filesOf(const QString& mediaId) const;
    int fileCount() const;
    QList<MediaFileInfo> files() const;
    QList<MediaSet> sets() const;
    std::optional<MediaSet> set(const QString& mediaId) const;
    // The media of the master clip with this MobID (its files' material
    // package), as for a clip imported from AAF (p.547).
    std::optional<MediaSet> setOf(const MobId& master) const;
    // The projects with media on the drives.
    QStringList projects() const;
    // Points a managed clip at where its files are now. False if it is not
    // a managed clip or a track's file is not found.
    bool resolve(model::BinItem& item) const;
    // Forgets a file (deleted).
    void remove(const QString& path);

private:
    QHash<QString, MediaFileInfo> files_; // by path
};

// Deletes media files and forgets them (Delete Media, p.347-348). Only
// managed media: linked media's files are not ours to delete. Returns the
// files deleted.
QStringList deleteMediaFiles(const QStringList& paths, MediaDatabase* db = nullptr);

} // namespace lumenis::media
