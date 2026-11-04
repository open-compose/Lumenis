// Color Correction mode's three monitors (correction_monitors.h; FX Guide p.589-594). The pictures are the Record
// monitor's sequence frames from the Composer; the shots come from the Timeline (Timeline::correctionShot).

#include "lumenis/ui/correction_monitors.h"

#include "lumenis/ui/composer.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

QString correctionSourceText(CorrectionSource source) {
    switch (source) {
    case CorrectionSource::Empty: return CorrectionMonitors::tr("Empty");
    case CorrectionSource::EntireSequence: return CorrectionMonitors::tr("Entire Sequence");
    case CorrectionSource::Reference: return CorrectionMonitors::tr("Reference");
    case CorrectionSource::Current: return CorrectionMonitors::tr("Current");
    case CorrectionSource::Previous: return CorrectionMonitors::tr("Previous");
    case CorrectionSource::Next: return CorrectionMonitors::tr("Next");
    case CorrectionSource::SecondPrevious: return CorrectionMonitors::tr("Second Previous");
    case CorrectionSource::SecondNext: return CorrectionMonitors::tr("Second Next");
    }
    return {};
}

namespace {

constexpr CorrectionSource kSources[] = {CorrectionSource::Empty,          CorrectionSource::EntireSequence, CorrectionSource::Reference,
                                         CorrectionSource::Current,        CorrectionSource::Previous,       CorrectionSource::Next,
                                         CorrectionSource::SecondPrevious, CorrectionSource::SecondNext};

// The monitor buttons' pictures (ours): Dual Split a picture half filled, the Go to buttons an arrow to a bar with a
// slash for "uncorrected", Remove Effect a cross.
QIcon buttonIcon(const QString& id) {
    QPixmap pm(20, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor ink(0xc8, 0xc8, 0xc8);
    p.setPen(QPen(ink, 1.2));
    if (id == QLatin1String("cc.dual_split")) {
        p.drawRect(QRectF(3.5, 3.5, 13, 9));
        p.fillRect(QRectF(3.5, 3.5, 6.5, 9), ink);
    } else if (id == QLatin1String("fx.remove_effect")) {
        p.drawLine(QPointF(5, 3), QPointF(15, 13));
        p.drawLine(QPointF(15, 3), QPointF(5, 13));
    } else {
        const bool back = id == QLatin1String("cc.previous_uncorrected_shot");
        QPainterPath arrow;
        arrow.moveTo(back ? 15 : 5, 3);
        arrow.lineTo(back ? 7 : 13, 8);
        arrow.lineTo(back ? 15 : 5, 13);
        arrow.closeSubpath();
        p.fillPath(arrow, ink);
        p.drawLine(QPointF(back ? 5 : 15, 3), QPointF(back ? 5 : 15, 13));
        p.drawLine(QPointF(9, 14), QPointF(12, 2));
    }
    return QIcon(pm);
}

} // namespace

// One monitor's picture: the frame its Source menu names, Dual Split's box, the active monitor's frame.
class CorrectionPicture : public QWidget {
public:
    CorrectionPicture(CorrectionMonitors& owner, int index, QWidget* parent) : QWidget(parent), owner_(owner), index_(index) {
        setObjectName(QStringLiteral("ccPicture%1").arg(index));
        setMinimumSize(120, 68);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    // The picture's place, at the project's aspect ratio, letterboxed or pillarboxed.
    QRect pictureRect() const {
        const Rational a = owner_.composer_.aspectRatio();
        const double ratio = static_cast<double>(a.num()) / static_cast<double>(std::max<std::int64_t>(a.den(), 1));
        int w = width() - 4;
        int h = static_cast<int>(std::lround(w / ratio));
        if (h > height() - 4) {
            h = height() - 4;
            w = static_cast<int>(std::lround(h * ratio));
        }
        return QRect((width() - w) / 2, (height() - h) / 2, w, h);
    }

    // The box's corner handles: top left and bottom right.
    std::array<QPointF, 2> handles() const {
        const QRect r = pictureRect();
        const QRectF b = owner_.splitBox(index_);
        return {QPointF(r.left() + b.left() * r.width(), r.top() + b.top() * r.height()),
                QPointF(r.left() + b.right() * r.width(), r.top() + b.bottom() * r.height())};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Window));
        const QRect r = pictureRect();
        p.fillRect(r, Qt::black);
        if (owner_.active_ == index_) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 2));
            p.drawRect(QRectF(r).adjusted(-1, -1, 1, 1));
        }
        const std::optional<std::int64_t> frame = owner_.frameOf(index_);
        if (!frame) return;
        Composer& composer = owner_.composer_;
        const bool preview = owner_.active_ == index_ && owner_.preview_ != CorrectionMonitors::Preview::Output;
        if (preview) composer.setUncorrectedPictures(true);
        QImage image = composer.pictureAt(MonitorId::Record, *frame, r.size());
        if (preview) composer.setUncorrectedPictures(false);
        if (preview && owner_.preview_ == CorrectionMonitors::Preview::ThreeTone && !image.isNull()) image = threeTone(image);
        if (!image.isNull()) p.drawImage(r, image);
        if (!owner_.dualSplit(index_)) return;
        // Dual Split: the box shows the frame without corrections.
        const QRectF b = owner_.splitBox(index_);
        const QRect box(r.left() + static_cast<int>(std::lround(b.left() * r.width())), r.top() + static_cast<int>(std::lround(b.top() * r.height())),
                        static_cast<int>(std::lround(b.width() * r.width())), static_cast<int>(std::lround(b.height() * r.height())));
        if (box.width() > 0 && box.height() > 0) {
            composer.setUncorrectedPictures(true);
            const QImage plain = composer.pictureAt(MonitorId::Record, *frame, r.size());
            composer.setUncorrectedPictures(false);
            if (!plain.isNull()) p.drawImage(box, plain, box.translated(-r.topLeft()));
        }
        p.setPen(QPen(palette().color(QPalette::BrightText), 1));
        p.drawRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5));
        const auto h = handles();
        p.setBrush(palette().color(QPalette::BrightText));
        p.drawPolygon(QPolygonF{h[0], h[0] + QPointF(9, 0), h[0] + QPointF(0, 9)});
        p.drawPolygon(QPolygonF{h[1], h[1] - QPointF(9, 0), h[1] - QPointF(0, 9)});
    }

    // Each pixel white, grey or black by the luminance range it mostly belongs to (model::lumaRangeWeights).
    QImage threeTone(const QImage& image) const {
        const model::ColorCorrection c = owner_.ranges_ ? owner_.ranges_() : model::ColorCorrection{};
        std::array<QRgb, 256> tone{};
        for (int y = 0; y < 256; ++y) {
            const auto w = model::lumaRangeWeights(c, y);
            const int k = w[2] >= w[1] && w[2] >= w[0] ? 2 : w[1] >= w[0] ? 1 : 0;
            tone[static_cast<std::size_t>(y)] = k == 2 ? qRgb(235, 235, 235) : k == 1 ? qRgb(128, 128, 128) : qRgb(16, 16, 16);
        }
        QImage out = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < out.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                const QRgb px = line[x];
                // Rec. 709 luma on the 8-bit video scale.
                const double l = 16.0 + 219.0 * (0.2126 * qRed(px) + 0.7152 * qGreen(px) + 0.0722 * qBlue(px)) / 255.0;
                line[x] = tone[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(l)), 0, 255))];
            }
        }
        return out;
    }

    void mousePressEvent(QMouseEvent* event) override {
        owner_.setActiveMonitor(index_);
        owner_.setFocus();
        if (event->button() != Qt::LeftButton || !owner_.dualSplit(index_)) return;
        const auto h = handles();
        for (int i = 0; i < 2; ++i) {
            if (QLineF(event->position(), h.at(static_cast<std::size_t>(i))).length() <= 10) drag_ = i;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (drag_ < 0) return;
        // "To resize the box that contains the split-screen image: Drag one of the box's triangular handles" (p.592).
        const QRect r = pictureRect();
        const double x = std::clamp((event->position().x() - r.left()) / std::max(r.width(), 1), 0.0, 1.0);
        const double y = std::clamp((event->position().y() - r.top()) / std::max(r.height(), 1), 0.0, 1.0);
        QRectF b = owner_.splitBox(index_);
        if (drag_ == 0) b.setTopLeft(QPointF(std::min(x, b.right()), std::min(y, b.bottom())));
        else b.setBottomRight(QPointF(std::max(x, b.left()), std::max(y, b.top())));
        owner_.setSplitBox(index_, b);
    }

    void mouseReleaseEvent(QMouseEvent*) override { drag_ = -1; }

    void contextMenuEvent(QContextMenuEvent* event) override {
        // "To lock the current frame as a reference, right-click in the monitor window ... and select Reference
        // Current" (p.591).
        QMenu menu(this);
        menu.addAction(CorrectionMonitors::tr("Reference Current"), &owner_, &CorrectionMonitors::referenceCurrent);
        menu.exec(event->globalPos());
    }

private:
    CorrectionMonitors& owner_;
    int index_;
    int drag_ = -1;
};

CorrectionMonitors::CorrectionMonitors(Composer& composer, QWidget* parent) : QWidget(parent), composer_(composer) {
    setObjectName(QStringLiteral("correctionMonitors"));
    setFocusPolicy(Qt::StrongFocus);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    monitors_[0].source = CorrectionSource::Previous;
    monitors_[1].source = CorrectionSource::Current;
    monitors_[2].source = CorrectionSource::Next;
    for (int i = 0; i < kMonitors; ++i) {
        Monitor& m = monitors_.at(static_cast<std::size_t>(i));
        auto* column = new QWidget(this);
        auto* v = new QVBoxLayout(column);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        // The Source menu above the monitor (p.591).
        m.menu = new QToolButton(column);
        m.menu->setObjectName(QStringLiteral("ccSource%1").arg(i));
        m.menu->setPopupMode(QToolButton::InstantPopup);
        m.menu->setToolButtonStyle(Qt::ToolButtonTextOnly);
        auto* menu = new QMenu(m.menu);
        for (CorrectionSource s : kSources) {
            QAction* a = menu->addAction(correctionSourceText(s));
            a->setData(static_cast<int>(s));
            connect(a, &QAction::triggered, this, [this, i, s] { setSource(i, s); });
        }
        connect(menu, &QMenu::aboutToShow, this, [this, menu, i] {
            for (QAction* a : menu->actions()) {
                const auto s = static_cast<CorrectionSource>(a->data().toInt());
                bool taken = false;
                for (int k = 0; k < kMonitors; ++k) {
                    if (k != i && s == CorrectionSource::Current && source(k) == CorrectionSource::Current) taken = true;
                }
                a->setEnabled(!taken);
            }
        });
        m.menu->setMenu(menu);
        v->addWidget(m.menu, 0, Qt::AlignLeft);
        m.picture = new CorrectionPicture(*this, i, column);
        v->addWidget(m.picture, 1);
        // Under it: "Dual Split button, Go to Previous Uncorrected Shot button, Go to Next Uncorrected Shot button, and
        // Remove Effect button" (p.592).
        m.buttons = new QWidget(column);
        auto* h = new QHBoxLayout(m.buttons);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(2);
        h->addStretch(1);
        const std::pair<const char*, const char*> buttons[] = {{"cc.dual_split", QT_TR_NOOP("Dual Split")},
                                                               {"cc.previous_uncorrected_shot", QT_TR_NOOP("Go to Previous Uncorrected Shot")},
                                                               {"cc.next_uncorrected_shot", QT_TR_NOOP("Go to Next Uncorrected Shot")},
                                                               {"fx.remove_effect", QT_TR_NOOP("Remove Effect")}};
        for (const auto& [id, tip] : buttons) {
            auto* b = new QToolButton(m.buttons);
            const QString command = QString::fromLatin1(id);
            b->setObjectName(command);
            b->setToolTip(tr(tip));
            b->setAutoRaise(true);
            b->setIcon(buttonIcon(command));
            b->setIconSize(QSize(20, 16));
            if (command == QLatin1String("cc.dual_split")) {
                b->setCheckable(true);
                connect(b, &QToolButton::clicked, this, [this, i](bool on) { setDualSplit(i, on); });
            } else {
                connect(b, &QToolButton::clicked, this, [this, i, command] {
                    setActiveMonitor(i);
                    emit commandRequested(command);
                });
            }
            h->addWidget(b);
        }
        h->addStretch(1);
        v->addWidget(m.buttons);
        row->addWidget(column, 1);
        syncMenu(i);
    }
}

bool CorrectionMonitors::setSource(int monitor, CorrectionSource source) {
    if (monitor < 0 || monitor >= kMonitors) return false;
    if (source == CorrectionSource::Current) {
        for (int k = 0; k < kMonitors; ++k) {
            if (k != monitor && monitors_.at(static_cast<std::size_t>(k)).source == CorrectionSource::Current) return false;
        }
    }
    monitors_.at(static_cast<std::size_t>(monitor)).source = source;
    syncMenu(monitor);
    monitors_.at(static_cast<std::size_t>(monitor)).picture->update();
    return true;
}

std::optional<std::int64_t> CorrectionMonitors::frameOf(int monitor) const {
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (record.isEmpty()) return std::nullopt;
    auto shot = [&](int step, bool last) -> std::optional<std::int64_t> {
        if (!shots_) return std::nullopt;
        const auto s = shots_(step);
        if (!s) return std::nullopt;
        return last ? std::max(s->first, s->second - 1) : s->first;
    };
    switch (source(monitor)) {
    case CorrectionSource::Empty: return std::nullopt;
    case CorrectionSource::EntireSequence:
    case CorrectionSource::Current: return record.position();
    case CorrectionSource::Reference: return reference_;
    // "the last frame of the segment preceding the current segment, and the first frame of the segment following"
    // (p.590).
    case CorrectionSource::Previous: return shot(-1, true);
    case CorrectionSource::SecondPrevious: return shot(-2, true);
    case CorrectionSource::Next: return shot(1, false);
    case CorrectionSource::SecondNext: return shot(2, false);
    }
    return std::nullopt;
}

void CorrectionMonitors::setActiveMonitor(int monitor) {
    if (monitor < 0 || monitor >= kMonitors || monitor == active_) return;
    active_ = monitor;
    for (const Monitor& m : monitors_) m.picture->update();
    emit activeMonitorChanged(monitor);
}

void CorrectionMonitors::referenceCurrent() {
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (record.isEmpty()) return;
    reference_ = record.position();
    // "The reference frame displays in the right monitor and the right monitor's Source menu changes to Reference"
    // (p.594); a left monitor already showing the reference keeps it.
    int target = 2;
    if (source(0) == CorrectionSource::Reference && source(2) != CorrectionSource::Reference) target = 0;
    setSource(target, CorrectionSource::Reference);
    refresh();
}

void CorrectionMonitors::switchCurrentAndReference() {
    setActiveMonitor(active_ == 2 ? 1 : 2);
}

void CorrectionMonitors::setDualSplit(int monitor, bool on) {
    Monitor& m = monitors_.at(static_cast<std::size_t>(monitor));
    m.dualSplit = on;
    if (QToolButton* b = monitorButton(monitor, QStringLiteral("cc.dual_split"))) b->setChecked(on);
    m.picture->update();
}

void CorrectionMonitors::setSplitBox(int monitor, const QRectF& box) {
    Monitor& m = monitors_.at(static_cast<std::size_t>(monitor));
    m.box = box.normalized() & QRectF(0, 0, 1, 1);
    m.picture->update();
}

QWidget* CorrectionMonitors::pictureWidget(int monitor) const {
    return monitors_.at(static_cast<std::size_t>(monitor)).picture;
}

QToolButton* CorrectionMonitors::sourceButton(int monitor) const {
    return monitors_.at(static_cast<std::size_t>(monitor)).menu;
}

QToolButton* CorrectionMonitors::monitorButton(int monitor, const QString& commandId) const {
    return monitors_.at(static_cast<std::size_t>(monitor)).buttons->findChild<QToolButton*>(commandId);
}

void CorrectionMonitors::setPreview(Preview preview, std::function<model::ColorCorrection()> ranges) {
    preview_ = preview;
    ranges_ = std::move(ranges);
    refresh();
}

void CorrectionMonitors::refresh() {
    for (const Monitor& m : monitors_) m.picture->update();
}

void CorrectionMonitors::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        switchCurrentAndReference();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CorrectionMonitors::syncMenu(int monitor) {
    const Monitor& m = monitors_.at(static_cast<std::size_t>(monitor));
    m.menu->setText(correctionSourceText(m.source));
}

} // namespace lumenis::ui
