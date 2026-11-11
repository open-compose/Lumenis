#pragma once

#include "lumenis/core/rational.h"

#include <QImage>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>

// Graphics and movies with an alpha channel on import (9.2b; docs/spec/graphics-import.md; Editing Guide p.217-218,
// p.1196, p.1231): which files carry alpha, the pictures of PICT files (which FFmpeg does not read), and the fill and
// matte a Matte Key clip is made of (lumenis/model/matte_key_clip.h).
namespace lumenis::media {

// The Import setting's Alpha Channel option (p.1196).
enum class AlphaHandling {
    // "Invert on import (white = opaque)": the file's alpha is white where the picture is opaque, as graphics
    // applications make it (p.989: "You need to invert the alpha channel of graphics on import").
    Invert,
    // "Do not invert (black = opaque)": the file's alpha is already as Avid applications keep it.
    NoInvert,
    // "Import an image that contains alpha channel transparency information as one opaque graphic."
    Ignore,
};
// From the setting's value ("invert", "noInvert", "ignore"); Ignore for anything else.
AlphaHandling alphaHandlingOf(const QString& value);

// PICT files (.pic, .pct, .pict; p.1231), read by media/pict.h.
bool isPictFile(const QString& path);

// A picture source with an alpha channel: its frames (RGBA, not premultiplied) and how many; a still has one.
struct AlphaSource {
    std::int64_t frames = 0;
    Rational rate{25, 1};
    QSize size;
    std::function<QImage(std::int64_t frame)> frame;
};
// The file's alpha source: a PICT picture with alpha, or the first video stream of a file FFmpeg reads whose pixel
// format has alpha (pixelFormatHasAlpha). Nothing for files without alpha.
std::optional<AlphaSource> alphaSourceOf(const QString& path);

// The fill of a picture with alpha: its colours, opaque. With `dilate` (the Import setting's Dilate Fill, p.1196: "bleeds
// the fill just a bit along the edges where transparent alpha meets non-transparent alpha"), each transparent pixel that
// touches a pixel with some opacity takes the mean colour of those neighbours, so no black seeps into the fill's edges
// when it is keyed.
QImage fillPicture(const QImage& rgba, bool dilate);
// The matte: the alpha as grey, white where the picture is opaque (the compositor's Matte Key convention). Invert takes
// the file's alpha as white = opaque, NoInvert as black = opaque.
QImage mattePicture(const QImage& rgba, AlphaHandling handling);

} // namespace lumenis::media
