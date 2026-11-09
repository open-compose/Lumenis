#pragma once

#include "lumenis/model/bin.h"

#include <QJsonObject>
#include <QString>

#include <optional>

// Stereoscopic clips and the project's Stereoscopic option (Editing Guide p.65-66, p.213, p.243; 8.6d1,
// docs/spec/stereoscopic.md). A Stereo Master Clip is "A single clip generated from a left and right eye pair of master
// clips" (p.243). Ours: it is a master clip whose link is its leading eye's media, so everything that plays a clip plays
// it as a 2D clip of that eye, with both eyes' links kept in its Source Settings under "stereo". How the project shows
// the eyes (Left Eye Only, Side by Side ...) is the project's Stereoscopic option; MainWindow::adaptedFrame applies it
// through eyeClip() and media::frameCompatible().
namespace lumenis::model {

// The bin column with the leading eye ("The leading eye image is defined by the S3D Leading Eye clip attribute", p.66).
inline constexpr char kLeadingEyeHeading[] = "S3D Leading Eye";

enum class Eye { Left, Right };

// What a Stereo Master Clip keeps of its two eyes: their master clips' links and names, and its leading eye.
struct StereoClip {
    QJsonObject leftLink;
    QJsonObject rightLink;
    QString leftName;
    QString rightName;
    Eye leadingEye = Eye::Left;
    QJsonObject toJson() const;
    static std::optional<StereoClip> fromJson(const QJsonObject& o);
};

// The clip's stereo eyes; nothing for a 2D clip.
std::optional<StereoClip> stereoClipOf(const BinItem& item);
inline bool isStereoClip(const BinItem& item) { return stereoClipOf(item).has_value(); }

// A Stereo Master Clip from a left and a right eye master clip with picture (p.213: "After the master clips are created,
// they need to be converted to stereoscopic clips"). Ours: named "<left name> S3D", the left eye's columns, length the
// shorter eye's, leading eye Left. Nothing, with the reason, when either is not a master clip with media or their rates
// differ.
std::optional<BinItem> makeStereoClip(const BinItem& left, const BinItem& right, QString* error = nullptr);

// Sets the S3D Leading Eye: the clip's link becomes that eye's, so 2D use plays it.
bool setLeadingEye(BinItem& item, Eye eye);

// The project's Stereoscopic option (p.65-66), from its text in stereoscopicModes() (project_format.h).
enum class StereoMode { Off, LeadingEye, LeftEyeOnly, RightEyeOnly, SideBySide, OverUnder, Full };
StereoMode stereoModeOf(const QString& text);
// Side by Side and Over / Under are "frame compatible": both eyes in one picture.
inline bool isFrameCompatible(StereoMode m) { return m == StereoMode::SideBySide || m == StereoMode::OverUnder; }

// The clip as one eye plays it: a Stereo Master Clip with that eye's link; any other clip as it is ("If you have any
// standard (non-stereo) material in the sequence, it will use the same image in both the left and right frames", p.66).
BinItem eyeClip(const BinItem& item, Eye eye);
// The eye a one-picture mode shows of a stereo clip: Left/Right Eye Only their eye, the others (Off, Leading Eye, Full)
// the leading eye ("Any stereo material in the sequence is treated as a standard format, and only the leading eye image
// is used", p.66). Full: the leading eye too where no S3D View shows both (MainWindow::stereoPicture, 8.6d2).
Eye shownEye(StereoMode mode, const BinItem& item);

} // namespace lumenis::model
