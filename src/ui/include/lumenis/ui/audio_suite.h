#pragma once

#include "lumenis/media/audio_suite.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/sequence.h"

#include <QDialog>
#include <QWidget>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QLineEdit;
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

// Tools > AudioSuite (p.796-805), in two modes:
// - on the Timeline (p.797-800): AudioSuite plug-in effects on the segments under the Record position
//   on the tracks chosen with the Track Selection Menu, their dialogs, Render Effect, and the Fast
//   menu's Set, Render and Remove items over the marks (Global, IN/OUT, From IN; p.799);
// - master clip processing (p.800-804): master clips and subclips dropped on the window (or a
//   template's clip found with Find Source From Effect) are processed into new master clips in the
//   target bin, each with an AudioSuite effect template beside it.
//
// A panel's contents ("AudioSuite"), made by MainWindow, which provides the renderers, the target
// drives and bins, and finds dropped items. It follows Timeline::changed and Composer::changed. Each
// Timeline change is an undo step through Timeline::setAudioSuite and Timeline::renderAudioSuite.
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

    // The plug-in shown: in Timeline mode the effect of the segment under the position on track(),
    // else the one last chosen.
    QString plugin() const { return plugin_; }
    // The values the next effect gets: the shown effect's, or the last ones set.
    const QMap<QString, double>& values() const { return lastValues_; }
    // The effect of the segment under the position on track().
    std::optional<model::AudioSuiteEffect> current() const;
    // Plug-In Selection (p.797). On the Timeline: the plug-in on the segment under the position on
    // every selected track, with its default values ("Media Composer automatically applies the
    // plug-in effect"); an empty id removes the effect. In master clip mode: the plug-in the next
    // render uses. Plug-ins only for new master clips (Duplicate) are refused on the Timeline.
    bool choosePlugin(const QString& id);
    // The values of the shown effect: on the Timeline on the same segments, as an undo step.
    bool setValues(const QMap<QString, double>& values);
    // Activate Current Plug-In (p.797): the plug-in's dialog, opened (window modal) for the shown
    // effect; OK sets its values, Render sets them and renders. Null without a plug-in.
    AudioSuitePluginDialog* activate();
    // Render Effect (p.797, p.803): on the Timeline the effects of the segments under the position
    // on the selected tracks; in master clip mode a new master clip from the active clip.
    bool render();

    // The Fast menu's scope (p.799): "Global" without marks, "IN/OUT" with an OUT, "From IN" with an
    // IN alone.
    QString scope() const;
    // Set, Render and Remove AudioSuite over the scope on the selected tracks.
    bool setInScope();
    bool renderInScope();
    bool removeInScope();
    // An AudioSuite template applied from the Fast menu (p.799): its plug-in and values, as Plug-In
    // Selection and OK would set them.
    bool applyTemplate(const model::AudioSuiteTemplate& t);

    // ---- Master clip processing (p.800-804)
    // A clip dropped on the window, from the bin `binId`.
    struct SourceClip {
        QString binId;
        model::BinItem item;
    };
    // Toggle Master Clip Mode (9 on p.801): yellow while on. Display/Hide Master Clip Controls
    // shows the controls below; dropping a clip does both.
    bool masterClipMode() const { return masterMode_; }
    void setMasterClipMode(bool on);
    bool controlsShown() const;
    void showControls(bool on);
    // Items dropped from a bin: master clips and subclips with sound join the Clip Selection menu,
    // the last one active, and the window enters master clip mode; an AudioSuite template arms Find
    // Source From Effect. Returns how many were taken.
    int addItems(const QString& binId, const QList<model::BinItem>& items);
    const QList<SourceClip>& clips() const { return clips_; }
    // The Clip Selection menu (1 on p.801): -1 with no clips.
    int activeClip() const { return active_; }
    void setActiveClip(int index);
    // The Input Source Track selectors (2): the active clip's audio tracks used (0-based). All are on
    // when a clip becomes active.
    QList<int> inputTracks() const;
    void setInputTrack(int track, bool on);
    // The Processing Mode Selection menu (3, p.802-803): what the plug-in offers. Normalize offers
    // its two modes (its "mode" value); the other processes and the effects work on each track
    // alone ("Mono").
    QStringList processingModes() const;
    int processingMode() const;
    void setProcessingMode(int index);
    // The active clip's marks (10): its IN and OUT in the Source monitor, if it is loaded there.
    std::optional<std::int64_t> clipIn() const;
    std::optional<std::int64_t> clipOut() const;
    // Handle Length for End of Master Clip, in seconds (7), and Load Result (6).
    double handleSeconds() const;
    void setHandleSeconds(double seconds);
    bool loadResult() const;
    void setLoadResult(bool on);
    // Target Bin for New Master Clip (4): bin node ids and names; `current` at first.
    void setBins(const QList<QPair<QString, QString>>& bins, const QString& current);
    QString targetBin() const;
    // Load In Source Monitor (8): asks for the active clip in the Source monitor (sourceRequested).
    bool loadInSourceMonitor();
    // Find Source From Effect (11, p.804): the dropped template's master clip becomes the active
    // clip, with the template's plug-in, values, tracks and handle. "A template in the AudioSuite
    // window must reference an existing master clip": false, with a message, otherwise.
    bool findSourceFromEffect();
    bool canFindSource() const { return pendingTemplate_.has_value(); }

    // What a master clip render makes (MainWindow's, through media::audioSuiteClip).
    struct MasterClipRender {
        SourceClip source;
        QList<int> tracks;
        std::optional<std::int64_t> in;
        std::optional<std::int64_t> out;
        double handleSeconds = 0;
        model::AudioSuiteEffect effect;
        QString targetBin;
        QString drive;
        bool loadResult = false;
    };
    // Makes the new master clip and its template; false with `error` on failure.
    using ClipMaker = std::function<bool(const MasterClipRender&, QString* error)>;
    void setClipMaker(ClipMaker maker) { clipMaker_ = std::move(maker); }
    // Finds a bin item: for dropped items and templates' clips.
    using ItemResolver = std::function<std::optional<model::BinItem>(const QString& binId, const QString& id)>;
    void setItemResolver(ItemResolver resolver) { resolver_ = std::move(resolver); }
    // The AudioSuite templates the Fast menu offers (ours: those in the open bins).
    using TemplateSource = std::function<QList<model::BinItem>()>;
    void setTemplateSource(TemplateSource source) { templates_ = std::move(source); }

    // The Target Drive menu (10 on p.797): drive names and roots; the render's media go to the one
    // chosen, `current` at first.
    void setDrives(const QList<QPair<QString, QString>>& drives, const QString& current);
    QString drive() const;
    // Makes a segment's render on `drive` (media::renderAudioSuite); MainWindow's.
    using Renderer = std::function<std::optional<model::AudioSuiteEffect>(const model::Segment&, const QString& drive,
                                                                          QString* error)>;
    void setRenderer(Renderer renderer) { renderer_ = std::move(renderer); }
    // The template the effect icon drags to a bin (p.798): the shown plug-in and values, named after
    // the plug-in; in master clip mode with its clip. Nothing without a plug-in.
    std::optional<model::AudioSuiteTemplate> iconTemplate() const;

    // The controls (pictures on p.797 and p.801).
    QToolButton* trackButton() const { return trackButton_; }
    QToolButton* pluginButton() const { return pluginButton_; }
    QToolButton* activateButton() const { return activateButton_; }
    QToolButton* renderButton() const { return renderButton_; }
    QToolButton* effectIcon() const { return effectIcon_; }
    QToolButton* masterModeButton() const { return masterButton_; }
    QToolButton* findSourceButton() const { return findSource_; }
    QComboBox* driveMenu() const { return drive_; }
    QComboBox* clipMenu() const { return clipMenu_; }
    QComboBox* binMenu() const { return bin_; }
    QComboBox* modeMenu() const { return mode_; }
    QToolButton* inputButton(int track) const;
    QLabel* markIndicator(bool in) const { return in ? markIn_ : markOut_; }
    QLabel* statusLabel() const { return status_; }
    QLabel* messageLabel() const { return message_; }
    // Fills the Fast menu for the marks and returns it (the tool owns it).
    QMenu* fastMenu();

    void refresh();

signals:
    // Load In Source Monitor: MainWindow loads the clip.
    void sourceRequested(const QString& binId, const model::BinItem& clip);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool fail(const QString& text);
    bool renderRange(bool scoped);
    bool renderMasterClip();
    void rebuildInputs();
    void showClip();

    Timeline& timeline_;
    QString plugin_;
    QMap<QString, double> lastValues_; // the values last set
    Renderer renderer_;
    ClipMaker clipMaker_;
    ItemResolver resolver_;
    TemplateSource templates_;
    QList<SourceClip> clips_;
    int active_ = -1;
    QList<int> inputs_; // the active clip's tracks used
    std::optional<model::AudioSuiteTemplate> pendingTemplate_;
    bool masterMode_ = false;
    QPoint pressAt_; // where a drag of the effect icon starts
    QToolButton* effectIcon_ = nullptr;
    QToolButton* trackButton_ = nullptr;
    QToolButton* fast_ = nullptr;
    QToolButton* activateButton_ = nullptr;
    QToolButton* renderButton_ = nullptr;
    QToolButton* pluginButton_ = nullptr;
    QToolButton* masterButton_ = nullptr;
    QToolButton* controlsButton_ = nullptr;
    QToolButton* findSource_ = nullptr;
    QToolButton* loadSource_ = nullptr;
    QComboBox* drive_ = nullptr;
    QComboBox* clipMenu_ = nullptr;
    QComboBox* bin_ = nullptr;
    QComboBox* mode_ = nullptr;
    QWidget* controls_ = nullptr;
    QWidget* inputRow_ = nullptr;
    QCheckBox* loadResult_ = nullptr;
    QLineEdit* handle_ = nullptr;
    QLabel* markIn_ = nullptr;
    QLabel* markOut_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* message_ = nullptr;
};

} // namespace lumenis::ui
