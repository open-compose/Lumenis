#pragma once

#include "lumenis/model/bin.h"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QListWidget;
class QTableWidget;

namespace lumenis::ui {

// What the Media Tool shows (the Media Tool Display dialog, p.346).
struct MediaToolDisplay {
    QStringList drives;   // empty: all drives
    bool allProjects = false;
    bool masterClips = true;
    bool linkedMasterClips = true;
    bool renderedEffects = false;  // Precompute Clips - Rendered Effects
    bool titlesAndMattes = false;  // Precompute Clips - Titles and Matte Keys
    bool mediaFiles = false;
};

// The Media Tool Display dialog (picture on p.346): the media drives and
// projects to look at, and the kinds of objects to show.
class MediaToolDisplayDialog : public QDialog {
    Q_OBJECT

public:
    MediaToolDisplayDialog(const QString& currentProject, const QStringList& drives, const MediaToolDisplay& display,
                           QWidget* parent = nullptr);
    MediaToolDisplay display() const;
    QListWidget* driveList() const { return drives_; }
    QListWidget* projectList() const { return projects_; }
    QCheckBox* option(const QString& objectName) const;

private:
    QListWidget* drives_ = nullptr;
    QListWidget* projects_ = nullptr;
    QCheckBox* master_ = nullptr;
    QCheckBox* linked_ = nullptr;
    QCheckBox* rendered_ = nullptr;
    QCheckBox* titles_ = nullptr;
    QCheckBox* media_ = nullptr;
};

// Tools > Media Tool (p.344-348): the project's master clips, precompute
// clips and media files on the chosen drives, in a bin-like list (picture
// on p.347: Color, Name, Duration, Drive, IN-OUT, Creation Date). There is
// no media on disk until Phase 2, so the list holds master clips and
// deleting media is not available yet.
class MediaTool : public QWidget {
    Q_OBJECT

public:
    explicit MediaTool(QWidget* parent = nullptr);

    // The project's bins' items, filtered by the display options.
    void setItems(const QList<model::BinItem>& items, const MediaToolDisplay& display);
    const MediaToolDisplay& display() const { return display_; }
    QTableWidget* table() const { return table_; }
    QStringList names() const;
    static QStringList columns();
    // Edit > Delete in the Media Tool (p.347); false while there are no
    // media files.
    bool deleteSelected();
    QString lastMessage() const { return message_; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QTableWidget* table_ = nullptr;
    MediaToolDisplay display_;
    QString message_;
};

} // namespace lumenis::ui
