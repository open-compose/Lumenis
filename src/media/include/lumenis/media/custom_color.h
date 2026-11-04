#pragma once

// Custom colours saved to bins (FX Guide p.630-631, p.608; docs/spec/color-correction.md, 6.10c3b): "You can save a color
// that you select in the Color Match control as an item in a bin ... You can then load that color back into the Color
// Match control". An Effect item holding the colour under "customColor" ("#rrggbb"); its bin icon is a swatch of it.

#include "lumenis/model/bin.h"

#include <QColor>
#include <QDateTime>

#include <optional>

namespace lumenis::media {

// The Correction setting's Saved Color Labels (p.608): how a saved colour is named. The setting's keys are "none",
// "rgb", "name" and "nameRgb" (the default).
enum class SavedColorLabels { None, Rgb, Name, NameAndRgb };
SavedColorLabels savedColorLabelsFromKey(const QString& key);

// "None: Media Composer does not supply a name. RGB: ... the 8-bit values for the red, green, and blue components as
// the name. Name: ... the name from the standard HTML color scheme that most closely matches the color ... Name and
// RGB: ... both" (p.608). Ours: the values on the video scale as the Color Match swatches show them ("R:61 G:62 B:66"),
// the HTML (CSS) colour nearest in R'G'B' with its first letter capitalised, and the two joined by a space.
QString customColorName(const QColor& color, SavedColorLabels labels);
QString nearestHtmlColorName(const QColor& color);

model::BinItem customColorItem(const QColor& color, const QString& name, const QDateTime& when);
// The colour a custom colour item holds; nothing for other items.
std::optional<QColor> customColorOf(const model::BinItem& item);

} // namespace lumenis::media
