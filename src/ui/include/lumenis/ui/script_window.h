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
// * Changes mark the window modified (an asterisk in its title, p.570); MainWindow saves the script into its bin item
//   (saveRequested, File > Save Script).

#include "lumenis/model/script.h"

#include <QWidget>

#include <functional>
#include <optional>
#include <utility>

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace lumenis::ui {

class ScriptTextView;

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

    // Test hooks: the Add Scene/Add Page dialog's answer, and the Delete dialog's {scenes, pages}.
    using AskNumber = std::function<std::optional<QString>(const QString& title, const QString& current)>;
    using AskDelete = std::function<std::optional<std::pair<bool, bool>>(bool hasScenes, bool hasPages)>;
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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void refreshMenus();
    void refreshStatus();
    void setModified(bool on);
    void textEdited();
    void expandSelectionToLines();

    ScriptTextView* view_ = nullptr;
    QToolButton* editButton_ = nullptr;
    QToolButton* findButton_ = nullptr;
    QWidget* findBar_ = nullptr;
    QLineEdit* find_ = nullptr;
    QLineEdit* replace_ = nullptr;
    QComboBox* scenes_ = nullptr;
    QComboBox* pages_ = nullptr;
    QLabel* status_ = nullptr;
    model::Script script_;
    QString binId_;
    QString itemId_;
    QString name_;
    bool modified_ = false;
    bool editMode_ = false;
    bool updating_ = false;
    AskNumber askNumber_;
    AskDelete askDelete_;
    friend class ScriptTextView;
};

} // namespace lumenis::ui
