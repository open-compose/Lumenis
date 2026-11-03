#pragma once

// Title Effect clips (ROADMAP 6.8d4a; FX Guide p.357-360, p.365, p.488-489; docs/spec/title-tool.md): what saving a
// classic title makes. "When the Title tool or Marquee saves a title, it creates a Title Effect clip and saves it in the
// bin you select. One media object, referred to as a precompute, has references to the RGB and alpha channel
// information" (p.365).
//
// Ours: a bin item of kind Title whose composition is a sequence of one track, V1, holding one segment: filler with the
// Title, Rolling Title or Crawling Title effect (p.488-489), the classic title itself among the effect's parameters (for
// revising), and a nest of two tracks, V1 the fill precompute and V2 the matte precompute ("the new title's graphic and
// alpha tracks", p.374). The precomputes are RenderedEffect master clips of one frame per page (a still title has one
// page), each frame a whole frame of the title: the fill its colours over black, the matte its alpha as grey. A
// fast-saved title has no precomputes ("just the raw title objects ... are saved", p.357-358) and plays as offline. The
// compositor keys the fill over what lies below by the matte (media/classic_title.h: classicTitlePicture).

#include "lumenis/model/bin.h"
#include "lumenis/model/classic_title.h"
#include "lumenis/model/sequence.h"

#include <QDateTime>
#include <QSize>
#include <QString>

#include <cstdint>
#include <optional>

namespace lumenis::model {

// The effect's names: "Title Effects: Crawling Title and Rolling Title", "Title Effects: Title" (p.488-489).
inline const QString kTitleEffectCategory = QStringLiteral("Title");
QString classicTitleEffectName(ClassicMotion motion);
// Whether an effect is a classic Title, Rolling Title or Crawling Title effect.
bool isClassicTitleEffect(const Effect& effect);
// The parameter category holding the classic title (ours): {"Title": the title's JSON, "Unrendered": true when fast
// saved}.
inline const QString kClassicTitleParameters = QStringLiteral("Title Tool");

// A Title effect for `title`, fast saved (without media) or not.
Effect classicTitleEffect(const ClassicTitle& title, bool unrendered);
// The classic title an effect holds; nothing for other effects.
std::optional<ClassicTitle> classicTitleOf(const Effect& effect);
bool isUnrenderedTitle(const Effect& effect);

// Rolling and crawling titles in a sequence (6.8e1, p.370): "the entire roll shrinks to fit within the duration of
// the marked segment in the sequence", and marks in the Source monitor "change the start and finish points". Where in
// the whole roll (0 to 1) the segment's first frame is: its source IN over the clip's length when it was edited in
// (ours: the finish stays the roll's end). Trimming changes how fast it plays, not where it starts.
double titleRollStart(const Effect& effect);
void setTitleRollStart(Effect& effect, double start);

// The Title Effect clip a sequence's title segment was edited in from (6.8e2): "If you want to revise a title after
// you edit it into a sequence, you must reopen the title in the Title tool directly from the sequence" (p.372), and
// Save Title then "replaces the previous clip in the sequence ... and saves the new title to the bin" (p.373). Empty
// when not known (titles placed before 6.8e2, promoted ones).
struct TitleSource {
    QString bin;
    QString clip;
    bool isEmpty() const { return clip.isEmpty(); }
};
TitleSource titleSourceOf(const Effect& effect);
void setTitleSource(Effect& effect, const TitleSource& source);
// A sequence's title segment given the revised clip's title and precomputes, keeping its length, roll start and the
// effect's own parameters (Foreground, Scaling, Position, Crop and their keyframes).
void reviseTitleSegment(Segment& segment, const BinItem& clip, const QString& bin);

// "A 2-minute Title Effect clip that corresponds to the new title appears in the bin" (p.358): its length in frames
// at `rate`.
std::int64_t titleClipLength(const Rational& rate);

// The precomputes a Title Effect clip's segment plays: the fill and matte clips' ids and bin; empty for a fast-saved
// title. `resolution` (a media::Resolution id) is what they were made at, or were to be made at when fast saved ("The
// default resolution is the resolution specified at the time you originally saved the titles with the Fast Save
// option", FX Guide p.375); `raster` the frame size they were made at, for Wrong Format (p.376). Both are kept under
// the Title Tool parameters ("Resolution", "Raster" as "WxH").
struct TitleMedia {
    QString binId;
    QString fill;
    QString matte;
    QString resolution;
    QSize raster;
    bool isEmpty() const { return fill.isEmpty() || matte.isEmpty(); }
    bool operator==(const TitleMedia&) const = default;
};
TitleMedia titleMediaOf(const Segment& segment);
// Re-create Title Media (FX Guide p.374): the segment's precomputes replaced by `media` (empty: unrendered), as long as
// the segment; "The Re-create Title Media command removes any nested edits inside a title, and replaces them with the
// new title's graphic and alpha tracks." The title, its roll start, source clip and the effect's own parameters stay.
void setTitleMedia(Segment& segment, const TitleMedia& media);

// The Title effect segment (filler with the effect and the nest of the precomputes) `length` frames long.
Segment classicTitleSegment(const ClassicTitle& title, std::int64_t length, const TitleMedia& media);
// A new Title Effect clip named `name` of the title at `rate` (2 minutes long), with its precomputes (none: fast
// saved).
BinItem classicTitleClip(const ClassicTitle& title, const QString& name, const Rational& rate, const TitleMedia& media,
                         const QDateTime& when);
// Gives an existing Title Effect clip a revised title and new precomputes: "The revised Title Effect clip replaces the
// previous clip in the bin" (p.360).
void reviseClassicTitleClip(BinItem& clip, const ClassicTitle& title, const TitleMedia& media, const QDateTime& when);
// The Title effect segment of a Title Effect clip, and the classic title it holds; nothing for other items.
std::optional<Segment> titleSegmentOf(const BinItem& item);
std::optional<ClassicTitle> classicTitleOf(const BinItem& item);
// Whether an item is a fast-saved ("unrendered") Title Effect clip: Bin > Select > Unrendered Titles (p.359).
bool isUnrenderedTitle(const BinItem& item);
// "Fast Save ... just the raw title objects (text and graphics) are saved in the bin, with the prefix "unrendered""
// (p.357): the name a fast-saved title gets.
QString unrenderedTitleName(const QString& name);
// Save Title As keeping the name: "Media Composer retains the original Title Effect clip in the bin and creates a new
// clip with the same name plus a two-digit extension that adds incremental numbering for each revision" (p.360): the
// first "<name>.NN" no item in `bin` has.
QString revisionTitleName(const Bin& bin, const QString& name);

} // namespace lumenis::model
