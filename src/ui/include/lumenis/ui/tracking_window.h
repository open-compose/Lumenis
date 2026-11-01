#pragma once

#include <QList>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

namespace lumenis::ui {

class EffectEditor;

// One tracker's timeline in the Tracking window (FX Guide p.254-255, p.267-268): the length of the effect, a
// black line at the top where the tracker has data, a red bar at the bottom where it has none, a vertical bar
// where each region after the first starts, a red diamond at the reference frame and the position indicator.
class TrackerTimeline : public QWidget {
public:
    TrackerTimeline(EffectEditor& editor, int index, QWidget* parent);
    int index() const { return index_; }
    QSize sizeHint() const override { return {240, 22}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    EffectEditor& editor_;
    int index_;
};

// The Tracking window (FX Guide p.253-255, p.262-269; docs/spec/tracking.md): the Setup Tracking controls (Start
// Tracking, New Tracker, Tracking Engine, Track Background/Foreground), the Display Tracking Data menus (Point
// Range, Display), and one row per tracker of the shown effect: its Enable Tracker, Offset Tracking, SteadyGlide
// and Smoothing buttons, Smoothing Value, Tracker Color box, Tracker Name, timeline and Close button. Everything it
// changes goes through the Effect Editor (one undo step each). Made by the Effect Editor, which refreshes it.
class TrackingWindow : public QWidget {
    Q_OBJECT

public:
    // `editor` must outlive the window.
    explicit TrackingWindow(EffectEditor& editor, QWidget* parent = nullptr);

    // Rebuilds the tracker rows when the trackers changed, and brings every control up to date.
    void refresh();

    QToolButton* startButton() const { return start_; }
    QToolButton* newTrackerButton() const { return newTracker_; }
    QComboBox* engineMenu() const { return engine_; }
    QComboBox* trackMenu() const { return track_; }
    QComboBox* pointRangeMenu() const { return pointRange_; }
    QComboBox* displayMenu() const { return display_; }
    // A tracker row's controls by kind ("enable", "offset", "steadyglide", "smoothing", "value", "color", "name",
    // "timeline", "close") and the tracker's index; null when there is no such row.
    QWidget* control(const QString& kind, int index) const;

private:
    EffectEditor& editor_;
    QToolButton* start_ = nullptr;
    QToolButton* newTracker_ = nullptr;
    QComboBox* engine_ = nullptr;
    QComboBox* track_ = nullptr;
    QComboBox* pointRange_ = nullptr;
    QComboBox* display_ = nullptr;
    QVBoxLayout* rows_ = nullptr;
    QStringList shown_; // the tracker names the rows were built for
    bool syncing_ = false;
    void rebuildRows();
};

} // namespace lumenis::ui
