#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

// The Source and Record monitors' Display Color Space (Editing Guide p.544; 8.5e1, docs/spec/color-adapters.md):
// "Media Composer supports Rec. 709, BT/Rec. 2020 and DCI-P3 color spaces. The source and record viewers also support
// these colors if your monitor has been calibrated for them. To set the appropriate color space in the computer display:
// Right-click in the desired viewer (source /record), select Display Color Space, and choose from the list."
//
// The picture is in the project's colour space; a monitor's display colour space says what the computer's display is
// calibrated for, so the picture is converted for it: decoded with the project's transfer, its primaries mapped to the
// display's (Bradford-adapted to the display's white), encoded with the display's transfer. Ours, as the guide gives no
// numbers: Rec.709 and Rec.2020 displays are BT.1886 (gamma 2.4) with a D65 white; DCI-P3 is gamma 2.6 with the DCI white
// (0.314, 0.351); PQ and HLG pictures are shown with their reference white (203 cd/m², BT.2408) at the display's white,
// brighter values clipped.
namespace lumenis::media {

// The Display Color Space menu's choices, in the guide's order.
QStringList displayColorSpaces();
// The display colour space a project colour space (project_format.h's colorSpacesFor texts) is shown in without a
// conversion: "Rec.709" for 601 and 709 projects, "Rec.2020" for the 2020 ones (PQ and HLG included), "DCI-P3". Empty
// for a colour space we do not convert (ACES RGB, until its output transforms, 8.5e4).
QString displaySpaceOfProject(const QString& projectColorSpace);
// Whether a picture in the project's colour space needs converting for `display`; false when they match or either is
// unknown.
bool convertsForDisplay(const QString& projectColorSpace, const QString& display);
// One colour (0 to 1, the project's code values) as the display shows it, in the display's code values.
std::array<double, 3> toDisplayColor(const std::array<double, 3>& rgb, const QString& projectColorSpace,
                                     const QString& display);
// The picture converted for the display; returned unchanged when convertsForDisplay is false. Alpha is kept.
QImage toDisplayColorSpace(const QImage& image, const QString& projectColorSpace, const QString& display);

} // namespace lumenis::media
