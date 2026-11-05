// The Safe Color Settings graph (FX Guide p.722-723). See lumenis/ui/safe_color_graph.h.

#include "lumenis/ui/safe_color_graph.h"

#include <QPainter>

#include <algorithm>
#include <array>

namespace lumenis::ui {

namespace {

constexpr double kFrom = -40.0; // the graph's span, in 8-bit video levels (ours)
constexpr double kTo = 295.0;

} // namespace

SafeColorGraph::SafeColorGraph(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("safeColorGraph"));
    setMinimumSize(200, 60);
}

double SafeColorGraph::xOf(double level8, int width) {
    return std::clamp((level8 - kFrom) / (kTo - kFrom), 0.0, 1.0) * width;
}

QColor SafeColorGraph::barColor(int bar) {
    // "The colors are the same as those used to display safe color warnings in the monitors" (p.723): Composite
    // yellow, Luminance white (p.724, p.729), then red, green and blue.
    static const std::array<QColor, 5> colors{QColor(240, 220, 40), QColor(240, 240, 240), QColor(230, 40, 40), QColor(40, 200, 60),
                                              QColor(50, 90, 240)};
    return colors.at(static_cast<std::size_t>(std::clamp(bar, 0, 4)));
}

void SafeColorGraph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    const model::SafeColorLimits now = limits();
    const model::SafeColorLimits defaults;
    const std::array<model::SafeRange, 5> current{now.composite, now.luma, now.rgb, now.rgb, now.rgb};
    const std::array<model::SafeRange, 5> standard{defaults.composite, defaults.luma, defaults.rgb, defaults.rgb, defaults.rgb};
    const int rows = 5;
    const double rowHeight = static_cast<double>(height() - 4) / rows;
    for (int i = 0; i < rows; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const double y = 2.0 + i * rowHeight;
        const double h = std::max(rowHeight - 2.0, 1.0);
        const double low = xOf(current.at(k).low8(), width());
        const double high = xOf(current.at(k).high8(), width());
        p.fillRect(QRectF(0.0, y, low, h), barColor(i));
        p.fillRect(QRectF(high, y, width() - high, h), barColor(i));
        // The default limits as grey lines.
        p.setPen(QPen(QColor(150, 150, 150), 2));
        for (const double level : {standard.at(k).low8(), standard.at(k).high8()}) {
            const double x = xOf(level, width());
            p.drawLine(QPointF(x, y), QPointF(x, y + h));
        }
    }
}

} // namespace lumenis::ui
