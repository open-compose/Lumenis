#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/managed.h"
#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSize>
#include <QString>

#include <optional>

// MultiRez and Dynamic Relink (Editing Guide ch.31, p.1095-1121; the Dynamic Relink setting, p.1169-1171;
// docs/spec/dynamic-relink.md). Media Composer keeps a master clip's media at several resolutions through the Media
// Indexer; ours are the managed media files that name the clip's MobID as their material package (MediaDatabase), next
// to the media the clip was made with (its "affinity" media, p.1101-1102). Dynamic relink chooses which of them the clip
// plays by the rules of the setting's Working or Target tab ("quality matching", p.1120-1121) whenever clips load into
// the Source monitor or the Timeline (p.1107).
namespace lumenis::media {

// One tab of the Dynamic Relink setting: Working Settings or Target Settings (p.1169-1171).
struct RelinkRules {
    // "If no match is found" (p.1170).
    enum class NoMatch { HighestQuality, LowerBandwidth, KeepExisting, Offline };
    // Video Settings' Relink Method: "Most Recent (the latest media files created), Highest Quality, Minimal
    // bandwidth, or Specific Resolution" (p.1170).
    enum class Method { MostRecent, HighestQuality, MinimalBandwidth, SpecificResolution };
    // Audio's Relink method: "Most Recent ..., Highest Quality, Most compressed, or Specific Quality" (p.1170-1171).
    enum class AudioMethod { MostRecent, HighestQuality, MostCompressed, SpecificQuality };

    NoMatch noMatch = NoMatch::KeepExisting;
    Method video = Method::MostRecent;
    // Specific Resolution's Relink Parameters (p.1170): empty for any. Raster as "1920x1080", Frame Rate as a number
    // ("23.976", "25"), Codec Family ("Avid DNx", "Apple ProRes", "H.264", "H.265", "Uncompressed", "Other"),
    // Compression Quality (a resolution's name, "Avid DNx SQ"), Container ("MXF", "QuickTime", "MP4", "Other").
    QString raster;
    QString frameRate;
    QString codecFamily;
    QString compression;
    QString container;
    // "When multiple matches are found": "Prefer target raster, then prefer highest bandwidth" (true) or "Prefer
    // lowest bandwidth" (false) (p.1170).
    bool preferTargetRaster = true;

    AudioMethod audio = AudioMethod::MostRecent;
    // Specific Quality's filters (p.1171): Codec Family ("PCM", "AAC", "Other"), Bit Depth and Sample Rate as numbers,
    // Container ("MXF", "WAV", "Other"); empty for any.
    QString audioCodec;
    QString bitDepth;
    QString sampleRate;
    QString audioContainer;
    // "Prefer highest bitrate" (true) or "Prefer lowest bitrate" (false) (p.1171).
    bool preferHighestBitrate = true;
};

// The Dynamic Relink Project setting (p.1106-1110, p.1169-1171).
struct DynamicRelinkSettings {
    bool enabled = false;
    // "Allow Mixed Frame Rate Media": media at other rates than the project's may be chosen (p.1109, p.1169).
    bool mixedRates = false;
    // "Override Working Settings with Target Settings: Only Target settings tab will be used" (p.1169).
    bool overrideWorking = false;
    RelinkRules working;
    RelinkRules target;
    // The tab in use: Target with overrideWorking, else Working.
    const RelinkRules& active() const { return overrideWorking ? target : working; }

    // From the setting's values (keys as in settings.json: "enabled", "working.noMatch", "target.raster", ...); missing
    // or unknown values keep the defaults above.
    static DynamicRelinkSettings fromValues(const QJsonObject& values);
    bool operator==(const DynamicRelinkSettings& o) const;
};

// One media a master clip can play: its affinity media (the link it was made with) or a managed media set naming it.
struct MediaCandidate {
    QJsonObject link;  // the clip's link to it (Link JSON)
    QString mediaId;   // the managed media's id; empty for linked affinity media
    bool affinity = false;
    bool video = false; // has a picture
    QString resolution; // "Avid DNx HQ", or the linked file's codec ("H.264")
    QString codecFamily;
    QString container;
    QSize raster;
    Rational rate{0, 1}; // the picture's frame rate; 0/1 for sound only
    int sampleRate = 0;
    int bitDepth = 0;
    double bandwidth = 0.0; // bytes a clip frame (all its files)
    QDateTime created;
};

// How much of a clip's length a media set holds, by their timecode spans (p.1102, p.1118-1120).
enum class MediaCoverage { None, Partial, Full };
MediaCoverage mediaCoverage(const MediaSet& set, const model::BinItem& clip);

// The media sets associated with a master clip or audio clip (p.1099-1102): those naming its MobID ("Single Clip,
// Multiple Resolutions", the affinity media) and, unless `affinityOnly`, those of other clips sharing its source (its
// tape, else its source file) and some of its timecode span ("Multiple Clips, Multiple Resolutions", "Partial Clips").
QList<MediaSet> associatedMedia(const model::BinItem& clip, const MediaDatabase& db, bool affinityOnly = false);

// The MultiRez bin headings (p.1116): "Video resolutions" and "Audio sample rate" as our media have them: our
// resolutions' names and 32, 44.1, 48 and 96 kHz. Choose Columns lists them at the bottom.
QStringList multiRezHeadings();
// What a clip's associated media hold under each heading.
QHash<QString, MediaCoverage> multiRezCoverage(const model::BinItem& clip, const MediaDatabase& db);
// A MultiRez cell: "Filled circle Online, Half-filled circle Partially online, Empty circle Offline or Not Available".
QString multiRezCellText(MediaCoverage coverage);

// The media files Delete may remove with a clip, by heading (p.1117): "For clips in a bin, you can delete only the media
// that was originally associated with the clip", every resolution that names its MobID, and not those of clips that
// only share its source and timecode.
QMap<QString, QStringList> affinityMediaFiles(const model::BinItem& clip, const MediaDatabase& db);

// What chooseMedia() decided.
struct RelinkChoice {
    std::optional<MediaCandidate> media; // nothing: offline
    // The media matches the rules (a Show Mismatches display would not colour it, p.1113).
    bool matched = false;
};

// The media a master clip or audio clip can play now: its affinity media while its files are there, then the media
// database's associated sets (associatedMedia()) that hold the clip's tracks for its whole length, newest first. Empty for other
// items. `affinity` is the clip's own media, as affinityLink() gives it.
QList<MediaCandidate> mediaCandidates(const model::BinItem& clip, const MediaDatabase& db);

// The link the clip was made with: kept under "multiRez" in the link while dynamic relink has it on other media.
QJsonObject affinityLink(const model::BinItem& clip);
// The candidate the clip plays now: by media id, or the affinity media; nothing when offline or unknown.
std::optional<MediaCandidate> currentMedia(const model::BinItem& clip, const QList<MediaCandidate>& candidates);

// Quality matching (p.1120-1121, p.1169-1171): the candidate the rules choose for a clip whose media is `current`, in a
// project at `projectRate` and `projectRaster`. Media at the project's rate is preferred; with none, or with
// `mixedRates`, the others are taken, and with none at the project's rate "Media Composer uses the most recent online
// compression out of all available" (p.1169). Pictures go by the video rules, sound-only clips by the audio rules. With
// `crossRate`, Specific Resolution's Frame Rate also takes media at a whole multiple of the project's rate (Cross Rate
// Dynamic Relink, p.1109-1110); "the clips placed in the Source Monitor will not be linked to a different rate media".
RelinkChoice chooseMedia(const QList<MediaCandidate>& candidates, const std::optional<MediaCandidate>& current,
                         const RelinkRules& rules, const Rational& projectRate, const QSize& projectRaster,
                         bool mixedRates, bool crossRate = true);

// Relinks a master clip or audio clip to the media the setting's active rules choose (p.1107): its link is the
// chosen candidate's, keeping the affinity link under "multiRez"; offline is the affinity link marked unlinked with
// "multiRezOffline" ("Relinking to offline does not delete existing media", p.1170). Returns whether the item
// changed; `choice` gets the decision. Nothing happens while the setting is off, or for other items. The Video column
// shows the resolution linked to (p.1116), its own text back with the affinity media. `crossRate` as chooseMedia().
bool dynamicRelink(model::BinItem& clip, const MediaDatabase& db, const DynamicRelinkSettings& settings,
                   const Rational& projectRate, const QSize& projectRaster, RelinkChoice* choice = nullptr,
                   bool crossRate = true);

// Puts a clip back on its affinity media (Dynamic Relink turned off); whether it changed.
bool restoreAffinityMedia(model::BinItem& clip);

// The codec family a FFmpeg codec name belongs to, as the Relink Parameters list them: "Avid DNx", "Apple ProRes",
// "H.264", "H.265", "Uncompressed", "PCM", "AAC" or "Other".
QString codecFamilyOf(const QString& codec);
// The container a FFmpeg format name and file name are: "MXF", "QuickTime", "MP4", "WAV" or "Other".
QString containerOf(const QString& format, const QString& path);

} // namespace lumenis::media
