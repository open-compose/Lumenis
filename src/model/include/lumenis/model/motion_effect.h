#pragma once

#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QString>

#include <cstdint>
#include <optional>

// Traditional (source-side) motion effects (FX Guide p.165-173, p.476-477, p.561-563; ROADMAP 6.5a;
// docs/spec/motion-effects.md): Freeze Frame, Variable Speed and Strobe Motion clips. "You create a
// traditional Freeze Frame, Variable Speed, or Strobe Motion effect by generating a new clip with
// specific motion characteristics from an original clip ... You then edit the motion effect clip into
// your sequence" (p.165). Ours: the clip is a bin item of kind MotionEffect that refers to its source
// clip and says which source frame each of its frames shows; it has no media of its own until rendered
// (then its link is the rendered media, frame for frame). The monitors and the compositor follow the
// reference (MainWindow::frameAt, fillComposition). Motion effect clips are picture only.
namespace lumenis::model {

// "Render 2-Field Motion Effect Using" (p.563) and Two Field Freeze Frames (p.166): kept with the clip.
// Ours: our media is progressive, so all four show whole frames; the choice matters once fields do.
enum class FieldMotion { DuplicatedField, BothFields, InterpolatedField, VtrStyle };
QString fieldMotionName(FieldMotion f);       // "duplicated", "both", "interpolated", "vtr"
FieldMotion fieldMotionFromName(const QString& name);

// What a motion effect clip shows.
struct MotionEffect {
    QString sourceBin;  // the clip it was made from
    QString sourceClip;
    std::int64_t sourceStart = 0; // the source frame its first frame shows
    // Source frames per frame: 1 normal, 0.5 half speed, 0 a freeze frame, negative reverse.
    double speed = 1.0;
    // Strobe Motion's "Update every n frames" (p.563): each nth frame is held for n frames; 1 for none.
    int strobe = 1;
    FieldMotion field = FieldMotion::DuplicatedField;

    // The source frame its frame `frame` shows.
    std::int64_t sourceFrame(std::int64_t frame) const;
    bool isFreeze() const { return speed == 0.0; }
    // "Freeze Frame: Clips play in real time ... Variable Speed, forward motion: Clips play in real
    // time ... Variable Speed, reverse motion [and] Strobe Motion ... Clips cannot play in real time"
    // (p.172): the green or blue dot.
    bool isRealTime() const { return strobe <= 1 && speed >= 0.0; }
    bool operator==(const MotionEffect&) const = default;
};

// The motion effect a MotionEffect clip holds (in its `composition` as "motion"); nothing for others.
std::optional<MotionEffect> motionEffectOf(const BinItem& item);
void setMotionEffect(BinItem& item, const MotionEffect& motion);

// The speed as the Motion Effect dialog shows it (p.563): frames per second at `rate` and percent.
double motionFps(double speed, const Rational& rate);

// A Freeze Frame clip of `source`'s frame `frame` (Composer > Freeze Frame, p.166-167), `duration`
// frames long: "A new clip ... The new clip has the original clip name followed by the letters FF".
BinItem freezeFrameClip(const BinItem& source, const QString& sourceBin, std::int64_t frame, std::int64_t duration,
                        FieldMotion field, const QDateTime& now);

// A Variable Speed and/or Strobe Motion clip of `source`'s frames [in, out] (inclusive; p.168-172) at
// `speed` (negative: reverse, from `out` back to `in`) with `strobe`. It lasts the frames played at
// that speed (at least one). Its name: "the original clip name followed by the frame rate in
// parentheses" (p.170), "followed by the word Strobe and the number of update frames in parentheses"
// (p.172), and both together as "Turbocharge (15FPS) (Strobe: 5)" (p.178).
BinItem motionEffectClip(const BinItem& source, const QString& sourceBin, std::int64_t in, std::int64_t out, double speed,
                         int strobe, FieldMotion field, const QDateTime& now);
// The length of such a clip.
std::int64_t motionEffectLength(std::int64_t in, std::int64_t out, double speed);

} // namespace lumenis::model
