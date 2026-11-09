#pragma once

#include "lumenis/model/stereo_view.h"

#include <QImage>
#include <QSize>

// Stereoscopic pictures (Editing Guide p.66, p.1162-1163, p.1190-1191, p.1226-1227; 8.6d, docs/spec/stereoscopic.md): the
// project's Side by Side and Over / Under modes put both eyes in one picture (8.6d1), and the S3D Views show the two eyes
// together with the S3D Overlay's parallax guides over them (8.6d2).
namespace lumenis::media {

// "Side by Side: Frame compatible format that uses the left and right eye images one beside the other using horizontal
// half res for each eye"; "Over / Under: ... one over the other using vertical half res for each eye" (p.66). The left
// eye goes left or on top (ours: the guide does not say which). The result is `size` (the left eye's size when invalid);
// a null eye leaves its half black.
QImage frameCompatible(const QImage& left, const QImage& right, bool sideBySide, QSize size = {});

// The two eyes as an S3D View shows them (p.1162-1163, p.1190, p.1226-1227), at the left eye's size:
// * Mix, "a 50/50 blend of both the left and right eye images";
// * Difference, gray where the eyes agree and embossed where they differ (ours: middle gray plus half the difference of
//   the eyes' brightness);
// * Frame Compatible, `sideBySide` or over/under as frameCompatible(); Full Frame, both eyes at full size side by side
//   (twice as wide);
// * Mono, one eye (Leading as `leading` says);
// * Anaglyph, each eye's colour channels: Red-Cyan the left eye's red and the right eye's green and blue, Green-Magenta the
//   left's green and the right's red and blue, Amber-Blue the left's red and green and the right's blue;
// * B/W Anaglyph, the same with each eye's brightness;
// * Checkerboard, blocks of `block` pixels from each eye in turn;
// * Compare, the left eye above a diagonal from top right to bottom left placed at `percent` of the way along the other
//   diagonal (ours), the right eye below it.
// Follow Project and Follow S/R are worked out by the caller; here they are Frame Compatible.
QImage stereoView(const QImage& left, const QImage& right, const model::S3DView& view, bool sideBySide,
                  model::Eye leading = model::Eye::Left);

// The S3D Overlay over `image`: green guides for Parallax Near and blue for Parallax Far (p.1163), each a pair of vertical
// lines either side of the centre as far apart as the parallax, in pixels of a raster `rasterWidth` wide scaled to the
// image (ours: the guide does not draw them).
void drawParallaxGuides(QImage& image, model::S3DOverlay overlay, double nearPixels, double farPixels, int rasterWidth);

} // namespace lumenis::media
