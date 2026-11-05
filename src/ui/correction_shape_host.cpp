// The shape tools on the Color Correction effect in the Current monitor (FX Guide p.698; 6.10e3b2). See
// lumenis/ui/correction_shape_host.h.

#include "lumenis/ui/correction_shape_host.h"

#include "lumenis/model/correction_shapes.h"
#include "lumenis/ui/timeline.h"

#include <algorithm>

namespace lumenis::ui {

CorrectionShapeHost::CorrectionShapeHost(Timeline& timeline, QObject* parent) : QObject(parent), timeline_(timeline) {
    tool_ = new IntraframeTool(*this, this);
    connect(tool_, &IntraframeTool::changed, this, &CorrectionShapeHost::overlayChanged);
}

bool CorrectionShapeHost::active() const {
    return timeline_.isCorrectionMode() && timeline_.correctionOnEffect() && timeline_.correctionLayer() == model::CorrectionLayer::Shapes &&
           timeline_.correctionShot(0).has_value();
}

std::optional<model::Effect> CorrectionShapeHost::intraframeEffect() const {
    if (!active()) return std::nullopt;
    if (auto e = timeline_.correctionEffectAt()) return e;
    // A segment without the effect: the first shape adds it (Timeline::setCorrectionShapeObjects).
    model::Effect e;
    e.category = QStringLiteral("Image");
    e.name = QStringLiteral("Color Correction");
    return e;
}

void CorrectionShapeHost::previewIntraframe(const std::optional<model::Effect>& effect) {
    // While a drag goes on the outlines follow it; the picture changes at its undo step.
    preview_ = effect;
    emit overlayChanged();
}

bool CorrectionShapeHost::changeIntraframe(const QString& description, const model::Effect& effect) {
    preview_.reset();
    const bool done = timeline_.setCorrectionShapeObjects(description, model::intraframeObjects(effect));
    emit overlayChanged();
    return done;
}

void CorrectionShapeHost::setIntraframeSelection(const QStringList& ids) {
    timeline_.setCorrectionShapeSelection(ids);
    emit selectionChanged();
}

void CorrectionShapeHost::refreshIntraframeOverlay() {
    emit overlayChanged();
}

void CorrectionShapeHost::refresh() {
    if (!active()) {
        if (!tool_->selection().isEmpty() || tool_->busy()) tool_->reset();
        emit overlayChanged();
        return;
    }
    // Shapes selected that the effect at the position no longer has (another segment, an undo) are let go.
    const QStringList shapes = timeline_.correctionShapes();
    QStringList kept = tool_->selection();
    kept.removeIf([&](const QString& id) { return !shapes.contains(id); });
    if (kept != tool_->selection()) {
        tool_->keepSelection(kept);
        timeline_.setCorrectionShapeSelection(kept);
        emit selectionChanged();
    }
    emit overlayChanged();
}

Composer::EffectPreview CorrectionShapeHost::overlay() const {
    Composer::EffectPreview p;
    if (!active()) return p;
    p.on = true;
    tool_->fillOverlay(p);
    return p;
}

const model::EffectSpan& CorrectionShapeHost::span() const {
    const auto shot = timeline_.correctionShot(0);
    span_ = model::EffectSpan(shot ? std::max<std::int64_t>(shot->second - shot->first, 1) : 1);
    return span_;
}

double CorrectionShapeHost::positionTime() const {
    const auto shot = timeline_.correctionShot(0);
    if (!shot) return 0.0;
    const std::int64_t position = timeline_.composer().state(MonitorId::Record).position();
    return span().timeOf(static_cast<double>(position - shot->first));
}

double CorrectionShapeHost::previewAspect() const {
    const Rational a = timeline_.composer().aspectRatio();
    return static_cast<double>(a.num()) / static_cast<double>(std::max<std::int64_t>(a.den(), 1));
}

} // namespace lumenis::ui
