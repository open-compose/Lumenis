#pragma once

#include "lumenis/model/bin.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Colour encoding and colour adapters (Editing Guide p.536-541, p.544; 8.5c, docs/spec/color-adapters.md). A clip's
// Source Settings name its source colour space and the colour transformations applied to it, top to bottom; a video
// segment edited from the clip takes them as its Color Adapter ("A Color Adapter effect is applied when a color
// transformation applied to the clip", p.528). media::applyColorTransformations (lumenis/media/color_transform.h) does
// the transformations.
namespace lumenis::model {

// "Any applied transformations can be seen from the Color Encoding tab or under the Color Transformations bin column"
// (p.536).
inline constexpr char kColorTransformationsHeading[] = "Color Transformations";

// The Source color space menu (p.538): "Unknown" and the spaces Auto knows. Ours: the guide does not list them.
QStringList sourceColorSpaces();

// A clip's Color Encoding tab (p.538-540).
struct ColorEncoding {
    // "If you know the color space of the media you can select it here ... You may leave it as Unknown".
    QString sourceSpace = QStringLiteral("Unknown");
    // The transformation names (media::colorTransformationNames()), "applied cumulatively starting from top to bottom".
    QStringList transformations;
    // "Bypass all color transformations: Ignore all color encoding settings".
    bool bypass = false;

    bool isEmpty() const { return sourceSpace == QLatin1String("Unknown") && transformations.isEmpty() && !bypass; }
    // The transformations that apply: none when bypassed.
    QStringList applied() const { return bypass ? QStringList() : transformations; }
    QJsonObject toJson() const;
    static ColorEncoding fromJson(const QJsonObject& json);
    bool operator==(const ColorEncoding&) const = default;
};

// The clip's encoding, kept in its Source Settings (BinItem::sourceSettings "colorEncoding"); nothing when it has none.
std::optional<ColorEncoding> colorEncodingOf(const BinItem& item);
// Sets (or, with nothing or an empty one, removes) it, and the Color Transformations column with it.
void setColorEncoding(BinItem& item, const std::optional<ColorEncoding>& encoding);
// The column's text: the transformations in order, separated by ", "; "Bypassed: ..." when bypassed (ours).
QString colorTransformationsText(const ColorEncoding& encoding);

// "If you click the Auto button, the application will do the necessary color mapping to go from the specified color space
// to the project's color space" (p.538): the transformations from `sourceSpace` to Rec. 709. Empty for Unknown, Rec. 709
// and a space no transformation leaves (ours: every project is taken as Rec. 709 until 8.5e's colour spaces).
QStringList autoTransformations(const QString& sourceSpace);

// The Color Adapter a video segment from the clip takes: its encoding when transformations apply; nothing otherwise.
std::optional<ColorEncoding> colorAdapterOf(const BinItem& item);

// Clip > Refresh Sequence > Color Adapters (p.478: "clips whose color space has been changed now use the attributes set
// on the master clip"): video segments take their clip's adapter. Returns how many changed.
class Sequence;
int refreshColorAdapters(Sequence& sequence, const std::function<std::optional<BinItem>(const QString& bin, const QString& clip)>& resolve);
// Refresh Sequence > Remove Color Adapters (p.544): "It does not affect adapters on the master clips (as source settings);
// only the sequence is modified." Returns how many segments lost one.
int removeColorAdapters(Sequence& sequence);

} // namespace lumenis::model
