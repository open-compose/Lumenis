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
    // The window's title: the script's name, with "*" while modified.
    QString title() const;

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
    // Where things are drawn in the text view's viewport (tests): a slate's box, a take's tab.
    QRect slateRect(const QString& slateId) const;
    QRect takeTabRect(const QString& takeId) const;

    // Test hooks: the Add Scene/Add Page dialog's answer, and the Delete dialog's choices.
    using AskNumber = std::function<std::optional<QString>(const QString& title, const QString& current)>;
    using AskDelete = std::function<std::optional<ScriptDeleteChoice>(bool hasScenes, bool hasPages, int takes)>;
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
    QString indicatorColor_ = QStringLiteral("#d2c832");
    void scriptChanged();
    friend class ScriptTextView;
};

} // namespace lumenis::ui
