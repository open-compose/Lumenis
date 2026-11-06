// Closed captions on the Data track (Phase 7.4b; docs/spec/closed-captions.md; Editing Guide p.232, p.973-985): the
// CEA-608 ("Line 21") byte pairs a Scenarist Closed Caption (.scc) file holds, kept on a D-track only master clip
// ("it is a D-track only clip that contains only the 608 compatibility bytes from EIA-708", p.232), edited into the
// sequence's D1 track and decoded into the captions Show Captions draws and TTML exports. Pure functions; the
// application wires them up in MainWindow, the Timeline and the Composer.
#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/core/timecode.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/subcap.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace lumenis::model {

// A clip's or sequence's caption data: two bytes per frame (field 1's byte pair, odd parity as sent), 0x80 0x80 where
// nothing is sent.
inline constexpr char kNullCaptionWord[] = "\x80\x80";

// The master clip's caption data, kept in its composition under "closedCaptions" (ours). Empty when it has none.
QByteArray clipCaptionData(const BinItem& item);
bool hasCaptionData(const BinItem& item);
void setClipCaptionData(BinItem& item, const QByteArray& data);

// A byte with odd parity, as CEA-608 sends it.
char withParity(int byte);

// ---------------------------------------------------------------- Scenarist Closed Caption files

// One line of an .scc file: its timecode as a frame count in the file's timecode (30 frames a second, drop frame or
// not) and its byte pairs.
struct SccLine {
    std::int64_t frame = 0;
    QByteArray bytes;
    bool operator==(const SccLine&) const = default;
};
struct SccFile {
    bool dropFrame = false; // "00:00:01;00": a semicolon before the frames
    QList<SccLine> lines;
    // The first line's timecode: the AncData Import dialog's starting timecode (p.232).
    std::int64_t startFrame() const { return lines.isEmpty() ? 0 : lines.first().frame; }
};
std::optional<SccFile> parseScc(const QString& text, QString* error = nullptr);
// The caption data from the file, frame 0 at `startFrame` (in the file's timecode), laid out at `rate` (ours: pairs
// that would land on a frame already used follow on the next free frame; lines before the start are left out).
QByteArray sccCaptionData(const SccFile& file, std::int64_t startFrame, const Rational& rate);
// An .scc file of the data: one line for each run of byte pairs, timecode from `firstFrame` in `format`.
QString writeScc(const QByteArray& data, std::int64_t firstFrame, const TimecodeFormat& format);

// ---------------------------------------------------------------- Decoding (CEA-608 channel CC1)

// A row of a caption: row 1 to 15 on the caption grid, the column (0-31) of its first character and its text.
struct CaptionRow {
    int row = 15;
    int column = 0;
    QString text;
    bool operator==(const CaptionRow&) const = default;
};
// What is on the screen: Pop-on, Roll-up and Paint-on captions (p.983).
struct CaptionScreen {
    QList<CaptionRow> rows;
    bool isEmpty() const { return rows.isEmpty(); }
    QString text() const; // the rows joined by line breaks, without their columns
    bool operator==(const CaptionScreen&) const = default;
};
// A caption shown over frames [start, end).
struct ShownCaption {
    std::int64_t start = 0;
    std::int64_t end = 0;
    CaptionScreen screen;
    bool operator==(const ShownCaption&) const = default;
};
// Every caption shown by the data, decoded frame by frame from its start; a frame's byte pair shows on that frame.
QList<ShownCaption> decodeCaptions(const QByteArray& data);
// The caption on `frame`, if any (a binary search of `captions`).
std::optional<CaptionScreen> captionAt(const QList<ShownCaption>& captions, std::int64_t frame);
// The frames [start, end) where byte pairs are sent: "The caption loading phase (black solid line) contains
// information to properly display the caption" (p.984).
QList<std::pair<std::int64_t, std::int64_t>> captionLoadingSpans(const QByteArray& data);
// Pop-on caption data for `captions` (frames [start, end), text rows by line breaks; ours, for the SubCap captions and
// tests): each caption loads in the frames before its start, is shown from its start and erased at its end. Rows are
// centred at the bottom of the grid; characters other than the basic set are sent as their nearest 608 character.
QByteArray popOnCaptionData(const QList<Caption>& captions, std::int64_t length);

// ---------------------------------------------------------------- Sequences

// The sequence's caption data: its D1 track's segments' clip data (`clipData` gives a segment's clip's), null pairs
// in filler and past a clip's data. Empty without a D1 track.
QByteArray sequenceCaptionData(const Sequence& sequence, const std::function<QByteArray(const Segment&)>& clipData);
// The cuts on D1 that break a caption: "If you edit in the solid black area of the D-track, you could break the
// caption ... an orange triangle icon appears in the Timeline" (p.984). A cut breaks one where byte pairs are sent on
// both of its sides, or where the segment after it starts within its clip's loading span.
QList<std::int64_t> brokenCaptionCuts(const Sequence& sequence, const std::function<QByteArray(const Segment&)>& clipData);

// ---------------------------------------------------------------- TTML

// A TTML document of the captions (p.985; W3C TTML1): one <p> per caption with its rows split by <br/>, times as
// media time from the data's first frame at `rate`.
QString writeTtml(const QList<ShownCaption>& captions, const Rational& rate);

} // namespace lumenis::model
