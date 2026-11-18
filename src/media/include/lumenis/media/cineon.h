#pragma once

#include <QByteArray>
#include <QImage>

#include <optional>

// Kodak Cineon files (the Graphic export's Cineon format, Editing Guide p.1186; ROADMAP 11.6e). Our own writer and
// reader of Kodak's published "Cineon Image File Format" version 4.5: a 1024-byte generic header (file information
// then image information) and a 1024-byte motion-picture film header, then the pixels as 10-bit red, green and blue
// printing-density codes packed three to a big-endian 32-bit word, left-justified (packing 5).
//
// The codes are logarithmic, as film densities are. Ours: the picture's code values are taken as Rec. 709 video, made
// linear through the inverse of its transfer function and written with the usual conversion of 300 codes a decade from
// black at 95 to white at 685 (0.002 density a code, a film gamma of 0.6). The reader turns them back.
namespace lumenis::media {

// The file of a picture (8-bit RGB is enough; alpha is dropped).
QByteArray cineonData(const QImage& picture);
// A file's picture as 8-bit RGB; nothing for what is not a Cineon file this writes (another packing or channel count).
std::optional<QImage> readCineon(const QByteArray& data);

} // namespace lumenis::media
