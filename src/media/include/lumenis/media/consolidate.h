#pragma once

#include "lumenis/media/managed.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdint>

// Clip > Consolidate/Transcode (Editing Guide p.349-356): new media for the
// selected clips and sequences, and the clips that use it.
namespace lumenis::media {

// The Consolidate/Transcode dialog's choices (src/ui/consolidate_dialog.cpp).
struct ConsolidateOptions {
    // Consolidate copies the picture as it is (DNx only, canConsolidate()); Transcode re-encodes it.
    enum class Mode { Consolidate, Transcode };
    Mode mode = Mode::Consolidate;
    // Frames kept outside what a sequence uses (p.351).
    std::int64_t handles = 24;
    // Create new sequence(s): the selected sequences stay with the old
    // media, new ones use the new (p.351, p.354).
    bool newSequences = false;
    // Consolidate/Transcode only linked media (p.351, p.354).
    bool linkedOnly = false;
    // Skip media files already on the target drive (p.351).
    bool skipOnTarget = false;
    // The Copying Media Files dialog (p.352): master clips keep their media
    // and new ones (".new") take the copies; or they take the copies and
    // new ones (".old") keep the old media (p.349).
    bool keepMastersLinked = true;
    // Transcode's Raster Dimensions (p.355): the project's, or the source's,
    // a quarter or a sixteenth of it; never below kMinimumRaster.
    enum class Raster { Project, Source, SourceQuarter, SourceSixteenth };
    Raster raster = Raster::Project;
    // Transcode's "Apply source transformations > Color encoding - Select this option if you want to transcode clips
    // with their applied color adapters" (p.544; 8.5c2): the clips' colour transformations go into the new media, so
    // the new clips and the new sequences' segments have none. Off, "the adapters are bypassed during the sequence
    // transcode only. The source transformations still remain on the clips in the bin": the new clips keep the
    // encoding.
    bool applyColorEncoding = false;
    // ACES projects (8.5e4b2): the applied encoding goes on to the ACEScct working space (media::withAcesWorkingSpace), as
    // the clips play there (p.558).
    bool acesWorkingSpace = false;
    // Transcode's "Create new clips" (p.354-355), offered with Dynamic Relink on: off, "the existing master clip is
    // associated with both the original media file and the transcoded media file". The new media of a selected master
    // clip then name its MobID (MultiRez, p.1099-1101; 8.6a) and no ".new" clip is made; a sequence's clips keep the
    // media of the ranges it uses (with handles), partly online at that resolution (p.1118; 8.6c).
    bool newClips = true;
    MediaCreation creation; // its raster is the project's
};

// The raster a clip is transcoded at.
// The source sizes come from probing the clip's file; even, and at least kMinimumRaster.
QSize rasterFor(const model::BinItem& clip, ConsolidateOptions::Raster raster, QSize project);

// The handle length the dialog starts with (p.351): 60 frames for NTSC
// rates, 50 for PAL, 24 for film rates.
std::int64_t defaultHandles(const Rational& editRate);

// "<name>.new.01", the first number no item in `taken` has (p.349).
QString numberedName(const QString& name, const QString& suffix, const QStringList& taken);

// What consolidate() made and changed; the caller puts them in the bin.
struct ConsolidateResult {
    QList<model::BinItem> added;   // new clips and sequences, for the bin
    QList<model::BinItem> changed; // selected items now using the new media
    // Master clips that have media at one more resolution (newClips off): Dynamic Relink chooses which they play.
    QList<model::BinItem> associated;
    QStringList skipped;           // why items or clips were left as they were
    QString error;                 // what stopped it (cancelled too)
};

// Master clips are copied whole; sequences, the parts of their clips they
// use with handles, as new master clips (p.349). `clips` finds a sequence's
// clips; `taken` are the names in the bin, for numbering.
// The work runs on the calling thread and can take long (the UI runs it from the Background
// Queue). It stops at the first error, keeping what was made so far in `added`.
ConsolidateResult consolidate(const QList<model::BinItem>& selection, const QString& binId, const ClipLookup& clips,
                              const QStringList& taken, const ConsolidateOptions& options);

// AAF export's Copy All Media (Editing Guide p.870, p.1175; 7.1c1): each clip's media files of the kinds chosen
// (its video tracks' files with `video`, its audio tracks' with `audio`) are copied into `folder`, and the clip's file
// sources then name the copies. A file used by several clips is copied once; names taken in the folder get " 2", " 3"
// ... (ours). What could not be copied is said in `warnings` and the clip keeps its file there. Files in `exclude`
// (absolute paths) are not copied and the clips keep naming them: Export Media Difference Between Sequences, "Media
// Composer will export any clips that reference assets not included in the previous sequence" (p.885-886).
struct CopiedMedia {
    QList<model::BinItem> items; // the clips with their sources naming the copies
    QStringList warnings;
    int files = 0;               // files copied
};
CopiedMedia copyClipMedia(const QList<model::BinItem>& clips, const QString& folder, bool video, bool audio,
                          const QSet<QString>& exclude = {});

} // namespace lumenis::media
