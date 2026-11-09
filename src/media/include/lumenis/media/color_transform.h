#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>

// The colour transformations of the Source Settings' Color Encoding tab (Editing Guide p.539-540; 8.5c,
// docs/spec/color-adapters.md), applied to a clip's frames before its spatial adapter (MainWindow::adaptedFrame). Values
// are RGB from 0 to 1, full range unless a transformation's name says video levels (16-235 in 8 bits).
namespace lumenis::media {

// One step of a transformation: a curve applied to each channel, a 3 x 3 matrix on the three (row by row), or a mapping
// of the whole colour (a 3D LUT, a CDL's saturation).
struct ColorStep {
    std::function<double(double)> curve;
    std::array<double, 9> matrix{};
    bool isMatrix = false;
    std::function<std::array<double, 3>(const std::array<double, 3>&)> mapping;
    // Whether the step mixes the channels, so that it is worked out per pixel.
    bool mixes() const { return isMatrix || static_cast<bool>(mapping); }
};
using ColorTransformation = QList<ColorStep>;

// The transformations offered, in the guide's order: ours ("Levels scaling (full range to video levels)" ... "Canon C-Log
// to REC709") and those registered. Sony's looks (p.540) come through registerColorTransformation, since their curves
// are not published (ours).
QStringList colorTransformationNames();
// A provider hook for transformations we do not make (Sony's looks; 8.5d's LUT files): `name` as the list shows it.
// Registering a name again replaces it.
void registerColorTransformation(const QString& name, const ColorTransformation& steps);
bool hasColorTransformation(const QString& name);

// The image with `names` applied, top to bottom ("Transformations are applied cumulatively starting from top to bottom",
// p.540). Unknown names are skipped; the image is returned unchanged for none.
QImage applyColorTransformations(const QImage& image, const QStringList& names);
// One colour through them (for tests and the Color Encoding tab's numbers).
std::array<double, 3> transformColor(std::array<double, 3> rgb, const QStringList& names);

// ---- CDLs (p.539-540, p.543-544; 8.5d1)
// "CDL ASC_SOP=(111)...)(111)ASC_SAT=1 - Applies CDL values already associated with the master clips (found in the
// ASC_SOP and ASC_SAT bin columns)" (p.540): the transformation of a clip's columns, named with its values so that it
// stands on its own in a list: "CDL ASC_SOP=(s s s)(o o o)(p p p) ASC_SAT=x". Empty when `sop` does not read.
QString cdlTransformationName(const QString& sop, const QString& sat);
// Whether a name is such a transformation. applyColorTransformations() applies it: out = (in x slope + offset) ^ power,
// clamped to 0-1, then the saturation about Rec. 709 luma (the ASC CDL's formulas).
bool isCdlTransformation(const QString& name);

} // namespace lumenis::media
