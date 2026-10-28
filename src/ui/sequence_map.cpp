#include "lumenis/ui/sequence_map.h"

#include "lumenis/media/link.h"
#include "lumenis/media/waveform.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// The Timeline Sequence Map (sequence_map.h; Editing Guide p.606, p.1219). The map reads everything
// from the Timeline: the shown sequence, the rows of its tracks area (TimelineView::rows) and what
// part of them is in view (Timeline::firstFrame, scale, TimelineView::topOffset). It changes nothing
// but the Timeline's view, through Timeline::scrollTo.

namespace lumenis::ui {

namespace {

constexpr int kMargin = 4;
constexpr QRgb kMapBackground = 0xff262626;
constexpr QRgb kMapRow = 0xff303030;
constexpr QRgb kViewBox = 0xffffffff;   // "the white box" (p.606)
constexpr QRgb kEffectMark = 0xffc87dd8; // ours: a segment with an effect or a transition at its start
constexpr QRgb kMapIndicator = 0xff33c3ff;

} // namespace

SequenceMap::SequenceMap(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("sequenceMap"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(120, 50);
    setCursor(Qt::OpenHandCursor);
}

double SequenceMap::contentFrames() const {
    return static_cast<double>(std::max<std::int64_t>(timeline_.shown().duration(), 1));
}

double SequenceMap::contentHeight() const {
    const TimelineView* v = timeline_.view();
    return std::max(v->contentHeight() - v->rulerHeight(), 1);
}

QRectF SequenceMap::mapArea() const {
    if (timeline_.shown().tracks().isEmpty()) return {};
    return QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin);
}

QRectF SequenceMap::viewBox() const {
    const QRectF area = mapArea();
    if (area.isEmpty()) return {};
    const TimelineView* v = timeline_.view();
    const double frames = contentFrames();
    const double height = contentHeight();
    const double visibleFrames = std::max(v->width() - v->headerWidth(), 1) / std::max(timeline_.scale(), 1e-9);
    const double visibleHeight = std::max(v->height() - v->rulerHeight(), 1);
    const double x = area.left() + std::clamp(timeline_.firstFrame() / frames, 0.0, 1.0) * area.width();
    const double y = area.top() + std::clamp(v->topOffset() / height, 0.0, 1.0) * area.height();
    const double w = std::min(visibleFrames / frames, 1.0) * area.width();
    const double h = std::min(visibleHeight / height, 1.0) * area.height();
    return QRectF(x, y, std::min(w, area.right() - x), std::min(h, area.bottom() - y));
}

void SequenceMap::centreViewAt(const QPointF& centre) {
    const QRectF area = mapArea();
    if (area.isEmpty()) return;
    const QRectF box = viewBox();
    const double frame = (centre.x() - box.width() / 2 - area.left()) / area.width() * contentFrames();
    const double top = (centre.y() - box.height() / 2 - area.top()) / area.height() * contentHeight();
    timeline_.scrollTo(std::max(frame, 0.0), static_cast<int>(std::lround(std::max(top, 0.0))));
    update();
}

void SequenceMap::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(kMapBackground));
    const QRectF area = mapArea();
    if (area.isEmpty()) return;
    const model::Sequence& seq = timeline_.shown();
    const TimelineView* v = timeline_.view();
    const double frames = contentFrames();
    const double height = contentHeight();
    const double xScale = area.width() / frames;
    const double yScale = area.height() / height;
    const int shift = v->topOffset() - v->rulerHeight(); // rows() tops are scrolled; undo it
    const Timeline::Options& o = timeline_.options();
    for (const TimelineView::Row& r : v->rows()) {
        const QRectF rowBox(area.left(), area.top() + (r.top + shift) * yScale, area.width(), std::max(r.height * yScale, 1.0));
        p.fillRect(rowBox.adjusted(0, 0, 0, rowBox.height() > 3 ? -1 : 0), QColor(kMapRow));
        const model::Track* t = r.exists ? seq.track(r.track) : nullptr;
        if (!t) continue;
        const QColor fill = Timeline::trackColor(*t);
        std::int64_t at = 0;
        for (const model::Segment& s : t->segments) {
            const QRectF box(area.left() + static_cast<double>(at) * xScale, rowBox.top(), std::max(static_cast<double>(s.length) * xScale, 1.0),
                             rowBox.height());
            if (!s.isFiller()) {
                // Clip colours (p.594-598) as in the Timeline.
                const QString clipColor = timeline_.segmentColor(s);
                p.fillRect(box, clipColor.isEmpty() ? fill : QColor(clipColor));
                // Display waveforms (p.1219).
                if (o.mapWaveforms && t->kind == model::TrackKind::Audio && box.height() >= 4) paintWaveform(p, box, *t, s);
            }
            // Display effects (p.1219): a mark along the top of each segment with one.
            if (o.mapEffects && s.hasEffects()) {
                p.fillRect(QRectF(box.left(), box.top(), box.width(), std::max(box.height() / 4, 1.0)), QColor(kEffectMark));
            }
            at += s.length;
        }
    }
    // The position indicator, then the white box (p.606).
    const double x = area.left() + static_cast<double>(timeline_.composer().state(timeline_.shownMonitor()).position()) * xScale;
    p.setPen(QColor(kMapIndicator));
    p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    p.setPen(QPen(QColor(kViewBox), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(viewBox());
}

// The first channel of the segment's sound, as the Timeline reads it (TimelineView::paintWaveform),
// one column of peaks a pixel.
void SequenceMap::paintWaveform(QPainter& p, const QRectF& box, const model::Track& t, const model::Segment& s) const {
    const auto clip = timeline_.composer().resolve(s.binId, s.clipId);
    const auto link = clip ? media::linkOf(*clip) : std::nullopt;
    const auto ref = model::parseTrackName(s.sourceTrack);
    if (!clip || !link || link->unlinked || !ref || clip->editRate.num() <= 0) return;
    const int track = ref->number - 1 + timeline_.waveformChannels(t.format).value(0, 0);
    const auto channel = link->channelOf(track);
    if (!channel) return;
    const auto waveform = timeline_.waveforms().get({link->audioFile(track), channel->first, channel->second});
    if (!waveform || waveform->sampleRate <= 0) return;
    const double first = static_cast<double>(link->first) / std::max(link->rate.toDouble(), 1.0);
    const double clipRate = clip->editRate.toDouble();
    const double framesPerPixel = box.width() > 0 ? static_cast<double>(s.length) / box.width() : 0;
    auto sampleAt = [&](double frame) {
        return static_cast<std::int64_t>(std::floor((first + (static_cast<double>(s.sourceIn) + frame) / clipRate) * waveform->sampleRate));
    };
    const double middle = box.center().y();
    const double half = box.height() / 2 - 1;
    p.setPen(QColor::fromRgba(Timeline::kWaveformColor));
    for (int i = 0; i < static_cast<int>(std::ceil(box.width())); ++i) {
        const std::int64_t from = sampleAt(i * framesPerPixel);
        const auto [lo, hi] = waveform->range(from, std::max(sampleAt((i + 1) * framesPerPixel), from + 1));
        const double xi = box.left() + i;
        p.drawLine(QPointF(xi, middle - std::min(static_cast<double>(hi), 1.0) * half),
                   QPointF(xi, middle - std::max(static_cast<double>(lo), -1.0) * half));
    }
}

// "Mouse down on the white box and drag in any direction" (p.606). Ours: a press elsewhere on the
// map moves the box there first.
void SequenceMap::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const QPointF pos = event->position();
    const QRectF box = viewBox();
    if (box.contains(pos)) {
        grab_ = pos - box.center();
    } else {
        centreViewAt(pos);
        grab_ = QPointF();
    }
    setCursor(Qt::ClosedHandCursor);
}

void SequenceMap::mouseMoveEvent(QMouseEvent* event) {
    if (!grab_) return;
    centreViewAt(event->position() - *grab_);
}

void SequenceMap::mouseReleaseEvent(QMouseEvent*) {
    grab_.reset();
    setCursor(Qt::OpenHandCursor);
}

QMenu* SequenceMap::contextMenu() {
    auto* menu = new QMenu(this);
    const bool inWindow = timeline_.sequenceMapInWindow();
    QAction* a = menu->addAction(inWindow ? tr("Show in Timeline") : tr("Show in Dedicated Window"));
    connect(a, &QAction::triggered, this, [this, inWindow] { timeline_.setSequenceMapInWindow(!inWindow); });
    return menu;
}

void SequenceMap::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = contextMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

bool SequenceMap::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress && isVisible()) {
        auto* w = qobject_cast<QWidget*>(watched);
        // Its own menu is a window of its own: a press there is not "elsewhere".
        const bool inMenu = w && qobject_cast<QMenu*>(w->window());
        if (w && w != this && !isAncestorOf(w) && !inMenu) timeline_.showSequenceMap(false);
    }
    return QWidget::eventFilter(watched, event);
}

// The filter is in place only while the map is an overlay and the setting asks for it.
void SequenceMap::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    const bool want = !timeline_.sequenceMapInWindow() && timeline_.options().mapClickHides;
    if (want && !filtering_) qApp->installEventFilter(this);
    if (!want && filtering_) qApp->removeEventFilter(this);
    filtering_ = want;
}

void SequenceMap::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (filtering_) qApp->removeEventFilter(this);
    filtering_ = false;
}

SequenceMapWindow::SequenceMapWindow(QWidget* parent) : QWidget(parent, Qt::Tool) {
    setObjectName(QStringLiteral("sequenceMapWindow"));
    setWindowTitle(tr("Sequence Map"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    resize(420, 180);
}

void SequenceMapWindow::closeEvent(QCloseEvent* event) {
    QWidget::closeEvent(event);
    emit closed();
}

} // namespace lumenis::ui
