#pragma once

// Direct manipulation of effects in the Effect Preview monitor (ROADMAP 6.7a4; FX Guide p.92-93 for 2D
// effects, p.292-293 for 3D effects; docs/spec/3d-effects.md, "Direct manipulation"): where an effect puts
// its foreground picture (the wire-frame outline), the handles a tool shows on it, what dragging a handle
// does to the effect's parameters, and the motion path. Everything is in pixels of a picture of the size
// given, x to the right and y down, like the picture the compositor makes; the Effect Editor turns the
// values into undoable changes and the Composer draws the outline, handles and path.

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>

#include <array>
#include <optional>

namespace lumenis::media {

// The 3D effect handle buttons "along the right side of the Effect Editor" (p.292), and None for no button
// selected. 2D effects have one set of handles and need no button (p.92).
enum class HandleTool { None, CornerPin, Crop, Scale, Axis, ZRotation, XRotation, YRotation, XYPosition, XZPosition };

// The effects that can be manipulated directly: the 2D effects that place a picture with Position and
// Scaling (Picture-in-Picture, Resize, Mask and the Film masks; p.92 "multilayer video effects such as
// Picture-in-Picture effects") and the 3D effects (is3DEffect).
bool hasEffectOutline(const model::Effect& effect);

// The wire-frame outline (p.92-93): the foreground's four corners as placed at `time` (top left, top right,
// bottom right, bottom left of the picture, after Crop) and its centre. A 3D shape's plane before it is
// bent. Nothing for another effect, or a 3D plane that is not in front of the viewer.
struct EffectOutline {
    std::array<QPointF, 4> corners{};
    QPointF center;
};
std::optional<EffectOutline> effectOutline(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size);

// A handle: its name (below), where it is and, for the rotation handles, the point its line is drawn from.
// 2D effects: "move" (the centre; dragging inside the outline does the same), "top", "side" and "corner"
// (p.92: "the top handle adjusts the height of the effect image, the side handle adjusts the width, and the
// corner handle adjusts width and height together, maintaining the aspect ratio"). 3D effects, by tool:
// Corner Pin "corner1".."corner4"; Crop "crop:T", "crop:B", "crop:L", "crop:R"; Scale "top", "side", "corner"
// (p.293); Axis "axis"; X Rotation "rotX:top" and "rotX:bottom"; Y Rotation "rotY:left" and "rotY:right"; Z
// Rotation "rotZ"; XY and XZ Position "move".
struct EffectHandle {
    QString id;
    QPointF at;
    std::optional<QPointF> from;
    bool operator==(const EffectHandle&) const = default;
};
QList<EffectHandle> effectHandles(const model::Effect& effect, HandleTool tool, double time, const model::EffectSpan& span,
                                  const QSizeF& size);
// The handle within `reach` pixels of `point` (the nearest), or "move" when the point is inside the outline
// and the tool moves the picture; empty for none.
QString effectHandleAt(const model::Effect& effect, HandleTool tool, double time, const model::EffectSpan& span, const QSizeF& size,
                       const QPointF& point, double reach);

// A parameter value a drag sets.
struct HandleValue {
    QString category;
    QString name;
    double value = 0.0;
    bool operator==(const HandleValue&) const = default;
};
// Dragging handle `id` of `effect` (as it was when the drag began) from `from` to `to`: the values that make
// the handle follow the pointer at `time`. The picture's geometry is solved for numerically, so handles stay
// under the pointer through perspective, rotation and Target; rotation handles turn by the distance dragged
// (X and Y: half a turn across the picture; Z: the angle swept about the centre). Values are not rounded or
// clamped to the controls' ranges. Empty when the handle does nothing for this effect.
QList<HandleValue> dragEffectHandle(const model::Effect& effect, HandleTool tool, const QString& id, const QPointF& from,
                                    const QPointF& to, double time, const model::EffectSpan& span, const QSizeF& size);

// The Outline/Path button's motion path (p.92-93): the centre's track over the effect as a line, and a dot
// "on the path [for] the location of the center of the PIP at a keyframe" at each keyframe time of the
// effect's parameters (within it). An empty line when the centre does not move.
struct EffectPath {
    QList<QPointF> line;
    QList<QPointF> dots;
};
EffectPath effectPath(const model::Effect& effect, const model::EffectSpan& span, const QSizeF& size);

// The 3D effects' forward geometry (warp3d.cpp), for the outline and handles: where the point (x, y) of the
// foreground (pixels from its centre, Y up, before Scaling) lands at `time`, and where the Axis (the point
// the plane turns about) lands; nothing behind the viewer.
std::optional<QPointF> warp3dScreenPoint(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size,
                                         double x, double y);
std::optional<QPointF> warp3dAxisPoint(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size);
// The cropped foreground's corners in its own coordinates (pixels from its centre, Y up): left, right, bottom
// and top.
std::array<double, 4> warp3dCropBox(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size);

} // namespace lumenis::media
