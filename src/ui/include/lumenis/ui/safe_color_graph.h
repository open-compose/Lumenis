#pragma once

// The Safe Color Settings graph (FX Guide p.722-723; docs/spec/color-correction.md, 6.10e4a): "colored bars that
// represent the current low and high levels for each limit type ... It also displays gray vertical lines that represent
// the default high and low levels". Five bars top to bottom, Composite (yellow), Luminance (white), Red, Green and Blue,
// "Bars on the left indicate the low limit, while bars on the right indicate the high limit": each from the graph's
// edge to its limit, so that a bar past its grey line towards the centre is "more restrictive than the default".
//
// Ours: the graph spans 8-bit video levels -40 to 295 (wide enough for the composite signal's 120 IRE) across.
// SettingsDialog shows it for the Safe Colors setting's "safeColorGraph" option and redraws it as the limits change.

#include "lumenis/model/safe_colors.h"

#include <QWidget>

#include <functional>

namespace lumenis::ui {

class SafeColorGraph : public QWidget {
    Q_OBJECT

public:
    explicit SafeColorGraph(QWidget* parent = nullptr);

    // Where the limits shown come from (the dialog's values); without it, the defaults.
    void setLimitsSource(std::function<model::SafeColorLimits()> source) { source_ = std::move(source); }
    model::SafeColorLimits limits() const { return source_ ? source_() : model::SafeColorLimits{}; }

    // The x of an 8-bit level in a graph `width` wide, and the colours of the five bars (Composite, Luminance, Red,
    // Green, Blue), for tests and the monitors' warning indicators.
    static double xOf(double level8, int width);
    static QColor barColor(int bar);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override { return {260, 70}; }

private:
    std::function<model::SafeColorLimits()> source_;
};

} // namespace lumenis::ui
