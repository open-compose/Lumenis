// The background services' indicator and dialog (lumenis/ui/background_services.h; Editing Guide p.375-377).
#include "lumenis/ui/background_services.h"

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lumenis::ui {

BackgroundIndicator::BackgroundIndicator(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("backgroundIndicator"));
    setFixedSize(sizeHint());
    spin_.setInterval(80);
    connect(&spin_, &QTimer::timeout, this, [this] {
        angle_ = (angle_ + 30) % 360;
        update();
    });
    setToolTip(shownDescription());
}

void BackgroundIndicator::setState(const State& state) {
    if (state_ == state) return;
    state_ = state;
    // "Spinning ... This indicates that background processing is in progress" (p.376).
    const bool spinning = state.busy && state.services == BackgroundQueue::ServiceState::Active;
    if (spinning && !spin_.isActive()) spin_.start();
    if (!spinning) spin_.stop();
    setToolTip(shownDescription());
    update();
}

QString BackgroundIndicator::description() const {
    QString d;
    using S = BackgroundQueue::ServiceState;
    if (state_.services == S::Stopped) d = QStringLiteral("stopped");
    else if (state_.services == S::Paused) d = QStringLiteral("paused");
    else if (state_.busy) d = QStringLiteral("busy");
    else if (state_.assets) d = QStringLiteral("assets");
    else d = QStringLiteral("idle");
    if (state_.assets && d == QStringLiteral("busy")) d += QStringLiteral(" assets");
    if (state_.error) d += QStringLiteral(" error");
    return d;
}

QString BackgroundIndicator::shownDescription() const {
    // description()'s words as the tooltip shows them, in the language installed (10.4b).
    QStringList words;
    for (const QString& w : description().split(QLatin1Char(' '))) {
        if (w == QLatin1String("stopped")) words << tr("stopped");
        else if (w == QLatin1String("paused")) words << tr("paused");
        else if (w == QLatin1String("busy")) words << tr("busy");
        else if (w == QLatin1String("assets")) words << tr("assets");
        else if (w == QLatin1String("error")) words << tr("error");
        else words << tr("idle");
    }
    return words.join(QLatin1Char(' '));
}

void BackgroundIndicator::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF ring = QRectF(rect()).adjusted(3, 3, -3, -3);
    using S = BackgroundQueue::ServiceState;
    // The ring: grey when the services are stopped, yellow when paused (p.376); otherwise the text colour, with a
    // turning arc while busy. Ours: the drawing; the guide pictures Avid's icons.
    QColor ringColor = palette().color(QPalette::WindowText);
    if (state_.services == S::Stopped) ringColor = QColor(128, 128, 128);
    if (state_.services == S::Paused) ringColor = QColor(220, 190, 40);
    p.setPen(QPen(ringColor, 2));
    p.drawEllipse(ring);
    if (state_.busy && state_.services == S::Active) {
        p.setPen(QPen(palette().color(QPalette::Highlight), 3));
        p.drawArc(ring, -angle_ * 16, 90 * 16);
    }
    // "Solid green dot: You can access the DMF folder to ingest the assets into your bin."
    if (state_.assets) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 190, 70));
        p.drawEllipse(ring.center(), ring.width() / 4, ring.height() / 4);
    }
    // "Icon with yellow triangle ... an error has occurred during previous background processing."
    if (state_.error) {
        QPainterPath tri;
        const QRectF r(rect().width() - 11, rect().height() - 10, 10, 9);
        tri.moveTo(r.center().x(), r.top());
        tri.lineTo(r.right(), r.bottom());
        tri.lineTo(r.left(), r.bottom());
        tri.closeSubpath();
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(QColor(240, 200, 30));
        p.drawPath(tri);
    }
}

void BackgroundIndicator::contextMenuEvent(QContextMenuEvent* event) {
    // "right+click the progress indicator and select Dynamic Media Folders"; "Right-click the progress indicator and
    // select Generate Error Report in Console" (p.376).
    QMenu menu(this);
    menu.addAction(tr("Dynamic Media Folders"), this, &BackgroundIndicator::dynamicMediaFoldersRequested);
    QAction* report = menu.addAction(tr("Generate Error Report in Console"), this, &BackgroundIndicator::errorReportRequested);
    report->setEnabled(state_.error);
    menu.exec(event->globalPos());
}

BackgroundServicesDialog::BackgroundServicesDialog(BackgroundQueue& queue, const Options& options, QWidget* parent)
    : QDialog(parent), queue_(queue) {
    setObjectName(QStringLiteral("backgroundServicesDialog"));
    setWindowTitle(tr("Background Services"));
    auto* v = new QVBoxLayout(this);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("servicesStatus"));
    v->addWidget(status_);
    auto* row = new QHBoxLayout;
    auto button = [this, row](const QString& text, const char* name) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QString::fromLatin1(name));
        b->setAutoDefault(false);
        row->addWidget(b);
        return b;
    };
    connect(button(tr("Start"), "startServices"), &QPushButton::clicked, this, &BackgroundServicesDialog::start);
    connect(button(tr("Stop"), "stopServices"), &QPushButton::clicked, this, &BackgroundServicesDialog::stop);
    QPushButton* pauseButton = button(tr("Pause"), "pauseServices");
    // "Pause: Allows you to explicitly pause the background services for the specified amount of time" (p.377). Ours:
    // in minutes, 10 to start with.
    minutes_ = new QSpinBox(this);
    minutes_->setObjectName(QStringLiteral("pauseMinutes"));
    minutes_->setRange(1, 600);
    minutes_->setValue(10);
    minutes_->setSuffix(tr(" min"));
    row->addWidget(minutes_);
    connect(pauseButton, &QPushButton::clicked, this, [this] { pause(minutes_->value()); });
    row->addStretch(1);
    v->addLayout(row);
    startAtLaunch_ = new QCheckBox(tr("Always Start Background Services at Launch"), this);
    stopOnExit_ = new QCheckBox(tr("Always Stop Background Services upon Exit"), this);
    enablePausing_ = new QCheckBox(tr("Enable Pausing of Background Service"), this);
    startAtLaunch_->setObjectName(QStringLiteral("startAtLaunch"));
    stopOnExit_->setObjectName(QStringLiteral("stopOnExit"));
    enablePausing_->setObjectName(QStringLiteral("enablePausing"));
    startAtLaunch_->setChecked(options.startAtLaunch);
    stopOnExit_->setChecked(options.stopOnExit);
    enablePausing_->setChecked(options.enablePausing);
    v->addWidget(startAtLaunch_);
    v->addWidget(stopOnExit_);
    v->addWidget(enablePausing_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);
    connect(&queue_, &BackgroundQueue::servicesChanged, this, &BackgroundServicesDialog::refresh);
    refresh();
}

BackgroundServicesDialog::Options BackgroundServicesDialog::options() const {
    return {startAtLaunch_->isChecked(), stopOnExit_->isChecked(), enablePausing_->isChecked()};
}

void BackgroundServicesDialog::start() { queue_.startServices(); }
void BackgroundServicesDialog::stop() { queue_.stopServices(); }

void BackgroundServicesDialog::pause(int minutes) {
    // Pausing needs Enable Pausing (p.377): ticking it here applies at once.
    queue_.setPausingEnabled(enablePausing_->isChecked());
    queue_.pauseServices(minutes * 60 * 1000);
}

// "The services are either Active, Inactive or Paused depending upon the options selected" (p.377).
QString BackgroundServicesDialog::statusText() const {
    switch (queue_.serviceState()) {
    case BackgroundQueue::ServiceState::Active: return tr("Active");
    case BackgroundQueue::ServiceState::Stopped: return tr("Inactive");
    case BackgroundQueue::ServiceState::Paused: return tr("Paused");
    }
    return {};
}

void BackgroundServicesDialog::refresh() { status_->setText(tr("Background services: %1").arg(statusText())); }

} // namespace lumenis::ui
