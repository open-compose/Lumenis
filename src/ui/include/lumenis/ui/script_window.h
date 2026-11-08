#pragma once

// The Script window (Editing Guide ch.18, p.570-576; docs/spec/scriptsync.md, 8.3a): a panel showing one script of a
// script item (lumenis/model/script.h) with the left margin's scene numbers (and line numbers), the right margin's
// page numbers and a status bar of the current scene, page and line.
//
// * Out of Edit Mode, clicks and drags select whole lines; Add Scene and Add Page number the first selected line (the
//   AS and AP buttons, Script > Add Scene/Add Page, p.574); Delete opens the Delete dialog for the selected lines'
//   scene and page numbers; the Scene and Page menus go to them (p.575).
// * "The Edit mode button allows you to edit a script ... The Script Window outlines in purple" (p.573): the text can
//   be typed, cut, copied and pasted, with the editor's undo; Show Find Tools gives Find and Replace (p.575-576).
// * Slates and takes (8.3b; p.576-582): clips dragged from a bin onto selected lines make a slate (onto a slate, a take
//   of it); slates are drawn right of the text with their take tabs and lines, the active take's frame and name, the
//   off-screen (jagged) and colour indicators. Clicks, Ctrl+clicks, Shift+clicks and lassos select takes; dragging a slate
//   moves it (Ctrl: with its lines); Ctrl+dragging a take's end changes its lines; Left/Right step the selected takes'
//   frames; Delete offers Delete Takes; double-click loads a take (takeLoadRequested); the Play button plays the
//   selected ones (takesPlayRequested).
// * Script marks (8.3c; p.582-588): small bars across a take's line at the lines synced to clip frames. Add Script Mark
//   marks the selected line of the take last loaded; the Record button screens the selected takes (or those over the
//   selected lines) and a click on a line marks it at the playing frame; a click selects a mark, Ctrl+drag moves it,
//   Delete offers Delete mark(s); double-click on a mark cues its take there; Ctrl+Alt+double-click splices the marked
//   range (spliceRequested); Find Bin shows the selected takes' clips in their bins.
// * Shared scripts (8.3e; p.589-590): the script lock icon shows its bin's lock (lumenis/model/bin_lock.h, 3.7):
//   red, another user holds it; yellow, the holder saved changes (the title says "(modified)"); blue, released; green,
//   this user holds it. Clicks go to MainWindow (lockClicked), which refreshes, takes or (Alt) releases the lock.
// * Changes mark the window modified (an asterisk in its title, p.570); MainWindow saves the script into its bin item
//   (File > Save Script).

#include "lumenis/model/script.h"

#include <QImage>
#include <QWidget>

#include <cstdint>

#include <functional>
#include <optional>
#include <utility>

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace lumenis::ui {

class ScriptTextView;

// The Delete dialog's choices (p.574, p.579-580).
struct ScriptDeleteChoice {
    bool scenes = false;
    bool pages = false;
    bool takes = false;
    bool marks = false;
};

// How slates show (the Script setting and Script menu, p.578, p.1217).
struct ScriptSlateOptions {
    bool showFrames = true;
    bool showAllTakes = true;
    bool holdSlates = false;
    bool interpolatePosition = false;
    QString takeColor = QStringLiteral("#3c78d2"); // Take Coloring: the take lines' colour (ours: blue)
    double frameScale = 1.0; // Edit > Enlarge Frame / Reduce Frame (p.578)
};

// The script lock icon's states (p.589), as the script's bin's lock (BinDocument::LockState).
enum class ScriptLock { None, Mine, Others, Modified, Released, Protected };

class ScriptWindow : public QWidget {
    Q_OBJECT

public:
    explicit ScriptWindow(QWidget* parent = nullptr);
    ~ScriptWindow() override;

    // Shows the script of item `itemId` in bin `binId`, unmodified.
    void open(const QString& binId, const QString& itemId, const QString& name, const model::Script& script);
    void close();
    bool isOpen() const { return !itemId_.isEmpty(); }
    QString binId() const { return binId_; }
    QString itemId() const { return itemId_; }
    QString name() const { return name_; }
    // The script as shown now, edits and numbers included.
    model::Script script() const { return script_; }
    bool isModified() const { return modified_; }
    // After the script was saved: no asterisk.
    void markSaved();
    // The window's title: the script's name, with "*" while modified and "(modified)" while the lock's holder has saved
    // changes not read yet (p.589).
    QString title() const;
    // The script lock icon (8.3e). `holder` names who holds the lock, for its tooltip.
    void setLock(ScriptLock lock, const QString& holder = {});
    ScriptLock lock() const { return lock_; }
    // Someone else holds the lock (or the bin is protected): changes can be made but not saved, only saved as a new
    // script (p.590).
    bool isLockedByOther() const { return lock_ == ScriptLock::Others || lock_ == ScriptLock::Modified || lock_ == ScriptLock::Protected; }

    // How the Script setting shows scripts (p.1217): line numbers in the left margin, rows shaded alternately.
    void setDisplay(bool lineNumbers, bool shadedRows);
    // Script > Text Layout (p.571-572).
    void setTextLayout(const model::ScriptLayout& layout);
    model::ScriptLayout textLayout() const { return script_.layout; }

    bool editMode() const { return editMode_; }
    void setEditMode(bool on);
    bool findToolsShown() const;
    void setFindToolsShown(bool on);

    // The selected lines, [first, last]; nothing without a script.
    std::optional<std::pair<int, int>> selectedLines() const;
    void selectLines(int first, int last);
    // Add Scene / Add Page on the first selected line; an empty number removes it. False without a selection.
    bool addScene(const QString& number);
    bool addPage(const QString& number);
    // The Delete dialog's choices on the selected lines (p.574); how many numbers went.
    int deleteNumbers(bool scenes, bool pages);
    // The Scene and Page menus (p.575): selects the line the number begins at.
    bool goToScene(const QString& number);
    bool goToPage(const QString& number);
    // Find (p.575-576): the next match of the Find box's text (or `text`), selected; Replace All.
    bool findNext(bool backward = false);
    int replaceAll();
    void setFindText(const QString& find, const QString& replace = {});
    // Edits a line's text as typing would (Edit Mode only; tests).
    bool editLine(int line, const QString& text);
    // "Scene 33   Page 12   Line 4" (ours), the status bar.
    QString statusText() const;

    // Slates and takes (8.3b).
    void setSlateOptions(const ScriptSlateOptions& options);
    ScriptSlateOptions slateOptions() const { return slateOptions_; }
    // The clips a bin drag names, and a take's frame for its slate; set by MainWindow.
    using ClipResolver = std::function<std::optional<model::ScriptClip>(const QString& binId, const QString& itemId)>;
    using FrameProvider = std::function<QImage(const model::ScriptTake& take, QSize size)>;
    void setClipResolver(ClipResolver resolver) { clipResolver_ = std::move(resolver); }
    void setFrameProvider(FrameProvider provider) { frameProvider_ = std::move(provider); }
    // The clips dropped onto the selected lines (or onto slate `slateId`, as new takes of it). False without lines.
    bool linkClips(const QList<model::ScriptClip>& clips, const QString& slateId = {});
    // The selected takes, as Ctrl+click and the lasso make them; selectTakes replaces them.
    QStringList selectedTakes() const { return selectedTakes_; }
    void selectTakes(const QStringList& takeIds);
    // A slate selected with Shift+click: all its takes.
    void selectSlate(const QString& slateId);
    // Moving a slate and changing a take's lines, as the drags do.
    void moveSlate(const QString& slateId, int lines, int x, bool withTakes);
    void setTakeLines(const QString& takeId, int first, int last);
    // The selected takes' frames stepped (the arrow keys, p.581).
    void stepFrames(std::int64_t frames);
    // Set Offscreen and Set Color (p.581-582) on the selected lines of the selected takes; the colour Script > Color
    // chose.
    bool setOffscreen();
    bool setColor();
    void setIndicatorColor(const QString& color) { indicatorColor_ = color; }
    QString indicatorColor() const { return indicatorColor_; }
    // Double-click on a take's line `line` at `fraction` of the way down (p.580, p.577): takeLoadRequested.
    void loadTake(const QString& takeId, int line = -1, double fraction = 0.0);
    // The Play button (p.581): takesPlayRequested with the selected takes.
    void playSelected();
    // Where things are drawn in the text view's viewport (tests): a slate's box, a take's tab, a take's line at a script
    // line (its middle), empty when not drawn.
    QRect slateRect(const QString& slateId) const;
    QRect takeTabRect(const QString& takeId) const;
    QPoint takeLinePoint(const QString& takeId, int line) const;

    // Script marks (8.3c). The take marks go to: the one last loaded from the window (double-click), and the playing
    // clip's position as MainWindow gives it while screening.
    QString markTake() const { return markTake_; }
    using SourcePosition = std::function<std::optional<std::pair<QString, std::int64_t>>()>; // clip id, frame
    void setSourcePosition(SourcePosition position) { sourcePosition_ = std::move(position); }
    // Add Script Mark (p.582-583): the first selected line of the mark take synced to `frame`. False without either.
    bool addScriptMark(std::int64_t frame);
    // A mark selected with a click (p.586), as {take, line}; selectMark selects one (empty take: none).
    std::optional<std::pair<QString, int>> selectedMark() const { return selectedMark_; }
    void selectMark(const QString& takeId, int line);
    void moveScriptMark(const QString& takeId, int from, int to);
    // The Record button (p.583-586): screening the selected takes, else the takes over the selected lines.
    void recordSelected();
    // MainWindow says what screening plays now (empty: screening ended); the take is drawn green.
    void setRecordingTake(const QString& takeId);
    bool isRecording() const { return !recordTakes_.isEmpty(); }
    QString recordingTake() const { return recordingTake_; }
    // While screening, a click on line `line` marks the playing take there ("A script mark appears at that location in
    // the take, and the clip continues to play", p.584).
    bool markPlayingLine(int line);
    // The takes ScriptSync AI and the Play and Record buttons work on: the selected takes, else those over the selected
    // lines.
    QList<model::ScriptTake> chosenTakes() const { return takesOverSelection(); }
    // A change to the script from outside (ScriptSync AI): `edit` returns whether it changed anything; the window is then
    // marked modified.
    bool editScript(const std::function<bool(model::Script&)>& edit);
    // Find Script (p.586): selects the take and its lines.
    void showTakeLines(const QString& takeId, int first, int last);
    // Find Bin (p.586-587): findBinRequested with the selected takes.
    void findBins();
    // Ctrl+Alt+double-click (p.588): spliceRequested for the take over the selected lines (or `line` alone when it is
    // outside them).
    void spliceTake(const QString& takeId, int line);

    // Test hooks: the Add Scene/Add Page dialog's answer, and the Delete dialog's choices.
    using AskNumber = std::function<std::optional<QString>(const QString& title, const QString& current)>;
    using AskDelete = std::function<std::optional<ScriptDeleteChoice>(bool hasScenes, bool hasPages, int takes, int marks)>;
    void setAskHooks(AskNumber number, AskDelete remove) {
        askNumber_ = std::move(number);
        askDelete_ = std::move(remove);
    }
    // The AS and AP buttons and Script > Add Scene/Add Page: asks for the number, then adds it.
    void askScene();
    void askPage();
    // The Delete key out of Edit Mode.
    void askDelete();

signals:
    void modifiedChanged(bool modified);
    void titleChanged(const QString& title);
    // "Double-click any Takes tab" loads the take (p.581). With a line (>= 0, a double-click on the take's line at
    // `fraction` of the way down it) and Interpolate Position on, MainWindow puts the Source monitor at
    // model::interpolatedFrame (p.577); else at the take's representative frame.
    void takeLoadRequested(const model::ScriptTake& take, int line, double fraction);
    // "Select a take in the script, and then click the Play button ... If you select more than one take, each take plays
    // in sequence" (p.581).
    void takesPlayRequested(const QList<model::ScriptTake>& takes);
    // Alt+click on a take's tab: the Info window for its clip (p.572).
    void takeInfoRequested(const model::ScriptTake& take);
    // The Record button: screen these takes one after another (p.583).
    void recordRequested(const QList<model::ScriptTake>& takes);
    // A script mark was added (play stops after Add Script Mark, p.583).
    void scriptMarkAdded(const model::ScriptTake& take, int line);
    void findBinRequested(const QList<model::ScriptTake>& takes);
    // The script lock icon was clicked (p.589); `alt` with Alt held.
    void lockClicked(bool alt);
    // Splicing a Script Range (p.588): the take over script lines [first, last].
    void spliceRequested(const model::ScriptTake& take, int first, int last);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void refreshMenus();
    void refreshStatus();
    void setModified(bool on);
    void textEdited();
    void expandSelectionToLines();
    void refreshIndicatorButtons();

    ScriptTextView* view_ = nullptr;
    QToolButton* editButton_ = nullptr;
    QToolButton* findButton_ = nullptr;
    QWidget* findBar_ = nullptr;
    QLineEdit* find_ = nullptr;
    QLineEdit* replace_ = nullptr;
    QComboBox* scenes_ = nullptr;
    QComboBox* pages_ = nullptr;
    QLabel* status_ = nullptr;
    QToolButton* offscreenButton_ = nullptr;
    QToolButton* lockButton_ = nullptr;
    ScriptLock lock_ = ScriptLock::None;
    QToolButton* colorButton_ = nullptr;
    model::Script script_;
    QString binId_;
    QString itemId_;
    QString name_;
    bool modified_ = false;
    bool editMode_ = false;
    bool updating_ = false;
    AskNumber askNumber_;
    AskDelete askDelete_;
    ScriptSlateOptions slateOptions_;
    ClipResolver clipResolver_;
    FrameProvider frameProvider_;
    QStringList selectedTakes_;
    QString markTake_;
    SourcePosition sourcePosition_;
    std::optional<std::pair<QString, int>> selectedMark_;
    QStringList recordTakes_;
    QString recordingTake_;
    QList<model::ScriptTake> takesOverSelection() const;
    void jumpToLine(int line);
    QString indicatorColor_ = QStringLiteral("#d2c832");
    void scriptChanged();
    friend class ScriptTextView;
};

} // namespace lumenis::ui
