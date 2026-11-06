// Test patterns (Phase 7.3b; docs/spec/video-output.md): the Video Output tool's Test Patterns menu, "SMPTE_Bars
// (SMPTE standard bars), ColorBars (full-field bars at 75% level), or ColorBars_100 (full-field color bars at 100%
// level)" (Editing Guide p.915), drawn at any raster in 8-bit full-range RGB.
#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

namespace lumenis::media {

// The patterns' names, as the menu lists them.
QStringList testPatternNames();
// The pattern at `size`; a null image for an unknown name.
QImage testPattern(const QString& name, QSize size);

} // namespace lumenis::media
