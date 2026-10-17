#pragma once

#include "lumenis/media/managed.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>

// Clip > Consolidate/Transcode (Editing Guide p.348-356): new media for the
// selected clips and sequences, and the clips that use it.
namespace lumenis::media {

struct ConsolidateOptions {
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
    MediaCreation creation; // its raster is the project's
};

// The raster a clip is transcoded at.
QSize rasterFor(const model::BinItem& clip, ConsolidateOptions::Raster raster, QSize project);

// The handle length the dialog starts with (p.351): 60 frames for NTSC
// rates, 50 for PAL, 24 for film rates.
std::int64_t defaultHandles(const Rational& editRate);

// "<name>.new.01", the first number no item in `taken` has (p.349).
QString numberedName(const QString& name, const QString& suffix, const QStringList& taken);

struct ConsolidateResult {
    QList<model::BinItem> added;   // new clips and sequences, for the bin
    QList<model::BinItem> changed; // selected items now using the new media
    QStringList skipped;           // why items or clips were left as they were
    QString error;                 // what stopped it (cancelled too)
};

// Master clips are copied whole; sequences, the parts of their clips they
// use with handles, as new master clips (p.349). `clips` finds a sequence's
// clips; `taken` are the names in the bin, for numbering.
ConsolidateResult consolidate(const QList<model::BinItem>& selection, const QString& binId, const ClipLookup& clips,
                              const QStringList& taken, const ConsolidateOptions& options);

} // namespace lumenis::media
