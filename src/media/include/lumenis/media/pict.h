#pragma once

// PICT files (ROADMAP 6.9c2): Export Matte PICT writes "a high-contrast 32-bit PICT file that includes an alpha channel"
// (FX Guide p.418). Our own reader and writer of the Macintosh picture format as Apple published it (Inside Macintosh:
// Imaging With QuickDraw, 1994, appendix A "Picture Opcodes"): an extended version 2 picture holding one DirectBitsRect
// of 32-bit pixels. The writer stores the alpha, red, green and blue planes of each row PackBits-compressed (packType 4,
// four components); the reader takes such pictures and the unpacked ones (packType 1 and 2), and refuses others.

#include <QImage>
#include <QString>

namespace lumenis::media {

// Writes `image` as a PICT file at `path` (its alpha kept when `alpha`); false when the file cannot be written.
bool writePict(const QImage& image, const QString& path, bool alpha = true);
// The picture of a PICT file (ARGB32, opaque when the file has no alpha); a null image when it cannot be read.
QImage readPict(const QString& path);
// The same for the file's bytes (with or without the 512-byte file header).
QImage readPictData(const QByteArray& data);
QByteArray pictData(const QImage& image, bool alpha = true);

} // namespace lumenis::media
