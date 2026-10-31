// The Effect Editor's direct manipulation (ROADMAP 6.7a4; FX Guide p.92-93, p.292-293; docs/spec/3d-effects.md,
// "Direct manipulation"): the 3D handle buttons and Outline/Path, the overlay it gives the Composer's Effect
// Preview monitor, and handle drags turned into the shown effect's values. The geometry is media's
// (effect_handles.h); this file keeps the Effect Editor's ways of storing values (keyframes, ranges, one undo
// step per drag).
#include "lumenis/ui/effect_editor.h"

#include "lumenis/media/compositor.h"
#include "lumenis/ui/timeline.h"

#include <QJsonObject>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {

QPointF fraction(const QPointF& p, const QSizeF& size) {
    return {p.x() / size.width(), p.y() / size.height()};
}

} // namespace

void EffectEditor::setHandleTool(media::HandleTool tool) {
    handleTool_ = tool;
    // One button at a time; the selected one again turns the handles off (p.292).
    for (auto it = handleButtons_.cbegin(); it != handleButtons_.cend(); ++it) {
        it.value()->setChecked(it.key() == static_cast<int>(tool) && tool != media::HandleTool::None);
    }
    syncPreview();
}

void EffectEditor::setOutlineShown(bool shown) {
    outlineShown_ = shown;
    if (outline_->isChecked() != shown) outline_->setChecked(shown);
    syncPreview();
}

QSizeF EffectEditor::previewSize() const {
    const Rational a = timeline_.composer().aspectRatio();
    const double ratio = a.num() > 0 && a.den() > 0 ? static_cast<double>(a.num()) / static_cast<double>(a.den()) : 16.0 / 9.0;
    return {1920.0, 1920.0 / ratio};
}

void EffectEditor::syncHandleButtons() {
    const bool threeD = current_ && media::is3DEffect(*current_);
    for (QToolButton* b : std::as_const(handleButtons_)) b->setVisible(threeD);
    if (!threeD && handleTool_ != media::HandleTool::None) {
        handleTool_ = media::HandleTool::None;
        for (QToolButton* b : std::as_const(handleButtons_)) b->setChecked(false);
    }
    outline_->setEnabled(current_ && media::hasEffectOutline(*current_));
}

void EffectEditor::fillOverlay(const model::Effect& effect, Composer::EffectPreview& preview) const {
    if (!media::hasEffectOutline(effect)) return;
    const QSizeF size = previewSize();
    const double t = positionTime();
    const bool threeD = media::is3DEffect(effect);
    // 2D effects always show their wire frame and handles (ours: p.92 lets them be displayed); 3D effects
    // those of the button selected; Outline/Path the outline and the path for both.
    const bool handles = !threeD || handleTool_ != media::HandleTool::None;
    if (handles || outlineShown_) {
        if (const auto o = media::effectOutline(effect, t, span_, size)) {
            for (const QPointF& c : o->corners) preview.outline << fraction(c, size);
        }
    }
    if (handles) {
        for (const media::EffectHandle& h : media::effectHandles(effect, handleTool_, t, span_, size)) {
            preview.handles << Composer::EffectPreview::Handle{
                h.id, fraction(h.at, size), h.from ? std::optional<QPointF>(fraction(*h.from, size)) : std::nullopt};
        }
    }
    if (outlineShown_) {
        const media::EffectPath path = media::effectPath(effect, span_, size);
        for (const QPointF& p : path.line) preview.path << fraction(p, size);
        for (const QPointF& p : path.dots) preview.pathDots << fraction(p, size);
    }
}

void EffectEditor::putValue(model::Effect& effect, const QString& category, const QString& parameter, const QJsonValue& value) const {
    model::ParameterKeyframes keys = model::keyframesOf(effect, category, parameter);
    if (keys.keys.isEmpty()) {
        QJsonObject group = effect.parameters.value(category).toObject();
        group.insert(parameter, value);
        effect.parameters.insert(category, group);
        return;
    }
    // "make sure that the appropriate keyframe is selected, and then manipulate the wire frame and handles"
    // (p.92): the selected keyframes take the value; with none selected, a keyframe at the position does.
    const QString k = category + QLatin1Char('/') + parameter;
    bool any = false;
    for (const KeyframeId& id : selection_) {
        if (id.parameter != k || id.index >= keys.keys.size()) continue;
        keys.keys[id.index].value = value;
        any = true;
    }
    if (any) {
        model::setKeyframes(effect, category, parameter, keys);
    } else {
        model::setKeyframe(effect, category, parameter, positionTime(), span_, value);
    }
}

bool EffectEditor::dragHandle(const QString& id, const QPointF& from, const QPointF& to, bool finished) {
    if (!shown_ || !current_ || !timeline_.isEffectMode() || !media::hasEffectOutline(*current_)) {
        handleStart_.reset();
        return false;
    }
    if (!handleStart_) handleStart_ = *current_;
    const model::Effect start = *handleStart_;
    if (finished) handleStart_.reset();
    const QSizeF size = previewSize();
    const QPointF a(from.x() * size.width(), from.y() * size.height());
    const QPointF b(to.x() * size.width(), to.y() * size.height());
    const QList<media::HandleValue> values = media::dragEffectHandle(start, handleTool_, id, a, b, positionTime(), span_, size);
    model::Effect edited = start;
    for (const media::HandleValue& v : values) {
        // Whole numbers within the control's range, as the sliders give them.
        double value = std::round(v.value);
        if (const EffectParameter* info = parameterInfo(v.category + QLatin1Char('/') + v.name)) {
            value = std::clamp(value, static_cast<double>(info->min), static_cast<double>(info->max));
        }
        putValue(edited, v.category, v.name, static_cast<int>(value));
    }
    if (!values.isEmpty()) setActiveParameter(values.first().category + QLatin1Char('/') + values.first().name);
    if (!finished) {
        preview_ = edited;
        syncValues(edited);
        syncPreview();
        return edited.parameters != start.parameters;
    }
    preview_.reset();
    if (edited.parameters == start.parameters) {
        syncValues(start);
        syncPreview();
        return false;
    }
    const QJsonObject parameters = edited.parameters;
    return changeShown(tr("Modify Effect"), [&](model::Effect& e) { e.parameters = parameters; });
}

} // namespace lumenis::ui
