// The Color Info tool (FX Guide p.696-697). See lumenis/ui/color_info.h.

#include "lumenis/ui/color_info.h"

#include <QActionGroup>
#include <QContextMenuEvent>
#include <QCursor>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace lumenis::ui {

ColorInfoTool::ColorInfoTool(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("colorInfoTool"));
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setHorizontalSpacing(12);
    // Two columns as the guide's picture (p.697): "Video" and "Video LUT", each with its scale over R, G and B.
    for (int c = 0; c < 2; ++c) {
        auto* column = new QWidget(this);
        auto* v = new QVBoxLayout(column);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        auto* heading = new QLabel(c == 0 ? tr("Video") : tr("Video LUT"), column);
        heading->setAlignment(Qt::AlignHCenter);
        v->addWidget(heading);
        unitLabels_[c] = new QLabel(column);
        unitLabels_[c]->setObjectName(c == 0 ? QStringLiteral("colorInfoUnit") : QStringLiteral("colorInfoLutUnit"));
        v->addWidget(unitLabels_[c]);
        values_[c] = new QLabel(column);
        values_[c]->setObjectName(c == 0 ? QStringLiteral("colorInfoVideo") : QStringLiteral("colorInfoLut"));
        values_[c]->setMinimumWidth(80);
        v->addWidget(values_[c]);
        v->addStretch(1);
        grid->addWidget(column, 0, c);
        if (c == 1) lutColumn_ = column;
    }
    timer_ = new QTimer(this);
    timer_->setInterval(100);
    connect(timer_, &QTimer::timeout, this, [this] { sample(QCursor::pos()); });
    refresh();
}

void ColorInfoTool::sample(const QPoint& global) {
    color_ = sampler_ ? sampler_(global, monitorsOnly_) : std::nullopt;
    refresh();
}

void ColorInfoTool::setActiveOnlyForMonitors(bool on) {
    monitorsOnly_ = on;
    refresh();
}

void ColorInfoTool::setShowAfterDisplayColorSpace(bool on) {
    afterLut_ = on;
    refresh();
}

void ColorInfoTool::setUnit(Unit unit) {
    unit_ = unit;
    refresh();
}

QString ColorInfoTool::unitText(Unit unit) {
    switch (unit) {
    case Unit::Rgb8: return tr("RGB 8 Bit");
    case Unit::Rgb10: return tr("RGB 10 Bit");
    case Unit::Percent: return tr("RGB Percent");
    case Unit::Ire: return tr("IRE");
    case Unit::Millivolts: return tr("mVolts");
    }
    return {};
}

QString ColorInfoTool::valueText(int value, Unit unit) {
    switch (unit) {
    case Unit::Rgb8: return QString::number(value); // "on a scale from 0 to 255"
    // "on a scale from 0 to 1024" (p.697): ours, the 8-bit value scaled to 0-1023.
    case Unit::Rgb10: return QString::number(static_cast<int>(std::lround(value * 1023.0 / 255.0)));
    case Unit::Percent: return QString::number(value * 100.0 / 255.0, 'f', 1);
    // Ours: IRE and millivolts from video levels, reference black 16 at 0 and white 235 at 100 IRE or 700 mV, so
    // values outside them read below 0 and above 100 (p.697: "can be lower than 0 or higher than 100" for Percent).
    case Unit::Ire: return QString::number((value - 16) * 100.0 / 219.0, 'f', 1);
    case Unit::Millivolts: return QString::number((value - 16) * 700.0 / 219.0, 'f', 0);
    }
    return {};
}

QString ColorInfoTool::columnText(bool lut) const {
    Q_UNUSED(lut) // the Display Color Space is not applied yet: both columns show the video colour
    const QColor c = color_.value_or(QColor(0, 0, 0));
    return QStringLiteral("R: %1\nG: %2\nB: %3").arg(valueText(c.red(), unit_), valueText(c.green(), unit_), valueText(c.blue(), unit_));
}

void ColorInfoTool::refresh() {
    for (int c = 0; c < 2; ++c) {
        unitLabels_[c]->setText(unitText(unit_));
        values_[c]->setText(columnText(c == 1));
    }
    lutColumn_->setVisible(afterLut_);
}

QMenu* ColorInfoTool::shortcutMenu(QWidget* parent) {
    auto* menu = new QMenu(parent);
    // The options in the guide's picture (p.697).
    QAction* monitors = menu->addAction(tr("Active only for video monitors"));
    monitors->setObjectName(QStringLiteral("colorInfo:monitors"));
    monitors->setCheckable(true);
    monitors->setChecked(monitorsOnly_);
    connect(monitors, &QAction::toggled, this, &ColorInfoTool::setActiveOnlyForMonitors);
    QAction* lut = menu->addAction(tr("Show video color after applying Display Color Space"));
    lut->setObjectName(QStringLiteral("colorInfo:lut"));
    lut->setCheckable(true);
    lut->setChecked(afterLut_);
    connect(lut, &QAction::toggled, this, &ColorInfoTool::setShowAfterDisplayColorSpace);
    menu->addSeparator();
    auto* group = new QActionGroup(menu);
    for (Unit u : {Unit::Rgb8, Unit::Rgb10, Unit::Percent, Unit::Ire, Unit::Millivolts}) {
        static const char* const names[] = {QT_TR_NOOP("RGB 8 bit"), QT_TR_NOOP("RGB 10 bit"), QT_TR_NOOP("RGB Percent"), QT_TR_NOOP("IRE"),
                                            QT_TR_NOOP("mVolts")};
        QAction* a = menu->addAction(tr(names[static_cast<int>(u)]));
        a->setObjectName(QStringLiteral("colorInfo:unit%1").arg(static_cast<int>(u)));
        a->setCheckable(true);
        a->setChecked(u == unit_);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, u] { setUnit(u); });
    }
    return menu;
}

void ColorInfoTool::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = shortcutMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

void ColorInfoTool::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    timer_->start();
}

void ColorInfoTool::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    timer_->stop();
}

} // namespace lumenis::ui
