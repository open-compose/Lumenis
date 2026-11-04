#pragma once

#include "lumenis/model/color_correction.h"

#include <QWidget>

#include <array>
#include <optional>

class QButtonGroup;
class QDoubleSpinBox;
class QLabel;
class QStackedWidget;
class QToolButton;

namespace lumenis::ui {

class Timeline;
class ChromaWheel;
class LumaRangesGraph;

// The Color Correction tool (FX Guide p.595-605, p.631-644; docs/spec/color-correction.md, 6.10b2): the Correction Type
// button, the group tabs (HSL, Channels, Levels, Curves, Sec) and the HSL group's subdividing tabs, every one with its
// Enable button, and the HSL group's panes: the Controls tab's sliders for the Master, Highlights, Midtones and Shadows
// ranges, the Hue Offsets tab's ChromaWheels with Gain, Gamma and Setup, and the Luma Ranges graph. The other groups'
// panes come in 6.10b3.
//
// The tool shows the correction of the segment at the position on the Timeline's correction track (Timeline::
// currentCorrection) and changes it through Timeline::setCurrentCorrection, an undo step each time a control is let go
// of (ours: the monitors show the change then). MainWindow shows it as the "ColorCorrection" panel while the Timeline
// is in Color Correction mode.
class CorrectionTool : public QWidget {
    Q_OBJECT

public:
    explicit CorrectionTool(Timeline& timeline, QWidget* parent = nullptr);

    // Reads the correction at the position again (the position moved, the sequence changed).
    void reload();
    const model::ColorCorrection& correction() const { return correction_; }

    // The tabs shown: the group ("hsl", "channels", "levels", "curves", "secondary"), the HSL group's subdividing tab
    // ("controls", "hueOffsets", "lumaRanges") and the second level ("master", "highlights", "midtones", "shadows").
    QString group() const { return group_; }
    void showGroup(const QString& group);
    QString hslTab() const { return hslTab_; }
    void showHslTab(const QString& tab);
    QString rangeTab() const { return range_; }
    void showRangeTab(const QString& range);
    // The path of the tab shown at the deepest level ("hsl/controls/midtones"), whose controls Alt+click resets.
    QString shownPath() const;

    // An Enable button clicked (p.603-605): on and off, or with Alt "All controls linked to that button return to their
    // default values" when its controls are displayed; a tab whose controls are not displayed is displayed instead.
    void clickEnable(const QString& path, bool alt);
    // Whether the button at `path` is lit: on, and something under it set (ours).
    bool enableLit(const QString& path) const;

    // Sets a control and commits it as an undo step.
    void setControl(const QString& path, double value);
    // The Luma Ranges tab's preview (p.642-643).
    enum class LumaPreview { Input, ThreeTone, Output };
    LumaPreview lumaPreview() const { return lumaPreview_; }
    void setLumaPreview(LumaPreview preview);

    QWidget* control(const QString& path) const; // a slider row, wheel or graph by its path (for tests)
    QToolButton* typeButton() const { return type_; }
    QToolButton* enableButton(const QString& path) const;
    QToolButton* tabButton(const QString& path) const;
    ChromaWheel* wheel(const QString& range) const;
    LumaRangesGraph* lumaGraph() const { return lumaGraph_; }

signals:
    // The Luma Ranges preview changed, for the monitors.
    void lumaPreviewChanged(LumaPreview preview);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QWidget* tab(QWidget* parent, const QString& path, const QString& text);
    QWidget* controlsPane();
    QWidget* hueOffsetsPane();
    QWidget* lumaRangesPane();
    QWidget* sliderRow(QWidget* parent, const QString& path, const QString& label);
    void commit(const QString& step);
    void sync();
    void syncControls();

    Timeline& timeline_;
    model::ColorCorrection correction_;
    bool hasSegment_ = false;
    QString group_ = QStringLiteral("hsl");
    QString hslTab_ = QStringLiteral("hueOffsets"); // "The default display shows the controls in the Hue Offsets tab" (p.595)
    QString range_ = QStringLiteral("master");
    LumaPreview lumaPreview_ = LumaPreview::Output;
    QToolButton* type_ = nullptr;
    QStackedWidget* groups_ = nullptr;
    QStackedWidget* hslPages_ = nullptr;
    QWidget* rangeColumn_ = nullptr;
    QWidget* lumaColumn_ = nullptr;
    QStackedWidget* controlPages_ = nullptr;
    LumaRangesGraph* lumaGraph_ = nullptr;
    QDoubleSpinBox* lumaIn_ = nullptr;
    QDoubleSpinBox* lumaOut_ = nullptr;
};

// A ChromaWheel (p.635-638): hue around the circumference with red near the top, as on a vectorscope, saturation (the
// Amount, 0 to 100) along the radius; a crosshair pointer of its own shape for each wheel. Dragging moves the pointer;
// with Shift it moves a quarter as far for "more precise control ... in the central area" (ours: the fraction).
class ChromaWheel : public QWidget {
    Q_OBJECT

public:
    ChromaWheel(int pointer, QWidget* parent = nullptr);
    void setOffset(double hue, double amount);
    double hue() const { return hue_; }
    double amount() const { return amount_; }
    // Where a hue and amount fall in the widget, and back.
    QPointF pointFor(double hue, double amount) const;
    std::pair<double, double> offsetAt(const QPointF& point) const;
    QSize sizeHint() const override { return {120, 120}; }

signals:
    void moved(double hue, double amount);    // while dragging
    void released(double hue, double amount); // let go: commit

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int pointer_;
    double hue_ = 0, amount_ = 0;
    bool dragging_ = false;
    QPointF last_;
};

// The Luma Ranges graph (p.641-643): brightness 0-255 across, percent up, the three ranges' curves, the active one with
// its control points. Clicking the active curve adds a point, dragging moves it, Delete removes the selected one.
class LumaRangesGraph : public QWidget {
    Q_OBJECT

public:
    explicit LumaRangesGraph(QWidget* parent = nullptr);
    void setCurves(const std::array<QList<QPointF>, 3>& curves, int active); // shadows, midtones, highlights
    const QList<QPointF>& activeCurve() const { return curves_.at(static_cast<std::size_t>(active_)); }
    int selectedPoint() const { return selected_; }
    void selectPoint(int index);
    QPointF toWidget(const QPointF& value) const;
    QPointF toValue(const QPointF& point) const;
    QSize sizeHint() const override { return {260, 150}; }

signals:
    void curveChanged(const QList<QPointF>& curve); // let go: commit
    void selectionChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    std::array<QList<QPointF>, 3> curves_;
    int active_ = 2;
    int selected_ = -1;
    bool dragging_ = false;
};

} // namespace lumenis::ui
