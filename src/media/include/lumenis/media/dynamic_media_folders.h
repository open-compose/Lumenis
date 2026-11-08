#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/consolidate.h"
#include "lumenis/media/link.h"
#include "lumenis/model/bin.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Dynamic Media Folders (Editing Guide p.371-376; docs/spec/background-services.md, 8.4b): folders whose new files are
// linked and processed by a profile's actions in the background, then put into bins ("acquired", 8.4c). This file
// holds the folders and profiles as data (kept as JSON beside the user's settings, ours), finding a folder's new
// entries, and the work done on one entry: link it, fill the AIS columns, then run the profile's Actions in order.
// The UI runs that work on the Background Queue (src/ui/main_window_dmf.cpp).
namespace lumenis::media {

// One of a profile's Actions (p.374-375), run in the order of the profile's list ("reorder the actions by priority").
struct DmfAction {
    enum class Kind { CopyToFolder, Consolidate, Transcode };
    Kind kind = Kind::CopyToFolder;
    // Copy to Folder: "Copy to:" and "Auto Relink when complete". "Checkin to Production Management" is kept but has
    // nothing to check in to (no MediaCentral; dimmed in the Profile Editor, 8.4c).
    QString copyTo;
    bool autoRelink = true;
    bool checkin = false;
    // Consolidate: "Skip media files already on the target drive", "Relink selected clips to target drive before
    // skipping".
    bool skipOnTarget = false;
    bool relinkBeforeSkip = false;
    // Consolidate and Transcode: Convert Audio Sample Rate (0: keep, else 32000, 44100 or 48000), Convert Audio Bit
    // Depth (0: keep, else 16 or 24), Convert Audio Format ("" keeps; "OMF (WAVE)", "OMF (AIFF-C)", "MXF (PCM)"; our
    // managed media is MXF PCM, so the others are kept as names only) and the Video Drive and Audio Drive (roots;
    // empty: the Media Creation setting's).
    int sampleRate = 0;
    int bitDepth = 0;
    QString audioFormat;
    QString videoDrive;
    QString audioDrive;
    // Transcode: the Video Resolution (a Resolution id, media/managed.h; empty: the Media Creation setting's
    // Transcode resolution), "Apply Reformatting option (compatibility mode)" and "Apply color transformations"
    // (kept: our clips carry no framing or colour transformations to apply yet).
    QString resolution;
    bool reformat = false;
    bool colorTransforms = false;

    // "Copy to Folder", "Consolidate" or "Transcode" (the menu's names).
    static QString kindName(Kind kind);
    static std::optional<Kind> kindFromName(const QString& name);
    QJsonObject toJson() const;
    static DmfAction fromJson(const QJsonObject& json);
    bool operator==(const DmfAction&) const = default;
};

// A profile's Link Settings (p.373-374).
struct DmfLinkSettings {
    // Link Using: the plug-in. Ours links everything through FFmpeg (linkPluginName()); "Autodetect" is kept as the
    // guide's recommendation.
    QString plugin = QStringLiteral("Autodetect");
    // Bins: "Use active bin", or "Create a new bin" ("This is the default option") named by the default naming
    // convention, the volume's name, or binName.
    enum class Bin { Active, New };
    Bin bin = Bin::New;
    enum class Naming { Default, Volume, Specify };
    Naming naming = Naming::Default;
    QString binName;
    // Link: Multichannel Audio, the clips' Track Formats as model::trackFormatsText() writes them ("Stereo A1A2").
    bool multichannel = false;
    QString trackFormats;
    // AIS: Audio Start-Time Option (for Broadcast Wave): the clip starts at its frame-aligned BWF time
    // (alignToFrame()).
    bool audioStartTime = false;
    // Reel name for the Labroll column: the source file's name or a folder's, `reelLevel` folders up (1: the folder
    // the files are in, up to 8).
    enum class Reel { FileName, FolderName };
    Reel reel = Reel::FileName;
    int reelLevel = 1;
    // Frame Count for the DPX column: from 0, from 1, the Start timecode as frames, or the file name's number.
    enum class FrameCount { FromZero, FromOne, Timecode, FileName };
    FrameCount frameCount = FrameCount::FromZero;
    // Timecode for the Start column: embedded in the file, or from its name.
    enum class Timecode { Embedded, FileName };
    Timecode timecode = Timecode::Embedded;
    // Default FPS for files whose header gives none (image sequences).
    Rational defaultFps{24, 1};

    QJsonObject toJson() const;
    static DmfLinkSettings fromJson(const QJsonObject& json);
    bool operator==(const DmfLinkSettings&) const = default;
};

// A named profile (p.372-375): Link Settings and the Actions.
struct DmfProfile {
    QString name;
    DmfLinkSettings link;
    QList<DmfAction> actions;
    QJsonObject toJson() const;
    static DmfProfile fromJson(const QJsonObject& json);
    bool operator==(const DmfProfile&) const = default;
};

// A Dynamic Media Folder in the window's list (p.372, p.375): its path, its profile's name and Enable. `done` holds
// the entries (names in the folder) already processed, so they are not processed again (ours).
struct DmfFolder {
    QString path;
    QString profile;
    bool enabled = false;
    QStringList done;
    QJsonObject toJson() const;
    static DmfFolder fromJson(const QJsonObject& json);
    bool operator==(const DmfFolder&) const = default;
};

// Every folder and profile, kept in one JSON file (ours: Dynamic_Media_Folders.json beside the user's settings, so
// the folders stay with the user across projects, as the guide's service runs outside any project).
struct DynamicMediaFolders {
    QList<DmfProfile> profiles;
    QList<DmfFolder> folders;

    const DmfProfile* profile(const QString& name) const;
    DmfFolder* folder(const QString& path);
    // Adds or replaces the profile of that name.
    void storeProfile(const DmfProfile& profile);
    // Adds the folder (disabled, with no profile) unless it is in the list. False when it was.
    bool addFolder(const QString& path);
    void removeFolder(const QString& path);

    QJsonObject toJson() const;
    static DynamicMediaFolders fromJson(const QJsonObject& json);
    // Nothing on a missing or unreadable file; save() writes it whole.
    static DynamicMediaFolders load(const QString& file);
    bool save(const QString& file, QString* error = nullptr) const;
};

// The folder's entries not yet in `done`, by name: its files and its folders (each folder is one entry, its files
// linked together so that "each group of consecutive dpx files in that folder" is a master clip, p.376). Hidden
// entries and our own copies' folders are left out. Sorted by name.
QStringList newDmfEntries(const DmfFolder& folder);

// Every file an entry holds: the file itself, or a folder's files at any depth, sorted.
QStringList dmfEntryFiles(const QString& entryPath);

// The name of a new bin for a profile's clips (p.373): the default convention "<project> <n>" with the first number
// no bin in `taken` has, the volume's name (the drive or mount the folder is on, volumeName()), or the specified name.
QString dmfBinName(const DmfLinkSettings& link, const QString& folderPath, const QString& project, const QStringList& taken);
// The name or label of the volume a path is on: "D:" on Windows, else the mount's last folder name ("/" as "/").
QString volumeName(const QString& path);

// The AIS columns (p.373-374) for a linked clip whose first file is `file`: Labroll from the reel setting, Camroll
// from a DPX file's Input Device field, DPX from the Labroll prefix and the frame count, Start from the file name when
// that is chosen. Cells with nothing to show are left out.
void applyAisColumns(model::BinItem& clip, const QString& file, const DmfLinkSettings& link);
// A DPX file's Input Device (the orientation header's 32 bytes at 1556); empty when there is none.
QString dpxInputDevice(const QString& file);
// The last timecode-like run in a file name ("A001_10_00_00_12.mov" -> 10:00:00:12): four pairs of digits
// separated by ':', '_', '.', ';' or '-', or eight digits in a row. Nothing when there is none.
std::optional<std::int64_t> timecodeFromName(const QString& file, const Rational& editRate);

// What processDmfEntry() needs besides the profile: the link options, Media Creation's settings for Consolidate and
// Transcode, and the names in the target bin for numbering ".new" clips.
struct DmfProcessOptions {
    LinkOptions link;
    MediaCreation creation;
    QStringList taken;
};

// The result for one entry: the linked clips (relinked to copies when Auto Relink was on) and the clips Consolidate
// and Transcode made (".new", p.376), with what was skipped and the first error. `progress` returns false to stop.
struct DmfResult {
    QList<model::BinItem> clips;
    QStringList messages;
    QString error;
};
DmfResult processDmfEntry(const QString& entryPath, const DmfProfile& profile, const DmfProcessOptions& options,
                          const std::function<bool(double)>& progress = {});

} // namespace lumenis::media
