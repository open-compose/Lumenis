#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/link.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/frame_flex.h"

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <vector>
#include <map>
#include <optional>

// Managed media (Editing Guide ch.12, p.349-359; ROADMAP Phase 2.4): media
// the application makes itself, by importing, consolidating or
// transcoding. It is kept as MXF OP-Atom ("Avid OP-Atom", p.1209), one file
// per track, in "<drive>/Avid MediaFiles/MXF/<n>/" (p.513).
namespace lumenis::media {

// A video resolution media is made in (Media Creation, p.134; the
// resolutions of p.1240-1256): Avid DNx at the guide's quality levels,
// through FFmpeg's DNxHR encoder, which takes any raster; and Apple ProRes
// (Proxy to 4444 XQ, p.1240) where an encoder may be used: the operating
// system's on macOS (VideoToolbox, Apple's licensed encoder; the guide's
// "macOS only"), FFmpeg's elsewhere in builds with LUMENIS_PATENT_CODECS
// (ours; docs/EXTENSIONS.md, docs/spec/media-formats.md).
// A project format a resolution is made for (p.1252-1274): the project's raster and edit rate (1080i/59.94 is 29.97),
// the scan, and the raster the media is stored in when it is not the project's.
struct ResolutionFormat {
    enum class Scan { Any, Interlaced, Progressive };
    ResolutionFormat(QSize raster_, Rational rate_, Scan scan_ = Scan::Any, QSize stored_ = QSize())
        : raster(raster_), rate(rate_), scan(scan_), stored(stored_) {}
    QSize raster;
    Rational rate;
    Scan scan = Scan::Any;
    QSize stored; // empty: the project's raster
};

struct Resolution {
    QString id;          // kept in settings: "dnx_hq", "prores_hq"
    QString name;        // "Avid DNx HQ", "Apple ProRes 422 HQ"
    QString profile;     // FFmpeg's encoder profile: "dnxhr_hq"; "hq" (prores_ks)
    QString pixelFormat; // "yuv422p", "yuv422p10le", "yuv444p10le"
    bool rgb = false;    // Avid DNx 444 is RGB only (p.1241)
    QString codec = QStringLiteral("dnxhd"); // the codec: "dnxhd", "prores", "mpeg2video" or "dvvideo"
    QString osProfile;   // the profile for the OS's encoder (VideoToolbox: "standard", "xq"), where it differs
    QString probed;      // the profile FFmpeg reports when it reads the media: "DNXHR HQ", "HQ"
    int quality = 0;     // for picking the better of two: comparable DNx and ProRes levels rank the same
    // The encoder's own options and bit rate (XDCAM's MPEG-2: rate, GOP; ROADMAP 11.1c).
    QList<QPair<QString, QString>> options;
    std::int64_t bitRate = 0;
    // The project formats it is made for (p.1252-1274); empty: any (DNx, ProRes).
    QList<ResolutionFormat> formats;
};
// The project's format, for the resolutions offered (resolutions()), how interlaced media is encoded and how pictures
// of other pixel shapes fit it (fitPicture()); set by the main window when a project opens or its format changes. Until
// then, every format, and square pixels. `aspect` is the picture's (16:9; an SD project's 4:3 or 16:9).
void setProjectFormat(QSize raster, Rational rate, bool interlaced, Rational aspect = Rational());
// The project picture's aspect ratio (setProjectFormat()); invalid (0) for none.
Rational projectAspect();
// Whether the resolution is one of the DNxHD bit-rate resolutions ("DNxHD 120", ROADMAP 11.1d), not a DNxHR level.
bool isDnxhd(const Resolution& resolution);
// The DNxHD resolution media of that bit rate (bits a second; within 8%), depth, stored raster, rate and scan is in:
// the name a linked DNxHD clip is shown by ("DNxHD 185x"); null when none is near.
const Resolution* dnxhdResolutionOf(std::int64_t bitRate, int bitsPerSample, QSize raster, Rational rate, bool interlaced);
// Whether the resolution is made for a project of that raster and rate, interlaced or progressive (nothing: either).
bool resolutionFits(const Resolution& resolution, QSize raster, Rational rate, std::optional<bool> interlaced = std::nullopt);
// The raster the resolution's media is stored in for that project raster and rate: the format's own (DVCPro HD's
// 1280 x 1080 in 1920 x 1080 projects at 29.97, DV's 720 x 480 in NTSC's 720 x 486), else the project's.
QSize resolutionRaster(const Resolution& resolution, QSize raster, Rational rate);
// Where an SD project's picture lies in a stored raster with other lines, in the stored raster's pixels: DV's 480
// lines are NTSC's 5th to 484th (0, -4, 720, 486; "crops the top four and bottom two scan lines", p.1195), and MPEG
// IMX's D-10 frame has the vertical blanking above the picture (720 x 608 for PAL with the picture from line 32, 720 x
// 512 for NTSC from line 26; SMPTE 356M; p.1240, p.971). Nothing for other rasters, which are the same picture scaled.
std::optional<QRect> storedPlacement(QSize raster, QSize stored);
// A project-raster picture made the size the media is stored in (resolutionRaster()): placed by storedPlacement() where
// it has one (DV loses NTSC's top four lines and bottom two; MPEG IMX's blanking lines above the picture are black, the
// Video Display setting's VBI "Blank", p.1226); other sizes are the same picture scaled, their pixels another shape.
QImage toStoredRaster(const QImage& picture, QSize stored);
// A decoded picture, whose pixels are `pixelAspect` wide (1: square; FFmpeg's sample aspect ratio), fitted into `size`, a
// picture of the project frame (setProjectFormat()'s aspect; square pixels without one), on black: by the shape it is
// shown at, so DVCPro HD's 1280 x 1080 fills a 1920 x 1080 frame and HDV's 1440 x 1080 too. A picture within 3% of the
// frame's shape fills it (SD's Rec. 601 pixels against 4:3; ours); DV's 720 x 480 in a 486-line frame goes four lines
// down ("pads the frame out by six lines", p.1195). Square pixels fit by their pixels, as before (ours: such a picture
// is taken as drawn for the project's pixels).
QImage fitPicture(const QImage& image, double pixelAspect, QSize size);
// Where fitPicture() puts the picture in `size`.
QRectF fitPictureRect(QSize image, double pixelAspect, QSize size);
// The resolutions media can be made in, by this build on this computer, for the project's format (setProjectFormat()):
// Avid DNx, Apple ProRes where an encoder may be used (resolutionEncoder()), then XDCAM where the format takes it.
QList<Resolution> resolutions();
// The FFmpeg encoder that makes a resolution's media here ("dnxhd", "prores_videotoolbox", "prores_ks", "mpeg2video"),
// and its profile; empty when there is none.
QString resolutionEncoder(const Resolution& resolution);
QString resolutionProfile(const Resolution& resolution);
// The resolution media copied or probed as `codec` with FFmpeg's `profile` is in (for relinking by quality); nullptr
// when it is none of ours.
const Resolution* resolutionOfProbed(const QString& codec, const QString& profile);
// Any resolution we know by id or name, whether or not this build can make it (media made elsewhere); nullptr if none.
const Resolution* knownResolution(const QString& idOrName);
// By id or name, among those this build can make (whatever the project's format: making media in one that does not fit
// the project fails with the reason); nullptr if there is none.
const Resolution* findResolution(const QString& idOrName);
const Resolution& defaultResolution(); // Avid DNx HQ

// A place media can be made: a volume, or a folder the user chose. Its
// "Avid MediaFiles" folder goes at `root`.
struct MediaDrive {
    // The name shown in the drive menus, the folder (cleaned path) and the space free on its volume.
    QString name;
    QString root;
    std::int64_t freeBytes = 0;
};
// The writable volumes, the home folder when its volume's top is not
// writable (macOS, Linux), and the `extra` folders. The drive with the most
// space is shown in bold (p.135); see mostSpace(). With the environment
// variable LUMENIS_NO_SYSTEM_DRIVES set (not "0"), only the `extra` folders:
// the tests run so (tests/CMakeLists.txt), each with a drive folder of its
// own, so that none writes to or scans a real volume's Avid MediaFiles while
// another test writes there.
QList<MediaDrive> mediaDrives(const QStringList& extra);
// With the folders set here, and those in the environment variable
// LUMENIS_MEDIA_DRIVES (separated as PATH is): for tests, and for media
// kept in a folder rather than on a drive of its own.
// Drives unmounted with File > Media > Unmount (p.343-344) are left out.
QList<MediaDrive> mediaDrives();
void setExtraMediaDrives(const QStringList& folders);
// File > Media > Unmount (p.343-344): "The drives are no longer available to Media Composer". The
// roots (cleaned paths) are left out of mediaDrives() until remountAllDrives(); "You can also remount
// all the drives ... However, you cannot mount selected drives". Process-wide, not saved (ours).
void unmountDrives(const QStringList& roots);
// The Media Creation setting's Drive Filtering & Indexing tab (p.1207): "Filter Out System Drive ... Removes as a
// storage choice the drive on which the operating system resides" and "Filter Out Launch Drive ... the drive on which
// Media Composer resides". mediaDrives() leaves out the drives on those volumes (QStorageInfo::root() and the
// application's folder's volume). Process-wide, set by MainWindow from the setting.
void setDriveFilters(bool systemDrive, bool launchDrive);
// The drives not on `systemRoot`'s or `launchRoot`'s volume (empty: not filtered), by their volumes' roots as
// `volumeOf` gives them.
QList<MediaDrive> filteredDrives(const QList<MediaDrive>& drives, const QString& systemRoot, const QString& launchRoot,
                                 const std::function<QString(const QString& folder)>& volumeOf);
void remountAllDrives();
QStringList unmountedDrives();
// The folders set with setExtraMediaDrives() (or, until then, from LUMENIS_MEDIA_DRIVES).
QStringList extraMediaDrives();
// The drive a setting names, or the one with the most space when it names
// none or one that is not there.
QString chooseDrive(const QString& setting);
// The index of the drive with the most free space; -1 if there are none.
int mostSpace(const QList<MediaDrive>& drives);

// "<drive>/Avid MediaFiles".
QString mediaFilesFolder(const QString& drive);
// The MXF folder new media goes in: the highest-numbered one under
// "Avid MediaFiles/MXF", or "1" when there is none (p.513). Made if needed;
// empty on failure.
QString mxfFolder(const QString& drive, QString* error = nullptr);

// How media is made (Media Creation settings, p.1206-1209; Audio Project
// settings, p.1148).
// MXF OP-Atom audio is 48 kHz (FFmpeg's muxer takes no other rate).
inline constexpr int kManagedSampleRate = 48000;

struct MediaCreation {
    QSize raster{1920, 1080}; // the project's
    Resolution resolution = defaultResolution();
    // Drive roots (MediaDrive::root) the picture and the sound are written to.
    QString videoDrive;
    QString audioDrive; // empty: the video drive
    int bitDepth = 24;  // PCM, 16 or 24
    // The sound's sample rate. Media at another rate than kManagedSampleRate is written as WAV
    // beside the MXF files (Change Sample Rate's 32 and 44.1 kHz, p.731; ours), and the resampler's
    // filter length (Resampler) sets its quality.
    int sampleRate = kManagedSampleRate;
    int resampleFilter = 32;
    // The Import settings' Audio tab (p.1198-1199). With relabelPullRates, sound at a pullup or
    // pulldown rate of sampleRate (0.1% off, isPullRate()) is written unconverted at sampleRate: "The
    // length and pitch of the imported files are changed by plus or minus .1%". audioShift puts that
    // many samples (at the file's rate) of silence before the sound, or leaves that many of it out
    // when negative (Subframe Alignment to Broadcast WAV Start Time; alignToFrame()).
    bool relabelPullRates = false;
    std::int64_t audioShift = 0;
    QString project;    // the project's name, kept in the files
    QDateTime when;
    // The new master clip's MobID, which its files name as their material
    // package; null: a new one. Consolidate sets it to the clip's own when
    // the clip itself takes the new media (p.349).
    MobId master;
    // The fraction done; returning false cancels.
    std::function<bool(double)> progress;
    // The folder under "Avid MediaFiles/MXF" the picture goes in, when not the one mxfFolder()
    // chooses: background renders go in "__bg.1" (FX Guide p.59). Only renderedVideoClip() reads it.
    QString folder;
    // Transcode's "Apply source transformations > Color encoding" (p.544; 8.5c2): the colour transformations
    // (media::applyColorTransformations) each frame goes through before it is written. Empty: the picture as it is.
    QStringList colorTransformations;
    // Transcode's "Apply source transformations > Frame Flex" (p.355; 11.4a1): the clip's spatial adapter (its FrameFlex
    // box and Reformat) applied to each frame, so the new media holds the framed picture. Unset: the picture fitted as
    // it is.
    std::optional<model::SpatialAdapter> spatial;
    // The Import setting's Image tab (p.1194-1195; 11.4f1), for imports: Image Size Adjustment and Color Scaling.
    //  - Fit: "Resize image to fit format raster ... maintains the file's aspect ratio" (fitPictureRect()), the default;
    //  - Current: "Image sized for current format": the pixels converted "so that the image fills the screen";
    //  - CropPad: "Crop/Pad for DV scan line difference": 480 lines into 486 padded by six, 486 into 480 with "the top
    //    four and bottom two scan lines" cropped, line for line; other sizes as Current;
    //  - NoResize: "Do not resize smaller images": a picture smaller than the raster keeps its pixels, centred; larger
    //    ones fitted.
    // `fullToLegal`, "Scale from Full Range to Legal Range": RGB 0-255 remapped to 16-235.
    enum class ImageSize { Fit, Current, CropPad, NoResize };
    ImageSize imageSize = ImageSize::Fit;
    bool fullToLegal = false;
};
// Where an imported picture of `image` pixels (shown `pixelAspect` wide) goes in `raster` under `mode`
// (MediaCreation::ImageSize).
QRectF importedPictureRect(QSize image, double pixelAspect, QSize raster, MediaCreation::ImageSize mode);
// "Scale from Full Range to Legal Range" (p.1195): each RGB value v becomes 16 + v * 219 / 255.
QImage fullToLegalRange(const QImage& image);
// Whether `rate` is a pullup or pulldown of `nominal` (times 1001/1000 or 1000/1001, to the sample):
// 48048 and 47952 for 48 kHz, 44144 and 44056 for 44.1 kHz.
bool isPullRate(int rate, int nominal);
// The rates audio media can be imported at without conversion (Import settings, p.1198; ours: what
// our WAV and MXF writers take): 32, 44.1, 48, 88.2 and 96 kHz.
bool isImportRate(int rate);

// The smallest raster media is made at (p.355; DNxHR's too).
inline const QSize kMinimumRaster{256, 120};

// Transcode (p.353-356) and import (p.213): new media for frames [in, out)
// of a clip with media, linked or managed, every track its own file: the
// picture in the resolution at the project's raster, fitted keeping its
// shape; each audio channel as PCM. `out` -1 is the clip's end. The result
// is a master clip (an audio clip without picture) of that media, named as
// the clip, with its columns.
std::optional<model::BinItem> transcodeClip(const model::BinItem& clip, std::int64_t in, std::int64_t out,
                                            const MediaCreation& creation, QString* error = nullptr);

// Consolidate (p.349-352): copies the media of frames [in, out) without
// converting the picture. That needs a picture whose frames stand alone
// and that MXF OP-Atom holds (DNx), at the clip's rate; sound is written
// as PCM, which loses nothing.
bool canConsolidate(const model::BinItem& clip);
std::optional<model::BinItem> consolidateClip(const model::BinItem& clip, std::int64_t in, std::int64_t out,
                                              const MediaCreation& creation, QString* error = nullptr);

// Clip > Audio > Change Sample Rate (p.731): the clip with new media for its audio tracks at
// creation.sampleRate, its picture left as it is. The new files go on the audio drive; *oldFiles
// gets the managed files (under "Avid MediaFiles") the old audio used, for Delete Original Media.
// Nothing, with the reason in *error, for a clip without audio media.
std::optional<model::BinItem> resampleClipAudio(const model::BinItem& clip, const MediaCreation& creation,
                                                QStringList* oldFiles = nullptr, QString* error = nullptr);

// Sound made by Cutthulhu itself (Audio Mixdown, p.731-732): `channels` tracks of `frames` frames at
// `editRate`, each written as a mono 48 kHz PCM file in the audio drive's MXF folder. `render` fills
// `count` samples of every track (out[track][i], resized by the caller) from output sample `at`. The
// result is an audio clip named `name` linked to the files, starting at `startTimecode` (empty:
// 00:00:00:00). On failure the files made are removed and `error` says why.
using RenderAudio = std::function<void(std::int64_t at, int count, std::vector<std::vector<float>>& out)>;
std::optional<model::BinItem> renderedAudioClip(const QString& name, const Rational& editRate, std::int64_t frames,
                                                int channels, const RenderAudio& render, const QString& startTimecode,
                                                const MediaCreation& creation, QString* error = nullptr);

// A picture made frame by frame (rendered effects, FX Guide p.57-59): a master clip `name` of
// `frames` frames at `editRate` with only V1, whose frame `f` is what `render(f)` gives (fitted to the
// creation's raster on black), written as managed media in the creation's resolution on its video
// drive. `kind` is the new item's kind (RenderedEffect for a precompute). On failure, or when
// MediaCreation::progress cancels, the file is removed and `error` says why; with `keepOnCancel` a cancel
// keeps the frames made so far instead, the clip as long as they are (partial render, FX Guide p.150).
using RenderVideo = std::function<QImage(std::int64_t frame)>;
std::optional<model::BinItem> renderedVideoClip(const QString& name, const Rational& editRate, std::int64_t frames,
                                                const RenderVideo& render, const MediaCreation& creation,
                                                model::BinItemKind kind = model::BinItemKind::MasterClip,
                                                QString* error = nullptr, bool keepOnCancel = false);

// Which clip and track a managed media file holds, from its metadata, and
// what it holds: for relinking by source name and timecode (p.362-363).
struct MediaFileInfo {
    // Where the file is now (not kept in the index: the index is per folder, by file name).
    QString path;
    // The id every track file of one clip's media shares (a UUID).
    QString mediaId;
    QString track; // "V1", "A1", ...
    // The master clip's name, and the project the media was made in.
    QString clipName;
    QString project;
    QString source;     // the source file it was made from, or the clip
    QString tape;
    QString start;      // starting timecode
    // Length in frames at `rate` (the clip's edit rate).
    std::int64_t frames = 0;
    Rational rate{25, 1};
    QString resolution; // "Avid DNx HQ", or the copied picture's codec
    // What the file's stream holds, for quality matching (Dynamic Relink, p.1120-1121; 8.6a): a picture's raster and
    // FFmpeg codec name, a sound's sample rate and bits a sample. 0 or empty when unknown.
    int width = 0;
    int height = 0;
    QString codec;
    int sampleRate = 0;
    int bitDepth = 0;
    // The file's size, and its creation time (else its modification time).
    std::int64_t bytes = 0;
    QDateTime created;
    // The master clip's MobID: the file's material package
    // (docs/spec/native-model.md). How a clip read from AAF finds its media.
    MobId material;

    // The entry kept in a folder's MediaDatabase.json.
    QJsonObject toJson() const;
    static MediaFileInfo fromJson(const QJsonObject& o);
};
// The file's MediaFileInfo from its metadata; nothing if it is not readable or not managed media.
std::optional<MediaFileInfo> readMediaFile(const QString& path);

// The files that make one clip's media, by track.
struct MediaSet {
    QString mediaId;
    std::map<QString, MediaFileInfo> tracks; // "A1", "A2", ..., "V1"; never empty
    const MediaFileInfo& any() const { return tracks.begin()->second; }
    // The track's file; empty if there is none.
    MediaFileInfo track(const QString& name) const;
    bool hasVideo() const { return tracks.contains(QStringLiteral("V1")); }
    // A1, A2, ... counted until the first missing one.
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

    // Reads the Avid MediaFiles folders of the drives (their roots). The
    // files known from those drives are replaced by what is there now;
    // other drives' files are kept.
    void scan(const QStringList& drives);
    // Forgets every file.
    void clear();
    // Track ("V1", "A1") to file, for a clip's id.
    QHash<QString, QString> filesOf(const QString& mediaId) const;
    // How many files are known, and the files sorted by path.
    int fileCount() const;
    QList<MediaFileInfo> files() const;
    // The files grouped by media id, and one media id's files; nothing if it has none.
    QList<MediaSet> sets() const;
    std::optional<MediaSet> set(const QString& mediaId) const;
    // The media of the master clip with this MobID (its files' material
    // package), as for a clip imported from AAF (p.547).
    std::optional<MediaSet> setOf(const MobId& master) const;
    // Every media set naming the master clip's MobID: its media at each resolution (MultiRez, p.1099-1101; 8.6a).
    QList<MediaSet> setsOf(const MobId& master) const;
    // Whether the master clip with this MobID has video media at more than one resolution (Show local
    // media icons' "mixed resolution", p.1154). Worked out once per change to the files.
    bool hasMixedResolution(const MobId& master) const;
    // The projects with media on the drives.
    QStringList projects() const;
    // Points a managed clip at where its files are now. False if it is not
    // a managed clip or a track's file is not found. Dynamic relink's affinity link ("multiRez") is kept.
    bool resolve(model::BinItem& item) const;
    // Forgets a file (deleted).
    void remove(const QString& path);

private:
    QHash<QString, MediaFileInfo> files_; // by path
    // MobID (as text) -> the resolutions of its video files; rebuilt after files_ changes.
    mutable std::optional<QHash<QString, QSet<QString>>> resolutions_;
};

// Deletes media files and forgets them (Delete Media, p.347-348). Only
// managed media: linked media's files are not ours to delete. Returns the
// files deleted.
QStringList deleteMediaFiles(const QStringList& paths, MediaDatabase* db = nullptr);

} // namespace lumenis::media
