#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

// Photoshop's layers (9.2e; docs/spec/graphics-import.md "Multilayer Photoshop files"; Editing Guide p.218-221). Our own
// reader of the layer and mask information section of Photoshop files, as Adobe publishes the format ("Adobe Photoshop
// File Formats Specification"): each layer's name, place, opacity, visibility and pixels, for Sequence of Layers and
// Select Layers. A flattened import needs none of this: FFmpeg reads the file's merged picture. The reader keeps to
// what the guide says import supports (p.220): RGB 8 or 16 bits a channel and grayscale; layer groups are read as their
// layers; masks, styles and blending modes are left out. A small writer of such files is here for tests and fuzz seeds.
namespace lumenis::media {

struct PsdLayer {
    QString name;
    QRect rect;           // in the canvas
    int opacity = 255;    // 0-255
    bool visible = true;  // hidden layers are imported too ("Hidden layers are imported as matte keys")
    bool transparency = false; // has a transparency channel (a background layer has none)
    bool divider = false;      // a group's start or end marker, not a layer of pixels
    QImage image;              // the layer's pixels at rect's size, RGBA (opaque without transparency)
};

struct PsdFile {
    QSize size;  // the canvas
    int depth = 8;
    int mode = 3; // Photoshop's colour mode: 1 grayscale, 3 RGB, 8 duotone
    QList<PsdLayer> layers; // bottom to top, as the file keeps them
};

bool isPsdFile(const QString& path);
// The file's layers; nothing, with the reason in `error`, for what cannot be read (another colour mode or depth, a
// broken file). A flattened file reads with no layers.
std::optional<PsdFile> readPsdData(const QByteArray& data, QString* error = nullptr);
std::optional<PsdFile> readPsdFile(const QString& path, QString* error = nullptr);

// The layers an import offers: those with pixels, bottom to top (group markers and empty layers, such as adjustment
// layers, left out: "Adjustment layers ... No", p.220).
QList<PsdLayer> importableLayers(const PsdFile& file);
// A layer as a canvas-sized RGBA picture: its pixels at their place, transparent elsewhere.
QImage psdLayerPicture(const PsdFile& file, const PsdLayer& layer);

// An 8-bit RGB Photoshop file of the file's layers (a transparency channel for those with `transparency`), raw or with
// `rle` PackBits channels, and its merged picture of the visible layers over white.
QByteArray psdData(const PsdFile& file, bool rle = false);

} // namespace lumenis::media
