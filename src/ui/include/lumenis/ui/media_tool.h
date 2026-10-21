#pragma once

#include "lumenis/media/managed.h"
#include "lumenis/model/bin.h"

#include <QDialog>
#include <QPair>
#include <QWidget>

#include <functional>

class QCheckBox;
class QListWidget;
class QTableWidget;

namespace lumenis::ui {

// What the Media Tool shows (the Media Tool Display dialog, p.346).
struct MediaToolDisplay {
    QStringList drives;      // the drives' roots; empty: all drives
    // Every drive offered, selected or not, when `drives` is not empty: a file linked in place
    // belongs to the deepest of these roots that holds it (a volume mounted inside another).
    QStringList knownDrives;
    bool allProjects = false; // else the current project's media only
    QString project;          // the current project
    bool masterClips = true;       // managed master clips
    bool linkedMasterClips = true; // master clips linked to files in place
    bool renderedEffects = false;  // Precompute Clips - Rendered Effects
    bool titlesAndMattes = false;  // Precompute Clips - Titles and Matte Keys
    bool mediaFiles = false;       // the media database's files, a row each
};

// The Media Tool Display dialog (picture on p.346): the media drives and
// projects to look at, and the kinds of objects to show.
class MediaToolDisplayDialog : public QDialog {
    Q_OBJECT

public:
    // `drives`: name and root. `projects`: those with media on the drives;
    // the current project comes first (p.346).
    MediaToolDisplayDialog(const QString& currentProject, const QList<QPair<QString, QString>>& drives,
                           const QStringList& projects, const MediaToolDisplay& display, QWidget* parent = nullptr);
    // The choices. Every drive selected gives an empty drive list (all drives); OK is disabled while
    // no drive is selected, since the tool loads only the drives selected (p.345). Every project
    // selected, when there is more than the current one, gives allProjects.
    MediaToolDisplay display() const;
    QListWidget* driveList() const { return drives_; }
    QListWidget* projectList() const { return projects_; }
    // A check box by object name: "masterClips", "linkedMasterClips", "renderedEffects",
    // "titlesAndMattes" or "mediaFiles". Null for others.
    QCheckBox* option(const QString& objectName) const;

private:
    QString current_;
    QListWidget* drives_ = nullptr;
    QListWidget* projects_ = nullptr;
    QCheckBox* master_ = nullptr;
    QCheckBox* linked_ = nullptr;
    QCheckBox* rendered_ = nullptr;
    QCheckBox* titles_ = nullptr;
    QCheckBox* media_ = nullptr;
};

// The Delete Media dialog (p.349): which of the selection's media files to
// delete, by track.
class DeleteMediaDialog : public QDialog {
    Q_OBJECT

public:
    // `tracks`: "V1", "A1", ... that the selection has media for.
    // A box per track, all checked; the other kinds of file on p.349 are disabled.
    DeleteMediaDialog(const QStringList& tracks, QWidget* parent = nullptr);
    // The tracks whose boxes are checked, in the order given.
    QStringList chosenTracks() const;
    // The box of track `name`; null if there is none.
    QCheckBox* track(const QString& name) const;

private:
    QList<QCheckBox*> tracks_;
};

// Tools > Media Tool (p.344-348): the project's master clips and the media
// files on the chosen drives, in a bin-like list (picture on p.347: Color,
// Name, Duration, Drive, IN-OUT, Creation Date).
//
// A panel's contents ("MediaTool"), made by MainWindow. MainWindow opens the Media Tool Display
// dialog, fills the tool with setItems() from the project's bins and the media database
// (media::MediaDatabase in src/media/managed.*), and provides `deleter`.
class MediaTool : public QWidget {
    Q_OBJECT

public:
    explicit MediaTool(QWidget* parent = nullptr);

    // The project's bins' items and the media files on the drives, as the
    // display options say.
    void setItems(const QList<model::BinItem>& items, const MediaToolDisplay& display,
                  const QList<media::MediaFileInfo>& files = {});
    // The display options last given to setItems().
    const MediaToolDisplay& display() const { return display_; }
    QTableWidget* table() const { return table_; }
    // The Name column of every row, in the order shown.
    QStringList names() const;
    // The column headings, in order.
    static QStringList columns();
    // Edit > Delete in the Media Tool (p.347-348): the Delete Media dialog
    // for the selected rows' media files, a confirmation, then `deleter`.
    // False when nothing was deleted.
    bool deleteSelected();
    // What the last deleteSelected() reported, for the caller to show; empty before.
    QString lastMessage() const { return message_; }
    // Selects the rows whose names are in `names`, and only those.
    void selectNames(const QStringList& names);

    // Deletes files and returns those deleted (the main window's, with
    // the media database).
    std::function<QStringList(const QStringList&)> deleter;
    // Stand in for the dialogs (tests): the Delete Media dialog (true: OK)
    // and the confirmation of `count` files (true: Delete).
    std::function<bool(DeleteMediaDialog&)> deleteMediaHook;
    std::function<bool(int count)> confirmHook;

signals:
    // After deleteSelected(): the files `deleter` deleted; not emitted when none were.
    void mediaDeleted(const QStringList& paths);

protected:
    // Delete or Backspace: deleteSelected().
    void keyPressEvent(QKeyEvent* event) override;

private:
    QTableWidget* table_ = nullptr;
    MediaToolDisplay display_;
    QString message_;
};

} // namespace lumenis::ui
