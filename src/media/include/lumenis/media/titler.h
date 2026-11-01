#pragma once

// Avid Titler+'s picture (ROADMAP 6.8a; FX Guide p.302-324; docs/spec/titler.md). The title itself is the model's
// (lumenis/model/titler.h); this draws it.

#include "lumenis/media/compositor.h"
#include "lumenis/model/titler.h"

#include <QImage>
#include <QPolygonF>
#include <QTransform>

#include <optional>

namespace lumenis::media {

// Title pixels (a 1080-line frame with 0, 0 at its centre) to a picture of `size`: scaled by its height, centred.
QTransform titleToPicture(const QSize& size);
// The title drawn at `size`, transparent where it has nothing (premultiplied ARGB).
QImage titleImage(const model::Title& title, const QSize& size);
// Where object `id` is drawn: its shape's bounds placed by its Transform (and its groups'), in title pixels; for the
// Record monitor's bounding boxes and clicks (6.8b). Nothing for no such object.
std::optional<QPolygonF> titleObjectOutline(const model::Title& title, const QString& id);
// An Avid Titler+ effect's segment picture: its title over `layer`.
Picture titlePicture(const model::Effect& effect, const Picture& layer);

} // namespace lumenis::media
