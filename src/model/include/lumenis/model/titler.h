#pragma once

// Avid Titler+ titles (ROADMAP 6.8a; FX Guide p.302-324; docs/spec/titler.md): the title an Avid Titler+ effect
// carries — its text and graphic objects with their Font, Transform, Fill, Outline and Shadow, their layer order
// and groups, the Background and the Foreground — kept as JSON in the effect's parameters under "Titler". The
// picture is media's (media::titlePicture); applying the effect is the Timeline's (Timeline::applyTitler).
//
// Coordinates (ours, in the guide's terms): "Avid Titler+ uses a universal coordinate system where "0,0" ... is
// always the center of the frame" (p.313); we count pixels of a 1920 x 1080 frame, x to the right and y down, and
// font sizes in those pixels, so a title looks the same in every raster.

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>

#include <optional>

namespace lumenis::model {

// The Titler+ effect's name and category (ours: the Title category of the Effect Palette, p.488).
inline constexpr char kTitlerCategory[] = "Title";
inline constexpr char kTitlerName[] = "Titler+"; // "Avid Titler+" in the guide; renamed as the Tools menu item is (ui-inventory.md)
bool isTitlerEffect(const Effect& effect);

// A colour or a four-corner gradient: "Fill Color Gradients can originate from each of the four corners, which
// include Gradient Top Left, Gradient Top Right, Gradient Bottom Left, and Gradient Bottom Right" (p.316).
// Colours are "#rrggbb" (or "#aarrggbb") as elsewhere in the model.
struct TitleColor {
    QString color = QStringLiteral("#ffffff");
    bool gradient = false;
    QString topLeft = QStringLiteral("#ffffff"), topRight = QStringLiteral("#ffffff"), bottomLeft = QStringLiteral("#ffffff"),
            bottomRight = QStringLiteral("#ffffff");
    bool operator==(const TitleColor&) const = default;
};

// Fill (p.315-316, p.322-323): "Fill Color Solid is on by default for an object". Opacity 0-100; Softness in pixels.
struct TitleFill {
    bool on = true;
    TitleColor color;
    double opacity = 100.0;
    double softness = 0.0;
    bool operator==(const TitleFill&) const = default;
};

// Outline (p.316, p.323): "Type ... Outer, Inner, or Border"; "Shape ... Round or Square". Size and Softness in
// pixels (ours: 4 and 0 when turned on).
enum class OutlineType { Outer, Inner, Border };
enum class OutlineShape { Round, Square };
struct TitleOutline {
    bool on = false;
    TitleColor color = {QStringLiteral("#000000")};
    double opacity = 100.0;
    double size = 4.0;
    double softness = 0.0;
    OutlineType type = OutlineType::Outer;
    OutlineShape shape = OutlineShape::Round;
    bool operator==(const TitleOutline&) const = default;
};

// Shadow (p.316-317, p.323-324): Distance (pixels), Angle ("from 0 to 360"; ours: degrees clockwise from the
// right, 45 — down and to the right — until set), Softness (pixels).
struct TitleShadow {
    bool on = false;
    TitleColor color = {QStringLiteral("#000000")};
    double opacity = 75.0;
    double distance = 8.0;
    double angle = 45.0;
    double softness = 4.0;
    bool operator==(const TitleShadow&) const = default;
};

// Font (p.306-310, p.321-322). Size in pixels of a 1080-line frame (ours: 72 until set); Kerning the space added
// between characters and Leading between lines (pixels); Skew in degrees.
enum class HorizontalJustify { Left, Center, Right };
enum class VerticalJustify { Top, Center, Bottom };
enum class FontOrientation { LeftToRight, RightToLeft, TopToBottom };
struct TitleFont {
    QString family; // empty: the application's default sans-serif family (ours)
    QString style;  // "Regular", "Bold", "Italic", "Bold Italic" ... (p.307)
    double size = 72.0;
    bool underline = false;
    HorizontalJustify horizontal = HorizontalJustify::Left;
    VerticalJustify vertical = VerticalJustify::Top;
    FontOrientation orientation = FontOrientation::LeftToRight;
    double kerning = 0.0;
    double leading = 0.0;
    double skew = 0.0;
    bool operator==(const TitleFont&) const = default;
};

// Transform (p.310-314, p.322): Opacity 0-100, Position (pixels from the frame's centre), Scale (percent), Rotation
// (degrees clockwise), and the Anchor Point, "the points where position, scale, and rotation occur", relative to
// the object's own origin (a text's origin is where its justification puts it, p.313).
struct TitleTransform {
    double opacity = 100.0;
    QPointF position;
    double scaleX = 100.0, scaleY = 100.0;
    bool fixedAspect = true;
    double rotation = 0.0;
    QPointF anchor;
    bool operator==(const TitleTransform&) const = default;
};

// An object: a text, a rectangle or an ellipse (p.305), or a group of objects (p.318). Text objects without a box
// width grow with their text; with one they are "a defined paragraph text box" that wraps (p.304). Rectangles and
// ellipses are `size` large, centred on their origin.
enum class TitleObjectKind { Text, Rectangle, Ellipse, Group };
struct TitleObject {
    QString id; // unique within the title
    TitleObjectKind kind = TitleObjectKind::Text;
    QString text;
    QSizeF size;
    TitleFont font;
    TitleTransform transform;
    TitleFill fill;
    TitleOutline outline;
    TitleShadow shadow;
    QList<TitleObject> children; // a group's, back to front
    // By hand: a defaulted comparison cannot see through the list of its own type.
    bool operator==(const TitleObject& o) const;
};

// The whole title: its objects back to front ("Bring Forward ... move the object up one level in the layer order",
// p.318), the Background ("a solid color or color gradient behind all of your text and graphic objects", p.317,
// off until turned on) and the Foreground ("Fill Overlay", "Opacity", "Crop" of the entire composition, p.324; crop
// in percent of the frame from each edge, ours).
struct TitleBackground {
    bool on = false;
    TitleColor color = {QStringLiteral("#000000")};
    double opacity = 100.0;
    bool operator==(const TitleBackground&) const = default;
};
struct TitleForeground {
    double fillOverlay = 0.0;
    double opacity = 100.0;
    double cropTop = 0.0, cropBottom = 0.0, cropLeft = 0.0, cropRight = 0.0;
    bool operator==(const TitleForeground&) const = default;
};
struct Title {
    QList<TitleObject> objects;
    TitleBackground background;
    TitleForeground foreground;
    bool operator==(const Title&) const = default;

    // The object with `id`, also inside groups; null if none.
    const TitleObject* object(const QString& id) const;
    TitleObject* object(const QString& id);
    // An id no object has ("Text 1", "Rectangle 2" ... by kind, ours).
    QString newId(TitleObjectKind kind) const;
    // The text of the topmost text object, for the "Title Text" Clip Text label: "dynamically labels segments based
    // on their topmost Titler+ text content" (p.305). Empty without text.
    QString topText() const;
};

Title titleOf(const Effect& effect);
void setTitle(Effect& effect, const Title& title);
// A new, empty Avid Titler+ effect.
Effect titlerEffect();

// A new text object at `position` (pixels from the frame's centre) with the defaults above.
TitleObject textObject(const Title& title, const QString& text, const QPointF& position);
// A new rectangle or ellipse `size` large centred at `position`.
TitleObject shapeObject(const Title& title, TitleObjectKind kind, const QPointF& position, const QSizeF& size);

// Layer order (p.318): one level up or down among its siblings; false at the top or bottom, or for no such object.
bool bringForward(Title& title, const QString& id);
bool sendBackward(Title& title, const QString& id);
// "Multiple layers of text and graphic objects can be combined into groups" (p.318): the objects (siblings at the
// top level, ours) become one group where the topmost of them was; returns the group's id. Ungroup puts a group's
// children back in its place.
std::optional<QString> groupObjects(Title& title, const QStringList& ids);
bool ungroupObject(Title& title, const QString& id);
// Removes an object (and a group's children).
bool removeObject(Title& title, const QString& id);

// ---- Keyframes on titles (6.8c1)
// The Effect Editor gives Titler+ "access to animation and other standard Effect Editor controls" (p.320): an object's
// numbers are keyframed as effect parameters of the category "Titler:<object id>", the whole title's of "Titler"
// (ours). They are Fixed, so that "When title segments are modified during editorial operations such as trimming,
// overwriting, splitting, or duplicating, Avid Titler+ preserves the relationship between the title and its associated
// keyframes" (2026.8 FX Guide p.325; What's New 2026.8 p.9).
QString titleCategory(const QString& objectId);
// The names of an object's (or, with an empty id, the title's) keyframeable numbers: Opacity, Position X ... for every
// object, Size, Kerning, Leading and Skew for text; Background Opacity, Foreground Opacity, Fill Overlay and the Crops
// for the title.
QStringList titleParameters(TitleObjectKind kind);
QStringList titleParameters();
// A number's value in a title, and setting it; nothing / false for an unknown object or name.
std::optional<double> titleValue(const Title& title, const QString& objectId, const QString& name);
bool setTitleValue(Title& title, const QString& objectId, const QString& name, double value);
// The title at time `t` of the effect: its keyframed numbers at their values then.
Title titleAt(const Effect& effect, double t, const EffectSpan& span);
// Stores `shown`, the title as it is at time `t` (edited from titleAt), in the effect: its numbers that have keyframes
// get (or replace) a keyframe at `t` where they changed, the rest are stored as they are.
void storeTitleAt(Effect& effect, const Title& shown, double t, const EffectSpan& span);
// Keyframes at `t` for every number of the objects `ids` (empty: the title's), at their values then; and removing
// the keyframes there. False when nothing changes.
bool addTitleKeyframes(Effect& effect, const QStringList& ids, double t, const EffectSpan& span);
bool removeTitleKeyframes(Effect& effect, const QStringList& ids, double t, const EffectSpan& span);

// The title effect on segments (Tools > Avid Titler+, p.303): "Select the clip or an empty track segment where you
// want to add a title. If you want to create a new clip containing your title, set in and out marks in the Timeline
// to highlight a portion of the track". With marks (`in` < `out`), `track` is cut at both and the segments between
// get the effect; otherwise the segments `targets` name do (filler included). A segment that has an effect already
// keeps it in a nest under the title, as Alt-nesting does (ours; FX Guide p.216). Returns how many segments got one.
int applyTitler(Sequence& sequence, const QString& track, std::optional<std::pair<std::int64_t, std::int64_t>> marks,
                const QList<SegmentRef>& targets);

} // namespace lumenis::model
