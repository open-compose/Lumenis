// The SubCap effect's captions (Phase 7.4a; docs/spec/subcap.md; FX Guide ch.14, p.378-388, p.501-504): "add text to
// the video in your sequences, either by entering text directly or by importing text from files formatted in standard
// captioning formats", the Avid DS caption file and EBU N19 (.stl, EBU Tech 3264). A caption is a SubCap segment's text
// over the segment's frames; importing a file subdivides the segment at the captions' timecode. The picture is drawn
// by media::subcapPicture (src/media/subcap.cpp).
#pragma once

#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"

#include <QByteArray>
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace lumenis::model {

inline constexpr char kSubCapCategory[] = "SubCap Effect Parameters";
// The Caption Text category's text box, where a segment's caption is kept.
inline constexpr char kCaptionTextKey[] = "Caption text box";

// The Global Properties dialog box's information (p.504), kept under its own category, with the Synchronize tab's
// choices as an object under kSynchronizeKey: {parameter or information name: "Clip", "Track" or "Sequence"}.
inline constexpr char kSubCapPropertiesCategory[] = "SubCap Effect Global Properties";
inline constexpr char kSynchronizeKey[] = "Synchronize";

bool isSubCap(const Effect& effect);
QString captionText(const Effect& effect);
void setCaptionText(Effect& effect, const QString& text);

// One caption: frames [start, end) in timecode frames (a file's) or sequence frames (a sequence's), and its text, rows
// separated by line breaks.
struct Caption {
    std::int64_t start = 0;
    std::int64_t end = 0;
    QString text;
    bool operator==(const Caption&) const = default;
};

// The Avid DS caption file: "00:00:02:00 00:00:03:00" then the caption's rows, captions apart by an empty line
// (p.379). Timecode in `format`.
std::optional<QList<Caption>> parseDsCaptions(const QString& text, const TimecodeFormat& format, QString* error = nullptr);
QString writeDsCaptions(const QList<Caption>& captions, const TimecodeFormat& format);

// The EBU N19 file's General Subtitle Information that the Global Properties dialog keeps (p.504), each limited as the
// standard limits it ("The character limits ... are imposed by the EBU N19 (.stl) caption file standard").
struct StlInfo {
    QString originalProgramTitle, originalEpisodeTitle, translatedProgramTitle, translatedEpisodeTitle; // 32 each
    QString translatorName, translatorContact; // 32 each
    QString publisher, editorName, editorContact; // 32 each
    QString countryOfOrigin;  // 3 letters, "GBR"
    QString languageCode;     // the GSI's two hexadecimal digits, "09" for English
    QString userData;         // the Custom tab, 576
    QDate creationDate, revisionDate;
    bool operator==(const StlInfo&) const = default;
};

// An .stl file: the GSI block (1024 bytes) and one TTI block (128 bytes) for each caption (ours: one block, its text
// field cut at 112 bytes). `fps` 25 writes STL25.01, 30 STL30.01; reading takes the file's own. Text is ASCII (ours:
// other characters are written as '?'); rows are joined by the standard's line break 0x8A.
std::optional<QList<Caption>> parseStl(const QByteArray& data, int* fps = nullptr, StlInfo* info = nullptr, QString* error = nullptr);
QByteArray writeStl(const QList<Caption>& captions, int fps, const StlInfo& info = {});

// The captions of the SubCap segments on `tracks` of the sequence (every video track when empty), in sequence frames,
// in order. "A caption file can have only one caption for any given timecode range" (p.386): where captions of two
// tracks overlap, the higher track's wins with `topWins`, else the lower's.
QList<Caption> sequenceCaptions(const Sequence& sequence, const QStringList& tracks = {}, bool topWins = true);

// Imports `captions` (sequence frames) into the SubCap segment at `frame` on `track`: "Media Composer subdivides the
// effect at the timecode points specified by the file. This creates a series of separate effects, one for each
// caption" (p.379); the frames between captions keep the effect without text (ours). False, with nothing changed and
// the reason in `error`, when there is no SubCap segment there or a caption falls outside it ("the import could not
// take place").
bool importCaptions(Sequence& sequence, const QString& track, std::int64_t frame, const QList<Caption>& captions,
                    QString* error = nullptr);
// Synchronizing (p.384-385): the SubCap effect at `frame` on `track` was just changed from `before`; each parameter or
// information item that changed is copied to the other SubCap effects on the track ("Track") or in the sequence
// ("Sequence") as its Synchronize choice says ("Sequence" until chosen, ours), or to none ("Clip"). The caption text
// stays each clip's own. Returns how many effects changed.
int synchronizeSubCap(Sequence& sequence, const QString& track, std::int64_t frame, const Effect& before);
// The Synchronize choice of an item: "Clip", "Track" or "Sequence".
QString subcapSyncMode(const Effect& effect, const QString& name);
// The EBU N19 information of the sequence's first SubCap effect (its Global Properties).
StlInfo subcapStlInfo(const Sequence& sequence);

// The range [start, end) in sequence frames of the SubCap segment at `frame` on `track`; nothing without one.
std::optional<std::pair<std::int64_t, std::int64_t>> subcapSegmentRange(const Sequence& sequence, const QString& track, std::int64_t frame);

} // namespace lumenis::model
