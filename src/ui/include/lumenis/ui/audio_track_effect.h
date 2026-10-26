#pragma once

#include "lumenis/model/sequence.h"

#include <QWidget>

#include <optional>

class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace lumenis::ui {

class Timeline;

// Tools > Audio Track Effect (Editing Guide p.791-795; docs/spec/track-effects.md): one insert of
// one audio track at a time, chosen with Select Track and Select Insert (or the arrow keys); Select
// Effect puts an effect there, and its parameters show below as sliders.
//
// A panel's contents ("AudioTrackEffect"), made by MainWindow. It follows Timeline::changed and
// Composer::changed. A finished change is an undo step (Timeline::setTrackInsert); while a slider is
// dragged the new value plays at once ("you can modify the effect dynamically without stopping
// playback", p.793): preview() holds it, and previewChanged() asks MainWindow to play it.
class AudioTrackEffectTool : public QWidget {
    Q_OBJECT

public:
    // `timeline` must outlive the tool.
    explicit AudioTrackEffectTool(Timeline& timeline, QWidget* parent = nullptr);

    // The track and insert (0-4: a-e) shown. An unknown track picks the first audio track.
    QString track() const { return track_; }
    int slot() const { return slot_; }
    void select(const QString& track, int slot);
    // The insert shown, as it is now (with a value being dragged); none for an empty slot.
    std::optional<model::TrackInsert> insert() const;

    // The controls (picture on p.793).
    QToolButton* trackButton() const { return trackButton_; }
    QToolButton* insertButton() const { return insertButton_; }
    QToolButton* effectButton() const { return effectButton_; }
    QToolButton* settingsButton() const { return settingsButton_; }
    QToolButton* bypassButton() const { return bypassButton_; }
    QLabel* messageLabel() const { return message_; }
    // A parameter's slider; null if the effect shown has no such parameter.
    QSlider* paramSlider(const QString& id) const;
    QLabel* paramValue(const QString& id) const;

    // Select Effect (p.793): the effect in the shown insert, with its defaults; an empty id removes it
    // ("no insert", p.795).
    bool setEffect(const QString& effectId);
    // A parameter of the shown insert; with `done` an undo step, else only previewed.
    bool setParam(const QString& id, double value, bool done = true);
    // Bypass (p.794): the shown insert plays without its effect (saved with the sequence). With
    // `allTracks` (Ctrl+Bypass) every track's effects are left out of what plays (not saved).
    void bypass(bool allTracks);
    bool bypassedAll() const { return bypassAll_; }
    // Copy Settings and Paste Settings (p.794): the parameter values, onto the same effect.
    void copySettings();
    bool pasteSettings();

    // The value being dragged, for what plays: the track, the slot and the insert as previewed.
    struct Preview {
        QString track;
        int slot = 0;
        model::TrackInsert insert;
    };
    const std::optional<Preview>& preview() const { return preview_; }

    void refresh();

signals:
    // What plays should change now: a previewed value, or Ctrl+Bypass.
    void previewChanged();

protected:
    // Up and Down change the track; Left and Right the insert (p.794).
    void keyPressEvent(QKeyEvent* event) override;

private:
    // The record sequence's audio tracks, in order.
    QStringList audioTracks() const;
    void rebuildParams();
    void showValues();

    Timeline& timeline_;
    QString track_;
    int slot_ = 0;
    QToolButton* trackButton_ = nullptr;
    QToolButton* insertButton_ = nullptr;
    QToolButton* effectButton_ = nullptr;
    QToolButton* settingsButton_ = nullptr;
    QToolButton* bypassButton_ = nullptr;
    QWidget* params_ = nullptr;
    QVBoxLayout* paramsLayout_ = nullptr;
    QLabel* message_ = nullptr;
    QString shownEffect_; // the effect the parameter rows were made for
    std::optional<Preview> preview_;
    std::optional<model::TrackInsert> clipboard_;
    bool bypassAll_ = false;
    bool syncing_ = false;
};

} // namespace lumenis::ui
