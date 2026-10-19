#pragma once

#include "lumenis/model/marker.h"
#include "lumenis/ui/composer.h"

#include <QDialog>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QTableWidget;

namespace lumenis::ui {

class Timeline;

// The tracks a marker can be on: a sequence's tracks and TC1, or a clip's
// source tracks. These fill the Track choices of the dialog and the Markers window's menu.
QStringList trackChoices(const model::BinItem& item);

// The Edit Marker dialog (p.413-418): name, user, colour, frame, track and
// comment. The Timeline opens it; the caller stores the result (Timeline::changeMarker).
class MarkerDialog : public QDialog {
    Q_OBJECT

public:
    // Edits a copy of `marker`; `tracks` are the choices for Track (trackChoices()). A marker on a
    // track not in `tracks` keeps it as an extra choice. The frame is shown, not editable.
    MarkerDialog(const model::Marker& marker, const QStringList& tracks, QWidget* parent = nullptr);
    // The marker as edited: the one given, with the dialog's name, user, colour, track and comment.
    model::Marker marker() const;

    // The fields, for tests.
    QLineEdit* nameEdit() const { return name_; }
    QComboBox* colorBox() const { return color_; }
    QComboBox* trackBox() const { return track_; }
    QPlainTextEdit* commentEdit() const { return comment_; }

private:
    model::Marker marker_;
    QLineEdit* name_ = nullptr;
    QLineEdit* user_ = nullptr;
    QComboBox* color_ = nullptr;
    QComboBox* track_ = nullptr;
    QPlainTextEdit* comment_ = nullptr;
};

// Tools > Markers (p.419-424): the markers of the clip or sequence in the
// active monitor. Double-click jumps to one; Delete deletes the selected
// ones; the right-click menu changes their colour or track.
//
// A tool window made by MainWindow. It refreshes whenever the Composer changes, and makes its changes
// through the Timeline (deleteMarker, changeMarker), which keeps undo for the Record monitor's
// sequence.
class MarkersWindow : public QWidget {
    Q_OBJECT

public:
    // `timeline` must outlive the window.
    explicit MarkersWindow(Timeline& timeline, QWidget* parent = nullptr);

    // The monitor whose markers are listed: the one active at the last refresh().
    MonitorId monitor() const { return monitor_; }
    QTableWidget* table() const { return table_; }
    // The markers of the clip in monitor(), sorted by frame, one per table row; empty if the
    // monitor has nothing loaded.
    QList<model::Marker> markers() const;
    // Follows the active monitor and refills the table.
    void refresh();
    // Makes monitor() active and moves its position to row `row`'s marker. Out of range: nothing.
    void jumpTo(int row);
    // Deletes the markers of the selected rows.
    void deleteSelected();
    // The right-click menu for row `row` (-1 or out of range: its commands are disabled). The
    // caller owns the menu.
    QMenu* contextMenu(int row);

    // The table's columns.
    enum Column { ColorColumn, NameColumn, UserColumn, TimecodeColumn, TrackColumn, CommentColumn };

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    Timeline& timeline_;
    MonitorId monitor_ = MonitorId::Record;
    QTableWidget* table_ = nullptr;
};

} // namespace lumenis::ui
