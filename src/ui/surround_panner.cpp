#include "lumenis/ui/surround_panner.h"

#include <QDial>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// The Pan grid and the Advanced Panner (surround_panner.h). Their colours follow the picture on p.705:
// a dark green grid, light green speakers and a yellow Pan Location cursor.
namespace lumenis::ui {

namespace {

constexpr QRgb kGridBack = 0xff16240f;
constexpr QRgb kGridLine = 0xff3d5a22;
constexpr QRgb kSpeaker = 0xff8fd43a;
constexpr QRgb kCursor = 0xfff2e01e;
constexpr QRgb kTrajectory = 0xffb0c070;

} // namespace

// ---------------------------------------------------------------- PanGrid

PanGrid::PanGrid(bool large, QWidget* parent) : QWidget(parent), large_(large) {
    setObjectName(large ? QStringLiteral("advancedPanGrid") : QStringLiteral("panGrid"));
    setToolTip(tr("Pan Grid"));
    setCursor(Qt::CrossCursor);
}

QSize PanGrid::sizeHint() const {
    return large_ ? QSize(260, 260) : QSize(44, 44);
}

void PanGrid::setFormat(model::AudioFormat format) {
    format_ = format;
    update();
}

void PanGrid::setPan(const model::SurroundPan& pan) {
    pan_ = pan;
    update();
}

QList<PanGrid::SpeakerSpot> PanGrid::speakers() const {
    QList<SpeakerSpot> out{{QStringLiteral("L"), -100, 100}, {QStringLiteral("C"), 0, 100}, {QStringLiteral("R"), 100, 100}};
    if (format_ == model::AudioFormat::Surround71) {
        out << SpeakerSpot{QStringLiteral("Lss"), -100, 0} << SpeakerSpot{QStringLiteral("Rss"), 100, 0};
    }
    out << SpeakerSpot{QStringLiteral("Lsr"), -100, -100} << SpeakerSpot{QStringLiteral("Rsr"), 100, -100};
    return out;
}

QRectF PanGrid::area() const {
    const double margin = large_ ? 14 : 5;
    return QRectF(rect()).adjusted(margin, margin, -margin, -margin);
}

QPointF PanGrid::pointFor(double x, double y) const {
    const QRectF a = area();
    return {a.left() + (x + 100.0) / 200.0 * a.width(), a.top() + (100.0 - y) / 200.0 * a.height()};
}

std::pair<double, double> PanGrid::gridAt(const QPointF& point) const {
    const QRectF a = area();
    const double x = (point.x() - a.left()) / std::max(a.width(), 1.0) * 200.0 - 100.0;
    const double y = 100.0 - (point.y() - a.top()) / std::max(a.height(), 1.0) * 200.0;
    return {std::clamp(x, -100.0, 100.0), std::clamp(y, -100.0, 100.0)};
}

void PanGrid::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF a = area();
    p.fillRect(rect(), QColor(kGridBack).darker(130));
    p.fillRect(a, isEnabled() ? QColor(kGridBack) : QColor(kGridBack).darker(150));
    p.setPen(QPen(QColor(kGridLine), 1));
    for (int i = 0; i <= 4; ++i) {
        const double fx = a.left() + a.width() * i / 4.0;
        const double fy = a.top() + a.height() * i / 4.0;
        p.drawLine(QPointF(fx, a.top()), QPointF(fx, a.bottom()));
        p.drawLine(QPointF(a.left(), fy), QPointF(a.right(), fy));
    }
    // Speakers: small marks on the edges, pointing in.
    const double s = large_ ? 6 : 2.5;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(kSpeaker));
    for (const SpeakerSpot& spot : speakers()) {
        const QPointF c = pointFor(spot.x, spot.y);
        p.drawRect(QRectF(c.x() - s, c.y() - s, 2 * s, 2 * s));
    }
    if (!isEnabled()) return;
    // The trajectory, then the cursor.
    const QPointF cursor = pointFor(pan_.x(), pan_.frontRear);
    if (large_ && pan_.front != pan_.rear) {
        p.setPen(QPen(QColor(kTrajectory), 1, Qt::DashLine));
        p.drawLine(pointFor(pan_.front, 100), pointFor(pan_.rear, -100));
    }
    p.setPen(QPen(QColor(kCursor).darker(160), 1));
    p.setBrush(QColor(kCursor));
    const double r = large_ ? 6 : 3;
    p.drawEllipse(cursor, r, r);
}

void PanGrid::dragTo(const QPointF& point, bool done) {
    const auto [x, y] = gridAt(point);
    pan_ = model::SurroundPan::at(x, y, pan_);
    update();
    emit panMoved(pan_, done);
}

void PanGrid::mousePressEvent(QMouseEvent* event) {
    if (!isEnabled() || event->button() != Qt::LeftButton) return;
    dragging_ = true;
    dragTo(event->position(), false);
}

void PanGrid::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) dragTo(event->position(), false);
}

void PanGrid::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_) return;
    dragging_ = false;
    dragTo(event->position(), true);
}

// "You can snap the Pan Location cursor to one speaker position by double-clicking a speaker icon"
// (p.707). Snapping to C or a side speaker also sets its percentage to 100, so the sound is fully there.
void PanGrid::mouseDoubleClickEvent(QMouseEvent* event) {
    const double reach = large_ ? 14 : 8;
    for (const SpeakerSpot& spot : speakers()) {
        const QPointF c = pointFor(spot.x, spot.y);
        if (std::hypot(c.x() - event->position().x(), c.y() - event->position().y()) > reach) continue;
        pan_ = model::SurroundPan::at(spot.x, spot.y, pan_);
        if (spot.name == QLatin1String("C")) pan_.center = 100;
        if (spot.name == QLatin1String("Lss") || spot.name == QLatin1String("Rss")) pan_.side = 100;
        dragging_ = false;
        update();
        emit panMoved(pan_, true);
        return;
    }
}

// ---------------------------------------------------------------- AdvancedPanner

namespace {

QDial* knob(const QString& name, int low, int high, QWidget* parent) {
    auto* d = new QDial(parent);
    d->setObjectName(name);
    d->setRange(low, high);
    d->setFixedSize(40, 40);
    d->setNotchesVisible(false);
    return d;
}

} // namespace

// The grids side by side (stereo: left channel, then right), then the knob row with its values, and on
// the right the Stereo Link and Mirror buttons over the LFE slider (picture on p.705).
AdvancedPanner::AdvancedPanner(const QString& track, model::AudioFormat sequenceFormat,
                               const QList<model::SurroundPan>& pans, QWidget* parent)
    : QDialog(parent), track_(track), format_(sequenceFormat), pans_(pans) {
    setObjectName(QStringLiteral("advancedPanner"));
    setWindowTitle(tr("Advanced Panner - %1").arg(track));
    if (pans_.isEmpty()) pans_ << model::SurroundPan{};
    auto* grids = new QHBoxLayout;
    for (int k = 0; k < pans_.size(); ++k) {
        auto* g = new PanGrid(true, this);
        g->setObjectName(QStringLiteral("advancedPanGrid:%1").arg(k));
        g->setFormat(format_);
        g->setPan(pans_.at(k));
        connect(g, &PanGrid::panMoved, this, [this, k](const model::SurroundPan& pan, bool done) {
            setCurrentChannel(k);
            movePan(k, pan, done);
        });
        grids_ << g;
        grids->addWidget(g, 1);
    }

    front_ = knob(QStringLiteral("front"), -100, 100, this);
    rear_ = knob(QStringLiteral("rear"), -100, 100, this);
    frontRear_ = knob(QStringLiteral("frontRear"), -100, 100, this);
    center_ = knob(QStringLiteral("center"), 0, 100, this);
    side_ = knob(QStringLiteral("side"), 0, 100, this);
    auto* knobs = new QGridLayout;
    const QList<std::pair<QString, QDial*>> named{{tr("front"), front_}, {tr("rear"), rear_}, {tr("f/r"), frontRear_},
                                                  {tr("center"), center_}, {tr("side"), side_}};
    for (int i = 0; i < named.size(); ++i) {
        auto* title = new QLabel(named.at(i).first, this);
        auto* value = new QLabel(this);
        value->setObjectName(named.at(i).second->objectName() + QStringLiteral("Value"));
        value->setAlignment(Qt::AlignCenter);
        knobs->addWidget(title, 0, i, Qt::AlignHCenter);
        knobs->addWidget(named.at(i).second, 1, i, Qt::AlignHCenter);
        knobs->addWidget(value, 2, i, Qt::AlignHCenter);
        values_ << value;
        // "Side/Center percentage: For 7.1 surround sound sequences" (p.706).
        if (named.at(i).second == side_ && format_ != model::AudioFormat::Surround71) {
            title->hide();
            side_->hide();
            value->hide();
        }
    }
    auto onKnob = [this](bool done) {
        if (syncing_) return;
        model::SurroundPan p = pans_.value(current_);
        p.front = front_->value();
        p.rear = rear_->value();
        p.frontRear = frontRear_->value();
        p.center = center_->value();
        p.side = side_->value();
        movePan(current_, p, done);
    };
    for (QDial* d : {front_, rear_, frontRear_, center_, side_}) {
        connect(d, &QDial::valueChanged, this, [onKnob, d] { onKnob(!d->isSliderDown()); });
        connect(d, &QDial::sliderReleased, this, [onKnob] { onKnob(true); });
    }

    // Stereo Link and Stereo Mirror (3, 4), the LFE slider (5).
    auto* side = new QVBoxLayout;
    linkButton_ = new QToolButton(this);
    linkButton_->setObjectName(QStringLiteral("stereoLink"));
    linkButton_->setText(QStringLiteral("∞"));
    linkButton_->setToolTip(tr("Stereo Link"));
    linkButton_->setCheckable(true);
    mirrorButton_ = new QToolButton(this);
    mirrorButton_->setObjectName(QStringLiteral("stereoMirror"));
    mirrorButton_->setText(QStringLiteral("↔"));
    mirrorButton_->setToolTip(tr("Stereo Mirror"));
    mirrorButton_->setCheckable(true);
    connect(linkButton_, &QToolButton::clicked, this, [this](bool on) {
        setLink(on ? model::StereoLink::Linked : model::StereoLink::Unlinked);
    });
    connect(mirrorButton_, &QToolButton::clicked, this, [this](bool on) {
        setLink(on ? model::StereoLink::Mirror : model::StereoLink::Unlinked);
    });
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(linkButton_);
    buttons->addWidget(mirrorButton_);
    side->addLayout(buttons);
    linkButton_->setEnabled(pans_.size() == 2);
    mirrorButton_->setEnabled(pans_.size() == 2);
    side->addWidget(new QLabel(tr("Lfe"), this), 0, Qt::AlignHCenter);
    lfe_ = new QSlider(Qt::Vertical, this);
    lfe_->setObjectName(QStringLiteral("lfe"));
    lfe_->setRange(0, 100);
    lfe_->setMinimumHeight(100);
    side->addWidget(lfe_, 1, Qt::AlignHCenter);
    lfeValue_ = new QLabel(this);
    lfeValue_->setObjectName(QStringLiteral("lfeValue"));
    lfeValue_->setAlignment(Qt::AlignCenter);
    side->addWidget(lfeValue_);
    connect(lfe_, &QSlider::valueChanged, this, [this] {
        if (syncing_) return;
        model::SurroundPan p = pans_.value(current_);
        p.lfe = lfe_->value();
        movePan(current_, p, !lfe_->isSliderDown());
    });
    connect(lfe_, &QSlider::sliderReleased, this, [this] {
        model::SurroundPan p = pans_.value(current_);
        p.lfe = lfe_->value();
        movePan(current_, p, true);
    });

    auto* close = new QPushButton(tr("Close"), this);
    close->setObjectName(QStringLiteral("close"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto* left = new QVBoxLayout;
    left->addLayout(grids, 1);
    left->addLayout(knobs);
    auto* body = new QHBoxLayout;
    body->addLayout(left, 1);
    body->addLayout(side);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(close, 0, Qt::AlignRight);
    syncControls();
}

void AdvancedPanner::setPans(const QList<model::SurroundPan>& pans) {
    if (pans.size() != pans_.size()) return;
    pans_ = pans;
    for (int k = 0; k < pans_.size(); ++k) grids_.at(k)->setPan(pans_.at(k));
    syncControls();
}

void AdvancedPanner::setCurrentChannel(int channel) {
    if (channel < 0 || channel >= pans_.size() || channel == current_) return;
    current_ = channel;
    syncControls();
}

void AdvancedPanner::setLink(model::StereoLink link) {
    link_ = pans_.size() == 2 ? link : model::StereoLink::Unlinked;
    linkButton_->setChecked(link_ == model::StereoLink::Linked);
    mirrorButton_->setChecked(link_ == model::StereoLink::Mirror);
}

void AdvancedPanner::movePan(int channel, const model::SurroundPan& pan, bool done) {
    if (channel < 0 || channel >= pans_.size()) return;
    std::optional<model::SurroundPan> other;
    if (pans_.size() == 2 && link_ != model::StereoLink::Unlinked) {
        other = model::linkedPan(pans_.at(1 - channel), pans_.at(channel), pan, link_);
        pans_[1 - channel] = *other;
        grids_.at(1 - channel)->setPan(*other);
    }
    pans_[channel] = pan;
    grids_.at(channel)->setPan(pan);
    syncControls();
    emit panChanged(channel, pan, other, done);
}

void AdvancedPanner::syncControls() {
    syncing_ = true;
    const model::SurroundPan& p = pans_.at(current_);
    front_->setValue(p.front);
    rear_->setValue(p.rear);
    frontRear_->setValue(p.frontRear);
    center_->setValue(p.center);
    side_->setValue(p.side);
    lfe_->setValue(p.lfe);
    values_.at(0)->setText(model::surroundXText(p.front));
    values_.at(1)->setText(model::surroundXText(p.rear));
    values_.at(2)->setText(model::surroundYText(p.frontRear));
    values_.at(3)->setText(QString::number(p.center));
    values_.at(4)->setText(QString::number(p.side));
    lfeValue_->setText(QString::number(p.lfe));
    syncing_ = false;
}

} // namespace lumenis::ui
