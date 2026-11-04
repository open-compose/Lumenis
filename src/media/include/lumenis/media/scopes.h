#pragma once

// Color Correction mode's Waveform and Vectorscope monitors (FX Guide p.686-692; docs/spec/color-correction.md,
// 6.10c1): pictures of a frame's colour information, drawn for a monitor whose Source menu names one of them. The
// frames are the Composer's pictures, full-range R'G'B' that stand for video levels (0-255 as 16-235, as the Levels
// histograms read them); Y'CbCr is Rec. 709.

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>

namespace lumenis::media {

// The Source menu's waveform and vectorscope commands, in the guide's order (p.591, p.687-691).
enum class Scope {
    QuadDisplay,
    RgbHistogram,
    RgbParade,
    Vectorscope,
    YWaveform,
    YWaveformHdr1000,
    YWaveformHdr10000,
    YcWaveform,
    YcbcrHistogram,
    YcbcrParade,
};
QString scopeName(Scope scope); // "Quad Display", "RGB Histogram", ...

// The scope of `frame` at `size`, on black with its graticule and scale. `pal` chooses the YC Waveform's scale:
// "NTSC black is 7.5 IRE ... NTSC white is 100 IRE. PAL black is 0 millivolts (mV), and PAL white is 700 mV" (p.690).
QImage scopeImage(const QImage& frame, Scope scope, const QSize& size, bool pal = false);

// Where the waveforms put a level, for the scale and tests: the row of an 8-bit video level (0 at the bottom edge,
// 255 at the top, with a margin) in a picture `height` high.
double waveformRow(double level, int height);
// Where the vectorscope puts a colour difference pair (Cb and Cr, -0.5 to 0.5): Cb to the right, Cr up, the circle's
// edge at a chroma of 0.5 (ours, so 100% bars fall inside it).
QPointF vectorscopePoint(double cb, double cr, const QSize& size);
// The Y'CbCr of a full-range R'G'B' pixel (each 0 to 1): Y' as an 8-bit video level, Cb and Cr -0.5 to 0.5.
struct YCbCr {
    double y = 16;
    double cb = 0;
    double cr = 0;
};
YCbCr videoYCbCr(double r, double g, double b);
// SMPTE ST 2084 (PQ): the code value (0 to 1) of a luminance in nits, for the HDR waveforms' scales (p.689-690).
double pqCode(double nits);

} // namespace lumenis::media
