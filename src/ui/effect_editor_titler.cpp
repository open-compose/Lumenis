// The Effect Editor's side of Titler+ (ROADMAP 6.8b; FX Guide p.303-324; docs/spec/titler.md): the title shown, its
// previews while the Record monitor's tools work (titler_tool.*) and its undo steps.
#include "lumenis/ui/effect_editor.h"

#include "lumenis/model/tracking.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tracking_window.h"
#include "lumenis/ui/titler_panel.h"
#include "lumenis/ui/titler_tool.h"

namespace lumenis::ui {

TitlerDashboard* EffectEditor::titlerDashboard() {
    if (!titlerDashboard_) titlerDashboard_ = new TitlerDashboard(*titler_, this);
    return titlerDashboard_;
}

std::optional<model::Title> EffectEditor::shownTitle() const {
    const model::Effect* e = shownEffect();
    if (!e || !model::isTitlerEffect(*e)) return std::nullopt;
    // The title as it is at the position: its keyframed numbers at their values there (6.8c1).
    return model::titleAt(*e, positionTime(), span_);
}

std::optional<model::Title> EffectEditor::storedTitle() const {
    if (!current_ || !model::isTitlerEffect(*current_)) return std::nullopt;
    return model::titleAt(*current_, positionTime(), span_);
}

void EffectEditor::previewTitle(const model::Title& title) {
    if (!current_ || !model::isTitlerEffect(*current_)) return;
    model::Effect e = *current_;
    model::storeTitleAt(e, title, positionTime(), span_);
    preview_ = e;
    syncPreview();
    // The Record monitor shows the title as it is being made (MainWindow's picture resolver, pictureOverride).
    timeline_.composer().refreshDisplay();
}

void EffectEditor::cancelTitlePreview() {
    if (!preview_) return;
    preview_.reset();
    syncPreview();
    timeline_.composer().refreshDisplay();
}

bool EffectEditor::commitTitle(const QString& description, const model::Title& title) {
    preview_.reset();
    const bool done = changeShown(description, [&](model::Effect& e) { model::storeTitleAt(e, title, positionTime(), span_); });
    syncPreview();
    return done;
}

bool EffectEditor::setTitleKeyframes(bool add, bool wholeTitle) {
    if (!current_ || !model::isTitlerEffect(*current_) || !titler_) return false;
    // The selected objects' numbers, or the title's own (the Background and Foreground) with none selected.
    const QStringList ids = wholeTitle ? QStringList{} : titler_->selection();
    const double t = positionTime();
    titler_->finishEditing();
    const bool done = changeKeyframes(add ? tr("Add Keyframe") : tr("Delete Keyframe"), [&](model::Effect& e) {
        return add ? model::addTitleKeyframes(e, ids, t, span_) : model::removeTitleKeyframes(e, ids, t, span_);
    });
    syncPreview();
    return done;
}

bool EffectEditor::setTitleRollCrawl(bool on) {
    if (!current_ || !model::isTitlerEffect(*current_)) return false;
    preview_.reset();
    const bool done = changeKeyframes(tr("Roll / Crawl"), [&](model::Effect& e) { return model::setRollCrawl(e, on, span_); });
    syncPreview();
    return done;
}

QStringList EffectEditor::trackerNames() const {
    QStringList out;
    if (!current_) return out;
    for (const model::Tracker& t : model::trackingOf(*current_).trackers) out << t.name;
    return out;
}

bool EffectEditor::trackingWindowShown() const {
    return trackingWindow_ && trackingWindow_->isVisible();
}

QStringList EffectEditor::titleKeyframed() const {
    QStringList out;
    const model::Effect* e = shownEffect();
    if (!e || !model::isTitlerEffect(*e)) return out;
    const QString prefix = model::titleCategory(QString());
    for (const QString& key : model::keyframedParameters(*e)) {
        if (key.startsWith(prefix + QLatin1Char('/')) || key.startsWith(prefix + QLatin1Char(':'))) out << key;
    }
    return out;
}

std::optional<std::pair<model::EffectRef, model::Effect>> EffectEditor::pictureOverride() const {
    if (!preview_ || !shown_ || level_ != 0) return std::nullopt;
    return std::pair{*shown_, *preview_};
}

} // namespace lumenis::ui
