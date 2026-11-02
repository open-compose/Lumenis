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
// `origin` is the title point drawn at the picture's top-left: a rolling or crawling title's page or scroll place.
QImage classicTitleImage(const model::ClassicTitle& title, const QSize& size, bool antiAliased = true, const QPointF& origin = {});

// ---- Rolling and crawling titles' pages (6.8d2; p.342-346)
// How many pages the objects reach (at least one): their lowest bottom for a rolling title, their rightmost right for a
// crawling one.
int classicContentPages(const model::ClassicTitle& title);
// "The Title tool inserts a blank page at that page number, and moves all objects after it by one page ... only if the
// left boundary of an object appears on the page whose number you typed" (p.345). Ours: pages 1 to the last, refused
// when an object reaches across the page's start, or past the page limit for `lines`.
bool insertClassicPage(model::ClassicTitle& title, int page, int lines = 1080);
// "If the page is empty, the page is removed and all pages after the removed page are moved by one page" (p.345).
bool removeClassicPage(model::ClassicTitle& title, int page);
// Copy to Page and Move to Page (p.346): the object (copied with a new id) at its place within its page on page `page`,
// "and automatically adds pages if necessary"; refused past the page limit.
bool placeOnClassicPage(model::ClassicTitle& title, const QString& id, int page, bool copy, int lines = 1080);
// "Select Object > Make Crawl ... transform the title into a single-line text object and position it on the lower
// third of the screen" (p.344): its lines joined by spaces, as wide as its text, its top two thirds down the frame.
bool makeClassicCrawl(model::ClassicObject& object);
// For the Text tool: the text index (0 to the text's length) whose cursor is nearest a point (title pixels), the
// cursor before an index, and the boxes over the characters [from, to), in title pixels.
qsizetype classicTextIndexAt(const model::ClassicObject& object, const QPointF& point);
QLineF classicTextCaret(const model::ClassicObject& object, qsizetype index);
QList<QPolygonF> classicTextRange(const model::ClassicObject& object, qsizetype from, qsizetype to);

} // namespace lumenis::media
