#pragma once

// The shape tools of the Color Correction tool's shape panel (FX Guide p.698: "Use the shape tools in the Color
// Correction tool to mark the area you to want to color correct"; docs/spec/color-correction.md, 6.10e3b2): the
// Intraframe drawing tool (intraframe_tool.h) working on the Color Correction effect at the position, in Color
// Correction mode's Current monitor. This host gives the tool that effect (a new one on a segment without it, made at
// the first shape as an adjustment makes it), keeps its changes as undo steps through
// Timeline::setCorrectionShapeObjects, and hands the tool's outlines to the monitors through `overlayChanged`.
//
// The tool works only while the Timeline is in Color Correction mode with CC Effect chosen and the Shapes layer
// selected (ours: "Shapes - selects the shape layer that allows you to use the shape based tools", p.698).

#include "lumenis/ui/composer.h"
#include "lumenis/ui/intraframe_tool.h"

#include <QObject>

namespace lumenis::ui {

class Timeline;

class CorrectionShapeHost : public QObject, public IntraframeHost {
    Q_OBJECT

public:
    explicit CorrectionShapeHost(Timeline& timeline, QObject* parent = nullptr);

    IntraframeTool* tool() const { return tool_; }
    // Whether the tools can draw now: Color Correction mode, CC Effect, the Shapes layer, a sequence loaded.
    bool active() const;
    // The outlines to draw over the Current monitor (empty while not active).
    Composer::EffectPreview overlay() const;
    // The position or the segment changed: forget a selection of shapes no longer there, draw again.
    void refresh();

    // IntraframeHost.
    std::optional<model::Effect> intraframeEffect() const override;
    void previewIntraframe(const std::optional<model::Effect>& effect) override;
    bool changeIntraframe(const QString& description, const model::Effect& effect) override;
    void setIntraframeSelection(const QStringList& ids) override;
    void refreshIntraframeOverlay() override;
    double positionTime() const override;
    const model::EffectSpan& span() const override;
    double previewAspect() const override;
    bool pickPaintColor(const QPointF&) override { return false; }

signals:
    // The outlines changed (overlay()), and the selection of shapes did (for the Softness slider).
    void overlayChanged();
    void selectionChanged();

private:
    Timeline& timeline_;
    IntraframeTool* tool_ = nullptr;
    std::optional<model::Effect> preview_;
    mutable model::EffectSpan span_;
};

} // namespace lumenis::ui
