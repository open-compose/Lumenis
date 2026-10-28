#pragma once

#include "lumenis/model/monitor.h"
#include "lumenis/ui/composer.h"

#include <QList>
#include <QWidget>

class QMenu;
class QVBoxLayout;

namespace lumenis::ui {

// The Timecode window (Tools > Timecode Window; Editing Guide p.388-389; docs/spec/timecode-window.md):
// "up to 48 lines of timecode in a separate window". Each line shows one tracking format of the
// sequence (the Record monitor) or the source (the Source monitor), or nothing; clicking a line opens
// the Timecode menu: Sequence and Source with their formats, None, Add Line, Size, and the
// sequence's values. MainWindow opens it, one window, kept.
class TimecodeWindow : public QWidget {
    Q_OBJECT

public:
    // `composer` must outlive the window.
    explicit TimecodeWindow(Composer& composer, QWidget* parent = nullptr);

    // A line: which monitor and format it shows; format None shows nothing.
    struct Line {
        MonitorId monitor = MonitorId::Record;
        model::TrackingFormat format = model::TrackingFormat::None;
        bool operator==(const Line&) const = default;
    };
    static constexpr int kMaxLines = 48;
    const QList<Line>& lines() const { return lines_; }
    void setLine(int index, const Line& line);
    // Add Line: a new line showing nothing; false at kMaxLines.
    bool addLine();
    // Size > font size, in points.
    void setFontSize(int points);
    int fontSize() const { return fontSize_; }
    static const QList<int>& fontSizes();
    // A line's label and value as shown ("Mas", "01:00:05:24"); both empty for None.
    QString labelText(int index) const;
    QString valueText(int index) const;
    // The Timecode menu for line `index` (p.389). The caller owns it.
    QMenu* menuFor(int index);

public slots:
    // Shows the current values.
    void refresh();

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void rebuild();
    int lineAt(const QPoint& pos) const;

    Composer& composer_;
    QList<Line> lines_;
    int fontSize_ = 14;
    QVBoxLayout* layout_ = nullptr;
    QList<QWidget*> rows_;
};

} // namespace lumenis::ui
