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
    int min = 0;
    int max = 100;
    QJsonValue defaultValue;
    QStringList options;
};

// The parameter categories, by name as printed (p.491).
const QList<EffectParameter>* effectParameters(const QString& category);

// Tools > Effect Editor (FX guide p.83-88): the effect at the Record
// monitor's position on the selected tracks (p.84; blank otherwise), its
// parameter categories with triangular openers, sliders and their values,
// the buttons down the right side and the Transition parameters at the
// bottom (p.500). Changes are undoable edits of the sequence. Playback,
// rendering and keyframes come later (ROADMAP Phase 6), so their buttons
// are off.
class EffectEditor : public QWidget {
    Q_OBJECT

public:
    explicit EffectEditor(Timeline& timeline, QWidget* parent = nullptr);

    // Shows the effect under the position, or the Transition parameters of
    // cuts with trim rollers, or nothing.
    void refresh();
    const std::optional<model::EffectRef>& shown() const { return shown_; }
    bool isBlank() const;
    QString effectName() const;
    QStringList categories() const;
    bool isOpen(const QString& category) const;
    void setOpen(const QString& category, bool open);
    QSlider* slider(const QString& category, const QString& parameter) const;
    QCheckBox* checkBox(const QString& category, const QString& parameter) const;
    QLineEdit* durationBox() const { return duration_; }
    QToolButton* alignmentButton() const { return alignment_; }
    // Transition Effect Alignment and Duration (p.500).
    void setAlignment(model::TransitionAlignment alignment);
    bool setDuration(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild(const model::Effect* effect);
    void syncValues(const model::Effect& effect);
    void syncTransition();
    void setValue(const QString& category, const QString& parameter, const QJsonValue& value);
    QList<model::TrimRoller> transitionCuts() const;
    QString durationText(std::int64_t frames) const;

    Timeline& timeline_;
    std::optional<model::EffectRef> shown_;
    QString shownName_;
    bool transitionOnly_ = false;
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
    bool syncing_ = false;
};

} // namespace lumenis::ui
