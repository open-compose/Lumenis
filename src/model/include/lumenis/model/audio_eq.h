#pragma once

#include "lumenis/model/sequence.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <array>
#include <optional>

// Audio EQ (Editing Guide p.736-745; docs/spec/audio-eq.md): the limits of the three bands, the
// filters that play them, their frequency response for the Audio EQ tool's graph, and setting
// and removing the effect on segments. Playback and Audio Mixdown run the filters
// (media::AudioMix, src/media/playback.cpp); the tool is src/ui/audio_eq.cpp.
namespace lumenis::model {

inline constexpr double kEqMin = -20.0; // "The range is from +15 dB to -20 dB" (p.739)
inline constexpr double kEqMax = 15.0;
inline constexpr std::array<int, 4> kLowTurnovers{50, 80, 120, 240};       // p.738
inline constexpr std::array<int, 4> kHighTurnovers{6000, 8000, 12000, 15000};
inline constexpr double kEqRangeMin = 50.0; // the EQ Range slider's ends (picture on p.737)
inline constexpr double kEqRangeMax = 15000.0;
// What a predefined template may hold (AudioEq::preset): any turnover or centre in the range of
// hearing, and a midrange from 1/16 octave to 4 octaves wide.
inline constexpr int kPresetMinHz = 20;
inline constexpr int kPresetMaxHz = 20000;
inline constexpr double kPresetMinOctaves = 0.0625;
inline constexpr double kPresetMaxOctaves = 4.0;

// The EQ with each value put in range: gains to kEqMin..kEqMax, rounded to 0.1 dB; turnovers to
// the nearest of their four; the midrange to kEqRangeMin..kEqRangeMax. A template's turnovers,
// centre and width are kept, within the kPreset limits.
AudioEq clampEq(const AudioEq& eq);
// An EQ that changes nothing: every band at 0 dB.
bool isFlat(const AudioEq& eq);

// One second-order filter section, normalised (a0 = 1): y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};
// The EQ's three sections at `sampleRate`: a low shelf turning over at lowTurnover, a peaking
// filter at midFrequency (AudioEq::octaves() wide) and a high shelf at highTurnover. They are the
// widely published "Audio EQ Cookbook" second-order sections (shelf slope 1; ours: the guide
// gives the controls, not the filter shapes).
std::array<Biquad, 3> eqFilters(const AudioEq& eq, double sampleRate);
// The Cookbook's sections themselves, for other effects (media::audioEffects()): shelves with a
// slope of 1, a peak `octaves` wide, and second-order high- and low-pass filters with quality `q`
// (0.7071: Butterworth).
Biquad lowShelf(double db, double frequency, double sampleRate);
Biquad highShelf(double db, double frequency, double sampleRate);
Biquad peaking(double db, double frequency, double octaves, double sampleRate);
Biquad highPass(double frequency, double q, double sampleRate);
Biquad lowPass(double frequency, double q, double sampleRate);
// The EQ's gain in dB at `frequency` Hz (the Audio EQ tool's graph, p.738).
double eqResponse(const AudioEq& eq, double frequency, double sampleRate = 48000);

// A predefined EQ template (p.743-745): the EQ (its `preset` the template's name), and where its
// values come from ("observed", "documented" or "guess") with a note. The table is
// src/ui/resources/eq_templates.json.
struct EqTemplate {
    AudioEq eq;
    QString source;
    QString note;
};
// Reads a templates table ({"templates": [{name, low, lowTurnover, mid, midFrequency, midOctaves,
// high, highTurnover, source, note}, ...]}), in order. Values are put in range (clampEq); entries
// without a name are skipped. Empty, with `error` set, if the JSON cannot be read.
QList<EqTemplate> parseEqTemplates(const QByteArray& json, QString* error = nullptr);
// "-18 dB at 210 Hz, +11 dB at 1222 Hz (3/4 octave), -17 dB at 6 kHz": an EQ's values for the
// Console (p.744 note).
QString describeEq(const AudioEq& eq);
// A width in octaves as the tool shows it: "2", "1/4", "3/4", "1.5".
QString octavesText(double octaves);

// The EQ as sequences keep it ({low, mid, high, lowTurnover, highTurnover, midFrequency, wideMid, in,
// preset, midOctaves}), and back with its values put in range (clampEq).
QJsonObject audioEqToJson(const AudioEq& eq);
AudioEq audioEqFromJson(const QJsonObject& o);
// An EQ template in a bin (p.740, p.745): an Effect named `name` holding the EQ in
// BinItem::composition["eq"]; made by dragging the Audio EQ tool's effect icon to a bin.
BinItem eqTemplateItem(const AudioEq& eq, const QString& name, const QDateTime& when);
// The EQ an EQ template holds; nothing for any other item.
std::optional<AudioEq> eqTemplateOf(const BinItem& item);
// The name a template dragged from the tool takes (ours: the guide does not give one): the
// predefined template's name, else "Audio EQ".
QString eqTemplateName(const AudioEq& eq);

// Set EQ - In/Out (p.739-741) on audio track `track`: `eq` on every whole segment that
// overlaps record frames [from, to); without one, their EQ is removed. "Apply Audio EQ to entire
// segments only" (p.738). Filler and a locked track are left alone. Returns how many segments
// changed.
int setEq(Sequence& sequence, const QString& track, std::int64_t from, std::int64_t to,
          const std::optional<AudioEq>& eq);

} // namespace lumenis::model
