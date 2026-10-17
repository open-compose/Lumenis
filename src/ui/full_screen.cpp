#include "lumenis/ui/full_screen.h"

#include "lumenis/ui/composer.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>

namespace lumenis::ui {

FullScreenView::Reformat FullScreenView::reformatFrom(const QString& value) {
    if (value == QLatin1String("stretch")) return Reformat::Stretch;
    if (value == QLatin1String("crop")) return Reformat::Crop;
    if (value == QLatin1String("keepSize")) return Reformat::KeepSize;
    if (value == QLatin1String("rawPixel")) return Reformat::RawPixel;
    return Reformat::Letterbox;
}

FullScreenView::FullScreenView(Composer& composer, QWidget* parent)
    : QWidget(parent, Qt::Window), composer_(composer) {
    setObjectName(QStringLiteral("fullScreenPlayback"));
    setWindowTitle(tr("Full Screen Playback"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    connect(&composer_, &Composer::changed, this, qOverload<>(&QWidget::update));
}

void FullScreenView::setReformat(Reformat reformat) {
    reformat_ = reformat;
    update();
}

void FullScreenView::setFullScreenScaling(bool on) {
    fullScreen_ = on;
    update();
}

QRect FullScreenView::pictureRect(QSize area, QSize image) const {
    if (image.isEmpty()) return {};
    auto centred = [&](QSize s) {
        return QRect((area.width() - s.width()) / 2, (area.height() - s.height()) / 2, s.width(), s.height());
    };
    // "Full Screen" off: as little scaling as possible (p.1191).
    if (!fullScreen_) {
        return centred(image.width() <= area.width() && image.height() <= area.height()
                           ? image
                           : image.scaled(area, Qt::KeepAspectRatio));
    }
    switch (reformat_) {
    case Reformat::Stretch: return QRect(QPoint(0, 0), area);
    case Reformat::Letterbox: return centred(image.scaled(area, Qt::KeepAspectRatio));
    case Reformat::Crop: return centred(image.scaled(area, Qt::KeepAspectRatioByExpanding));
    case Reformat::KeepSize: return centred(image);
    case Reformat::RawPixel:
        // Pixel for pixel; scaled to fit only when larger (p.1190).
        return centred(image.width() <= area.width() && image.height() <= area.height()
                           ? image
                           : image.scaled(area, Qt::KeepAspectRatio));
    }
    return centred(image);
}

void FullScreenView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    const MonitorId monitor = composer_.activeMonitor();
    if (composer_.state(monitor).isEmpty()) return;
    // The frame at its own size, placed as the setting says.
    bool offline = false;
    const QImage image = composer_.picture(monitor, QSize(), &offline);
    if (image.isNull()) {
        if (offline) {
            QFont f = font();
            f.setPointSizeF(std::max(12.0, height() / 20.0));
            p.setFont(f);
            p.setPen(palette().color(QPalette::BrightText));
            p.drawText(rect(), Qt::AlignCenter, tr("Media Offline"));
        }
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, composer_.smoothScaling());
    p.drawImage(pictureRect(size(), image.size()), image);
}

void FullScreenView::open(QWidget* near) {
    // The client display: another screen than the editing one, when there
    // is one (p.389-391).
    QScreen* editing = near ? near->screen() : QGuiApplication::primaryScreen();
    QScreen* target = editing;
    for (QScreen* s : QGuiApplication::screens()) {
        if (s != editing) {
            target = s;
            break;
        }
    }
    if (target) setGeometry(target->geometry());
    showFullScreen();
    raise();
    activateWindow();
    setFocus();
}

void FullScreenView::keyPressEvent(QKeyEvent* event) {
    // Shift+Ctrl+F (Windows) or Shift+Command+F (Mac) disables it (p.391);
    // Esc too (ours).
    const bool toggle = event->key() == Qt::Key_F &&
                        (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) ==
                            (Qt::ControlModifier | Qt::ShiftModifier);
    if (event->key() == Qt::Key_Escape || toggle) {
        close();
        return;
    }
    // Other keys play and stop as in the Composer.
    QCoreApplication::sendEvent(&composer_, event);
}

void FullScreenView::closeEvent(QCloseEvent* event) {
    QWidget::closeEvent(event);
    emit closed();
}

} // namespace lumenis::ui
