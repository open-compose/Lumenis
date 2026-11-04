#pragma once

// The Color Match control (FX Guide p.624-631; docs/spec/color-correction.md, 6.10c3a): "Media Composer replaces the
// input color value with the output color value and adjusts all the other color values in the image proportionally.
// Media Composer also automatically adjusts the other controls in the group to reflect the change" (p.624). Each group
// has its own Match Types (p.626-629); NaturalMatch in the Curves and Secondary groups "adopts the new hue value,
// preserves the original luminance value, and adjusts the saturation value in relation to the other values" (p.629).

#include "lumenis/model/color_correction.h"

#include <QColor>
#include <QStringList>

namespace lumenis::media {

// Where a Color Match control is: the HSL group's Controls tab (one of its ranges), its Hue Offsets tab, the Channels,
// Levels, Curves or Secondary group.
enum class MatchGroup { HslControls, HslHueOffsets, Channels, Levels, Curves, Secondary };

// The Match Type menu's entries for a group, in the guide's order (p.627-629), as keys: "hsl", "hue", "saturation",
// "luminance", "hs", "hl", "sl" (Controls); "master", "highlights", "midtones", "shadows" (Hue Offsets); "master",
// "rgb", "red", "green", "blue", "rg", "rb", "gb" (Channels and Curves); "black", "gray", "white" (Levels); "hue",
// "saturation", "hs" (Secondary). NaturalMatch is a switch of its own, not a key.
QStringList matchTypes(MatchGroup group);
// The menu's text for a key ("H+S+L", "Black Point", ...); "(Nat)" is added when NaturalMatch is on (p.627).
QString matchTypeText(const QString& key, bool natural = false);
// Whether the group has NaturalMatch (the Curves and Secondary groups, p.627, p.629).
bool hasNaturalMatch(MatchGroup group);

struct ColorMatch {
    MatchGroup group = MatchGroup::HslControls;
    QString type;            // a key of matchTypes(group)
    QString range = "master"; // HslControls: the range tab the control is in ("master", "highlights", ...)
    QString vector;          // Secondary: the selected vector
    bool natural = false;    // NaturalMatch (Curves and Secondary)
};

// The Match Color button (p.626): changes `correction` so that `input` (a colour picked from the picture through the
// correction) comes out as `output`, by the Match Type's components. False when nothing changes.
bool matchColor(model::ColorCorrection& correction, const QColor& input, const QColor& output, const ColorMatch& match);

// The output colour NaturalMatch aims at: the output's hue, the input's luma, the output's saturation scaled by the
// input's brightness against the output's (ours).
QColor naturalMatchColor(const QColor& input, const QColor& output);

} // namespace lumenis::media
