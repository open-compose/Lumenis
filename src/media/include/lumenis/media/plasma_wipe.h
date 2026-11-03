#pragma once

// PlasmaWipe effects (ROADMAP 6.9e; FX Guide ch.16, p.442-455, p.478; docs/spec/plasma-wipe.md): "two-stream, real-time
// effects that use gradient image bitmaps to create wipes and segment effects". The Level parameter is a threshold:
// "gray tones in the gradient image that are lighter than the threshold point" show the outgoing video, darker ones
// the incoming (p.442). Frame effects (a path with both "border" and "frame") colour the image's grey frame with the
// Border category's colours (p.453).
//
// The gradient images: ours, generated (Avid's 64 presets are Avid's artwork and are not shipped), and the user's own
// raw 8-bit greyscale images found in PlasmaWipes folders laid out as the guide says,
// PlasmaWipes/720x576/<folder>/.../<name>.raw (or 720x486), each folder level becoming part of the Effect Palette
// category's name: "PlasmaWipe <folder> <subfolder>" (p.443, p.450-451).

#include "lumenis/media/compositor.h"
#include "lumenis/model/effect_keyframes.h"

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

namespace lumenis::media {

struct PlasmaWipe {
    QString category; // "PlasmaWipe Basic Horiz"
    QString name;     // the file's name without .raw
    QString path;     // the .raw file; empty for our own generated ones
    // "If both words appear in the path, the software applies the frame effect defaults (and only allows the effect
    // to be used as a segment effect)" (p.453).
    bool frame = false;
};

// The PlasmaWipes folders searched besides our own effects: the application's SupportingFiles/PlasmaWipes, the
// folders in LUMENIS_PLASMAWIPES (the platform's path list), and these. Setting them scans again.
void setPlasmaWipeFolders(const QStringList& folders);
// Every PlasmaWipe effect: ours, then the folders' in path order.
const QList<PlasmaWipe>& plasmaWipes();
// The PlasmaWipe effect an effect names; null when it is not one.
const PlasmaWipe* plasmaWipeOf(const model::Effect& effect);
bool isPlasmaWipe(const model::Effect& effect);
// The effect's gradient image, greyscale, `size` large (scaled from the file's 720 pixels wide; a frame image by the
// nearest pixel, "to avoid blending between the black, white, and gray areas", p.455); null when it cannot be read.
QImage plasmaWipeGradient(const PlasmaWipe& wipe, const QSize& size);
// A raw image's picture: width 720 and the height its size gives (576, 486 or 540 lines); null otherwise.
QImage readPlasmaRaw(const QString& path);

// The effect at threshold `level` (0 to 1): `incoming` (on a segment, the segment's own picture) where the gradient
// is darker, `outgoing` (the tracks below) where lighter, with the Border category's softness and band, and a frame
// effect's frame in its colours.
Picture plasmaWipePicture(const model::Effect& effect, const Picture& outgoing, const Picture& incoming, double level, double time,
                          const model::EffectSpan& span);
// A segment's PlasmaWipe: its Foreground Level (50 until set; a frame's 5) as the threshold.
Picture plasmaWipeSegmentPicture(const model::Effect& effect, const Picture& layer, const Picture& below, double time, const model::EffectSpan& span);

} // namespace lumenis::media
