#pragma once

#include "lumenis/media/color_transform.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

// External LUT files (Editing Guide p.541-542; 8.5d1, docs/spec/color-adapters.md): "1D LUT: A 1-dimensional lookup table
// maps each input channel value to an output channel value on a per-channel basis ... 3D LUT: A 3-dimensional lookup
// table maps any given color value (R,G,B) to an output color value (R,G,B)". The guide lists the products by their
// file's first line (p.541); we read the IRIDAS/Mistika ".cube"/".itx" text (LUT_1D_SIZE, LUT_3D_SIZE), Kodak KDM and
// Lustre ".3dl" grids and Autodesk "LUT:" tables. Installed LUTs are registered as colour transformations named
// "External <file name>" ("The installed LUT(s) will be listed at the bottom, prefixed with the word External", p.542).
namespace lumenis::media {

// A LUT as read: a 1D table (each entry an output RGB), a 3D cube (red changing fastest), or both (the 1D first, as a
// shaper). Values are 0 to 1 on input and output, within the domain.
struct LutFile {
    QString title;
    QList<std::array<double, 3>> table1D;
    int size3D = 0;
    QList<std::array<double, 3>> table3D; // index r + g * size + b * size * size
    std::array<double, 3> domainMin{0.0, 0.0, 0.0};
    std::array<double, 3> domainMax{1.0, 1.0, 1.0};

    bool isEmpty() const { return table1D.isEmpty() && table3D.isEmpty(); }
    // One colour through it: the 1D table interpolated linearly, the cube trilinearly.
    std::array<double, 3> apply(const std::array<double, 3>& rgb) const;
};

// The product a file's first line names (p.541: "Other product LUTs may be supported but the first line entry of the file
// must appear as listed"): "IRIDAS 1D", "IRIDAS 3D" (also Mistika 3D), "Kodak KDM", "Autodesk", "Avid DS", "LUTher",
// "Nucoda"; empty for none.
QString lutProduct(const QString& firstLine);
// Reads a LUT file; nothing, with `error` saying why, for one we cannot read. Avid DS, LUTher and Nucoda files are
// recognised but not read (ours: their layouts are not published).
std::optional<LutFile> readLutFile(const QString& path, QString* error = nullptr);
// The same from a file's bytes; `suffix` (".3dl" ...) tells a Lustre grid without its KDM first line.
std::optional<LutFile> readLutData(const QByteArray& data, const QString& suffix, QString* error = nullptr);
// The LUT as a colour transformation step.
ColorTransformation lutTransformation(const LutFile& lut);

// The name an installed LUT has in the transformation list: "External " and the file's name without its suffix.
QString externalLutName(const QString& path);
// The LUT files in a folder (by suffix: .cube, .itx, .3dl, .lut, .txt), sorted by name.
QStringList lutFilesIn(const QString& folder);
// Registers every LUT file of the folders (media::registerColorTransformation). Returns the names registered; files that
// cannot be read are left out, with why in `failures` ("<file>: <why>").
QStringList registerLutFolders(const QStringList& folders, QStringList* failures = nullptr);
// The project's LUTs folder ("Imported LUTs are stored in the project folder in which the LUT was imported", p.542) and
// the folder for every project ("Library/Application Support/Avid/ColorManagement/LUTs", ours: the application data's
// "ColorManagement/LUTs"; LUMENIS_SHARED_LUTS names another, and with LUMENIS_NO_SYSTEM_DRIVES=1 there is none).
QString projectLutFolder(const QString& projectFolder);
QString sharedLutFolder();
// "Browse for your file, select it and click Open to install it" (p.541): the file copied into `folder` (made if missing)
// and registered. Returns its name in the list; empty, with `error`, when it cannot be read or copied.
QString installLut(const QString& file, const QString& folder, QString* error = nullptr);

} // namespace lumenis::media
