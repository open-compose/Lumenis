#pragma once

// Avid Titler+'s picture (ROADMAP 6.8a; FX Guide p.302-324; docs/spec/titler.md). The title itself is the model's
// (lumenis/model/titler.h); this draws it.

#include "lumenis/media/compositor.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/titler.h"

#include <QImage>
#include <QLineF>
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
// Where a Roll or Crawl has moved the title's objects at its Effect Position (title pixels; nothing when it is off,
// 6.8c3). titleImage, titleObjectOutline and titleObjectPlacement include it.
QPointF titleScroll(const model::Title& title);
// An object's own shape's bounds in its own coordinates (a group's: its children's, placed), for anchor presets.
QRectF titleObjectBounds(const model::TitleObject& object);
// Where object `id`'s own coordinates go in title pixels (its Transform and its groups'); nothing for no such object.
std::optional<QTransform> titleObjectPlacement(const model::Title& title, const QString& id);
// A text object's cursor before text index `index` (0 to the text's length), a line from the top of its line to
// the bottom, and the boxes over the characters [from, to), both in the object's own coordinates (6.8b).
QLineF titleTextCaret(const model::TitleObject& object, qsizetype index);
// The text index (0 to the text's length) whose cursor is nearest a point in the object's own coordinates: where a
// click puts the cursor and a drag highlights to (p.304, 6.8c2).
qsizetype titleTextIndexAt(const model::TitleObject& object, const QPointF& point);
QList<QPolygonF> titleTextRange(const model::TitleObject& object, qsizetype from, qsizetype to);
// A Titler+ template's still in a bin's Frame and Script views: "Avid Titler+ templates are displayed as a still image
// of the template's first frame, with titles displayed over a black background" (p.319; 6.8c4a).
QImage titleFirstFrame(const model::Effect& effect, std::int64_t length, const QSize& size);
// An Avid Titler+ effect's segment picture: its title at `time` of the effect (its keyframes applied, 6.8c1) over
// `layer`.
Picture titlePicture(const model::Effect& effect, const Picture& layer, double time = 0.0, const model::EffectSpan& span = {});

} // namespace lumenis::media
