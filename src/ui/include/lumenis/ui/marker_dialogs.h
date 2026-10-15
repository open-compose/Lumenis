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

// The tracks a marker can be on: a sequence's tracks and TC1, or a clip's.
QStringList trackChoices(const model::BinItem& item);

// The Edit Marker dialog (p.413-418): name, user, colour, frame, track and
// comment.
class MarkerDialog : public QDialog {
    Q_OBJECT

public:
    MarkerDialog(const model::Marker& marker, const QStringList& tracks, QWidget* parent = nullptr);
    model::Marker marker() const;

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
class MarkersWindow : public QWidget {
    Q_OBJECT

public:
    explicit MarkersWindow(Timeline& timeline, QWidget* parent = nullptr);

    MonitorId monitor() const { return monitor_; }
    QTableWidget* table() const { return table_; }
    QList<model::Marker> markers() const;
    void refresh();
    void jumpTo(int row);
    void deleteSelected();
    QMenu* contextMenu(int row);

    enum Column { ColorColumn, NameColumn, UserColumn, TimecodeColumn, TrackColumn, CommentColumn };

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    Timeline& timeline_;
    MonitorId monitor_ = MonitorId::Record;
    QTableWidget* table_ = nullptr;
};

} // namespace lumenis::ui
