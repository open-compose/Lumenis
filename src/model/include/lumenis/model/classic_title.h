#pragma once

// The classic Title Tool's titles (ROADMAP 6.8d; FX Guide p.326-364; docs/spec/title-tool.md): text, boxes, ovals and
// lines over a video or colour background. The Title Tool (src/ui/title_tool.*) edits one; media/classic_title.h draws
// it; saving it as a title clip with fill and matte media comes with 6.8d4.
//
// Coordinates are pixels of a 1920 x 1080 frame from its top-left corner, y down (ours: the guide's titles are drawn in
// the project's frame). Font sizes are points, counted as pixels of that frame (ours). Colours are "#rrggbb" strings,
// as in titler.h, because the model has no QtGui.

#include "lumenis/model/titler.h"

#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>

#include <optional>

namespace lumenis::model {

// "Let you draw boxes, circles, ovals, and lines" (p.330); text from the Text tool (p.335).
enum class ClassicKind { Text, Box, Oval, Line };
// "The text justification buttons" (p.338).
enum class ClassicJustify { Left, Center, Right };

// One object of a classic title.
struct ClassicObject {
    QString id; // unique within the title
    ClassicKind kind = ClassicKind::Text;
    // A box's or oval's bounds; a line from its top-left to its bottom-right corner (either may be the larger); a text
    // object's left, top and width: "You can resize only the width of a text object. The height of a text object is
    // automatically determined by the number of words and the size of text" (p.336).
    QRectF rect;
    // Text and its formatting (p.336-340): the font family (empty: the system's), the point size ("between 5 and
    // 999", p.338), Bold and Italic, the justification, the kerning ("Values are a percentage of the point size",
    // p.340) and the leading in points added to the font's own ("Positive leading values add space", p.340).
    QString text;
    QString family;
    double size = 72.0;
    bool bold = false;
    bool italic = false;
    ClassicJustify justify = ClassicJustify::Left;
    double kerning = 0.0;
    double leading = 0.0;
    // Formatted characters ("use the Text tool to select a string of text within a text object to format individual
    // characters or words", p.336), as Titler+ keeps them (titler.h): family, style ("Bold", "Italic", "Bold Italic"),
    // size and kerning (here a percentage, as the object's).
    QList<TitleRun> runs;
    // The object's colour (the text's, a box's or oval's fill, a line's) and its transparency (0 opaque, 100 clear);
    // their controls come with 6.8d3.
    QString color = QStringLiteral("#ffffff");
    double transparency = 0.0;

    bool operator==(const ClassicObject&) const = default;
};

// "Media Composer automatically opens the title tool you selected" with Persist (p.327) and the drawing environment's
// settings are the tool's; what a title keeps is its objects and its background: "You use the Video Background button
// to switch between a video background and an opaque color background" (p.333), "The default background color is
// black" (p.335).
struct ClassicTitle {
    QList<ClassicObject> objects; // back to front
    bool videoBackground = true;
    QString backgroundColor = QStringLiteral("#000000");

    bool operator==(const ClassicTitle&) const = default;
    QJsonObject toJson() const;
    static ClassicTitle fromJson(const QJsonObject& o);
    // The object with `id`; null if none.
    const ClassicObject* object(const QString& id) const;
    ClassicObject* object(const QString& id);
    // An id no object has ("Text 1", "Box 2" ...).
    QString newId(ClassicKind kind) const;
};

inline constexpr double kClassicWidth = 1920.0;
inline constexpr double kClassicHeight = 1080.0;

// The style name of a Titler+ font for Bold and Italic ("Regular", "Bold", "Italic", "Bold Italic").
QString classicStyle(bool bold, bool italic);
// Whether a style name is bold, italic.
bool styleIsBold(const QString& style);
bool styleIsItalic(const QString& style);

// "This command displays only low saturation colors for use in text, objects, and background. Colors with low
// saturation look best when combined with video" (Safe Colors, p.333): the colour with its saturation and brightness
// held to at most 75% and 90% (ours), unchanged when already within.
QString safeColor(const QString& color);

} // namespace lumenis::model
