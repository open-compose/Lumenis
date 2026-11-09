#pragma once

#include "lumenis/model/mask_margins.h"

#include <QImage>
#include <QPainter>
#include <QRectF>

// Drawing the project's mask margins (Editing Guide p.550-552; 8.5e3): over a monitor's picture as its Target Mask says,
// and burned into exported frames ("You can export your sequence with mask margins burned in", p.900).
namespace lumenis::media {

// The masked region of `picture` (outside model::maskedArea) painted as `mask` says: nothing for No Mask or no margins,
// half white or half black for Mix to White and Mix to Black ("a translucent white ... a translucent black", p.1161; the
// half is ours), black for Black Mask.
void paintTargetMask(QPainter& painter, const QRectF& picture, const model::MaskMargins& margins, model::TargetMask mask);
// The frame with its masked region black ("The sequence is saved with the mask region burned in", p.900); the frame as
// it is without margins.
QImage burnMaskMargins(const QImage& frame, const model::MaskMargins& margins);

} // namespace lumenis::media
