// The Effect Editor's side of Titler+ (ROADMAP 6.8b; FX Guide p.303-324; docs/spec/titler.md): the title shown, its
// previews while the Record monitor's tools work (titler_tool.*) and its undo steps.
#include "lumenis/ui/effect_editor.h"

#include "lumenis/ui/timeline.h"
#include "lumenis/ui/titler_tool.h"

namespace lumenis::ui {

std::optional<model::Title> EffectEditor::shownTitle() const {
    const model::Effect* e = shownEffect();
    if (!e || !model::isTitlerEffect(*e)) return std::nullopt;
    return model::titleOf(*e);
}

std::optional<model::Title> EffectEditor::storedTitle() const {
    if (!current_ || !model::isTitlerEffect(*current_)) return std::nullopt;
    return model::titleOf(*current_);
}

void EffectEditor::previewTitle(const model::Title& title) {
    if (!current_ || !model::isTitlerEffect(*current_)) return;
    model::Effect e = *current_;
    model::setTitle(e, title);
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
    const bool done = changeShown(description, [&](model::Effect& e) { model::setTitle(e, title); });
    syncPreview();
    return done;
}

std::optional<std::pair<model::EffectRef, model::Effect>> EffectEditor::pictureOverride() const {
    if (!preview_ || !shown_ || level_ != 0) return std::nullopt;
    return std::pair{*shown_, *preview_};
}

} // namespace lumenis::ui
