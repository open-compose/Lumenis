#pragma once

#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

// Clip > Relink > Managed Media (Editing Guide p.361-364): clips pointed at
// managed media that holds the same material, found by source name and
// timecode.
namespace lumenis::media {

// The Relink dialog's choices (ManagedRelinkDialog, src/ui/relink_dialog.cpp).
struct RelinkOptions {
    // Relink selected items to: media on these drives' Avid MediaFiles
    // folders; empty for All Available Drives.
    QStringList drives;
    bool masterClips = true; // Master clips
    bool otherItems = true;  // All other items: sequences
    // Relink only to media from the current project (`project`).
    bool currentProjectOnly = false;
    QString project;
    // Allow relinking to offline items (p.362; 11.4a3): sequences' segments may take clips that are offline (their media
    // is not on any drive); "All available drives are searched regardless of the setting" for the online ones.
    bool offlineItems = false;
    // Relink by Source Name: Tape Name or Source File Name, else Name
    // (p.363); or a column's value (11.4a3: Camroll, Labroll, Disk Label), which only clips have: master clips are
    // matched to media files by Tape Name or Source File Name then.
    enum class By { SourceName, Name, Column };
    By by = By::SourceName;
    QString column;
    // Relink by Timecode (p.363; 11.4a3): the column a clip's material starts at, empty for Start; "Auxiliary TC1" to
    // "Auxiliary TC5" or "Sound TC". Media files have their own start only, which a master clip's chosen timecode is
    // compared with.
    QString timecode;
    // Video Relink Parameters' Relink to (p.363; 11.4a3): any video format, the current project's only (its raster and
    // rate), or any HD (720 lines and up) or SD format. Sound-only media passes.
    enum class Format { Any, Project, Hd, Sd };
    Format format = Format::Any;
    QSize projectRaster;
    Rational projectRate{25, 1};
    // Tracks to Relink (p.364; 11.4a3): "include or exclude video, audio and/or data tracks", for sequences' segments
    // ("relink your sequence to a higher resolution video without having to relink the audio"). Master clips relink
    // whole.
    bool relinkVideo = true;
    bool relinkAudio = true;
    bool relinkData = true;
    bool ignoreExtension = false;
    QString ignoreAfter; // Ignore characters after last occurrence
    bool matchCase = false;
    // Relink method (p.363).
    enum class Method { Highest, MostCompressed, Specific };
    Method method = Method::Highest;
    // With Specific Resolution: the resolution and "Relink if quality".
    QString resolution;
    enum class Quality { AtLeast, Equal, AtMost };
    Quality quality = Quality::AtLeast;
    // If no match is found: Use Existing Media, or Unlink (Take Offline).
    bool unlinkWithoutMatch = false;
    // Create new sequences: copies named "<name>.relinked" (p.364).
    bool newSequences = true;
    // Match material at another edit rate too (frame counts are then compared as they are).
    bool acrossRates = false;
};

// Where a clip's material is: the name it is matched by, its first frame
// in timecode (at its rate) and its length.
struct Material {
    QString name;
    // Timecode frames from midnight at `rate`.
    std::int64_t start = 0;
    std::int64_t frames = 0;
    Rational rate{25, 1};
};
// A clip's material from its Tape (else Source File) column, its Start (or the options' timecode column) and duration;
// Relink by Name uses its name, by Column that column. A media file's from its tape (else source) and starting
// timecode.
Material materialOf(const model::BinItem& clip, const RelinkOptions& options);
Material materialOf(const MediaFileInfo& file, const RelinkOptions& options);
// The names compared, with the options' Ignore and Match case applied.
bool sameName(const QString& a, const QString& b, const RelinkOptions& options);

// How good a resolution is, for Highest Quality and Most Compressed: its
// place in resolutions(), -1 when it is none of ours.
int qualityOf(const QString& resolution);

// The managed media for a master clip: a set holding its tracks and all its
// frames, of the same name, chosen by the relink method. Nothing if none
// fits.
std::optional<Link> findMedia(const model::BinItem& clip, const MediaDatabase& db, const RelinkOptions& options);

struct RelinkResult {
    QList<model::BinItem> changed; // relinked items
    QList<model::BinItem> added;   // new ".relinked" sequences
    QStringList notRelinked;       // names of items no match was found for
};

// Relinks the selection. Master clips find media in the database;
// sequences' segments find master clips among `candidates` holding the same
// material (the open bins' clips, with their bin ids).
// A candidate clip and the bin it is in.
struct BinClip {
    QString binId;
    model::BinItem item;
};
// `lookup` finds a segment's clip; `taken` are the bin's names, for numbering. Nothing in the bins
// changes here: the caller stores the result.
RelinkResult relinkManaged(const QList<model::BinItem>& selection, const MediaDatabase& db,
                           const QList<BinClip>& candidates, const ClipLookup& lookup,
                           const RelinkOptions& options, const QStringList& taken);

} // namespace lumenis::media
