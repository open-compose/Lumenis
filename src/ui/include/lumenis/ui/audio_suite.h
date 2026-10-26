#pragma once

#include "lumenis/media/audio_suite.h"
#include "lumenis/model/sequence.h"

#include <QDialog>
#include <QWidget>

#include <functional>
#include <optional>

class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QToolButton;

namespace lumenis::ui {

class Timeline;

// An AudioSuite plug-in's dialog box (p.798-799): the common buttons along the top (OK, Cancel,
// Preview, Render, Bypass, Find Level; Input Mode, Plug-In Settings and Compare beside them), then
// the plug-in's parameters, a slider or a menu each. OK accepts; Render accepts and asks for the
// render (renderRequested()). Preview, Bypass, Find Level, Plug-In Settings and Compare are dimmed:
// they come later.
class AudioSuitePluginDialog : public QDialog {
    Q_OBJECT

public:
    AudioSuitePluginDialog(const media::AudioSuiteInfo& info, const QMap<QString, double>& values,
                           QWidget* parent = nullptr);
    // The values set (every parameter).
    const QMap<QString, double>& values() const { return values_; }
    void setValue(const QString& id, double value);
    // Whether the dialog closed with Render.
    bool renderRequested() const { return render_; }
    QPushButton* button(const QString& name) const; // "ok", "cancel", "preview", "render", "bypass", "findLevel"
    QSlider* paramSlider(const QString& id) const;
    QComboBox* paramChoice(const QString& id) const;
    QLabel* paramValue(const QString& id) const;

private:
    media::AudioSuiteInfo info_;
    QMap<QString, double> values_;
    bool render_ = false;
};

// Tools > AudioSuite (p.796-800): AudioSuite plug-in effects on the segments under the Record
// position on the tracks chosen with the Track Selection Menu, their dialogs, Render Effect, and the
// Fast menu's Set, Render and Remove items over the marks (Global, IN/OUT, From IN; p.799).
//
// A panel's contents ("AudioSuite"), made by MainWindow, which provides the renderer and the target
// drives. It follows Timeline::changed and Composer::changed. Each change is an undo step through
// Timeline::setAudioSuite and Timeline::renderAudioSuite.
class AudioSuiteTool : public QWidget {
    Q_OBJECT

public:
    // `timeline` must outlive the tool.
    explicit AudioSuiteTool(Timeline& timeline, QWidget* parent = nullptr);

    // The selected audio tracks of the Record monitor's sequence, in order, and the first of them
    // (the tool's track, whose effect the tool shows).
    QStringList tracks() const;
    QString track() const;
    // The Track Selection Menu (p.797): `name` alone, or with `add` (Shift) added to or taken from
    // the selection; the Timeline's track selectors follow.
    void chooseTrack(const QString& name, bool add);

    // The plug-in shown: the effect of the segment under the position on track(), else the one last
    // chosen.
    QString plugin() const { return plugin_; }
    // The effect of the segment under the position on track().
    std::optional<model::AudioSuiteEffect> current() const;
    // Plug-In Selection (p.797): the plug-in on the segment under the position on every selected
    // track, with its default values ("Media Composer automatically applies the plug-in effect"). An
    // empty id removes the effect.
    bool choosePlugin(const QString& id);
    // The values of the shown effect, on the same segments, as an undo step.
    bool setValues(const QMap<QString, double>& values);
    // Activate Current Plug-In (p.797): the plug-in's dialog, opened (window modal) for the shown
    // effect; OK sets its values, Render sets them and renders. Null without a plug-in.
    AudioSuitePluginDialog* activate();
    // Render Effect (p.797): the effects of the segments under the position on the selected tracks.
    bool render();

    // The Fast menu's scope (p.799): "Global" without marks, "IN/OUT" with an OUT, "From IN" with an
    // IN alone.
    QString scope() const;
    // Set, Render and Remove AudioSuite over the scope on the selected tracks.
    bool setInScope();
    bool renderInScope();
    bool removeInScope();

    // The Target Drive menu (10 on p.797): drive names and roots; the render's media go to the one
    // chosen, `current` at first.
    void setDrives(const QList<QPair<QString, QString>>& drives, const QString& current);
    QString drive() const;
    // Makes a segment's render on `drive` (media::renderAudioSuite); MainWindow's.
    using Renderer = std::function<std::optional<model::AudioSuiteEffect>(const model::Segment&, const QString& drive,
                                                                          QString* error)>;
    void setRenderer(Renderer renderer) { renderer_ = std::move(renderer); }

    // The controls (picture on p.797).
    QToolButton* trackButton() const { return trackButton_; }
    QToolButton* pluginButton() const { return pluginButton_; }
    QToolButton* activateButton() const { return activateButton_; }
    QToolButton* renderButton() const { return renderButton_; }
    QComboBox* driveMenu() const { return drive_; }
    QLabel* statusLabel() const { return status_; }
    QLabel* messageLabel() const { return message_; }
    // Fills the Fast menu for the marks and returns it (the tool owns it).
    QMenu* fastMenu();

    void refresh();

private:
    bool fail(const QString& text);
    bool renderRange(bool scoped);

    Timeline& timeline_;
    QString plugin_;
    QMap<QString, double> lastValues_; // the values last set, for Set AudioSuite over the marks
    Renderer renderer_;
    QToolButton* trackButton_ = nullptr;
    QToolButton* fast_ = nullptr;
    QToolButton* activateButton_ = nullptr;
    QToolButton* renderButton_ = nullptr;
    QToolButton* pluginButton_ = nullptr;
    QComboBox* drive_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* message_ = nullptr;
};

} // namespace lumenis::ui
