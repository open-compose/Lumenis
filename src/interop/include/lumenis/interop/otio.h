#pragma once

#include "lumenis/core/mob_id.h"
#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// OpenTimelineIO (Phase 3.6b, docs/spec/otio.md): .otio files, the JSON serialization of an OTIO
// timeline, read into the native model and written from a sequence. Media Composer imports them
// through File > Input > Import Media (or by dropping them into a bin) and writes them from the List
// Tool's "OTIO" output format; after an import "it may be necessary to relink media" (Editing Guide
// p.969). So an import makes an offline master clip for each piece of media the timeline names (its
// file kept as the clip's import source, for Relink) and a sequence of them; the export is one
// sequence. "Only the basic features included in the OTIO specification are transferred, and only
// the most simple effects, such as dissolve" (p.969): clips, gaps, tracks, dissolves, speed changes
// and markers.
namespace lumenis::interop::otio {

// The MobID of the master clip an import makes for a piece of media (its file's URL, else the media
// reference's or the clip's name): the same for every import of the same media, so that a timeline
// imported again, or another of the same material, names the same clips. A name-based UUID
// (model::derivedMobId) of "otio-media:" and the key.
MobId mediaMobId(const QString& key);

// How an import fills in what the file does not say.
struct ImportOptions {
    QString binId;  // the bin the items go into: the segments name it
    QDateTime when; // Created and Modified; now when invalid
    // A timeline's name when it has none (importFile(): the file's base name).
    QString name;
};

// What an import makes.
struct ImportResult {
    // The master (and audio) clips, one for each piece of media, in the order first used, then one
    // sequence for each timeline (a file holds one, or a collection of them).
    QList<model::BinItem> items;
    // What was read only in part or not at all, once each, with a count when more than one.
    QStringList warnings;
};

// Reads an .otio file's JSON. Nothing, with `error` set, when it is not JSON or holds no timeline
// (a Timeline, or a SerializableCollection of them); what cannot be read inside one is a warning.
std::optional<ImportResult> importJson(const QByteArray& json, const ImportOptions& options = {}, QString* error = nullptr);
// Reads a file. Nothing, with `error` set, when it cannot be read or importJson() refuses it.
std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options = {}, QString* error = nullptr);

// Drops the clips of `result` whose MobID one of `existing` already has, and points the sequences'
// segments at those instead: media imported before is the same clip.
void reuseClips(ImportResult& result, const QList<model::BinItem>& existing);

// How a sequence is written (the List Tool's OTIO output, p.969).
struct ExportOptions {
    // The tracks written: all when empty.
    QStringList tracks;
    // The clip a segment uses (by its MobID and clip id), for its media's file, start and length;
    // unset or nullopt: the segment's media is named by its clip's name only (a MissingReference),
    // counted from 0, with a warning.
    std::function<std::optional<model::BinItem>(const MobId& mob, const QString& clipId)> resolve;
};

// What an export made.
struct ExportResult {
    bool ok = false;
    QString error;
    QByteArray json;       // the .otio file
    QStringList warnings;  // what was left out or changed, once each
    int clips = 0;         // how many clips (not gaps) were written
};

// The sequence as an OTIO Timeline. Fails (ok false) only when the item is not a sequence.
ExportResult exportJson(const model::BinItem& sequence, const ExportOptions& options = {});
// exportJson() written to `path` (through QSaveFile).
ExportResult exportFile(const model::BinItem& sequence, const QString& path, const ExportOptions& options = {});

} // namespace lumenis::interop::otio
