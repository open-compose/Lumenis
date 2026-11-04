// The Color Correction tool's Secondary pane (correction_tool.h; FX Guide p.668-683): the Input and Output Vector
// controls, the Input and Output vector wheels, the Selected Vectors panel with Invert Last Vector and Isolate, Lock
// Vectors, and the Color Selection buttons, whose samples come from the monitors (CorrectionMonitors::colorsSampled).

#include "lumenis/ui/correction_tool.h"

#include "lumenis/media/color_correction.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lumenis::ui {

namespace {

// The Selected Vectors panel's names, "G YL R MG B CY 1 2 3 4 5 6" (the illustration on p.669).
QString shortName(const QString& vector) {
    static const std::pair<const char*, const char*> names[] = {{"green", "G"}, {"yellow", "YL"}, {"red", "R"},
                                                                {"magenta", "MG"}, {"blue", "B"}, {"cyan", "CY"}};
    for (const auto& [key, name] : names) {
        if (vector == QLatin1String(key)) return QLatin1String(name);
    }
    return vector;
}

// As the ChromaWheels: red near the top, hues counterclockwise (p.670, p.636).
double redScreenAngle() {
    constexpr double kR = 0.2126, kB = 0.0722;
    return std::atan2(0.5, -kR / (2.0 * (1.0 - kB)));
}

QColor swatchColor(const model::SecondaryVector& v) {
    const auto rgb = media::wheelColor(v.hue, std::max(v.saturation, 20.0), 0.5);
    return QColor::fromRgbF(static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2]));
}

} // namespace

QWidget* CorrectionTool::secondaryPane() {
    // "1 Output Vector controls, 2 Input Vector controls, 3 Input Vector color wheel, 4 Output Vector color wheel, 5
    // Selected Vectors panel, 6 Color Match control, 7 Color Selection buttons" (p.669).
    auto* pane = new QWidget;
    pane->setObjectName(QStringLiteral("pane:secondary"));
    auto* h = new QHBoxLayout(pane);
    h->setContentsMargins(0, 0, 0, 0);
    // Input Vector controls (p.675-676), one page of sliders per vector.
    auto* inputColumn = new QVBoxLayout;
    inputPages_ = new QStackedWidget(pane);
    outputPages_ = new QStackedWidget(pane);
    for (const QString& v : model::secondaryVectors()) {
        auto* in = new QWidget(inputPages_);
        auto* rows = new QVBoxLayout(in);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(1);
        const bool standard = model::isStandardVector(v);
        const std::pair<const char*, const char*> inputs[] = {
            {"hue", QT_TR_NOOP("Hue")},
            {"saturation", QT_TR_NOOP("Saturation")},
            {"hueWidth", QT_TR_NOOP("Hue Width")},
            {standard ? "satMin" : "satWidth", standard ? QT_TR_NOOP("Sat Min") : QT_TR_NOOP("Sat Width")},
            {"softness", QT_TR_NOOP("Softness")}};
        for (const auto& [control, text] : inputs) rows->addWidget(sliderRow(in, model::vectorPath(v, QStringLiteral("input"), QLatin1String(control)), tr(text)));
        rows->addStretch(1);
        inputPages_->addWidget(in);
        // "only hue, saturation and luminance values are adjustable for output" (p.680).
        auto* out = new QWidget(outputPages_);
        auto* outRows = new QVBoxLayout(out);
        outRows->setContentsMargins(0, 0, 0, 0);
        outRows->setSpacing(1);
        const std::pair<const char*, const char*> outputs[] = {
            {"hue", QT_TR_NOOP("Hue")}, {"saturation", QT_TR_NOOP("Saturation")}, {"luminance", QT_TR_NOOP("Luminance")}};
        for (const auto& [control, text] : outputs) outRows->addWidget(sliderRow(out, model::vectorPath(v, QStringLiteral("output"), QLatin1String(control)), tr(text)));
        outRows->addStretch(1);
        outputPages_->addWidget(out);
    }
    inputColumn->addWidget(new QLabel(tr("Input Vector"), pane));
    inputColumn->addWidget(inputPages_, 1);
    // "To lock Input and Output Hue and Saturation controls together: Click Lock Vectors" (p.677).
    auto* lock = new QCheckBox(tr("Lock Vectors"), pane);
    lock->setObjectName(QStringLiteral("lockVectors"));
    connect(lock, &QCheckBox::toggled, this, &CorrectionTool::setLockVectors);
    inputColumn->addWidget(lock);
    h->addLayout(inputColumn, 2);
    inputWheel_ = new VectorWheel(false, pane);
    inputWheel_->setObjectName(QStringLiteral("inputVectorWheel"));
    outputWheel_ = new VectorWheel(true, pane);
    outputWheel_->setObjectName(QStringLiteral("outputVectorWheel"));
    h->addWidget(inputWheel_, 1);
    h->addWidget(outputWheel_, 1);
    // "Manipulate the vector directly in the Input color wheel by dragging its center point" (p.675).
    connect(inputWheel_, &VectorWheel::vectorClicked, this, &CorrectionTool::selectVector);
    connect(inputWheel_, &VectorWheel::centreMoved, this, [this](double hue, double saturation) {
        const QString v = selectedVector();
        model::setVectorInput(correction_, v, QStringLiteral("hue"), hue, lockVectors_);
        model::setVectorInput(correction_, v, QStringLiteral("saturation"), saturation, lockVectors_);
        commit(tr("Input Vector"));
    });
    connect(outputWheel_, &VectorWheel::centreMoved, this, [this](double hue, double saturation) {
        const QString v = selectedVector();
        correction_.set(model::vectorPath(v, QStringLiteral("output"), QStringLiteral("hue")), hue);
        correction_.set(model::vectorPath(v, QStringLiteral("output"), QStringLiteral("saturation")), saturation);
        commit(tr("Output Vector"));
    });
    auto* outputColumn = new QVBoxLayout;
    outputColumn->addWidget(new QLabel(tr("Output Vector"), pane));
    outputColumn->addWidget(outputPages_, 1);
    // The Selected Vectors panel: "vector names (standard on the left, custom on the right), color swatches, Enable
    // buttons, and the Invert Last Vector and Isolate check boxes" (p.674).
    auto* panel = new QGridLayout;
    panel->setSpacing(1);
    const QStringList vectors = model::secondaryVectors();
    for (int i = 0; i < vectors.size(); ++i) {
        const QString v = vectors.at(i);
        auto* name = new QLabel(shortName(v), pane);
        name->setAlignment(Qt::AlignCenter);
        panel->addWidget(name, 0, i);
        auto* swatch = new QToolButton(pane);
        swatch->setObjectName(QStringLiteral("swatch:") + v);
        swatch->setCheckable(true);
        swatch->setFixedSize(16, 12);
        // "Click the vector's color swatch in the Selected Vectors panel" (p.674).
        connect(swatch, &QToolButton::clicked, this, [this, v] { selectVector(v); });
        panel->addWidget(swatch, 1, i);
        auto* enable = new QToolButton(pane);
        enable->setObjectName(QStringLiteral("enable:secondary/vectors/") + v);
        enable->setCheckable(true);
        enable->setFixedSize(14, 14);
        connect(enable, &QToolButton::clicked, this,
                [this, v] { clickEnable(QStringLiteral("secondary/vectors/") + v, QApplication::keyboardModifiers().testFlag(Qt::AltModifier)); });
        panel->addWidget(enable, 2, i, Qt::AlignCenter);
    }
    outputColumn->addLayout(panel);
    auto* checks = new QHBoxLayout;
    for (const auto& [key, text] : {std::pair{"invertLast", QT_TR_NOOP("Invert Last Vector")}, std::pair{"isolate", QT_TR_NOOP("Isolate")}}) {
        auto* box = new QCheckBox(tr(text), pane);
        const QString path = QStringLiteral("secondary/") + QLatin1String(key);
        box->setObjectName(path);
        connect(box, &QCheckBox::clicked, this, [this, path](bool on) { setControl(path, on ? 1.0 : 0.0); });
        checks->addWidget(box);
    }
    outputColumn->addLayout(checks);
    h->addLayout(outputColumn, 2);
    // "Left to right: Syringe button, Add to Selection button, and Subtract from Selection button" (p.678).
    auto* selection = new QVBoxLayout;
    const std::pair<const char*, ColorSelection> buttons[] = {{QT_TR_NOOP("Syringe"), ColorSelection::Syringe},
                                                              {QT_TR_NOOP("Add to Selection"), ColorSelection::AddToSelection},
                                                              {QT_TR_NOOP("Subtract from Selection"), ColorSelection::SubtractFromSelection}};
    for (const auto& [text, mode] : buttons) {
        auto* b = new QToolButton(pane);
        b->setObjectName(QStringLiteral("colorSelection:%1").arg(static_cast<int>(mode)));
        b->setText(tr(text));
        b->setCheckable(true);
        // "The Syringe button remains selected until you explicitly turn it off by clicking it or by clicking one of the
        // other Color Selection buttons" (p.678).
        connect(b, &QToolButton::clicked, this, [this, m = mode](bool on) { setColorSelection(on ? m : ColorSelection::None); });
        selection->addWidget(b);
    }
    selection->addStretch(1);
    h->addLayout(selection);
    return pane;
}

QString CorrectionTool::selectedVector() const {
    return correction_.text(QStringLiteral("secondary/selected"));
}

void CorrectionTool::selectVector(const QString& vector) {
    if (!model::secondaryVectors().contains(vector) || vector == selectedVector()) return;
    // Kept in the correction: Isolate shows the selected vector (p.679).
    correction_.set(QStringLiteral("secondary/selected"), vector);
    commit(tr("Select Vector"));
}

void CorrectionTool::setLockVectors(bool on) {
    lockVectors_ = on;
    if (auto* box = findChild<QCheckBox*>(QStringLiteral("lockVectors")); box && box->isChecked() != on) box->setChecked(on);
}

void CorrectionTool::setColorSelection(ColorSelection selection) {
    colorSelection_ = selection;
    for (QToolButton* b : findChildren<QToolButton*>()) {
        if (b->objectName().startsWith(QLatin1String("colorSelection:")))
            b->setChecked(b->objectName() == QStringLiteral("colorSelection:%1").arg(static_cast<int>(selection)));
    }
    emit colorSelectionChanged(selection);
}

void CorrectionTool::applySamples(const QList<QColor>& colors) {
    if (colors.isEmpty() || colorSelection_ == ColorSelection::None) return;
    QList<QPointF> samples;
    for (const QColor& c : colors) samples << media::wheelHueSaturation({static_cast<double>(c.redF()), static_cast<double>(c.greenF()), static_cast<double>(c.blueF())});
    switch (colorSelection_) {
    case ColorSelection::Syringe:
        // "Selects and enables the closest vector ... Sets the Output Vector values to match the Input Vector values ...
        // Automatically selects Isolate" (p.678-679).
        model::syringeVector(correction_, samples);
        commit(tr("Syringe"));
        break;
    case ColorSelection::AddToSelection:
        model::addToVector(correction_, selectedVector(), samples.constFirst());
        commit(tr("Add to Selection"));
        break;
    case ColorSelection::SubtractFromSelection:
        model::subtractFromVector(correction_, selectedVector(), samples.constFirst());
        commit(tr("Subtract from Selection"));
        break;
    case ColorSelection::None: break;
    }
}

void CorrectionTool::syncSecondary() {
    const QString selected = selectedVector();
    const QStringList vectors = model::secondaryVectors();
    const auto index = vectors.indexOf(selected);
    if (index >= 0) {
        inputPages_->setCurrentIndex(static_cast<int>(index));
        outputPages_->setCurrentIndex(static_cast<int>(index));
    }
    // The Input wheel shows the vectors the Correction setting's Secondary Vectors menu names (p.672-673): all twelve,
    // the standard ones and the enabled custom ones ("This is the default setting"), the custom ones and the enabled
    // standard ones, or only the enabled ones; and the selected one (ours).
    const QStringList enabled = model::enabledVectors(correction_);
    QList<VectorWheel::Shown> shown, output;
    for (const QString& v : vectors) {
        const model::SecondaryVector vector = model::secondaryVector(correction_, v);
        const bool on = enabled.contains(v);
        bool show = v == selected;
        switch (options_.vectors) {
        case Options::Vectors::All: show = true; break;
        case Options::Vectors::Standard: show = show || vector.standard || on; break;
        case Options::Vectors::Custom: show = show || !vector.standard || on; break;
        case Options::Vectors::Enabled: show = show || on; break;
        }
        if (show) shown << VectorWheel::Shown{v, vector, v == selected};
        if (v == selected) output << VectorWheel::Shown{v, vector, true};
        if (auto* swatch = findChild<QToolButton*>(QStringLiteral("swatch:") + v)) {
            // "The color swatches for each vector ... update in real time to provide a visual indication of the defined
            // color range" and "A white box appears around the color swatch for the selected vector" (p.674).
            QPalette p = swatch->palette();
            p.setColor(QPalette::Button, swatchColor(vector));
            swatch->setPalette(p);
            swatch->setAutoFillBackground(true);
            swatch->setChecked(v == selected);
        }
    }
    inputWheel_->setVectors(shown);
    outputWheel_->setVectors(output);
    for (const char* key : {"invertLast", "isolate"}) {
        if (auto* box = findChild<QCheckBox*>(QStringLiteral("secondary/") + QLatin1String(key))) {
            const QSignalBlocker block(box);
            box->setChecked(correction_.number(QStringLiteral("secondary/") + QLatin1String(key)) > 0.5);
        }
    }
}

// ---------------------------------------------------------------- VectorWheel

VectorWheel::VectorWheel(bool output, QWidget* parent) : QWidget(parent), output_(output) {
    setMinimumSize(100, 100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VectorWheel::setVectors(const QList<Shown>& vectors) {
    if (dragging_) return;
    vectors_ = vectors;
    update();
}

QPointF VectorWheel::pointFor(double hue, double saturation) const {
    const double r = std::min(width(), height()) / 2.0 - 6.0;
    const double a = redScreenAngle() + hue * std::numbers::pi / 180.0;
    return QPointF(width() / 2.0, height() / 2.0) + QPointF(std::cos(a), -std::sin(a)) * (r * std::clamp(saturation, 0.0, 100.0) / 100.0);
}

std::pair<double, double> VectorWheel::placeAt(const QPointF& point) const {
    const double r = std::min(width(), height()) / 2.0 - 6.0;
    const QPointF d = point - QPointF(width() / 2.0, height() / 2.0);
    const double saturation = std::clamp(std::hypot(d.x(), d.y()) / std::max(r, 1.0) * 100.0, 0.0, 100.0);
    const double hue = std::remainder((std::atan2(-d.y(), d.x()) - redScreenAngle()) * 180.0 / std::numbers::pi, 360.0);
    return {hue, saturation};
}

QPointF VectorWheel::centreOf(const Shown& s) const {
    return output_ ? pointFor(s.vector.outHue, s.vector.outSaturation) : pointFor(s.vector.hue, s.vector.saturation);
}

void VectorWheel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double r = std::min(width(), height()) / 2.0 - 6.0;
    const QPointF c(width() / 2.0, height() / 2.0);
    const QColor ink = palette().color(QPalette::WindowText);
    p.setPen(QPen(ink, 1));
    p.setBrush(palette().color(QPalette::Base));
    p.drawEllipse(c, r, r);
    p.setPen(QPen(ink.darker(170), 1));
    for (int deg = 0; deg < 360; deg += 90) p.drawLine(c, pointFor(deg, 100));
    for (const Shown& s : vectors_) {
        const model::SecondaryVector& v = s.vector;
        const QColor colour = swatchColor(v);
        p.setPen(QPen(colour, s.selected ? 2.0 : 1.0));
        p.setBrush(Qt::NoBrush);
        if (!output_) {
            QPainterPath boundary;
            if (v.standard) {
                // A wedge from the saturation minimum to the edge, hueWidth degrees about its hue.
                const double lo = v.hue - v.hueWidth / 2.0, hi = v.hue + v.hueWidth / 2.0;
                boundary.moveTo(pointFor(lo, v.satMin));
                boundary.lineTo(pointFor(lo, 100));
                for (double a = lo; a <= hi; a += 2.0) boundary.lineTo(pointFor(a, 100));
                boundary.lineTo(pointFor(hi, 100));
                boundary.lineTo(pointFor(hi, v.satMin));
                for (double a = hi; a >= lo; a -= 2.0) boundary.lineTo(pointFor(a, v.satMin));
                boundary.closeSubpath();
            } else {
                // The ellipse of model::vectorMembership: satWidth across, hueWidth degrees along as seen from the
                // centre of the wheel.
                const double ar = std::max(0.5, v.satWidth / 2.0);
                const double at = std::max(0.5, v.saturation * std::tan(std::min(v.hueWidth, 179.0) / 2.0 * std::numbers::pi / 180.0));
                const double h = v.hue * std::numbers::pi / 180.0;
                for (int k = 0; k <= 72; ++k) {
                    const double t = k * 2.0 * std::numbers::pi / 72.0;
                    // In the wheel's plane with the vector's hue along x, then turned to its place.
                    const double x = v.saturation + ar * std::cos(t), y = at * std::sin(t);
                    const double sat = std::hypot(x, y), hue = (h + std::atan2(y, x)) * 180.0 / std::numbers::pi;
                    const QPointF at2 = pointFor(hue, sat);
                    if (k == 0) boundary.moveTo(at2);
                    else boundary.lineTo(at2);
                }
            }
            p.drawPath(boundary);
        }
        const QPointF centre = dragging_ && s.selected ? dragged_ : centreOf(s);
        // "Center points for standard vectors are filled circles. Center points for custom vectors are crosshairs" (p.670).
        if (v.standard) {
            p.setBrush(colour);
            p.drawEllipse(centre, s.selected ? 4.5 : 3.5, s.selected ? 4.5 : 3.5);
        } else {
            p.drawLine(centre - QPointF(5, 0), centre + QPointF(5, 0));
            p.drawLine(centre - QPointF(0, 5), centre + QPointF(0, 5));
        }
    }
}

void VectorWheel::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    // "Click the center point of the vector in the color wheel" (p.674); the selected one's is dragged.
    for (const Shown& s : vectors_) {
        if (QLineF(centreOf(s), event->position()).length() > 7) continue;
        if (!s.selected) {
            emit vectorClicked(s.name);
            return;
        }
        dragging_ = true;
        dragged_ = centreOf(s);
        return;
    }
}

void VectorWheel::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    dragged_ = event->position();
    update();
}

void VectorWheel::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) return;
    dragging_ = false;
    const auto [hue, saturation] = placeAt(event->position());
    emit centreMoved(hue, saturation);
}

} // namespace lumenis::ui
