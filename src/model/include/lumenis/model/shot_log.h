#pragma once

#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// The Import setting's Shot Log tab (Editing Guide p.1197-1198; docs/spec/ale.md, "Combining and merging events"): what
// a shot log's events become when they are imported. interop::ale reads a log into one master clip per event (as
// logged); shotLogEvents() then combines them into master clips with subclips, or merges them with clips the bin already
// has. MainWindow::importShotLogs applies the result to the bin.
namespace lumenis::model {

// The tab's five choices, in its order.
enum class ShotLogEvents {
    AsLogged,         // "Maintain events as logged."
    Scene,            // "Combine events based on scene and automatically create subclips."
    CameraRoll,       // "Combine events based on camera roll and automatically create subclips."
    KnownSources,     // "Merge events with known sources and automatically create subclips."
    KnownMasterClips, // "Merge events with known master clips."
};
// The setting's value ("asLogged", "scene", "camroll", "sources", "masters") as a choice; AsLogged for any other.
ShotLogEvents shotLogEventsOf(const QString& value);

// What an import adds to the bin and changes in it.
struct ShotLogResult {
    QList<BinItem> added;   // new items, in the log's order (a combined master clip before its subclips)
    QList<BinItem> updated; // items of `known` with the log's columns merged in (KnownMasterClips), same ids
    QStringList warnings;   // each once, with what it is about
};

// The events of one log (`events`, master and audio clips as interop::ale::importText makes them) under `mode`.
// `known` are the clips selected in the bin ("You must select the clips that you want to merge before selecting this
// option", p.1198); only the two merge modes read them. `bin` is the target bin (subclip names). Ours where the guide is
// silent (docs/spec/ale.md): events are combined per source (Tape, else Source File) and rate as well as by the Scene or
// Camroll column, the combined master clip is named for the column's value and spans its events' first Start to last
// End; an event without the column, or with no known clip to merge with, is kept as logged, with a warning.
ShotLogResult shotLogEvents(ShotLogEvents mode, const QList<BinItem>& events, const QList<BinItem>& known, const Bin& bin,
                            const QDateTime& when);

} // namespace lumenis::model
