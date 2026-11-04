// The Color Correction tool's Channels, Levels and Curves panes (correction_tool.h; FX Guide p.644-667), and the
// ChromaCurve graph and histogram they use. Values go through CorrectionTool::setControl and commit as undo steps.

#include "lumenis/ui/correction_tool.h"

#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadioButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {

const char* const kChannels[] = {"red", "green", "blue"};
const char* const kChannelNames[] = {QT_TRANSLATE_NOOP("CorrectionTool", "Red"), QT_TRANSLATE_NOOP("CorrectionTool", "Green"),
                                     QT_TRANSLATE_NOOP("CorrectionTool", "Blue")};
// "Component Type Options in the Channels Tab" (p.647).
const std::pair<const char*, const char*> kComponents[] = {
    {"red", QT_TRANSLATE_NOOP("CorrectionTool", "Red")},   {"green", QT_TRANSLATE_NOOP("CorrectionTool", "Green")},
    {"blue", QT_TRANSLATE_NOOP("CorrectionTool", "Blue")}, {"luma", QT_TRANSLATE_NOOP("CorrectionTool", "Luma")},
    {"cr", QT_TRANSLATE_NOOP("CorrectionTool", "Cr")},     {"cb", QT_TRANSLATE_NOOP("CorrectionTool", "Cb")},
    {"offset", QT_TRANSLATE_NOOP("CorrectionTool", "Offset")}};
// The Levels tabs, "Composite, Luma, Red, Green, Blue, Master" (p.653).
const std::pair<const char*, const char*> kLevelsTabs[] = {
    {"composite", QT_TRANSLATE_NOOP("CorrectionTool", "Composite")}, {"luma", QT_TRANSLATE_NOOP("CorrectionTool", "Luma")},
    {"red", QT_TRANSLATE_NOOP("CorrectionTool", "Red")},             {"green", QT_TRANSLATE_NOOP("CorrectionTool", "Green")},
    {"blue", QT_TRANSLATE_NOOP("CorrectionTool", "Blue")},           {"master", QT_TRANSLATE_NOOP("CorrectionTool", "Master")}};
const char* const kCurveGraphs[] = {"red", "green", "blue", "master"};

QString componentName(const QString& type) {
    for (const auto& [key, name] : kComponents) {
        if (type == QLatin1String(key)) return CorrectionTool::tr(name);
    }
    return type;
}

QDoubleSpinBox* valueBox(QWidget* parent, const QString& name, double lo, double hi) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(lo, hi);
    box->setDecimals(0);
    box->setKeyboardTracking(false);
    box->setFixedWidth(60);
    return box;
}

} // namespace

// An Enable button for a control or group in these panes (p.603), as the tool's own.
static QToolButton* paneEnable(CorrectionTool* tool, QWidget* parent, const QString& path) {
    auto* b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("enable:") + path);
    b->setCheckable(true);
    b->setFixedSize(14, 14);
    b->setToolTip(CorrectionTool::tr("Enable (Alt+click resets)"));
    QObject::connect(b, &QToolButton::clicked, tool,
                     [tool, path] { tool->clickEnable(path, QApplication::keyboardModifiers().testFlag(Qt::AltModifier)); });
    return b;
}

QWidget* CorrectionTool::channelsPane() {
    auto* pane = new QWidget;
    pane->setObjectName(QStringLiteral("pane:channels"));
    auto* h = new QHBoxLayout(pane);
    h->setContentsMargins(0, 0, 0, 0);
    // The Preview pane (p.645-646): inputs on the left, outputs on the right.
    auto* preview = new QGridLayout;
    auto* buttons = new QButtonGroup(pane);
    const std::pair<const char*, ChannelPreview> previews[] = {
        {QT_TR_NOOP("Red Input"), ChannelPreview::RedInput},       {QT_TR_NOOP("Red Output"), ChannelPreview::RedOutput},
        {QT_TR_NOOP("Green Input"), ChannelPreview::GreenInput},   {QT_TR_NOOP("Green Output"), ChannelPreview::GreenOutput},
        {QT_TR_NOOP("Blue Input"), ChannelPreview::BlueInput},     {QT_TR_NOOP("Blue Output"), ChannelPreview::BlueOutput},
        {QT_TR_NOOP("Master Input"), ChannelPreview::MasterInput}, {QT_TR_NOOP("Master Output"), ChannelPreview::MasterOutput}};
    for (int i = 0; i < 8; ++i) {
        auto* b = new QRadioButton(tr(previews[i].first), pane);
        b->setObjectName(QStringLiteral("channelPreview:%1").arg(i));
        b->setChecked(previews[i].second == ChannelPreview::MasterOutput);
        buttons->addButton(b);
        connect(b, &QRadioButton::toggled, this, [this, p = previews[i].second](bool on) {
            if (on) setChannelPreview(p);
        });
        preview->addWidget(b, i / 2, i % 2);
    }
    h->addLayout(preview);
    // The Channel Blending pane (p.646): for each channel an Enable button, then four text boxes and Component Type
    // buttons, "Red is 100 % Red + 0 % Red + 0 % Red + 0 % Offset" (p.645's illustration).
    auto* blending = new QGridLayout;
    blending->setHorizontalSpacing(3);
    for (int c = 0; c < 3; ++c) {
        const QString channel = QLatin1String(kChannels[c]);
        blending->addWidget(paneEnable(this, pane, QStringLiteral("channels/") + channel), c, 0);
        blending->addWidget(new QLabel(tr("%1 is").arg(tr(kChannelNames[c])), pane), c, 1);
        for (int n = 1; n <= model::kChannelComponents; ++n) {
            const QString amountPath = model::channelComponentPath(channel, n, QStringLiteral("amount"));
            const QString typePath = model::channelComponentPath(channel, n, QStringLiteral("type"));
            const QString invertPath = model::channelComponentPath(channel, n, QStringLiteral("invert"));
            // "Values range from –200% to 200%" (p.646).
            QDoubleSpinBox* amount = valueBox(pane, amountPath, -200, 200);
            connect(amount, &QDoubleSpinBox::valueChanged, this, [this, amountPath](double v) { setControl(amountPath, v); });
            auto* type = new QToolButton(pane);
            type->setObjectName(typePath);
            type->setPopupMode(QToolButton::InstantPopup);
            auto* menu = new QMenu(type);
            for (const auto& [key, name] : kComponents) {
                connect(menu->addAction(tr(name)), &QAction::triggered, this, [this, typePath, k = QString::fromLatin1(key)] {
                    if (correction_.text(typePath) == k && correction_.hasValues(typePath)) return;
                    correction_.set(typePath, k);
                    commit(tr("Component Type"));
                });
            }
            menu->addSeparator();
            // "Select one of the other options, and then select Invert to invert it" (p.647).
            QAction* invert = menu->addAction(tr("Invert"));
            invert->setObjectName(QStringLiteral("invert"));
            connect(invert, &QAction::triggered, this, [this, invertPath] { setControl(invertPath, correction_.number(invertPath) > 0.5 ? 0.0 : 1.0); });
            type->setMenu(menu);
            const int column = 2 + (n - 1) * 3;
            if (n > 1) blending->addWidget(new QLabel(QStringLiteral("+"), pane), c, column - 1);
            blending->addWidget(amount, c, column);
            blending->addWidget(type, c, column + 1);
        }
    }
    h->addLayout(blending, 1);
    return pane;
}

QWidget* CorrectionTool::levelsPane() {
    // "Components of the Levels tab: left to right, Input histogram, ChromaCurve graph, Output histogram" (p.649), with
    // the subdividing tabs on the left (p.653).
    auto* pane = new QWidget;
    pane->setObjectName(QStringLiteral("pane:levels"));
    auto* h = new QHBoxLayout(pane);
    h->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QVBoxLayout;
    tabs->setSpacing(1);
    for (const auto& [key, name] : kLevelsTabs) tabs->addWidget(tab(pane, QStringLiteral("levels/") + QLatin1String(key), tr(name)));
    tabs->addStretch(1);
    h->addLayout(tabs);
    auto column = [this, pane](bool output) {
        auto* v = new QVBoxLayout;
        auto* histogram = new Histogram(pane);
        histogram->setObjectName(output ? QStringLiteral("outputHistogram") : QStringLiteral("inputHistogram"));
        v->addWidget(histogram, 1);
        auto* boxes = new QHBoxLayout;
        const QString side = output ? QStringLiteral("out") : QStringLiteral("in");
        for (const char* point : {"Black", "Gray", "White"}) {
            const QString name = side + QLatin1String(point);
            // "if you are working with 8-bit values and you type 50 in the In Black Point text box, you set the black
            // point to 50 on a scale from 0 to 255" (p.657).
            QDoubleSpinBox* box = valueBox(pane, QStringLiteral("levels:") + name, 0, 255);
            connect(box, &QDoubleSpinBox::valueChanged, this, [this, name](double x) { setControl(QStringLiteral("levels/") + levelsTab_ + QLatin1Char('/') + name, x); });
            boxes->addWidget(box);
        }
        v->addLayout(boxes);
        connect(histogram, &Histogram::sliderMoved, this, [this, side](int index, double value) {
            const QStringList points = model::levelsPoints(levelsTab_);
            QStringList names;
            for (const QString& p : points) {
                if (p.startsWith(side)) names << p;
            }
            if (index >= 0 && index < names.size()) setControl(QStringLiteral("levels/") + levelsTab_ + QLatin1Char('/') + names.at(index), value);
        });
        return std::pair{v, histogram};
    };
    auto [inColumn, inHistogram] = column(false);
    inHistogram_ = inHistogram;
    h->addLayout(inColumn, 1);
    levelsGraph_ = new ChromaCurveGraph(false, pane);
    levelsGraph_->setObjectName(QStringLiteral("levelsGraph"));
    // "To adjust a control point in the ChromaCurve graph: Click a control point, and drag it to a new position" (p.657):
    // a point's input and output together, one undo step.
    connect(levelsGraph_, &ChromaCurveGraph::curveChanged, this, [this](const QList<QPointF>& points) {
        const QStringList names = model::levelsPoints(levelsTab_);
        const bool gray = names.contains(QStringLiteral("inGray"));
        const QStringList kinds = gray ? QStringList{QStringLiteral("Black"), QStringLiteral("Gray"), QStringLiteral("White")}
                                       : QStringList{QStringLiteral("Black"), QStringLiteral("White")};
        const QString base = QStringLiteral("levels/") + levelsTab_ + QLatin1Char('/');
        for (qsizetype i = 0; i < points.size() && i < kinds.size(); ++i) {
            if (correction_.number(base + QStringLiteral("in") + kinds.at(i)) != points.at(i).x())
                correction_.set(base + QStringLiteral("in") + kinds.at(i), points.at(i).x());
            if (correction_.number(base + QStringLiteral("out") + kinds.at(i)) != points.at(i).y())
                correction_.set(base + QStringLiteral("out") + kinds.at(i), points.at(i).y());
        }
        commit(tr("Levels"));
    });
    h->addWidget(levelsGraph_, 1);
    auto [outColumn, outHistogram] = column(true);
    outHistogram_ = outHistogram;
    h->addLayout(outColumn, 1);
    return pane;
}

QWidget* CorrectionTool::curvesPane() {
    // "four ChromaCurve graphs" and "four slider controls — Master Saturation, Master Gain, Master Gamma, and Master
    // Setup" (p.659; the illustration on p.660).
    auto* pane = new QWidget;
    pane->setObjectName(QStringLiteral("pane:curves"));
    auto* h = new QHBoxLayout(pane);
    h->setContentsMargins(0, 0, 0, 0);
    const char* const names[] = {QT_TR_NOOP("Red"), QT_TR_NOOP("Green"), QT_TR_NOOP("Blue"), QT_TR_NOOP("Master")};
    for (int g = 0; g < 4; ++g) {
        const QString path = QStringLiteral("curves/") + QLatin1String(kCurveGraphs[g]);
        auto* v = new QVBoxLayout;
        auto* head = new QHBoxLayout;
        head->addWidget(paneEnable(this, pane, path));
        head->addWidget(new QLabel(tr(names[g]), pane), 1);
        v->addLayout(head);
        auto* graph = new ChromaCurveGraph(true, pane);
        graph->setObjectName(path);
        v->addWidget(graph, 1);
        // The Input and Output text boxes under each graph (p.662-663).
        auto* boxes = new QHBoxLayout;
        QDoubleSpinBox* in = valueBox(pane, path + QStringLiteral(":in"), 0, 255);
        QDoubleSpinBox* out = valueBox(pane, path + QStringLiteral(":out"), 0, 255);
        boxes->addWidget(in);
        boxes->addWidget(out);
        v->addLayout(boxes);
        h->addLayout(v, 1);
        const QString graphName = QLatin1String(kCurveGraphs[g]);
        connect(graph, &ChromaCurveGraph::curveChanged, this, [this, graphName](const QList<QPointF>& points) {
            model::setColorCurve(correction_, graphName, points);
            commit(tr("Curves"));
        });
        connect(graph, &ChromaCurveGraph::selectionChanged, this, [graph, in, out](int i) {
            const QSignalBlocker b1(in), b2(out);
            if (i >= 0 && i < graph->points().size()) {
                in->setValue(graph->points().at(i).x());
                out->setValue(graph->points().at(i).y());
            }
        });
        auto typed = [this, graph, graphName, in, out] {
            const int i = graph->selectedPoint();
            QList<QPointF> points = graph->points();
            if (i < 0 || i >= points.size()) return;
            points[i] = QPointF(in->value(), out->value());
            model::setColorCurve(correction_, graphName, points);
            commit(tr("Curves"));
        };
        connect(in, &QDoubleSpinBox::valueChanged, this, typed);
        connect(out, &QDoubleSpinBox::valueChanged, this, typed);
    }
    auto* sliders = new QVBoxLayout;
    for (const char* n : {"masterSaturation", "masterGain", "masterGamma", "masterSetup"}) {
        const QString path = QStringLiteral("curves/") + QLatin1String(n);
        auto* row = sliderRow(pane, path, QString());
        // The sliders' names, as the tool's labels give them.
        if (auto* label = row->findChild<QLabel*>()) {
            static const std::pair<const char*, const char*> labels[] = {{"masterSaturation", QT_TR_NOOP("Master Saturation")},
                                                                         {"masterGain", QT_TR_NOOP("Master Gain")},
                                                                         {"masterGamma", QT_TR_NOOP("Master Gamma")},
                                                                         {"masterSetup", QT_TR_NOOP("Master Setup")}};
            for (const auto& [key, text] : labels) {
                if (QLatin1String(n) == QLatin1String(key)) label->setText(tr(text));
            }
        }
        sliders->addWidget(row);
    }
    sliders->addStretch(1);
    h->addLayout(sliders, 1);
    return pane;
}

void CorrectionTool::showLevelsTab(const QString& tab) {
    for (const auto& [key, name] : kLevelsTabs) {
        if (tab == QLatin1String(key)) levelsTab_ = tab;
    }
    sync();
}

void CorrectionTool::setChannelPreview(ChannelPreview preview) {
    if (preview == channelPreview_) return;
    channelPreview_ = preview;
    emit channelPreviewChanged(preview);
}

ChromaCurveGraph* CorrectionTool::curveGraph(const QString& graph) const {
    return findChild<ChromaCurveGraph*>(QStringLiteral("curves/") + graph);
}

void CorrectionTool::syncPanes() {
    // Channels: each component's amount and type, with "A minus sign (–) ... after the component name when it is
    // inverted" (p.647).
    for (const char* ch : kChannels) {
        for (int n = 1; n <= model::kChannelComponents; ++n) {
            const QString channel = QLatin1String(ch);
            if (auto* box = findChild<QDoubleSpinBox*>(model::channelComponentPath(channel, n, QStringLiteral("amount")))) {
                const QSignalBlocker block(box);
                box->setValue(correction_.number(model::channelComponentPath(channel, n, QStringLiteral("amount"))));
            }
            if (auto* type = findChild<QToolButton*>(model::channelComponentPath(channel, n, QStringLiteral("type")))) {
                const bool inverted = correction_.number(model::channelComponentPath(channel, n, QStringLiteral("invert"))) > 0.5;
                type->setText(QStringLiteral("% ") + componentName(correction_.text(model::channelComponentPath(channel, n, QStringLiteral("type")))) +
                              (inverted ? QStringLiteral(" –") : QString()));
            }
        }
    }
    // Levels: the tab's points in the text boxes, the graph and the histograms' sliders.
    const QString base = QStringLiteral("levels/") + levelsTab_ + QLatin1Char('/');
    const QStringList points = model::levelsPoints(levelsTab_);
    const bool gray = points.contains(QStringLiteral("inGray"));
    for (const char* name : {"inBlack", "inGray", "inWhite", "outBlack", "outGray", "outWhite"}) {
        if (auto* box = findChild<QDoubleSpinBox*>(QStringLiteral("levels:") + QLatin1String(name))) {
            const QSignalBlocker block(box);
            box->setValue(correction_.number(base + QLatin1String(name)));
            // "You can make only black point and white point adjustments in the Composite and Luma tabs" (p.657).
            box->setVisible(gray || !QLatin1String(name).endsWith(QLatin1String("Gray")));
        }
    }
    QList<QPointF> levels;
    QList<double> ins, outs;
    for (const char* kind : {"Black", "Gray", "White"}) {
        if (!gray && QLatin1String(kind) == QLatin1String("Gray")) continue;
        const double in = correction_.number(base + QStringLiteral("in") + QLatin1String(kind));
        const double out = correction_.number(base + QStringLiteral("out") + QLatin1String(kind));
        levels << QPointF(in, out);
        ins << in;
        outs << out;
    }
    const model::ColorCorrection c = correction_;
    const QString shownTab = levelsTab_;
    levelsGraph_->setCurve(levels, [c, shownTab](double x) { return model::levelsValue(c, shownTab, x); });
    inHistogram_->setSliders(ins);
    outHistogram_->setSliders(outs);
    if (group_ == QLatin1String("levels") && pictures_) {
        const Histogram::Channel channel = shownTab == QLatin1String("composite") ? Histogram::Channel::Composite
                                           : shownTab == QLatin1String("luma")    ? Histogram::Channel::Luma
                                           : shownTab == QLatin1String("red")     ? Histogram::Channel::Red
                                           : shownTab == QLatin1String("green")   ? Histogram::Channel::Green
                                           : shownTab == QLatin1String("blue")    ? Histogram::Channel::Blue
                                                                                  : Histogram::Channel::Master;
        // "The input histogram always shows the distribution of values for the uncorrected image" (p.657).
        inHistogram_->setImage(pictures_(false), channel);
        outHistogram_->setImage(pictures_(true), channel);
    }
    // Curves: each graph's points, drawn as model::curveValue reads them.
    for (const char* g : kCurveGraphs) {
        if (ChromaCurveGraph* graph = curveGraph(QLatin1String(g))) {
            const QList<QPointF> curve = model::colorCurve(c, QLatin1String(g));
            graph->setCurve(curve, [curve](double x) { return model::curveValue(curve, x); });
        }
    }
}

// ---------------------------------------------------------------- ChromaCurveGraph

ChromaCurveGraph::ChromaCurveGraph(bool editable, QWidget* parent) : QWidget(parent), editable_(editable) {
    setFocusPolicy(Qt::ClickFocus);
    setMinimumSize(90, 90);
}

void ChromaCurveGraph::setCurve(const QList<QPointF>& points, std::function<double(double)> mapping) {
    if (dragging_) return;
    points_ = points;
    mapping_ = std::move(mapping);
    if (selected_ >= points_.size()) selected_ = -1;
    update();
}

void ChromaCurveGraph::selectPoint(int index) {
    selected_ = index;
    update();
    emit selectionChanged(index);
}

QPointF ChromaCurveGraph::toWidget(const QPointF& value) const {
    const QRectF r = QRectF(rect()).adjusted(5, 5, -5, -5);
    return {r.left() + value.x() / 255.0 * r.width(), r.bottom() - value.y() / 255.0 * r.height()};
}

QPointF ChromaCurveGraph::toValue(const QPointF& point) const {
    const QRectF r = QRectF(rect()).adjusted(5, 5, -5, -5);
    return {std::clamp((point.x() - r.left()) / r.width() * 255.0, 0.0, 255.0), std::clamp((r.bottom() - point.y()) / r.height() * 255.0, 0.0, 255.0)};
}

void ChromaCurveGraph::paintEvent(QPaintEvent*) {
    // "A background grid indicates the quartile points on each axis" (p.655, p.660).
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().color(QPalette::Base));
    const QColor ink = palette().color(QPalette::WindowText);
    p.setPen(QPen(ink.darker(170), 1));
    for (int q = 0; q <= 4; ++q) {
        p.drawLine(toWidget(QPointF(255.0 * q / 4, 0)), toWidget(QPointF(255.0 * q / 4, 255)));
        p.drawLine(toWidget(QPointF(0, 255.0 * q / 4)), toWidget(QPointF(255, 255.0 * q / 4)));
    }
    QPainterPath path;
    for (int x = 0; x <= 255; ++x) {
        const double y = mapping_ ? std::clamp(mapping_(x), 0.0, 255.0) : x;
        if (x == 0) path.moveTo(toWidget(QPointF(x, y)));
        else path.lineTo(toWidget(QPointF(x, y)));
    }
    p.setPen(QPen(ink, 1.5));
    p.drawPath(path);
    for (qsizetype i = 0; i < points_.size(); ++i) {
        const QPointF at = toWidget(points_.at(i));
        p.setPen(QPen(palette().color(QPalette::BrightText), 1));
        p.setBrush(palette().color(QPalette::BrightText));
        p.drawRect(QRectF(at - QPointF(2.5, 2.5), QSizeF(5, 5)));
        // "When you click a control point, a circle appears around it to indicate that it is selected" (p.657).
        if (i == selected_) {
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(at, 6, 6);
        }
    }
}

void ChromaCurveGraph::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    for (qsizetype i = 0; i < points_.size(); ++i) {
        if (QLineF(toWidget(points_.at(i)), event->position()).length() <= 6) {
            selectPoint(static_cast<int>(i));
            dragging_ = true;
            return;
        }
    }
    if (!editable_ || points_.size() >= model::kMaxCurvePoints) return;
    // "Click the curve line in the graph at the point where you want the new point to appear" (p.662).
    const QPointF v = toValue(event->position());
    const double y = mapping_ ? mapping_(v.x()) : v.x();
    if (QLineF(toWidget(QPointF(v.x(), y)), event->position()).length() > 8) return;
    qsizetype at = 0;
    while (at < points_.size() && points_.at(at).x() < v.x()) ++at;
    points_.insert(at, QPointF(v.x(), y));
    selectPoint(static_cast<int>(at));
    dragging_ = true;
}

void ChromaCurveGraph::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_ || selected_ < 0 || selected_ >= points_.size()) return;
    QPointF v = toValue(event->position());
    // A point stays between its neighbours (ours).
    const double lo = selected_ > 0 ? points_.at(selected_ - 1).x() : 0.0;
    const double hi = selected_ + 1 < points_.size() ? points_.at(selected_ + 1).x() : 255.0;
    v.setX(std::clamp(v.x(), lo, hi));
    points_[selected_] = v;
    // The curve follows the drag through its points (the Levels graph's own mapping waits for the commit).
    if (editable_) {
        const QList<QPointF> now = points_;
        mapping_ = [now](double x) { return model::curveValue(now, x); };
    }
    emit selectionChanged(selected_);
    update();
}

void ChromaCurveGraph::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) return;
    dragging_ = false;
    emit curveChanged(points_);
}

void ChromaCurveGraph::keyPressEvent(QKeyEvent* event) {
    // "Press the Delete key ... If you have only two control points set in a ChromaCurve graph, you cannot delete one"
    // (p.663).
    if (editable_ && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && selected_ >= 0 && points_.size() > 2) {
        points_.removeAt(selected_);
        selected_ = -1;
        emit curveChanged(points_);
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------- Histogram

Histogram::Histogram(QWidget* parent) : QWidget(parent) {
    setMinimumSize(90, 60);
}

void Histogram::setImage(const QImage& image, Channel channel) {
    counts_.fill(0);
    channel_ = channel;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    auto bump = [this](double v) { ++counts_[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255))]; };
    // The 8-bit video scale: full-range 0-255 pixels as 16-235.
    auto video = [](int v) { return 16.0 + 219.0 * v / 255.0; };
    for (int y = 0; y < rgb.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb px = line[x];
            const double luma = video(static_cast<int>(std::lround(0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px))));
            switch (channel) {
            case Channel::Red: bump(video(qRed(px))); break;
            case Channel::Green: bump(video(qGreen(px))); break;
            case Channel::Blue: bump(video(qBlue(px))); break;
            case Channel::Master:
                bump(video(qRed(px)));
                bump(video(qGreen(px)));
                bump(video(qBlue(px)));
                break;
            case Channel::Luma: bump(luma); break;
            case Channel::Composite: {
                // Both ends of the composite swing: Y' less and plus the chroma's amplitude (as levelsPixel does).
                const double r = qRed(px) / 255.0, g = qGreen(px) / 255.0, b = qBlue(px) / 255.0;
                const double yy = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                const double amplitude = 224.0 * std::hypot((b - yy) / 1.8556, (r - yy) / 1.5748);
                bump(luma - amplitude);
                bump(luma + amplitude);
                break;
            }
            }
        }
    }
    update();
}

void Histogram::setSliders(const QList<double>& values) {
    if (dragging_ >= 0) return;
    sliders_ = values;
    update();
}

double Histogram::sliderX(int index) const {
    const QRectF r = QRectF(rect()).adjusted(5, 3, -5, -12);
    return r.left() + sliders_.value(index) / 255.0 * r.width();
}

void Histogram::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(5, 3, -5, -12);
    p.fillRect(r, palette().color(QPalette::Base));
    const QColor ink = palette().color(QPalette::WindowText);
    p.setPen(QPen(ink.darker(170), 1));
    for (int q = 0; q <= 4; ++q) {
        const double x = r.left() + r.width() * q / 4, y = r.top() + r.height() * q / 4;
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    const int peak = std::max(1, *std::max_element(counts_.begin(), counts_.end()));
    // "histograms that represent a single color channel display in the color of that channel" with Color Histograms on
    // (p.607, 6.10b4); the bars in the text colour until then.
    p.setPen(QPen(ink, 1));
    for (int v = 0; v < 256; ++v) {
        const double h = r.height() * counts_[static_cast<std::size_t>(v)] / peak;
        const double x = r.left() + r.width() * v / 255.0;
        p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() - h));
    }
    // "Sliders below a histogram control: Black Point (left), Gray Point (center), and White Point (right)" (p.656).
    p.setBrush(palette().color(QPalette::BrightText));
    for (int i = 0; i < sliders_.size(); ++i) {
        const double x = sliderX(i);
        p.drawPolygon(QPolygonF{QPointF(x, r.bottom() + 1), QPointF(x - 5, r.bottom() + 10), QPointF(x + 5, r.bottom() + 10)});
    }
}

void Histogram::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    for (int i = 0; i < sliders_.size(); ++i) {
        if (std::abs(event->position().x() - sliderX(i)) <= 6 && event->position().y() >= height() - 14) dragging_ = i;
    }
}

void Histogram::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ < 0) return;
    const QRectF r = QRectF(rect()).adjusted(5, 3, -5, -12);
    sliders_[dragging_] = std::clamp((event->position().x() - r.left()) / r.width() * 255.0, 0.0, 255.0);
    update();
}

void Histogram::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging_ < 0 || event->button() != Qt::LeftButton) return;
    const int i = dragging_;
    dragging_ = -1;
    emit sliderMoved(i, std::round(sliders_.value(i)));
}

} // namespace lumenis::ui
