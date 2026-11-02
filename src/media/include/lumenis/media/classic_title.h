#pragma once

// The classic Title Tool's picture (ROADMAP 6.8d; FX Guide p.326-364; docs/spec/title-tool.md): a classic title's
// objects drawn back to front, text through Titler+'s layout (media/titler.h) so that formatted characters, kerning and
// leading come out the same, boxes, ovals and lines with QPainter. Coordinates are the model's (classic_title.h): pixels
// of a 1920 x 1080 frame from its top-left corner, scaled to the picture's height.

#include "lumenis/model/classic_title.h"

#include <QImage>
#include <QLineF>
#include <QList>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QTransform>

namespace lumenis::media {

// Title pixels to the picture's (`size`), keeping the frame's height (as titleToPicture does).
QTransform classicToPicture(const QSize& size);
// A text object as a Titler+ object placed where it is drawn (its first line's top at the object's top), for its
// layout: kerning in pixels, Bold and Italic as the font style.
model::TitleObject classicTextObject(const model::ClassicObject& object);
// Where an object is: a text object's left, top and width with the height of its lines; a box's or oval's bounds; a
// line's bounds (normalized).
QRectF classicObjectBounds(const model::ClassicObject& object);
// The title alone over transparency, `size` large. "Media Composer creates text and objects in titles anti-aliased with
// an 8-bit alpha channel" (p.333); without `antiAliased` (Preview off) the edges are hard.
QImage classicTitleImage(const model::ClassicTitle& title, const QSize& size, bool antiAliased = true);
// For the Text tool: the text index (0 to the text's length) whose cursor is nearest a point (title pixels), the
// cursor before an index, and the boxes over the characters [from, to), in title pixels.
qsizetype classicTextIndexAt(const model::ClassicObject& object, const QPointF& point);
QLineF classicTextCaret(const model::ClassicObject& object, qsizetype index);
QList<QPolygonF> classicTextRange(const model::ClassicObject& object, qsizetype from, qsizetype to);

} // namespace lumenis::media
