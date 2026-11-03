#pragma once

// The classic Title Tool's picture (ROADMAP 6.8d; FX Guide p.326-364; docs/spec/title-tool.md): a classic title's
// objects drawn back to front, text through Titler+'s layout (media/titler.h) so that formatted characters, kerning and
// leading come out the same, boxes, ovals and lines with QPainter. Coordinates are the model's (classic_title.h): pixels
// of a 1920 x 1080 frame from its top-left corner, scaled to the picture's height.

#include "lumenis/media/managed.h"
#include "lumenis/model/classic_title.h"

#include <QImage>
#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QPointF>
#include <QStringList>
#include <QTransform>

#include <optional>
#include <utility>

namespace lumenis::media {

// Title pixels to the picture's (`size`), keeping the frame's height (as titleToPicture does).
QTransform classicToPicture(const QSize& size);
// A text object as a Titler+ object placed where it is drawn (its first line's top at the object's top), for its
// layout: kerning in pixels, Bold and Italic as the font style.
model::TitleObject classicTextObject(const model::ClassicObject& object);
// Where an object is: a text object's left, top and width with the height of its lines; a box's or oval's bounds; a
// line's bounds (normalized).
QRectF classicObjectBounds(const model::ClassicObject& object);
// An object's own shape in title pixels (6.8d3): a text object's characters, a box with its rounded corners, an oval,
// a line as wide as its width with its arrowheads. Empty for empty text.
QPainterPath classicObjectShape(const model::ClassicObject& object);
// The border round a text, box or oval object (p.347): its outline stroked twice the border width wide (the fill
// drawn over its inner half); empty without a border and for lines.
QPainterPath classicBorderShape(const model::ClassicObject& object);
// The title alone over transparency, `size` large. "Media Composer creates text and objects in titles anti-aliased with
// an 8-bit alpha channel" (p.333); without `antiAliased` (Preview off) the edges are hard. Each object is drawn with its
// shadow under its border under its fill, each coat with its colour and transparency blends (6.8d3).
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
// ---- Alignment and distributing (6.8d3b; p.356-357). Each group moves as one; locked objects stay.
// The Alignment menu's commands: to the frame ("The Title tool aligns the objects within the safe title area", `area`)
// or to each other ("uses the last object you selected as the reference object to which it aligns the others").
enum class ClassicAlign { FrameLeft, FrameRight, FrameTop, FrameBottom, FrameCenterH, FrameCenterV, ObjectsLeft, ObjectsRight, ObjectsTop, ObjectsBottom };
// "Distribute Left to Right ... Top to Bottom ... evenly" (ours: the first object's edge at the area's, the last's at
// its other edge, equal gaps between), and "Distribute First to Last ... evenly between the position of the first
// object you select and the position of the last" (their centres evenly along the way, in the order selected); "at
// least three objects".
enum class ClassicDistribute { LeftToRight, TopToBottom, FirstToLast };
// The objects `ids` (in the order selected) moved; false when nothing moves.
bool alignClassicObjects(model::ClassicTitle& title, const QStringList& ids, ClassicAlign align, const QRectF& area);
bool distributeClassicObjects(model::ClassicTitle& title, const QStringList& ids, ClassicDistribute how, const QRectF& area);

// ---- Title Effect clips' media (6.8d4a; p.357-358, p.365; model/title_clip.h)
// Page `page` (from 1; a still title's one) of the title as its precomputes hold it, `size` large: the fill, its colours
// over black (unpremultiplied, opaque), and the matte, its alpha as grey.
QImage classicTitleFill(const model::ClassicTitle& title, const QSize& size, int page = 1);
QImage classicTitleMatte(const model::ClassicTitle& title, const QSize& size, int page = 1);
// "Using Filtering for HD Titles" (p.376): the console's HDTitleFilter toggles a filter for titles made from then on
// ("The command does not affect media already created"); off at start. With it on, the precomputes of titles in an HD
// raster (720 lines or more) are softened vertically ([1 2 1] / 4 over lines, ours), which keeps a downconverted SD
// signal from overshooting.
void setHdTitleFilter(bool on);
bool hdTitleFilter();
// The filter on one image.
QImage hdTitleFiltered(const QImage& image);
// Saving a title (p.357-358): its precomputes written as managed media in the creation's resolution on its video drive,
// one frame per page at `rate`: RenderedEffect master clips "<name> Fill" and "<name> Matte" (ours). Nothing, with the
// reason in `error`, on failure (the files made are removed).
std::optional<std::pair<model::BinItem, model::BinItem>> renderClassicTitleMedia(const model::ClassicTitle& title, const QString& name,
                                                                                 const Rational& rate, const MediaCreation& creation,
                                                                                 QString* error = nullptr);
// Where a Title Effect segment shows the title's pages at `progress` (0 at its first frame, 1 at its last): the title
// point at the frame's top-left. A still title stays at its origin; "rolling titles scroll from the bottom to the top of
// the screen", "crawling titles scroll from right to left" (p.488): ours, from just below (right of) the frame to just
// past its top (left), evenly.
QPointF classicTitleScroll(const model::ClassicTitle& title, double progress);

// ---- Promote Titles > Avid Titler+ (6.8d4c; p.326): "Titles created ... using the classic Avid Title Tool can be
// promoted for use in Avid Titler+". The title as a Titler+ title: text objects with their font, characters and
// placing, boxes as rectangles, ovals as ellipses, lines as thin rectangles turned to their angle (ours); fills (a
// colour blend as the four-corner gradient it makes over the object's bounds), borders as Outer outlines, shadows;
// a colour background as Titler+'s Background; a rolling or crawling title as Roll or Crawl Left. What Titler+ has no
// place for is left out (ours): rounded corners, arrowheads, transparency blends (their mean is kept), depth shadows
// (as drop shadows), groups and locks.
model::Title promoteClassicTitle(const model::ClassicTitle& title);

// For the Text tool: the text index (0 to the text's length) whose cursor is nearest a point (title pixels), the
// cursor before an index, and the boxes over the characters [from, to), in title pixels.
qsizetype classicTextIndexAt(const model::ClassicObject& object, const QPointF& point);
QLineF classicTextCaret(const model::ClassicObject& object, qsizetype index);
QList<QPolygonF> classicTextRange(const model::ClassicObject& object, qsizetype from, qsizetype to);

} // namespace lumenis::media
