#pragma once

#include "lumenis/model/sequence.h"

#include <QHash>
#include <QJsonValue>
#include <QWidget>

#include <optional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace lumenis::ui {

class Timeline;

// A control of the Effect Editor (FX guide p.491-586), from
// resources/effect_parameters.json.
struct EffectParameter {
    QString name;
    QString kind; // slider, checkbox, color, menu, button, other
    QString subcategory;
    int min = 0;   // a slider's range; min <= max
    int max = 100;
    QJsonValue defaultValue; // as in the file; may be missing
    QStringList options;     // a menu's entries
};

// The parameter categories, by name as printed (p.491).
// The parameters of one category, in order; null for a category not in the file. The list lives as
// long as the program.
const QList<EffectParameter>* effectParameters(const QString& category);

// Tools > Effect Editor (FX guide p.83-88): the effect at the Record
// monitor's position on the selected tracks (p.84; blank otherwise), its
// parameter categories with triangular openers, sliders and their values,
// the buttons down the right side and the Transition parameters at the
// bottom (p.500). Changes are undoable edits of the sequence. Playback,
// rendering and keyframes come later (ROADMAP Phase 6), so their buttons
// are off.
//
// A panel's contents ("EffectEditor"), made by MainWindow. It follows Timeline::changed and
// Composer::changed, and makes every change through the Timeline (changeEffect, setTransitions).
// An effect's parameter values live in model::Effect::parameters as {category: {parameter: value}}.
class EffectEditor : public QWidget {
    Q_OBJECT

public:
    // `timeline` must outlive the editor.
    explicit EffectEditor(Timeline& timeline, QWidget* parent = nullptr);

    // Shows the effect under the position, or the Transition parameters of
    // cuts with trim rollers, or nothing.
    // The controls are rebuilt only when the effect shown changes; otherwise their values are
    // brought up to date.
    void refresh();
    // The effect shown; nothing when blank or showing only the Transition parameters.
    const std::optional<model::EffectRef>& shown() const { return shown_; }
    // Neither an effect nor Transition parameters are shown.
    bool isBlank() const;
    // The shown effect's name; empty if none.
    QString effectName() const;
    // The shown effect's parameter categories, in order.
    QStringList categories() const;
    // Whether a category's opener is open; false for one not shown.
    bool isOpen(const QString& category) const;
    void setOpen(const QString& category, bool open);
    // A parameter's control; null if it is not shown or is of another kind.
    QSlider* slider(const QString& category, const QString& parameter) const;
    QCheckBox* checkBox(const QString& category, const QString& parameter) const;
    QLineEdit* durationBox() const { return duration_; }
    QToolButton* alignmentButton() const { return alignment_; }
    // Transition Effect Alignment and Duration (p.500).
    // Both apply to the transition shown, or to the trim rollers' video cuts in Trim mode; they do
    // nothing without either. The duration is "seconds:frames" or frames; setDuration() returns
    // false, and shows the old value again, when it cannot be read or applied.
    void setAlignment(model::TransitionAlignment alignment);
    bool setDuration(const QString& text);

protected:
    // Shift+Left and Shift+Right on a slider move it by ten.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Replaces the controls with those of `effect`'s parameter categories (none for null).
    void rebuild(const model::Effect* effect);
    // Sets the controls to the values stored in `effect`, without storing them again.
    void syncValues(const model::Effect& effect);
    // Enables and fills the Transition alignment and duration.
    void syncTransition();
    // Stores a parameter's value in the shown effect, as an undoable "Modify Effect".
    void setValue(const QString& category, const QString& parameter, const QJsonValue& value);
    // The cuts the Transition parameters apply to: the shown transition's, or in Trim mode the
    // video rollers when no effect is shown; empty otherwise.
    QList<model::TrimRoller> transitionCuts() const;
    // The video rollers in Trim mode; empty outside it.
    QList<model::TrimRoller> trimCuts() const;
    // `frames` as seconds:frames at the project's nominal rate.
    QString durationText(std::int64_t frames) const;
    // The Record monitor's sequence; an empty one when nothing is loaded.
    model::Sequence recordSequence() const;

    Timeline& timeline_;
    std::optional<model::EffectRef> shown_;
    QString shownName_;           // "category/name" of the effect shown, to notice a replacement
    bool transitionOnly_ = false; // only the Transition parameters are shown (Trim mode)
    QWidget* body_ = nullptr;
    QVBoxLayout* groups_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* titleIcon_ = nullptr;
    QLabel* blank_ = nullptr;
    QToolButton* alignment_ = nullptr;
    QLineEdit* duration_ = nullptr;
    QHash<QString, QWidget*> groupBodies_;   // category -> its controls
    QHash<QString, QToolButton*> openers_;   // category -> its opener
    QHash<QString, QWidget*> controls_;      // "category/parameter" -> control
    QHash<QString, QLabel*> valueLabels_;    // "category/parameter" -> value
    model::TransitionAlignment shownAlignment_ = model::TransitionAlignment::CenteredOnCut;
    bool syncing_ = false; // controls are being set from the model; their changes are not stored
};

} // namespace lumenis::ui
