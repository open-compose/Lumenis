#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <optional>

// Avid Log Exchange (ALE) shot logs (Phase 3.6a, docs/spec/ale.md): the tab-separated text files of
// the Editing Guide's "Avid Log Specifications" (p.106-112), read into master clips (Importing Shot
// Log Files, p.131-132) and written from a bin's clips (Exporting Shot Log Files, p.129-130). A log has
// three sections in this order: Heading (global titles: FIELD_DELIM, VIDEO_FORMAT, AUDIO_FORMAT,
// TAPE, FPS), Column (the column titles) and Data (a line for each clip).
namespace lumenis::interop::ale {

// How an import fills in what the log does not say.
struct ImportOptions {
    // The tape of clips without a Tape column or a global TAPE: "If you omit this Title, the file name
    // becomes the global tape name" (p.107). importFile() sets it to the file's base name.
    QString tape;
    // Created and Modified of the clips; now when invalid.
    QDateTime when;
    // The frame rate of a log without FPS (it is required, p.107); 23.976 when unset.
    std::optional<Rational> rate;
};

// What an import makes.
struct ImportResult {
    // A master clip (an audio clip when it has no picture track) for each data line, in order.
    QList<model::BinItem> items;
    // Column titles that are not standard bin headings: the bin's custom columns (p.269), in the
    // log's order.
    QStringList customColumns;
    // The global titles and their values, in the log's order.
    QList<QPair<QString, QString>> heading;
    // Lines left out and what was not read, once each with a count.
    QStringList warnings;
};

// Reads a log's text. Never fails: what cannot be read is a warning, and a clip whose Start, End
// and Duration disagree is left out, as Media Composer leaves it out (p.114).
ImportResult importText(const QString& text, const ImportOptions& options = {});
// Reads a file (UTF-8, else Latin-1); nothing, with `error` set, when it cannot be read.
std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options = {}, QString* error = nullptr);
// Whether a file is a shot log: an .ale file, or a text file whose first line is "Heading".
bool isShotLog(const QString& path);

// How a shot log is written.
struct ExportOptions {
    // The columns after Name, Tracks, Start, End and Duration, in order (a bin's Text view columns,
    // say). Empty: every other heading the clips have, standard ones first, then custom ones.
    QStringList columns;
    // Subclips too: Export to File writes master clips and subclips (p.130, p.1174); Export Bin
    // "creates a shot log of only the master clips in the bin" (p.130).
    bool subclips = true;
    // VIDEO_FORMAT and AUDIO_FORMAT; from the first clip's Format column, and 48kHz, when empty.
    QString videoFormat;
    QString audioFormat;
    // Export As Tab Delimited (p.130, p.1174: "the selected bin as a shot log file in the form of a tab-delimited ASCII
    // text file"): the same columns and clips, as a line of the column titles and a line per clip, without the Heading,
    // Column and Data sections (ours: the guide does not give the layout).
    bool tabDelimited = false;
};

// What an export made.
struct ExportResult {
    bool ok = false;
    QString error;
    QString text;
    QStringList warnings;
    int clips = 0; // data lines written
};

// The clips among `items` as a shot log ("Information for other objects, such as group clips,
// sequences, and precomputes, is not included", p.130).
ExportResult exportText(const QList<model::BinItem>& items, const ExportOptions& options = {});
// exportText() written to `path` (through QSaveFile). Fails when there is no clip to write.
ExportResult exportFile(const QList<model::BinItem>& items, const QString& path, const ExportOptions& options = {});

// FPS as a log writes it ("23.98", "25", "29.97", p.107) and reads it back (also "23.976").
QString fpsText(const Rational& rate);
std::optional<Rational> rateFromFps(const QString& text);

} // namespace lumenis::interop::ale
