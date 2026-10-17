#pragma once

#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Clip > Relink > Managed Media (Editing Guide p.361-364): clips pointed at
// managed media that holds the same material, found by source name and
// timecode.
namespace lumenis::media {

struct RelinkOptions {
    // Relink selected items to: media on these drives' Avid MediaFiles
    // folders; empty for All Available Drives.
    QStringList drives;
    bool masterClips = true; // Master clips
    bool otherItems = true;  // All other items: sequences
    // Relink only to media from the current project (`project`).
    bool currentProjectOnly = false;
    QString project;
    // Relink by Source Name: Tape Name or Source File Name, else Name
    // (p.363).
    enum class By { SourceName, Name };
    By by = By::SourceName;
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
    bool acrossRates = false;
};

// Where a clip's material is: the name it is matched by, its first frame
// in timecode (at its rate) and its length.
struct Material {
    QString name;
    std::int64_t start = 0;
    std::int64_t frames = 0;
    Rational rate{25, 1};
};
Material materialOf(const model::BinItem& clip, RelinkOptions::By by);
Material materialOf(const MediaFileInfo& file, RelinkOptions::By by);
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
struct BinClip {
    QString binId;
    model::BinItem item;
};
RelinkResult relinkManaged(const QList<model::BinItem>& selection, const MediaDatabase& db,
                           const QList<BinClip>& candidates, const ClipLookup& lookup,
                           const RelinkOptions& options, const QStringList& taken);

} // namespace lumenis::media
