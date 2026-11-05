// The Color Correction tool's shape panel (correction_tool.h; FX Guide p.697-699; docs/spec/color-correction.md,
// 6.10e3b): shown with CC Effect chosen in the Correction Type menu, left of the group panes as the guide's picture
// (p.698) has it. The shape tools in a column (Selection, Rectangle, Oval, Polygon, Curve, Brush), the Shapes and Base
// layer buttons with the Show Alpha button beside Shapes, the Serial Mode, Show Result and Invert Alpha boxes and the
// Softness slider with its box and arrows. The layer and the two Show buttons are the Timeline's display state
// (Timeline::setCorrectionLayer ...); Serial Mode, Invert Alpha and Softness are kept on the effect as undo steps.

#include "lumenis/ui/correction_tool.h"

#include "lumenis/ui/correction_shape_host.h"
#include "lumenis/ui/timeline.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {

// Our own drawings for the shape tools and Show Alpha (not Avid's artwork): an arrow, a rectangle, an oval, a
// polygon, a closed curve and a brush; Show Alpha a square half filled.
QIcon shapeIcon(const QString& key, const QColor& ink) {
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ink, 1.3));
    if (key == QLatin1String("selection")) {
        p.setBrush(ink);
        p.drawPolygon(QPolygonF{QPointF(5, 2), QPointF(5, 15), QPointF(8.5, 11.5), QPointF(11, 16), QPointF(13, 15), QPointF(10.5, 10.5), QPointF(14, 10)});
    } else if (key == QLatin1String("rectangle")) {
        p.drawRect(QRectF(3, 4.5, 12, 9));
    } else if (key == QLatin1String("oval")) {
        p.drawEllipse(QRectF(2.5, 4.5, 13, 9));
    } else if (key == QLatin1String("polygon")) {
        p.drawPolygon(QPolygonF{QPointF(3, 14), QPointF(5, 4), QPointF(15, 3), QPointF(12, 9), QPointF(15, 14)});
    } else if (key == QLatin1String("curve")) {
        QPainterPath path(QPointF(4, 10));
        path.cubicTo(QPointF(2, 3), QPointF(11, 1), QPointF(13, 6));
        path.cubicTo(QPointF(17, 10), QPointF(12, 16), QPointF(8, 14));
        path.cubicTo(QPointF(5, 13), QPointF(6, 11), QPointF(4, 10));
        p.drawPath(path);
    } else if (key == QLatin1String("brush")) {
        p.drawLine(QPointF(12, 2), QPointF(9, 10));
        p.setBrush(ink);
        p.drawEllipse(QRectF(5, 9.5, 6, 6));
    } else if (key == QLatin1String("alpha")) {
        p.drawRect(QRectF(3, 3, 12, 12));
        p.setBrush(ink);
        p.drawPolygon(QPolygonF{QPointF(3, 15), QPointF(15, 3), QPointF(15, 15)});
    }
    return QIcon(pm);
}

} // namespace

QWidget* CorrectionTool::shapesPane() {
    auto* pane = new QWidget(this);
    pane->setObjectName(QStringLiteral("shapesPanel"));
    auto* row = new QHBoxLayout(pane);
    row->setContentsMargins(0, 0, 4, 0);
    row->setSpacing(6);
    // The shape tools, top to bottom as the picture (p.698): they draw in the Current monitor (6.10e3b2).
    auto* tools = new QVBoxLayout;
    tools->setSpacing(1);
    shapeTools_ = new QButtonGroup(pane);
    shapeTools_->setExclusive(true);
    const std::pair<const char*, const char*> kinds[] = {{"selection", QT_TR_NOOP("Selection")}, {"rectangle", QT_TR_NOOP("Rectangle")},
                                                         {"oval", QT_TR_NOOP("Oval")},           {"polygon", QT_TR_NOOP("Polygon")},
                                                         {"curve", QT_TR_NOOP("Curve")},         {"brush", QT_TR_NOOP("Brush")}};
    int id = 0;
    for (const auto& [key, text] : kinds) {
        auto* b = new QToolButton(pane);
        b->setObjectName(QStringLiteral("shapes:tool:") + QLatin1String(key));
        b->setToolTip(tr(text));
        b->setCheckable(true);
        b->setIcon(shapeIcon(QLatin1String(key), palette().color(QPalette::WindowText)));
        b->setFixedSize(26, 22);
        shapeTools_->addButton(b, id++);
        tools->addWidget(b);
    }
    shapeTools_->button(0)->setChecked(true);
    // The buttons' order is IntraframeTool::Tool's first six (Selection, Rectangle, Oval, Polygon, Curve, Brush).
    shapeHost_ = new CorrectionShapeHost(timeline_, this);
    connect(shapeTools_, &QButtonGroup::idClicked, this, [this](int tool) {
        shapeHost_->tool()->setTool(static_cast<IntraframeTool::Tool>(tool));
        emit shapeToolChosen(tool);
    });
    // The drawing tools return to Selection after a shape (p.396): the buttons follow the tool.
    connect(shapeHost_->tool(), &IntraframeTool::changed, this, [this] {
        const int tool = static_cast<int>(shapeHost_->tool()->tool());
        if (QAbstractButton* b = shapeTools_->button(tool); b && !b->isChecked()) b->setChecked(true);
    });
    connect(shapeHost_, &CorrectionShapeHost::selectionChanged, this, &CorrectionTool::reload);
    tools->addStretch(1);
    row->addLayout(tools);

    auto* right = new QGridLayout;
    right->setVerticalSpacing(3);
    // "Shapes - selects the shape layer ...; Base - sets the base layer" (p.698), one of them on.
    const auto layerButton = [&](const QString& name, const QString& text) {
        auto* b = new QToolButton(pane);
        b->setObjectName(name);
        b->setText(text);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setMinimumWidth(80);
        return b;
    };
    shapesLayer_ = layerButton(QStringLiteral("shapes:layerShapes"), tr("Shapes"));
    baseLayer_ = layerButton(QStringLiteral("shapes:layerBase"), tr("Base"));
    connect(shapesLayer_, &QToolButton::clicked, this, [this] { setShapeLayer(model::CorrectionLayer::Shapes); });
    connect(baseLayer_, &QToolButton::clicked, this, [this] { setShapeLayer(model::CorrectionLayer::Base); });
    showAlpha_ = new QToolButton(pane);
    showAlpha_->setObjectName(QStringLiteral("shapes:showAlpha"));
    showAlpha_->setToolTip(tr("Show Alpha"));
    showAlpha_->setCheckable(true);
    showAlpha_->setIcon(shapeIcon(QStringLiteral("alpha"), palette().color(QPalette::WindowText)));
    connect(showAlpha_, &QToolButton::toggled, this, [this](bool on) {
        timeline_.setShowShapeAlpha(on);
        emit shapeDisplayChanged();
    });
    right->addWidget(shapesLayer_, 0, 0);
    right->addWidget(showAlpha_, 0, 1);
    right->addWidget(baseLayer_, 1, 0);
    serialMode_ = new QCheckBox(tr("Serial Mode"), pane);
    serialMode_->setObjectName(QStringLiteral("shapes:serial"));
    showResult_ = new QCheckBox(tr("Show Result"), pane);
    showResult_->setObjectName(QStringLiteral("shapes:showResult"));
    invertAlpha_ = new QCheckBox(tr("Invert Alpha"), pane);
    invertAlpha_->setObjectName(QStringLiteral("shapes:invertAlpha"));
    const auto setOption = [this](bool serial, bool on) {
        model::CorrectionShapeOptions o = timeline_.correctionShapeOptions();
        (serial ? o.serial : o.invertAlpha) = on;
        timeline_.setCorrectionShapeOptions(o);
        reload();
    };
    connect(serialMode_, &QCheckBox::clicked, this, [setOption](bool on) { setOption(true, on); });
    connect(invertAlpha_, &QCheckBox::clicked, this, [setOption](bool on) { setOption(false, on); });
    connect(showResult_, &QCheckBox::toggled, this, [this](bool on) {
        timeline_.setShowShapeResult(on);
        emit shapeDisplayChanged();
    });
    right->addWidget(serialMode_, 2, 0, 1, 2);
    right->addWidget(showResult_, 3, 0, 1, 2);
    right->addWidget(invertAlpha_, 4, 0, 1, 2);
    // "Softness - Use the slider to add softness to the selected shape" (p.699): the box, the arrows and the slider.
    right->addWidget(new QLabel(tr("Softness"), pane), 5, 0, 1, 2);
    auto* soft = new QHBoxLayout;
    soft->setSpacing(2);
    softnessValue_ = new QSpinBox(pane);
    softnessValue_->setObjectName(QStringLiteral("shapes:softnessValue"));
    softnessValue_->setRange(0, 100);
    softnessValue_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    softnessValue_->setFixedWidth(36);
    softnessSlider_ = new QSlider(Qt::Horizontal, pane);
    softnessSlider_->setObjectName(QStringLiteral("shapes:softness"));
    softnessSlider_->setRange(0, 100);
    auto* down = new QToolButton(pane);
    down->setObjectName(QStringLiteral("shapes:softnessDown"));
    down->setArrowType(Qt::LeftArrow);
    auto* up = new QToolButton(pane);
    up->setObjectName(QStringLiteral("shapes:softnessUp"));
    up->setArrowType(Qt::RightArrow);
    soft->addWidget(softnessValue_);
    soft->addWidget(down);
    soft->addWidget(softnessSlider_, 1);
    soft->addWidget(up);
    right->addLayout(soft, 6, 0, 1, 2);
    // The slider commits when let go (one undo step a drag), the box and arrows at once.
    connect(softnessSlider_, &QSlider::valueChanged, softnessValue_, [this](int v) {
        const QSignalBlocker block(softnessValue_);
        softnessValue_->setValue(v);
        if (!softnessSlider_->isSliderDown()) setShapeSoftness(v);
    });
    connect(softnessSlider_, &QSlider::sliderReleased, this, [this] { setShapeSoftness(softnessSlider_->value()); });
    connect(softnessValue_, &QSpinBox::editingFinished, this, [this] { setShapeSoftness(softnessValue_->value()); });
    connect(down, &QToolButton::clicked, this, [this] { setShapeSoftness(softnessValue_->value() - 1); });
    connect(up, &QToolButton::clicked, this, [this] { setShapeSoftness(softnessValue_->value() + 1); });
    right->setRowStretch(7, 1);
    row->addLayout(right);
    return pane;
}

void CorrectionTool::setShapeLayer(model::CorrectionLayer layer) {
    if (timeline_.correctionLayer() == layer) return;
    timeline_.setCorrectionLayer(layer);
    reload();
    emit shapeDisplayChanged();
}

bool CorrectionTool::setShapeSoftness(double softness) {
    const bool done = timeline_.setSelectedShapeSoftness(std::clamp(softness, 0.0, 100.0));
    reload();
    return done;
}

void CorrectionTool::syncShapes() {
    const bool onEffect = timeline_.correctionOnEffect();
    shapesPane_->setVisible(onEffect);
    if (!onEffect) {
        shapeHost_->refresh();
        return;
    }
    const QSignalBlocker b1(shapesLayer_), b2(baseLayer_), b3(showAlpha_), b4(serialMode_), b5(showResult_), b6(invertAlpha_),
        b7(softnessSlider_), b8(softnessValue_);
    const bool shapes = timeline_.correctionLayer() == model::CorrectionLayer::Shapes;
    shapesLayer_->setChecked(shapes);
    baseLayer_->setChecked(!shapes);
    // The shape tools work on the shape layer (ours: dimmed on Base, p.698).
    for (QAbstractButton* b : shapeTools_->buttons()) b->setEnabled(shapes);
    shapeHost_->refresh();
    showAlpha_->setChecked(timeline_.showShapeAlpha());
    showResult_->setChecked(timeline_.showShapeResult());
    const model::CorrectionShapeOptions o = timeline_.correctionShapeOptions();
    serialMode_->setChecked(o.serial);
    invertAlpha_->setChecked(o.invertAlpha);
    // Softness belongs to a selected shape: dimmed without one (ours).
    const std::optional<double> softness = timeline_.selectedShapeSoftness();
    const int v = static_cast<int>(std::lround(softness.value_or(0.0)));
    softnessSlider_->setValue(v);
    softnessValue_->setValue(v);
    for (QWidget* w : {static_cast<QWidget*>(softnessSlider_), static_cast<QWidget*>(softnessValue_)}) w->setEnabled(softness.has_value());
    for (const char* name : {"shapes:softnessDown", "shapes:softnessUp"}) {
        if (auto* b = shapesPane_->findChild<QToolButton*>(QLatin1String(name))) b->setEnabled(softness.has_value());
    }
}

} // namespace lumenis::ui
