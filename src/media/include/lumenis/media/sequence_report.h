#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>

// The Sequence Report (9.2c; docs/spec/sequence-report.md; Editing Guide p.368-371 "Sequence and Clip Information
// Summary", p.237-238 "Sequence Report for AAF Imports"): "a report to display information about the contents of a
// sequence ... a list of the types of effects in your sequence or the location of a particular effect ... a clip summary
// or a source summary", written as text or, for the AAF import's popup report, as CSV. The words and layout of the
// report are ours (the guide shows none).
namespace lumenis::media {

// "Summary Info Options" (p.370-371) and the Import setting's OMFI/AAF options (p.238).
struct SequenceReportOptions {
    bool enabledTracksOnly = false; // "If you selected specific tracks, click Enabled Tracks Only"
    bool useMarks = false;          // "If you set In and Out points, click Use Marks"
    bool effectSummary = true;
    bool effectLocations = true;
    bool skipNonRenderable = false;    // "such as pan/volume effects"
    bool skipRelationshipOnly = false; // "color correction effects with only relationships"
    bool nestedOnly = false;
    bool missingOnly = false;
    bool clipSummary = true;
    bool sourceSummary = true;
    bool offlineOnly = false;
    bool skipNonSelectedGroupClips = false;
    bool showUid = false; // "display the unique identifiers (Mob IDs) associated with the clips and sources"
    bool csv = false;     // "Report As Comma Separated (CSV)" (p.238)
};

// What the report is about.
struct SequenceReportInput {
    QString binName;
    QString name;              // the sequence's name, as the dialog may have changed it
    std::int64_t startFrame = 0; // its Starting TC
    Rational rate{25, 1};
    bool dropFrame = false;
    QString project;
    model::Sequence sequence;
    QStringList enabledTracks; // the selected tracks, for Enabled Tracks Only
    std::optional<std::int64_t> markIn;  // sequence frames, for Use Marks; inclusive
    std::optional<std::int64_t> markOut;
    // A segment's clip (nothing when it cannot be found), and whether a clip's media are offline.
    std::function<std::optional<model::BinItem>(const model::Segment&)> clip;
    std::function<bool(const model::BinItem&)> offline;
};

// One effect where it is in the sequence (Effect Location List, p.370): its track (for a nested effect "(V1) V2",
// the parent track in parentheses), first and last frames in the sequence, name and category, and what it is.
struct ReportedEffect {
    QString track;
    std::int64_t start = 0;
    std::int64_t end = 0; // inclusive
    QString name;
    QString category;
    int depth = 0; // 0 in the sequence, 1 in a nest of it ...
    bool renderable = true;
    bool relationshipOnly = false;
    bool plugin = false;
    bool missing = false; // a plug-in effect whose plug-in is not installed: "Unavailable Effect"
    QString pluginId;
    QString pluginVendor;
    QString pluginVersion;
};

// The effects the options keep, in track order and then time.
QList<ReportedEffect> reportedEffects(const SequenceReportInput& input, const SequenceReportOptions& options);

// The report, as text or CSV (options.csv).
QString sequenceReport(const SequenceReportInput& input, const SequenceReportOptions& options);

} // namespace lumenis::media
