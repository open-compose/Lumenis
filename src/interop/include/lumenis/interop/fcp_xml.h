#pragma once

#include "lumenis/interop/otio.h"
#include "lumenis/model/bin.h"

#include <QByteArray>
#include <QString>

#include <optional>

// Final Cut Pro 7 XML (Phase 3.6c, docs/spec/fcp-xml.md): the "xmeml" interchange format that Final
// Cut Pro 7 wrote and that Premiere Pro, DaVinci Resolve and Hiero still read and write. Media
// Composer has no FCP XML feature: this is Cutthulhu's own, for turnovers with those programs.
//
// It is a translation layer over the OpenTimelineIO import and export (otio.h), as OpenTimelineIO's
// own FCP adapter is: an xmeml file is read into OTIO's JSON and imported by otio::importJson, and a
// sequence is exported by otio::exportJson and written as xmeml. So the two formats map to bins in
// one way: the same media is the same clip (otio::mediaMobId), and what each leaves out is the same.
namespace lumenis::interop::fcpxml {

// Whether `data` (a file's first bytes are enough) is an xmeml document: XML whose root element is
// <xmeml>.
bool isFcpXml(const QByteArray& data);
// Whether `path` is an .xml file holding xmeml (its first 4 KB are read).
bool isFcpXmlFile(const QString& path);

// The xmeml document as OpenTimelineIO JSON: a Timeline for one sequence, a SerializableCollection
// of them for more. The sequences are the root's and those inside projects and bins (<children>);
// those nested in clips are stacks inside them. Nothing, with `error` set, when it is not xmeml or
// holds no sequence. `warnings` gets what was read only in part.
std::optional<QByteArray> toOtioJson(const QByteArray& xml, QString* error = nullptr, QStringList* warnings = nullptr);

// Reads xmeml into bin items, as otio::importJson does (offline master clips and sequences).
std::optional<otio::ImportResult> importXml(const QByteArray& xml, const otio::ImportOptions& options = {},
                                            QString* error = nullptr);
// Reads a file; a timeline without a name is named after it.
std::optional<otio::ImportResult> importFile(const QString& path, const otio::ImportOptions& options = {},
                                             QString* error = nullptr);

// The sequence as an xmeml document (in ExportResult::json), from otio::exportJson's timeline. Fails
// (ok false) only when the item is not a sequence.
otio::ExportResult exportXml(const model::BinItem& sequence, const otio::ExportOptions& options = {});
// exportXml() written to `path` (through QSaveFile).
otio::ExportResult exportFile(const model::BinItem& sequence, const QString& path, const otio::ExportOptions& options = {});

} // namespace lumenis::interop::fcpxml
