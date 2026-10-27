#pragma once

#include "lumenis/model/sequence.h"

#include <QJsonObject>
#include <QWidget>

#include <optional>

class QLineEdit;
class QMenu;
class QTableWidget;
class QToolButton;

namespace lumenis::ui {

class Timeline;

// The Timeline Clip Notes window (Tools > Timeline Clip Notes; Editing Guide p.483-486;
// docs/spec/clip-notes.md): every note added to the clips of the Record monitor's sequence
// (Timeline::setClipNote), one row each. Notes are edited in place and deleted from the window;
// columns can be chosen, sorted and dragged; Gang mode selects the clips in the Timeline; Quick Find
// shows only the rows containing its text; Export to Text writes a tab-delimited file. MainWindow
// opens it, one window, kept.
class ClipNotesWindow : public QWidget {
    Q_OBJECT

public:
    // `timeline` must outlive the window.
    explicit ClipNotesWindow(Timeline& timeline, QWidget* parent = nullptr);

    // The columns, as Choose Columns lists them (the picture on p.484, and Frame, p.485).
    enum Column { FrameColumn, NameColumn, StartColumn, TrackColumn, EndColumn, DurationColumn, NoteColumn, ColumnCount };
    static QString columnName(int column);

    QTableWidget* table() const { return table_; }
    QToolButton* gangButton() const { return gang_; }
    QLineEdit* quickFind() const { return find_; }
    // Refills the rows from the record sequence, keeping the sort, the columns, Quick Find and the
    // selected rows.
    void refresh();
    // The clip a row stands for; nothing out of range.
    std::optional<model::SegmentRef> segmentAt(int row) const;
    // Deletes the notes of the selected rows (p.484).
    void deleteSelected();
    // Choose Columns (p.484): which columns show.
    void setColumnShown(int column, bool shown);
    bool isColumnShown(int column) const;
    // Show as Frames or Show as Timecode for the Duration column (p.484).
    void setDurationAsFrames(bool frames);
    bool durationAsFrames() const { return durationFrames_; }
    // The Frame column's pictures: Enlarge Frame (+1) and Reduce Frame (-1), Ctrl+L and Ctrl+K
    // (p.485-486). The height is 18 to 144 pixels, doubling or halving (ours).
    void resizeFrames(int steps);
    int frameHeight() const { return frameHeight_; }
    // Export to Text (p.485): the shown rows and columns, in their order on screen, tab-delimited,
    // headings first; the Frame column is left out. False if the file cannot be written.
    bool exportText(const QString& path) const;
    // The right-click and Fast menus (p.484-486); the caller owns them.
    QMenu* contextMenu(int row);
    QMenu* fastMenu();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyFind();
    void chooseColumns();
    void setFont();
    // The selected rows' clips, in Gang mode, become the Timeline's selection (p.485).
    void gangSelection();

    Timeline& timeline_;
    QTableWidget* table_ = nullptr;
    QToolButton* fast_ = nullptr;
    QToolButton* gang_ = nullptr;
    QLineEdit* find_ = nullptr;
    bool durationFrames_ = false;
    int frameHeight_ = 36;
    bool filling_ = false;
    // What the rows were made from (the record sequence's id, composition and start), so that a
    // change of position or playback does not refill them.
    QString shownId_;
    QJsonObject shownComposition_;
    std::int64_t shownStart_ = 0;
};

} // namespace lumenis::ui
