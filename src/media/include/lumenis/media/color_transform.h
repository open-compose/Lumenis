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

// One step of a transformation: a curve applied to each channel, or a 3 x 3 matrix on the three (row by row).
struct ColorStep {
    std::function<double(double)> curve;
    std::array<double, 9> matrix{};
    bool isMatrix = false;
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

} // namespace lumenis::media
