#pragma once

#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <optional>

// Mask margins and Target Mask (Editing Guide p.550-552, p.1161, p.1191; 8.5e3, docs/spec/color-adapters.md "Mask
// margins"): "You can specify mask margins on the output frame to view a master with a different aspect ratio than the
// project setting ... this selected aspect ratio will mask out (with a gray or black background) any area of the image
// that is not inside the specified rectangle. This is for viewing purposes only" (p.550). The margins are the project's
// (the Format tab's Mask Margins button and the Target Settings dialog, p.551); how a monitor shows them is its Target
// Mask (p.552).
namespace lumenis::model {

// The project's mask margins: the percentage of the picture's height masked at the top and the bottom and of its width at
// the left and the right ("set the margins manually by selecting the appropriate percentage of the image to be occluded",
// p.551), and the preset they came from ("Custom..." for ones set by hand, empty for none).
struct MaskMargins {
    QString preset;
    double top = 0.0;
    double bottom = 0.0;
    double left = 0.0;
    double right = 0.0;

    bool isEmpty() const { return top <= 0.0 && bottom <= 0.0 && left <= 0.0 && right <= 0.0; }
    QJsonObject toJson() const;
    static MaskMargins fromJson(const QJsonObject& o);
    bool operator==(const MaskMargins&) const = default;
};

// The "Mask margins preset" menu as the guide's picture shows it (p.551), "Custom..." last.
QStringList maskPresets();
inline QString customMaskPreset() { return QStringLiteral("Custom..."); }
// A preset's width over height ("16:9" 16/9, "1.85:1" 1.85); nothing for Custom... or unknown text.
std::optional<double> maskPresetRatio(const QString& preset);
// The margins a preset leaves on a picture whose width over height is `pictureRatio`: the picture letterboxed (top and
// bottom) for a wider preset, pillarboxed (left and right) for a narrower one, centred; none for the picture's own shape.
// Custom... and unknown presets give no margins.
MaskMargins maskForPreset(const QString& preset, double pictureRatio);
// The part of `picture` inside the margins (the "active area", p.894).
QRectF maskedArea(const QRectF& picture, const MaskMargins& margins);

// How a monitor shows the masked region (p.552, p.1161): not at all, mixed with a translucent white or black "so that you
// can view the output frame in context of the full image", or blacked out "to display the image as it would appear when
// output".
enum class TargetMask { None, MixToWhite, MixToBlack, BlackMask };
// The settings' values ("none", "white", "black", "blackMask") and back; unknown text is None.
QString targetMaskValue(TargetMask mask);
TargetMask targetMaskFrom(const QString& value);
// The menu's texts, in TargetMask order: No Mask, Mix to White, Mix to Black, Black Mask (p.552).
QStringList targetMaskLabels();

} // namespace lumenis::model
