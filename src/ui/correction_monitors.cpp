// Color Correction mode's three monitors (correction_monitors.h; FX Guide p.589-594). The pictures are the Record
// monitor's sequence frames from the Composer; the shots come from the Timeline (Timeline::correctionShot).

#include "lumenis/ui/correction_monitors.h"

#include "lumenis/ui/safe_color_graph.h"

#include "lumenis/media/scopes.h"
#include "lumenis/ui/composer.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
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

bool isScopeSource(CorrectionSource source) {
    return static_cast<int>(source) >= static_cast<int>(CorrectionSource::QuadDisplay);
}

QString correctionSourceText(CorrectionSource source) {
    if (isScopeSource(source)) {
        return media::scopeName(static_cast<media::Scope>(static_cast<int>(source) - static_cast<int>(CorrectionSource::QuadDisplay)));
    }
    switch (source) {
    case CorrectionSource::Empty: return CorrectionMonitors::tr("Empty");
    case CorrectionSource::EntireSequence: return CorrectionMonitors::tr("Entire Sequence");
    case CorrectionSource::Reference: return CorrectionMonitors::tr("Reference");
    case CorrectionSource::Current: return CorrectionMonitors::tr("Current");
    case CorrectionSource::Previous: return CorrectionMonitors::tr("Previous");
    case CorrectionSource::Next: return CorrectionMonitors::tr("Next");
    case CorrectionSource::SecondPrevious: return CorrectionMonitors::tr("Second Previous");
    case CorrectionSource::SecondNext: return CorrectionMonitors::tr("Second Next");
    default: break;
    }
    return {};
}

namespace {

constexpr CorrectionSource kSources[] = {
    CorrectionSource::Empty,       CorrectionSource::EntireSequence,   CorrectionSource::Reference,         CorrectionSource::Current,
    CorrectionSource::Previous,    CorrectionSource::Next,             CorrectionSource::SecondPrevious,    CorrectionSource::SecondNext,
    CorrectionSource::QuadDisplay, CorrectionSource::RgbHistogram,     CorrectionSource::RgbParade,         CorrectionSource::Vectorscope,
    CorrectionSource::YWaveform,   CorrectionSource::YWaveformHdr1000, CorrectionSource::YWaveformHdr10000, CorrectionSource::YcWaveform,
    CorrectionSource::YcbcrHistogram, CorrectionSource::YcbcrParade};

media::Scope scopeOf(CorrectionSource source) {
    return static_cast<media::Scope>(static_cast<int>(source) - static_cast<int>(CorrectionSource::QuadDisplay));
}

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
        shown_ = {};
        warnings_ = {};
        if (!frame) return;
        Composer& composer = owner_.composer_;
        const CorrectionSource source = owner_.source(index_);
        if (isScopeSource(source)) {
            // The active monitor's corrected frame (a small copy is enough to measure), as a waveform or vectorscope.
            const QImage picture = composer.pictureAt(MonitorId::Record, *frame, QSize(320, 180));
            const QRect area = rect().adjusted(2, 2, -2, -2);
            p.fillRect(rect(), Qt::black);
            p.drawImage(area, media::scopeImage(picture, scopeOf(source), area.size(), owner_.pal_, &owner_.safeColors_));
            warnings_ = {};
            return;
        }
        using P = CorrectionMonitors::Preview;
        const P mode = owner_.active_ == index_ ? owner_.preview_ : P::Output;
        const bool uncorrected = mode == P::Input || mode == P::ThreeTone || mode == P::RedInput || mode == P::GreenInput || mode == P::BlueInput;
        if (uncorrected) composer.setUncorrectedPictures(true);
        QImage image = composer.pictureAt(MonitorId::Record, *frame, r.size());
        if (uncorrected) composer.setUncorrectedPictures(false);
        if (mode == P::ThreeTone && !image.isNull()) image = threeTone(image);
        const int channel = mode == P::RedInput || mode == P::RedOutput ? 0 : mode == P::GreenInput || mode == P::GreenOutput ? 1
                          : mode == P::BlueInput || mode == P::BlueOutput                                         ? 2
                                                                                                                  : -1;
        if (channel >= 0 && !image.isNull()) image = monochrome(image, channel);
        shown_ = image;
        shownRect_ = r;
        if (!image.isNull()) p.drawImage(r, image);
        if (owner_.safeColors_.warns() && !image.isNull()) {
            warnings_ = model::warnedViolations(media::pictureViolations(image, owner_.safeColors_), owner_.safeColors_);
        }
        if (!owner_.dualSplit(index_) || (composer.isPlaying() && !owner_.dualSplitPlayback_)) {
            paintShapes(p);
            paintSafeWarnings(p, r);
            return;
        }
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
        paintShapes(p);
        paintSafeWarnings(p, r);
    }

public:
    model::SafeViolations warnings_; // the warned limits the shown picture exceeds

    // The safe colour warning (p.724): the orange triangle at the picture's top left and a column per warned value
    // type. Ours: columns 12 pixels wide and 30 high, on dark grey, right of the triangle.
    void paintSafeWarnings(QPainter& p, const QRect& r) const {
        if (!warnings_.any()) return;
        const model::SafeColorLimits& l = owner_.safeColors_;
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF at(r.left() + 6, r.top() + 6);
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(QColor(255, 140, 0));
        p.drawPolygon(QPolygonF{at + QPointF(8, 0), at + QPointF(16, 14), at + QPointF(0, 14)});
        p.setPen(QPen(Qt::black, 2));
        p.drawLine(at + QPointF(8, 4), at + QPointF(8, 9));
        p.drawPoint(at + QPointF(8, 12));
        p.setRenderHint(QPainter::Antialiasing, false);
        // Bar index (SafeColorGraph::barColor), too low, too high.
        struct Column {
            int bar;
            bool low, high;
        };
        QList<Column> columns;
        if (l.warnCompositeLuma) {
            columns << Column{0, warnings_.compositeLow, warnings_.compositeHigh} << Column{1, warnings_.lumaLow, warnings_.lumaHigh};
        }
        if (l.warnRgb) {
            for (std::size_t c = 0; c < 3; ++c) columns << Column{2 + static_cast<int>(c), warnings_.rgbLow.at(c), warnings_.rgbHigh.at(c)};
        }
        const int w = 12, h = 30;
        int x = r.left() + 28;
        const int top = r.top() + 6;
        for (const Column& c : columns) {
            const QRect column(x, top, w, h);
            p.fillRect(column, QColor(60, 60, 60));
            p.setPen(QColor(70, 100, 100));
            p.drawRect(column.adjusted(0, 0, -1, -1));
            const QColor colour = SafeColorGraph::barColor(c.bar);
            const auto bar = [&](int y) { p.fillRect(QRect(x + 2, y, w - 4, 5), colour); };
            if (c.high) bar(top + 3);
            if (c.low) bar(top + h - 8);
            if (!c.high && !c.low) bar(top + (h - 5) / 2);
            x += w + 2;
        }
        p.restore();
    }

protected:

    // Whether the shape tools work in this monitor: the Current one, with an input set.
    bool takesShapes() const { return owner_.overlayInput_ && owner_.source(index_) == CorrectionSource::Current; }

    // A widget point as a fraction of the picture (outside it beyond 0 and 1), and back.
    QPointF fraction(const QPointF& at) const {
        const QRect r = pictureRect();
        return QPointF((at.x() - r.left()) / std::max(r.width(), 1), (at.y() - r.top()) / std::max(r.height(), 1));
    }
    QPointF placed(const QPointF& f) const {
        const QRect r = pictureRect();
        return QPointF(r.left() + f.x() * r.width(), r.top() + f.y() * r.height());
    }

    // The shape tools' outlines, open lines, guides, anchors and handles, light lines on a dark edge as the Effect
    // Preview draws them (ours).
    void paintShapes(QPainter& p) const {
        if (owner_.source(index_) != CorrectionSource::Current) return;
        const Composer::EffectPreview& e = owner_.overlay_;
        if (e.boxes.isEmpty() && e.lines.isEmpty() && e.guides.isEmpty() && e.handles.isEmpty() && e.anchors.isEmpty()) return;
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        const QColor wire(235, 235, 235);
        const auto twice = [&](const std::function<void()>& draw) {
            p.setPen(QPen(QColor(0, 0, 0, 160), 3));
            draw();
            p.setPen(QPen(wire, 1));
            draw();
        };
        const auto polygon = [&](const QList<QPointF>& points) {
            QPolygonF out;
            for (const QPointF& c : points) out << placed(c);
            return out;
        };
        for (const QList<QPointF>& box : e.boxes) twice([&] { p.drawPolygon(polygon(box)); });
        for (const QList<QPointF>& line : e.lines) twice([&] { p.drawPolyline(polygon(line)); });
        for (const QList<QPointF>& guide : e.guides) {
            p.setPen(QPen(wire, 1, Qt::DashLine));
            p.drawPolygon(polygon(guide));
        }
        for (const QPointF& a : e.anchors) {
            const QPointF at = placed(a);
            twice([&] {
                p.drawLine(at - QPointF(6, 0), at + QPointF(6, 0));
                p.drawLine(at - QPointF(0, 6), at + QPointF(0, 6));
            });
        }
        for (const Composer::EffectPreview::Handle& h : e.handles) {
            const QPointF at = placed(h.at);
            if (h.from) twice([&] { p.drawLine(placed(*h.from), at); });
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(wire);
            p.drawEllipse(at, 3.5, 3.5);
            p.setBrush(Qt::NoBrush);
        }
        p.restore();
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

    // One channel of the picture as grey: "Displays the uncorrected red channel in monochrome" (p.645).
    static QImage monochrome(const QImage& image, int channel) {
        QImage out = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < out.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                const int v = channel == 0 ? qRed(line[x]) : channel == 1 ? qGreen(line[x]) : qBlue(line[x]);
                line[x] = qRgb(v, v, v);
            }
        }
        return out;
    }

    // The picture's colour under a point, as drawn.
    std::optional<QColor> colorAt(const QPointF& point) const {
        if (shown_.isNull() || !shownRect_.contains(point.toPoint())) return std::nullopt;
        const int x = std::clamp(static_cast<int>((point.x() - shownRect_.left()) * shown_.width() / std::max(shownRect_.width(), 1)), 0, shown_.width() - 1);
        const int y = std::clamp(static_cast<int>((point.y() - shownRect_.top()) * shown_.height() / std::max(shownRect_.height(), 1)), 0, shown_.height() - 1);
        if (!owner_.averaging_) return shown_.pixelColor(x, y);
        // The 3 x 3 pixels centred there (fewer at the picture's edges).
        double r = 0, g = 0, b = 0;
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int px = x + dx, py = y + dy;
                if (px < 0 || py < 0 || px >= shown_.width() || py >= shown_.height()) continue;
                const QColor c = shown_.pixelColor(px, py);
                r += static_cast<double>(c.redF());
                g += static_cast<double>(c.greenF());
                b += static_cast<double>(c.blueF());
                ++n;
            }
        }
        return QColor::fromRgbF(static_cast<float>(r / n), static_cast<float>(g / n), static_cast<float>(b / n));
    }

    void mousePressEvent(QMouseEvent* event) override {
        owner_.setActiveMonitor(index_);
        owner_.setFocus();
        if (event->button() == Qt::LeftButton && owner_.sampling_ != CorrectionMonitors::Sampling::None) {
            samples_.clear();
            if (const auto c = colorAt(event->position())) samples_ << *c;
            sampling_ = true;
            return;
        }
        if (event->button() == Qt::LeftButton && takesShapes()) {
            // The shape tools (6.10e3b2) before Dual Split's handles.
            if (owner_.overlayInput_->overlayPress(fraction(event->position()), event->modifiers(), false)) {
                overlayDrag_ = true;
                return;
            }
        }
        if (event->button() != Qt::LeftButton || !owner_.dualSplit(index_)) return;
        const auto h = handles();
        for (int i = 0; i < 2; ++i) {
            if (QLineF(event->position(), h.at(static_cast<std::size_t>(i))).length() <= 10) drag_ = i;
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        // A double-click ends a polygon (p.401).
        if (event->button() == Qt::LeftButton && takesShapes() &&
            owner_.overlayInput_->overlayPress(fraction(event->position()), event->modifiers(), true)) {
            overlayDrag_ = true;
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (overlayDrag_ && owner_.overlayInput_) {
            owner_.overlayInput_->overlayMove(fraction(event->position()), event->modifiers());
            return;
        }
        if (sampling_) {
            if (owner_.sampling_ == CorrectionMonitors::Sampling::Drag) {
                if (const auto c = colorAt(event->position())) samples_ << *c;
            }
            return;
        }
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

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (overlayDrag_) {
            overlayDrag_ = false;
            if (owner_.overlayInput_) owner_.overlayInput_->overlayRelease(fraction(event->position()), event->modifiers());
            return;
        }
        drag_ = -1;
        if (sampling_) {
            sampling_ = false;
            if (!samples_.isEmpty()) emit owner_.colorsSampled(samples_);
            samples_.clear();
        }
    }

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
    bool overlayDrag_ = false; // a press the shape tools took
    QImage shown_;
    QRect shownRect_;
    bool sampling_ = false;
    QList<QColor> samples_;
};

// The position bar below the Current monitor (FX Guide p.686): the current segment, its correction's keyframe markers
// as the Effect Preview monitor's bar draws them (triangles, pink when selected) and the position indicator. "In
// general, these keyframe markers behave like those you see in the position bar below the Effect Preview monitor": a
// click selects one (Ctrl-click adds or takes it away), Alt-drag moves the selected ones, a click elsewhere moves the
// position within the segment. "You cannot add keyframes manually in the Color Correction monitor".
class CorrectionKeyframeBar : public QWidget {
public:
    CorrectionKeyframeBar(CorrectionMonitors& owner, QWidget* parent) : QWidget(parent), owner_(owner) {
        setObjectName(QStringLiteral("ccKeyframeBar"));
        setFixedHeight(14);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(CorrectionMonitors::tr("Click a keyframe marker to select it, Ctrl-click to add or remove one, Alt-drag to move "
                                          "the selected ones"));
    }

    // The x of a frame into the segment, and back.
    double xOf(double frame, std::int64_t length) const {
        return length > 1 ? frame / static_cast<double>(length - 1) * (width() - 9) + 4.0 : 4.0;
    }
    double frameAt(double x, std::int64_t length) const {
        if (length <= 1) return 0.0;
        return std::clamp((x - 4.0) / std::max(width() - 9, 1) * static_cast<double>(length - 1), 0.0, static_cast<double>(length - 1));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0x5a, 0x5a, 0x5a));
        const auto m = owner_.keyframeMarkers();
        if (!m) return;
        p.setRenderHint(QPainter::Antialiasing);
        const double shift = dragging_ ? frameAt(dragX_, m->length) - frameAt(pressX_, m->length) : 0.0;
        for (int i = 0; i < m->frames.size(); ++i) {
            const bool selected = i < m->selected.size() && m->selected.at(i);
            const double f = selected ? std::clamp(m->frames.at(i) + shift, 0.0, static_cast<double>(m->length - 1)) : m->frames.at(i);
            const double x = xOf(f, m->length);
            p.setPen(Qt::NoPen);
            p.setBrush(selected ? QColor(0xff, 0x8c, 0xc8) : QColor(0xc4, 0xc4, 0xc4));
            p.drawPolygon(QPolygonF{QPointF(x - 4, 1), QPointF(x + 4, 1), QPointF(x, height() - 2)});
        }
        const std::int64_t position = owner_.composer_.state(MonitorId::Record).position() - m->start;
        if (position >= 0 && position < m->length) p.fillRect(QRectF(xOf(static_cast<double>(position), m->length) - 1, 0, 2, height()), QColor(0x52, 0x8c, 0xff));
    }

    void mousePressEvent(QMouseEvent* event) override {
        const auto m = owner_.keyframeMarkers();
        if (!m || event->button() != Qt::LeftButton) return;
        const double x = event->position().x();
        for (int i = 0; i < m->frames.size(); ++i) {
            if (std::abs(xOf(m->frames.at(i), m->length) - x) > 5.0) continue;
            const bool selected = i < m->selected.size() && m->selected.at(i);
            if (event->modifiers() & Qt::AltModifier) {
                // Alt-drag moves the selection; a marker not in it becomes the selection first.
                if (!selected) emit owner_.keyframePressed(m->frames.at(i), false);
                dragging_ = true;
                pressX_ = dragX_ = x;
                return;
            }
            emit owner_.keyframePressed(m->frames.at(i), event->modifiers() & Qt::ControlModifier);
            return;
        }
        scrubbing_ = true;
        moveTo(x, *m);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const auto m = owner_.keyframeMarkers();
        if (!m) return;
        if (dragging_) {
            dragX_ = event->position().x();
            update();
        } else if (scrubbing_) {
            moveTo(event->position().x(), *m);
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        const auto m = owner_.keyframeMarkers();
        if (dragging_ && m) {
            const double delta = std::round(frameAt(event->position().x(), m->length) - frameAt(pressX_, m->length));
            dragging_ = false;
            if (delta != 0.0) emit owner_.keyframesDragged(delta);
            update();
        }
        dragging_ = false;
        scrubbing_ = false;
    }

private:
    void moveTo(double x, const CorrectionMonitors::KeyframeMarkers& m) {
        owner_.composer_.stop();
        owner_.composer_.moveTo(MonitorId::Record, m.start + static_cast<std::int64_t>(std::lround(frameAt(x, m.length))));
    }

    CorrectionMonitors& owner_;
    bool dragging_ = false;
    bool scrubbing_ = false;
    double pressX_ = 0.0;
    double dragX_ = 0.0;
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
            if (s == CorrectionSource::QuadDisplay) menu->addSeparator();
            QAction* a = menu->addAction(correctionSourceText(s));
            a->setData(static_cast<int>(s));
            connect(a, &QAction::triggered, this, [this, i, s] { setSource(i, s); });
        }
        connect(menu, &QMenu::aboutToShow, this, [this, menu, i] {
            for (QAction* a : menu->actions()) {
                if (a->isSeparator()) continue;
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
        m.bar = new CorrectionKeyframeBar(*this, column);
        m.bar->setObjectName(QStringLiteral("ccKeyframeBar%1").arg(i));
        v->addWidget(m.bar);
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
    if (isScopeSource(source) && monitor == active_) {
        // The scope shows another monitor's frame: the one at Current, else the first that shows a frame.
        int other = -1;
        for (int k = 0; k < kMonitors; ++k) {
            if (isScopeSource(this->source(k))) continue;
            if (other < 0 || this->source(k) == CorrectionSource::Current) other = k;
        }
        if (other >= 0) {
            active_ = other;
            emit activeMonitorChanged(other);
        }
    }
    for (const Monitor& m : monitors_) m.picture->update();
    return true;
}

std::optional<std::int64_t> CorrectionMonitors::frameOf(int monitor) const {
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (record.isEmpty()) return std::nullopt;
    if (isScopeSource(source(monitor))) return isScopeSource(source(active_)) ? std::nullopt : frameOf(active_);
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
    default: break; // the scopes, above
    }
    return std::nullopt;
}

void CorrectionMonitors::setActiveMonitor(int monitor) {
    if (monitor < 0 || monitor >= kMonitors || monitor == active_ || isScopeSource(source(monitor))) return;
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

void CorrectionMonitors::setSafeColors(const model::SafeColorLimits& limits) {
    if (limits == safeColors_) return;
    safeColors_ = limits;
    for (const Monitor& m : monitors_) {
        if (m.picture) m.picture->update();
    }
}

model::SafeViolations CorrectionMonitors::safeWarnings(int monitor) const {
    if (monitor < 0 || monitor >= kMonitors) return {};
    const auto* picture = static_cast<const CorrectionPicture*>(monitors_.at(static_cast<std::size_t>(monitor)).picture);
    return picture ? picture->warnings_ : model::SafeViolations{};
}

void CorrectionMonitors::setPal(bool pal) {
    if (pal == pal_) return;
    pal_ = pal;
    refresh();
}

void CorrectionMonitors::setDualSplitDuringPlayback(bool on) {
    if (on == dualSplitPlayback_) return;
    dualSplitPlayback_ = on;
    refresh();
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
    for (const Monitor& m : monitors_) {
        m.picture->update();
        m.bar->update();
    }
}

QWidget* CorrectionMonitors::keyframeBar(int monitor) const {
    return monitors_.at(static_cast<std::size_t>(monitor)).bar;
}

void CorrectionMonitors::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        switchCurrentAndReference();
        return;
    }
    // The shape tools' keys first (Delete removes the selected shapes, p.403).
    if (overlayInput_ && overlayInput_->overlayWantsKey(event) && overlayInput_->overlayKey(event)) return;
    // "Delete selected keyframes" (p.686).
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        emit keyframesDeleteRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CorrectionMonitors::setOverlay(const Composer::EffectPreview& overlay) {
    if (overlay == overlay_) return;
    overlay_ = overlay;
    for (const Monitor& m : monitors_) {
        if (m.source == CorrectionSource::Current) m.picture->update();
    }
}

void CorrectionMonitors::syncMenu(int monitor) {
    const Monitor& m = monitors_.at(static_cast<std::size_t>(monitor));
    m.menu->setText(correctionSourceText(m.source));
    // "Keyframe markers display in the position bar below the Current monitor" (p.686).
    m.bar->setVisible(m.source == CorrectionSource::Current);
}

void CorrectionMonitors::dragEnterEvent(QDragEnterEvent* event) {
    if (templateDrop_ && templateDrop_(event->mimeData(), false)) event->acceptProposedAction();
}

void CorrectionMonitors::dragMoveEvent(QDragMoveEvent* event) {
    if (templateDrop_ && templateDrop_(event->mimeData(), false)) event->acceptProposedAction();
}

void CorrectionMonitors::dropEvent(QDropEvent* event) {
    if (templateDrop_ && templateDrop_(event->mimeData(), true)) event->acceptProposedAction();
}

} // namespace lumenis::ui
