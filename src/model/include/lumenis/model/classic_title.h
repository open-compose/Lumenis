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
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <optional>

namespace lumenis::model {

// "Let you draw boxes, circles, ovals, and lines" (p.330); text from the Text tool (p.335).
enum class ClassicKind { Text, Box, Oval, Line };
// "The text justification buttons" (p.338).
enum class ClassicJustify { Left, Center, Right };

// A colour and a transparency, each one value or a blend of two (p.349-352): "You can use these tools to blend two
// colors in a text or graphic object or in a border to create a color gradient, and to specify the direction of the
// blend". Transparency is 0 (opaque) to 100 (clear). A blend runs from the first value to the second across the
// object's bounds in its direction (ours: degrees clockwise from left to right).
struct ClassicPaint {
    QString color = QStringLiteral("#ffffff");
    QString color2; // empty: no colour blend
    double colorAngle = 0.0;
    double transparency = 0.0;
    std::optional<double> transparency2; // none: no transparency blend
    double transparencyAngle = 0.0;

    bool operator==(const ClassicPaint&) const = default;
};

// A paint of black (a border's until set).
inline ClassicPaint blackPaint() {
    ClassicPaint p;
    p.color = QStringLiteral("#000000");
    return p;
}

// "Arrowhead button and options, including Remove Arrowhead option (top)" (p.349): an arrow at the line's end (its
// bottom-right point in ClassicObject::rect), at its start, or both.
enum class ClassicArrows { None, End, Start, Both };

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
    // The fill (the text's colour, a box's or oval's inside, a line's colour), with its blends (6.8d3).
    ClassicPaint fill;
    // "You can use the Border Width button to change the width of lines created with the drawing tools or to apply a
    // border outline to text or graphic objects" (p.347): a line's width, and the border's (0: none) round the others,
    // in pixels; the border's paint (black until set, ours).
    double lineWidth = 2.0;
    double borderWidth = 0.0;
    ClassicPaint border = blackPaint();
    // "You can use the line attribute tools to round the corners of a box" (p.347): the corner radius in pixels, "The
    // maximum custom radius is 200".
    double cornerRadius = 0.0;
    // "add arrowheads to lines" (p.348): which ends have one, and their shape as multiples of the line's width ("The
    // shape of the arrowhead is in proportion to the line width", p.349): its length along the line and its width
    // across (ours: 3 and 3 until set).
    ClassicArrows arrows = ClassicArrows::None;
    QSizeF arrowSize{3.0, 3.0};
    // The shadow (p.353-355): drop or depth, its depth in pixels and direction (ours: degrees clockwise from the right,
    // 45 until set, down and to the right), its softness ("integer values between 4 and 40 ... zero ... no softening")
    // and paint (no blends: "You cannot blend the shadow for a title object", p.350).
    bool depthShadow = false;
    double shadowDepth = 0.0;
    double shadowAngle = 45.0;
    int shadowSoftness = 0;
    QString shadowColor = QStringLiteral("#000000");
    double shadowTransparency = 0.0;

    // "Combining objects into a group means that actions, such as moving or applying color, affect all objects in the
    // group" (p.355): the group's id (empty: none; ours, one level: grouping groups merges them). "You cannot change the
    // object until you unlock it" (p.356).
    QString group;
    bool locked = false;
    // An object a title template brought in (6.8d4b): "The template prevents you from changing the position, colors,
    // shadows, or graphics, which ensures consistency", only the text changes; Moveable Templates lets it move (p.360).
    bool fromTemplate = false;

    // The shadow's offset in pixels.
    QPointF shadowOffset() const;
    // Sets the depth and direction from an offset (the direction kept when it is zero).
    void setShadowOffset(const QPointF& offset);
    bool operator==(const ClassicObject&) const = default;
};

// "Media Composer automatically opens the title tool you selected" with Persist (p.327) and the drawing environment's
// settings are the tool's; what a title keeps is its objects and its background: "You use the Video Background button
// to switch between a video background and an opaque color background" (p.333), "The default background color is
// black" (p.335).
// "Rolling titles scroll vertically ... Crawling titles scroll horizontally" (p.341-342, 6.8d2). Their objects lie on
// pages, "a unit the size of a PAL, NTSC, or HD frame" (p.342): a rolling title's pages one under the other, a crawling
// title's side by side, page 1 at the origin.
enum class ClassicMotion { Still, Rolling, Crawling };

struct ClassicTitle {
    QList<ClassicObject> objects; // back to front
    bool videoBackground = true;
    QString backgroundColor = QStringLiteral("#000000");
    ClassicMotion motion = ClassicMotion::Still;
    int pages = 1;
    // "Auto Size mode adds or removes pages automatically as you add, delete, or reformat the text. Auto Size mode is
    // on by default when you click the Rolling Title button or the Crawling Title button" (p.343).
    bool autoSize = true;

    bool operator==(const ClassicTitle&) const = default;
    QJsonObject toJson() const;
    static ClassicTitle fromJson(const QJsonObject& o);
    // The object with `id`; null if none.
    const ClassicObject* object(const QString& id) const;
    ClassicObject* object(const QString& id);
    // An id no object has ("Text 1", "Box 2" ...), and a group id no object's group has ("Group 1" ...).
    QString newId(ClassicKind kind) const;
    QString newGroupId() const;
    // `ids` with every object in the same group as one of them, in the order given (the group's others after it).
    QStringList withGroups(const QStringList& ids) const;
};

inline constexpr double kClassicWidth = 1920.0;
inline constexpr double kClassicHeight = 1080.0;

// The page limits (p.342): "1080i and 1080p Rolling 29, Crawling 4; 720p Rolling 44, Crawling 6"; NTSC 67 and 11, PAL
// 56 and 11. `lines` is the project's frame height (ours: 486 or fewer is NTSC, up to 576 PAL); a still title has one.
int classicPageLimit(ClassicMotion motion, int lines = 1080);
// Where page `page` (from 1) begins, and the page a point is on (from 1).
QPointF classicPageOrigin(ClassicMotion motion, int page);
int classicPageAt(ClassicMotion motion, const QPointF& point);

// "To adjust the layering of objects in a title" (p.356): Bring To Front, Send To Back, Bring Forward (one layer up)
// and Send Backward. The objects `ids` move keeping their order among themselves; false when nothing moves.
enum class ClassicLayer { ToFront, ToBack, Forward, Backward };
bool reorderClassicObjects(ClassicTitle& title, const QStringList& ids, ClassicLayer layer);
// Object > Group and UnGroup (p.356): the objects `ids` made one group, or freed of theirs; false when nothing changes.
bool groupClassicObjects(ClassicTitle& title, const QStringList& ids);
bool ungroupClassicObjects(ClassicTitle& title, const QStringList& ids);
// Puts back every object that was locked in `before` (its place in the list aside): "You cannot change the object
// until you unlock it" (p.356), "You cannot delete locked objects" (p.356). Locking and unlocking themselves go through.
void keepLockedObjects(ClassicTitle& title, const ClassicTitle& before);

// ---- Title templates (6.8d4b; p.360-361)
// Templates > Include Template: the template's objects added to the title (new ids, their groups new), marked as the
// template's; their ids.
QStringList includeClassicTemplate(ClassicTitle& title, const ClassicTitle& templ);
// Puts back what a change did to the template's objects in `before` but their text (and, with `moveable`, where they
// are: Moveable Templates, p.361): the window runs it with every undo step, as keepLockedObjects.
void keepTemplateObjects(ClassicTitle& title, const ClassicTitle& before, bool moveable);
// A template file's contents ({"titleTemplate": the title}) and back; nothing for other JSON.
QJsonObject classicTemplateJson(const ClassicTitle& templ);
std::optional<ClassicTitle> classicTemplateFrom(const QJsonObject& json);

// ---- Title styles (6.8d4b; p.361-364): "You can save the following style parameters in the form of a title style:
// Font, style, size, justification, kerning, and leading. Fill color and transparency. Shadow color, transparency,
// depth, direction, softness, and type. Border color, transparency, and width."
enum class ClassicStyleField {
    Font, Style, Size, Justification, Kerning, Leading,
    FillColor, FillTransparency,
    ShadowColor, ShadowTransparency, ShadowDepth, ShadowDirection, ShadowSoftness, ShadowType,
    BorderColor, BorderTransparency, BorderWidth,
};
inline constexpr int kClassicStyleFields = 17;
// The field's check box label in the Title Style Sheet dialog box (ours: the guide's words).
QString classicStyleFieldName(ClassicStyleField field);
struct ClassicTitleStyle {
    // "This name appears in the Style list in the Title tool and in the Settings list" (p.363).
    QString name;
    // "(Option) Lets you designate a function key that you can use to apply the title style" (p.362): 1-12, 0 none.
    int functionKey = 0;
    // The attributes the style holds ("Select the current attributes of the object that you want to include", p.362)
    // and their values (the object's other members are not used).
    QList<ClassicStyleField> fields;
    ClassicObject values;

    bool includes(ClassicStyleField field) const { return fields.contains(field); }
    QJsonObject toJson() const;
    static ClassicTitleStyle fromJson(const QJsonObject& o);
    bool operator==(const ClassicTitleStyle&) const = default;
};
// A style named `name` of an object's attributes (every field unless `fields` says).
ClassicTitleStyle classicStyleOf(const ClassicObject& object, const QString& name);
// An object given a style's attributes; a text object's characters lose their own font, style, size and kerning where
// the style sets them.
void applyClassicStyle(ClassicObject& object, const ClassicTitleStyle& style);

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
