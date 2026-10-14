#include "lumenis/ui/timeline.h"

#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {

// Sizes. The guide's pictures show the layout but not the sizes.
constexpr int kRulerHeight = 20;
constexpr int kVideoRow = 30;
constexpr int kAudioRow = 26;
constexpr int kDataRow = 20;
constexpr int kTimecodeRow = 18;
constexpr int kSourceX = 4;
constexpr int kSourceWidth = 30;
constexpr int kRecordX = 38;
constexpr int kRecordWidth = 34;
constexpr int kSyncX = 74;
constexpr int kSyncWidth = 16;
constexpr int kMonitorX = 92;
constexpr int kMonitorWidth = 20;
constexpr int kHeaderWidth = 116;
constexpr int kLabelSpacing = 90; // pixels between ruler labels, at least
constexpr double kMaxScale = 40;  // pixels per frame, fully zoomed in

// Our colours; the guide's pictures show teal video and blue audio
// segments (p.607).
constexpr QRgb kBackground = 0xff353535;
constexpr QRgb kHeader = 0xff2b2b2b;
constexpr QRgb kRuler = 0xff2f2f2f;
constexpr QRgb kRowLine = 0xff262626;
constexpr QRgb kTimecodeRowColor = 0xff2d2d2d;
constexpr QRgb kVideoSegment = 0xff3f8f9c;
constexpr QRgb kAudioSegment = 0xff5d63c9;
constexpr QRgb kDataSegment = 0xff7a7a7a;
constexpr QRgb kButtonOn = 0xff767676;
constexpr QRgb kButtonOff = 0xff444444;
constexpr QRgb kButtonDim = 0xff383838;
constexpr QRgb kInOut = 0x38ffffff;
constexpr QRgb kIndicator = 0xff33c3ff; // the blue position indicator (p.608)
constexpr QRgb kPatchArrow = 0xffffffff; // "a white arrow appears" (p.629)
constexpr QRgb kGlyph = 0xffc8c8c8;
constexpr QRgb kSourceView = 0xff2fb36b; // the source shown in the Timeline
constexpr QRgb kSpliceColor = 0xffe6c619;    // Extract/Splice-in is yellow, Lift/Overwrite red (p.611)
constexpr QRgb kOverwriteColor = 0xffd23a32;
constexpr QRgb kReplaceColor = 0xff3a7bd2;   // Replace is blue (p.467)
constexpr QRgb kSelected = 0xffe8e8e8;       // selected segments
constexpr QRgb kLocked = 0xffd9a13a;

int rowHeight(const QString& track) {
    if (track == QLatin1String("TC1")) return kTimecodeRow;
    const auto ref = model::parseTrackName(track);
    if (!ref) return kTimecodeRow;
    switch (ref->kind) {
    case model::TrackKind::Video: return kVideoRow;
    case model::TrackKind::Audio: return kAudioRow;
    case model::TrackKind::Data: return kDataRow;
    }
    return kVideoRow;
}

// Display order (p.607): D1, video from the top track down, TC1, audio.
int rowOrder(const QString& track) {
    if (track == QLatin1String("TC1")) return 2000;
    const auto ref = model::parseTrackName(track);
    if (!ref) return 5000;
    switch (ref->kind) {
    case model::TrackKind::Data: return ref->number;
    case model::TrackKind::Video: return 1000 - ref->number;
    case model::TrackKind::Audio: return 3000 + ref->number;
    }
    return 5000;
}

// Toolbar pictures, our own drawings.
enum class Picture {
    FastMenu, Focus, SourceRecord, VideoQuality, ClientMonitor, StepIn, StepOut,
    ViewMenu, FindLeft, FindRight, ZoomOut, ZoomIn,
    SegmentSplice, SegmentOverwrite, Lift, Extract, CopyClipboard, AddEdit, Replace, Top, Tail, MatchFrame
};

QIcon picture(Picture which) {
    constexpr int size = 32;
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c(kGlyph);
    p.setPen(QPen(c, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    auto triangle = [&](QPointF a, QPointF b, QPointF d) {
        p.setBrush(c);
        p.drawPolygon(QPolygonF{a, b, d});
        p.setBrush(Qt::NoBrush);
    };
    switch (which) {
    case Picture::FastMenu:
        for (int y : {9, 16, 23}) p.drawLine(QPointF(7, y), QPointF(25, y));
        break;
    case Picture::Focus:
        p.drawLine(QPointF(10, 6), QPointF(6, 6));
        p.drawLine(QPointF(6, 6), QPointF(6, 26));
        p.drawLine(QPointF(6, 26), QPointF(10, 26));
        p.drawLine(QPointF(22, 6), QPointF(26, 6));
        p.drawLine(QPointF(26, 6), QPointF(26, 26));
        p.drawLine(QPointF(26, 26), QPointF(22, 26));
        p.drawLine(QPointF(16, 9), QPointF(16, 23));
        break;
    case Picture::SourceRecord:
        p.drawRect(QRectF(5, 9, 10, 14));
        p.drawRect(QRectF(17, 9, 10, 14));
        break;
    case Picture::VideoQuality:
        p.drawEllipse(QRectF(7, 7, 18, 18));
        p.setBrush(c);
        p.drawPie(QRectF(7, 7, 18, 18), 90 * 16, 180 * 16);
        break;
    case Picture::ClientMonitor:
        p.drawRect(QRectF(6, 8, 20, 13));
        p.drawLine(QPointF(12, 25), QPointF(20, 25));
        break;
    case Picture::StepIn:
        p.drawRect(QRectF(7, 15, 18, 11));
        p.drawLine(QPointF(16, 4), QPointF(16, 19));
        triangle({11, 14}, {21, 14}, {16, 20});
        break;
    case Picture::StepOut:
        p.drawRect(QRectF(7, 15, 18, 11));
        p.drawLine(QPointF(16, 20), QPointF(16, 6));
        triangle({11, 9}, {21, 9}, {16, 3});
        break;
    case Picture::ViewMenu:
        p.drawLine(QPointF(6, 10), QPointF(26, 10));
        p.drawLine(QPointF(6, 16), QPointF(26, 16));
        p.drawLine(QPointF(6, 22), QPointF(18, 22));
        triangle({20, 20}, {27, 20}, {23.5, 25});
        break;
    case Picture::FindLeft:
        triangle({20, 8}, {20, 24}, {9, 16});
        p.drawLine(QPointF(24, 8), QPointF(24, 24));
        break;
    case Picture::FindRight:
        triangle({12, 8}, {12, 24}, {23, 16});
        p.drawLine(QPointF(8, 8), QPointF(8, 24));
        break;
    case Picture::ZoomOut:
        p.drawLine(QPointF(8, 16), QPointF(24, 16));
        break;
    case Picture::ZoomIn:
        p.drawLine(QPointF(8, 16), QPointF(24, 16));
        p.drawLine(QPointF(16, 8), QPointF(16, 24));
        break;
    case Picture::SegmentSplice:
    case Picture::SegmentOverwrite: {
        // The yellow and red segment arrows (p.611).
        const QColor arrow(which == Picture::SegmentSplice ? kSpliceColor : kOverwriteColor);
        p.setPen(QPen(arrow, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(5, 16), QPointF(20, 16));
        p.setBrush(arrow);
        p.drawPolygon(QPolygonF{QPointF(18, 9), QPointF(27, 16), QPointF(18, 23)});
        break;
    }
    case Picture::Lift:
        p.drawRect(QRectF(5, 18, 22, 8));
        p.drawLine(QPointF(16, 15), QPointF(16, 4));
        triangle({11, 9}, {21, 9}, {16, 3});
        break;
    case Picture::Extract:
        p.drawRect(QRectF(5, 18, 9, 8));
        p.drawRect(QRectF(18, 18, 9, 8));
        p.drawLine(QPointF(16, 15), QPointF(16, 4));
        triangle({11, 9}, {21, 9}, {16, 3});
        break;
    case Picture::CopyClipboard:
        p.drawRect(QRectF(6, 6, 14, 16));
        p.drawRect(QRectF(12, 11, 14, 16));
        break;
    case Picture::AddEdit:
        p.drawRect(QRectF(4, 9, 24, 14));
        p.drawLine(QPointF(16, 5), QPointF(16, 27));
        break;
    case Picture::Replace:
        p.setPen(QPen(QColor(kReplaceColor), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawRect(QRectF(6, 9, 20, 14));
        p.drawLine(QPointF(10, 16), QPointF(22, 16));
        break;
    case Picture::Top:
        p.drawLine(QPointF(8, 6), QPointF(8, 26));
        p.drawLine(QPointF(8, 16), QPointF(24, 16));
        triangle({14, 10}, {14, 22}, {8, 16});
        break;
    case Picture::Tail:
        p.drawLine(QPointF(24, 6), QPointF(24, 26));
        p.drawLine(QPointF(8, 16), QPointF(24, 16));
        triangle({18, 10}, {18, 22}, {24, 16});
        break;
    case Picture::MatchFrame:
        p.drawRect(QRectF(4, 8, 10, 16));
        p.drawRect(QRectF(18, 8, 10, 16));
        p.drawLine(QPointF(14, 13), QPointF(18, 13));
        p.drawLine(QPointF(14, 19), QPointF(18, 19));
        break;
    }
    return QIcon(pm);
}

// Ruler label spacing: whole frames, seconds, minutes or hours.
std::int64_t labelInterval(double scale, int fps) {
    const std::int64_t f = fps;
    const std::int64_t steps[] = {1,       2,       5,       10,       f,        2 * f,     5 * f,      10 * f,
                                  15 * f,  30 * f,  60 * f,  120 * f,  300 * f,  600 * f,   900 * f,    1800 * f,
                                  3600 * f};
    for (std::int64_t s : steps) {
        if (static_cast<double>(s) * scale >= kLabelSpacing) return s;
    }
    return 3600 * f;
}

} // namespace

// ------------------------------------------------------------------ the view

TimelineView::TimelineView(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("timelineView"));
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(kRulerHeight + kVideoRow);
}

int TimelineView::rulerHeight() const {
    return kRulerHeight;
}

int TimelineView::headerWidth() const {
    return timeline_.display().trackPanel ? kHeaderWidth : 0;
}

QList<TimelineView::Row> TimelineView::rows() const {
    QStringList names;
    QSet<QString> missing;
    if (timeline_.hasShown()) {
        names = timeline_.shown().trackNames();
        if (timeline_.display().timecodeTrack && timeline_.shown().hasTimecodeTrack()) names << QStringLiteral("TC1");
        // A source track patched to a track the sequence lacks shows where
        // the edit will put it (p.626).
        if (!timeline_.showsSource()) {
            for (const QString& s : timeline_.patch().sourceTracks()) {
                const QString r = timeline_.patch().recordFor(s);
                if (!names.contains(r)) {
                    names << r;
                    missing.insert(r);
                }
            }
        }
    }
    std::stable_sort(names.begin(), names.end(), [](const QString& a, const QString& b) { return rowOrder(a) < rowOrder(b); });
    QList<Row> out;
    int y = kRulerHeight - top_;
    for (const QString& n : names) {
        Row r;
        r.track = n;
        r.top = y;
        r.height = rowHeight(n);
        r.exists = !missing.contains(n);
        out << r;
        y += r.height;
    }
    return out;
}

int TimelineView::contentHeight() const {
    int h = kRulerHeight;
    for (const Row& r : rows()) h += r.height;
    return h;
}

void TimelineView::setTopOffset(int offset) {
    top_ = std::clamp(offset, 0, std::max(contentHeight() + top_ - height(), 0));
    update();
}

std::int64_t TimelineView::frameAt(int x) const {
    const double scale = timeline_.scale();
    if (scale <= 0) return 0;
    return static_cast<std::int64_t>(std::floor(timeline_.firstFrame() + (x - headerWidth()) / scale));
}

double TimelineView::xFor(std::int64_t frame) const {
    return headerWidth() + (static_cast<double>(frame) - timeline_.firstFrame()) * timeline_.scale();
}

QRect TimelineView::sourceButton(const Row& row) const {
    return QRect(kSourceX, row.top + 2, kSourceWidth, std::max(row.height - 4, 12));
}

QRect TimelineView::recordButton(const Row& row) const {
    return QRect(kRecordX, row.top + 2, kRecordWidth, std::max(row.height - 4, 12));
}

QRect TimelineView::syncButton(const Row& row) const {
    return QRect(kSyncX, row.top + 2, kSyncWidth, std::max(row.height - 4, 12));
}

QRect TimelineView::monitorButton(const Row& row) const {
    return QRect(kMonitorX, row.top + 2, kMonitorWidth, std::max(row.height - 4, 12));
}

std::optional<TimelineView::Row> TimelineView::rowAt(const QPoint& pos) const {
    if (pos.y() < kRulerHeight) return std::nullopt;
    for (const Row& r : rows()) {
        if (pos.y() >= r.top && pos.y() < r.top + r.height) return r;
    }
    return std::nullopt;
}

void TimelineView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    setTopOffset(top_);
}

void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(kBackground));
    const int header = headerWidth();
    const QRect tracksArea(header, kRulerHeight, width() - header, height() - kRulerHeight);
    const QList<Row> rowList = rows();
    const model::Sequence& seq = timeline_.shown();
    const double scale = timeline_.scale();
    const std::int64_t fps = std::max(timeline_.format().nominalFps, 1);
    const std::int64_t start = timeline_.startFrame();
    const QColor text = palette().color(QPalette::WindowText);
    const QColor bright = palette().color(QPalette::BrightText);
    const MonitorId monitor = timeline_.shownMonitor();
    const model::MonitorState& state = timeline_.composer().state(monitor);
    const std::int64_t firstVisible = frameAt(header);
    const std::int64_t lastVisible = frameAt(width()) + 1;
    auto tc = [&](std::int64_t frame) {
        return QString::fromStdString(Timecode(std::max<std::int64_t>(start + frame, 0), timeline_.format()).toString());
    };
    const std::int64_t interval = scale > 0 ? labelInterval(scale, static_cast<int>(fps)) : fps;

    // Tracks and segments.
    p.save();
    p.setClipRect(tracksArea);
    QFont small = font();
    small.setPointSizeF(std::max(small.pointSizeF() - 1, 6.0));
    p.setFont(small);
    for (const Row& r : rowList) {
        if (r.top + r.height < kRulerHeight || r.top > height()) continue;
        const QRect rowRect(header, r.top, width() - header, r.height);
        if (r.track == QLatin1String("TC1")) {
            // TC1: the project's timecode (p.598).
            p.fillRect(rowRect, QColor(kTimecodeRowColor));
            p.setPen(text);
            for (std::int64_t f = (firstVisible / interval) * interval; f <= lastVisible; f += interval) {
                if (f < 0) continue;
                const int x = static_cast<int>(xFor(f));
                p.drawText(QRect(x + 3, r.top, kLabelSpacing, r.height), Qt::AlignVCenter | Qt::AlignLeft, tc(f));
            }
        } else if (const model::Track* t = r.exists ? seq.track(r.track) : nullptr) {
            const QColor fill(t->kind == model::TrackKind::Video   ? kVideoSegment
                              : t->kind == model::TrackKind::Audio ? kAudioSegment
                                                                   : kDataSegment);
            if (t->locked) p.fillRect(rowRect, QColor(kTimecodeRowColor));
            std::int64_t at = 0;
            const model::Segment* previous = nullptr;
            for (const model::Segment& s : t->segments) {
                const std::int64_t end = at + s.length;
                const bool selected = !timeline_.showsSource() &&
                                      timeline_.selection().contains(model::SegmentRef{r.track, at});
                if (end >= firstVisible && at <= lastVisible && previous && model::Sequence::isMatchFrame(*previous, s)) {
                    // A match-frame edit shows an equal sign (p.640).
                    const double x = xFor(at);
                    p.setPen(QPen(QColor(kGlyph), 1.5));
                    p.drawLine(QPointF(x - 3, r.top + r.height / 2.0 - 2), QPointF(x + 3, r.top + r.height / 2.0 - 2));
                    p.drawLine(QPointF(x - 3, r.top + r.height / 2.0 + 2), QPointF(x + 3, r.top + r.height / 2.0 + 2));
                }
                if (s.isFiller() && selected) {
                    const QRectF box(xFor(at), r.top + 1, xFor(end) - xFor(at), r.height - 2);
                    p.setPen(QPen(QColor(kSelected), 2));
                    p.drawRect(box.adjusted(1, 1, -1, -1));
                }
                if (!s.isFiller() && end >= firstVisible && at <= lastVisible) {
                    const double x0 = std::max(xFor(at), static_cast<double>(header - 1));
                    const double x1 = std::min(xFor(end), static_cast<double>(width() + 1));
                    const QRectF box(x0, r.top + 1, std::max(x1 - x0, 1.0), r.height - 2);
                    p.fillRect(box, selected ? fill.lighter(135) : fill);
                    p.setPen(fill.darker(160));
                    p.drawRect(box.adjusted(0, 0, -1, -1));
                    if (selected) {
                        p.setPen(QPen(QColor(kSelected), 2));
                        p.drawRect(box.adjusted(1, 1, -1, -1));
                    }
                    // Clip text (Fast menu, p.592).
                    QStringList parts;
                    const auto& d = timeline_.display();
                    if (d.clipNames) parts << s.name;
                    if (d.clipTracks) parts << s.sourceTrack;
                    if (d.clipDurations) parts << state.durationText(s.length);
                    if (!parts.isEmpty() && box.width() > 12) {
                        p.setPen(bright);
                        const QRectF textBox = box.adjusted(4, 0, -3, 0);
                        p.drawText(textBox, Qt::AlignVCenter | Qt::AlignLeft,
                                   p.fontMetrics().elidedText(parts.join(QStringLiteral("  ")), Qt::ElideRight,
                                                              static_cast<int>(textBox.width())));
                    }
                }
                previous = &s;
                at = end;
            }
        }
        p.setPen(QColor(kRowLine));
        p.drawLine(header, r.top + r.height - 1, width(), r.top + r.height - 1);
    }
    // IN to OUT highlighting (p.638), on the selected tracks.
    if (!state.isEmpty() && (state.in() || state.out())) {
        const double x0 = xFor(state.markedStart());
        const double x1 = xFor(state.markedEnd() + 1);
        for (const Row& r : rowList) {
            const bool selected = timeline_.showsSource() ? timeline_.patch().isSourceSelected(r.track)
                                                          : timeline_.patch().isRecordSelected(r.track);
            if (r.exists && selected && r.track != QLatin1String("TC1")) {
                p.fillRect(QRectF(x0, r.top, x1 - x0, r.height - 1), QColor::fromRgba(kInOut));
            }
        }
    }
    p.restore();

    // The ruler: master timecode (p.598).
    const QRect ruler(header, 0, width() - header, kRulerHeight);
    p.fillRect(ruler, QColor(kRuler));
    p.save();
    p.setClipRect(ruler);
    p.setFont(small);
    if (scale > 0) {
        const std::int64_t minor = std::max<std::int64_t>(interval / 5, 1);
        p.setPen(QColor(0xff8a8a8a));
        for (std::int64_t f = (firstVisible / minor) * minor; f <= lastVisible; f += minor) {
            if (f < 0) continue;
            const int x = static_cast<int>(xFor(f));
            const bool major = f % interval == 0;
            p.drawLine(x, kRulerHeight - (major ? 9 : 4), x, kRulerHeight - 1);
        }
        p.setPen(text);
        for (std::int64_t f = (firstVisible / interval) * interval; f <= lastVisible; f += interval) {
            if (f < 0) continue;
            const int x = static_cast<int>(xFor(f));
            p.drawText(QRect(x + 3, 0, kLabelSpacing, kRulerHeight - 8), Qt::AlignVCenter | Qt::AlignLeft, tc(f));
        }
    }
    // Marks in the ruler.
    if (!state.isEmpty()) {
        p.setPen(QPen(QColor(kGlyph), 1.5));
        if (state.in()) {
            const double x = xFor(*state.in());
            p.drawLine(QPointF(x, 3), QPointF(x, kRulerHeight - 2));
            p.drawLine(QPointF(x, 3), QPointF(x + 5, 3));
            p.drawLine(QPointF(x, kRulerHeight - 2), QPointF(x + 5, kRulerHeight - 2));
        }
        if (state.out()) {
            const double x = xFor(*state.out() + 1);
            p.drawLine(QPointF(x, 3), QPointF(x, kRulerHeight - 2));
            p.drawLine(QPointF(x, 3), QPointF(x - 5, 3));
            p.drawLine(QPointF(x, kRulerHeight - 2), QPointF(x - 5, kRulerHeight - 2));
        }
    }
    p.restore();

    // The position indicator, and its shadow at the end of the frame when
    // zoomed in (p.608).
    if (timeline_.hasShown()) {
        p.save();
        p.setClipRect(QRect(header, 0, width() - header, height()));
        const double x = xFor(state.position());
        p.setPen(QPen(QColor(kIndicator), 1));
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
        if (scale >= 4) {
            p.setPen(QPen(QColor(kIndicator), 1, Qt::DotLine));
            p.drawLine(QPointF(x + scale, kRulerHeight), QPointF(x + scale, height()));
        }
        p.setBrush(QColor(kIndicator));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF{QPointF(x - 5, 0), QPointF(x + 5, 0), QPointF(x, 7)});
        p.restore();
    }

    // Segments being dragged: their outlines at the new place (p.612).
    if (dragging_) {
        const std::int64_t frames = dragFrames(QApplication::keyboardModifiers());
        const int tracks = dragTracks();
        p.save();
        p.setClipRect(tracksArea);
        p.setPen(QPen(QColor(dragKind_ == model::EditKind::SpliceIn ? kSpliceColor : kOverwriteColor), 2));
        p.setBrush(Qt::NoBrush);
        for (const model::SegmentRef& ref : timeline_.selection()) {
            const auto seg = model::segmentFor(seq, ref);
            const auto from = model::parseTrackName(ref.track);
            if (!seg || !from) continue;
            const QString to = model::trackName(from->kind, from->number + tracks);
            int top = -1;
            int h = 0;
            for (const Row& r : rowList) {
                if (r.track == to) {
                    top = r.top;
                    h = r.height;
                }
            }
            if (top < 0) {
                // A new track above or below.
                top = tracks > 0 ? kRulerHeight : height() - kAudioRow;
                h = kAudioRow;
            }
            p.drawRect(QRectF(xFor(ref.start + frames), top + 2, xFor(ref.start + frames + seg->length) - xFor(ref.start + frames),
                              h - 4));
        }
        p.restore();
    }
    // The lasso: a dotted outline (p.611).
    if (lassoFrom_) {
        p.setPen(QPen(Qt::white, 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(*lassoFrom_, lassoTo_).normalized());
    }

    // The zoom area being picked.
    if (zoomFrom_) {
        p.setPen(QPen(Qt::white, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(QPoint(std::min(*zoomFrom_, zoomTo_), 0), QPoint(std::max(*zoomFrom_, zoomTo_), height() - 1)));
    }

    // The Track Selector panel (p.625).
    if (header > 0) {
        p.fillRect(QRect(0, 0, header, height()), QColor(kHeader));
        p.save();
        p.setClipRect(QRect(0, kRulerHeight, header, height() - kRulerHeight));
        p.setFont(small);
        const model::TrackPatch& patch = timeline_.patch();
        const bool sourceView = timeline_.showsSource();
        std::optional<model::TrackKind> dragKind;
        if (!patchFrom_.isEmpty()) {
            if (const auto ref = model::parseTrackName(patchFrom_)) dragKind = ref->kind;
        }
        auto button = [&](const QRect& box, const QString& label, bool on, bool dim) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(dim ? kButtonDim : on ? kButtonOn : kButtonOff));
            p.drawRoundedRect(box, 2, 2);
            p.setPen(dim ? text.darker(150) : text);
            p.drawText(box, Qt::AlignCenter, label);
        };
        for (const Row& r : rowList) {
            if (r.track == QLatin1String("TC1")) {
                button(recordButton(r), r.track, false, true);
                // The sync icon next to TC1: Sync Lock All (p.493).
                bool all = !seq.tracks().isEmpty();
                for (const model::Track& t : seq.tracks()) all = all && t.syncLocked;
                const QPointF c = QRectF(syncButton(r)).center();
                p.setPen(QPen(all ? QColor(kGlyph) : QColor(kButtonOff).lighter(130), 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QRectF(c.x() - 5, c.y() - 3, 6, 6));
                p.drawEllipse(QRectF(c.x() - 1, c.y() - 3, 6, 6));
                continue;
            }
            // Source tracks sit beside the record track they are patched to
            // (p.630).
            const QString source = sourceView ? (patch.sourceTracks().contains(r.track) ? r.track : QString())
                                              : patch.sourceFor(r.track);
            if (!source.isEmpty()) button(sourceButton(r), source, patch.isSourceSelected(source), false);
            if (r.exists && !sourceView) {
                const auto ref = model::parseTrackName(r.track);
                const bool cannotTake = dragKind && (!ref || ref->kind != *dragKind || ref->kind == model::TrackKind::Data);
                button(recordButton(r), r.track, patch.isRecordSelected(r.track), cannotTake);
                const model::Track* t = seq.track(r.track);
                if (t && t->locked) {
                    // A padlock on the track button (p.633).
                    const QRect b = recordButton(r);
                    const QRectF body(b.left() + 3, b.center().y() - 1, 7, 6);
                    p.setPen(QPen(QColor(kLocked), 1.2));
                    p.setBrush(QColor(kLocked));
                    p.drawRect(body);
                    p.setBrush(Qt::NoBrush);
                    p.drawArc(QRectF(body.left() + 1, body.top() - 5, 5, 8), 0, 180 * 16);
                }
                // Sync Lock buttons (p.633).
                if (t) {
                    const QRect sb = syncButton(r);
                    const QPointF c = QRectF(sb).center();
                    p.setPen(QPen(t->syncLocked ? QColor(kGlyph) : QColor(kButtonOff).lighter(130), 1.5));
                    p.setBrush(Qt::NoBrush);
                    p.drawEllipse(QRectF(c.x() - 5, c.y() - 3, 6, 6));
                    p.drawEllipse(QRectF(c.x() - 1, c.y() - 3, 6, 6));
                }
            }
            // Track Monitor buttons; not for data tracks (p.628).
            if (r.exists && !r.track.startsWith(QLatin1Char('D'))) {
                const QRect m = monitorButton(r);
                const bool on = timeline_.isMonitored(r.track);
                p.setPen(QPen(on ? QColor(kGlyph) : QColor(kButtonOff).lighter(130), 1.5));
                p.setBrush(Qt::NoBrush);
                const QPointF c = QRectF(m).center();
                if (r.track.startsWith(QLatin1Char('V'))) {
                    p.drawRect(QRectF(c.x() - 6, c.y() - 4, 12, 8));
                } else {
                    p.drawPolygon(QPolygonF{QPointF(c.x() - 6, c.y() - 2.5), QPointF(c.x() - 3, c.y() - 2.5),
                                            QPointF(c.x() + 2, c.y() - 6), QPointF(c.x() + 2, c.y() + 6),
                                            QPointF(c.x() - 3, c.y() + 2.5), QPointF(c.x() - 6, c.y() + 2.5)});
                }
            }
        }
        p.restore();
        if (sourceView) {
            p.fillRect(QRect(0, 0, header, kRulerHeight), QColor(kSourceView).darker(180));
        }
        p.setPen(QColor(kRowLine));
        p.drawLine(header - 1, 0, header - 1, height());
    }

    // A patch being dragged: a white arrow (p.629).
    if (!patchFrom_.isEmpty()) {
        for (const Row& r : rowList) {
            if (timeline_.patch().sourceFor(r.track) != patchFrom_) continue;
            const QPointF from = QRectF(sourceButton(r)).center();
            const QPointF to = patchPos_;
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(kPatchArrow), 2));
            p.drawLine(from, to);
            const QPointF dir = to - from;
            const double len = std::hypot(dir.x(), dir.y());
            if (len > 1) {
                const QPointF u = dir / len;
                const QPointF n(-u.y(), u.x());
                p.setBrush(QColor(kPatchArrow));
                p.drawPolygon(QPolygonF{to, to - u * 8 + n * 4, to - u * 8 - n * 4});
            }
            break;
        }
    }
}

void TimelineView::moveIndicator(const QPoint& pos, Qt::KeyboardModifiers modifiers) {
    std::int64_t frame = std::max<std::int64_t>(frameAt(pos.x()), 0);
    // Ctrl snaps to the head of transitions, Ctrl+Alt to the tail (p.609).
    if (modifiers & Qt::ControlModifier) {
        const QList<std::int64_t> points = timeline_.composer().events(timeline_.shownMonitor());
        std::int64_t best = frame;
        std::int64_t distance = -1;
        for (std::int64_t e : points) {
            const std::int64_t d = std::abs(e - frame);
            if (distance < 0 || d < distance) {
                distance = d;
                best = e;
            }
        }
        frame = (modifiers & Qt::AltModifier) ? std::max<std::int64_t>(best - 1, 0) : best;
    }
    Composer& composer = timeline_.composer();
    composer.stop();
    composer.setActiveMonitor(timeline_.shownMonitor());
    composer.moveTo(timeline_.shownMonitor(), frame);
}

std::optional<model::SegmentRef> TimelineView::segmentAt(const QPoint& pos) const {
    if (pos.x() < headerWidth() || timeline_.showsSource()) return std::nullopt;
    const auto row = rowAt(pos);
    if (!row || !row->exists) return std::nullopt;
    const model::Track* t = timeline_.shown().track(row->track);
    std::int64_t start = 0;
    const int i = t ? t->segmentAt(frameAt(pos.x()), &start) : -1;
    if (i < 0) return std::nullopt;
    // Not filler at the head or tail of the track (p.611).
    if (t->segments.at(i).isFiller() && (i == 0 || i == t->segments.size() - 1)) return std::nullopt;
    return model::SegmentRef{row->track, start};
}

model::EditKind TimelineView::segmentModeAt(const QPoint& pos) const {
    const bool lift = timeline_.isSegmentTool(model::EditKind::Overwrite);
    const bool splice = timeline_.isSegmentTool(model::EditKind::SpliceIn);
    if (lift != splice) return lift ? model::EditKind::Overwrite : model::EditKind::SpliceIn;
    if (lift && splice) {
        if (const auto row = rowAt(pos)) {
            return pos.y() < row->top + row->height / 2 ? model::EditKind::Overwrite : model::EditKind::SpliceIn;
        }
    }
    return timeline_.segmentKind();
}

std::int64_t TimelineView::dragFrames(Qt::KeyboardModifiers modifiers) const {
    if (!dragFrom_) return 0;
    std::int64_t frames = frameAt(dragPos_.x()) - frameAt(dragFrom_->x());
    // Ctrl snaps the selection's head to the nearest edit (p.609).
    if ((modifiers & Qt::ControlModifier) && !timeline_.selection().isEmpty()) {
        std::int64_t head = timeline_.selection().first().start;
        for (const auto& ref : timeline_.selection()) head = std::min(head, ref.start);
        const std::int64_t target = head + frames;
        std::int64_t best = target;
        std::int64_t distance = -1;
        for (std::int64_t e : timeline_.shown().editPoints()) {
            if (distance < 0 || std::abs(e - target) < distance) {
                distance = std::abs(e - target);
                best = e;
            }
        }
        frames = best - head;
    }
    return frames;
}

int TimelineView::dragTracks() const {
    if (!dragFrom_) return 0;
    const auto from = rowAt(*dragFrom_);
    const auto to = rowAt(dragPos_);
    if (!from || !to) return 0;
    const auto a = model::parseTrackName(from->track);
    const auto b = model::parseTrackName(to->track);
    if (!a || !b || a->kind != b->kind) return 0;
    return b->number - a->number;
}

void TimelineView::selectLasso(const QRect& area) {
    // Segments wholly inside the lasso's span, on the rows it crosses
    // (p.611).
    QList<model::SegmentRef> picked;
    const std::int64_t from = frameAt(area.left());
    const std::int64_t to = frameAt(area.right());
    for (const Row& r : rows()) {
        if (!r.exists || r.top + r.height < area.top() || r.top > area.bottom()) continue;
        const model::Track* t = timeline_.shown().track(r.track);
        if (!t) continue;
        std::int64_t at = 0;
        for (int i = 0; i < t->segments.size(); ++i) {
            const std::int64_t end = at + t->segments.at(i).length;
            const bool edge = i == 0 || i == t->segments.size() - 1;
            if (at >= from && end <= to + 1 && !(t->segments.at(i).isFiller() && edge)) picked << model::SegmentRef{r.track, at};
            at = end;
        }
    }
    // A lasso with no segment tool turns on Lift/Overwrite (p.612).
    if (!picked.isEmpty() && !timeline_.isSegmentTool(model::EditKind::Overwrite) &&
        !timeline_.isSegmentTool(model::EditKind::SpliceIn)) {
        timeline_.setSegmentTool(model::EditKind::Overwrite, true);
    }
    timeline_.setSelection(picked);
}

void TimelineView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const QPoint pos = event->position().toPoint();
    if (pos.x() >= headerWidth()) {
        if (zoomSelecting_) {
            zoomFrom_ = pos.x();
            zoomTo_ = pos.x();
            update();
            return;
        }
        // Alt-drag in the tracks draws a lasso (p.611).
        if ((event->modifiers() & Qt::AltModifier) && pos.y() >= kRulerHeight && !timeline_.showsSource()) {
            lassoFrom_ = pos;
            lassoTo_ = pos;
            return;
        }
        const bool tools = timeline_.isSegmentTool(model::EditKind::Overwrite) ||
                           timeline_.isSegmentTool(model::EditKind::SpliceIn);
        if (tools && pos.y() >= kRulerHeight) {
            if (const auto seg = segmentAt(pos)) {
                const bool shift = event->modifiers() & Qt::ShiftModifier;
                if (shift || !timeline_.selection().contains(*seg)) timeline_.clickSegment(*seg, shift);
                if (timeline_.selection().contains(*seg)) {
                    dragFrom_ = pos;
                    dragPos_ = pos;
                    dragKind_ = segmentModeAt(pos);
                }
                return;
            }
        }
        timeline_.setSelection({});
        scrubbing_ = true;
        moveIndicator(pos, event->modifiers());
        return;
    }
    const auto row = rowAt(pos);
    if (!row) return;
    if (sourceButton(*row).contains(pos)) {
        const QString source = timeline_.showsSource() ? row->track : timeline_.patch().sourceFor(row->track);
        if (timeline_.patch().sourceTracks().contains(source)) {
            patchFrom_ = source;
            patchPos_ = pos;
        }
    } else if (recordButton(*row).contains(pos) && row->exists && !timeline_.showsSource() &&
               row->track != QLatin1String("TC1")) {
        timeline_.toggleRecordTrack(row->track);
    } else if (syncButton(*row).contains(pos) && !timeline_.showsSource()) {
        if (row->track == QLatin1String("TC1")) {
            timeline_.toggleSyncLockAll();
        } else if (row->exists) {
            timeline_.toggleSyncLock(row->track);
        }
    } else if (monitorButton(*row).contains(pos) && row->exists) {
        timeline_.toggleMonitor(row->track);
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (zoomFrom_) {
        zoomTo_ = pos.x();
        update();
    } else if (lassoFrom_) {
        lassoTo_ = pos;
        update();
    } else if (dragFrom_) {
        dragPos_ = pos;
        dragging_ = dragging_ || (pos - *dragFrom_).manhattanLength() >= QApplication::startDragDistance();
        update();
    } else if (scrubbing_) {
        moveIndicator(pos, event->modifiers());
    } else if (!patchFrom_.isEmpty()) {
        patchPos_ = pos;
        update();
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (zoomFrom_) {
        const int from = *zoomFrom_;
        zoomFrom_.reset();
        zoomSelecting_ = false;
        if (std::abs(pos.x() - from) > 3) {
            timeline_.zoomTo(frameAt(std::min(from, pos.x())), frameAt(std::max(from, pos.x())));
        }
        update();
        return;
    }
    if (lassoFrom_) {
        const QRect area = QRect(*lassoFrom_, pos).normalized();
        lassoFrom_.reset();
        selectLasso(area);
        update();
        return;
    }
    if (dragFrom_) {
        dragPos_ = pos;
        const bool moved = dragging_;
        const std::int64_t frames = dragFrames(event->modifiers());
        const int tracks = dragTracks();
        dragFrom_.reset();
        dragging_ = false;
        // Dropped outside the Timeline: cancelled (p.620). Alt copies.
        if (moved && rect().contains(pos) && (frames != 0 || tracks != 0)) {
            timeline_.moveSelection(frames, tracks, dragKind_, event->modifiers() & Qt::AltModifier);
        }
        update();
        return;
    }
    scrubbing_ = false;
    if (patchFrom_.isEmpty()) return;
    const QString source = patchFrom_;
    patchFrom_.clear();
    const auto row = rowAt(pos);
    // A click toggles the source track; a drag to a record track patches it.
    if (row && (recordButton(*row).contains(pos) || sourceButton(*row).contains(pos)) &&
        timeline_.patch().recordFor(source) != row->track) {
        timeline_.patchTracks(source, row->track);
    } else if (row && sourceButton(*row).contains(pos)) {
        timeline_.toggleSourceTrack(source);
    }
    update();
}

void TimelineView::contextMenuEvent(QContextMenuEvent* event) {
    QString track;
    if (const auto row = rowAt(event->pos()); row && event->pos().x() < headerWidth()) track = row->track;
    QMenu* menu = timeline_.contextMenu(track);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

void TimelineView::wheelEvent(QWheelEvent* event) {
    const QPoint delta = event->angleDelta();
    if (delta.x() != 0 || (event->modifiers() & Qt::ShiftModifier)) {
        const int d = delta.x() != 0 ? delta.x() : delta.y();
        timeline_.setFirstFrame(timeline_.firstFrame() - d / 120.0 * 40 / std::max(timeline_.scale(), 0.001));
    } else {
        setTopOffset(top_ - delta.y() / 4);
        if (auto* bar = parentWidget() ? parentWidget()->findChild<QScrollBar*>(QStringLiteral("timelineVScroll")) : nullptr) {
            bar->setValue(top_);
        }
    }
    event->accept();
}

// ------------------------------------------------------------------ Timeline

Timeline::Timeline(Composer& composer, QWidget* parent) : QWidget(parent), composer_(composer) {
    setObjectName(QStringLiteral("Timeline"));
    view_ = new TimelineView(*this, this);
    vscroll_ = new QScrollBar(Qt::Vertical, this);
    vscroll_->setObjectName(QStringLiteral("timelineVScroll"));
    connect(vscroll_, &QScrollBar::valueChanged, view_, &TimelineView::setTopOffset);

    // The bottom toolbar, left to right (p.607).
    toolbar_ = new QWidget(this);
    toolbar_->setObjectName(QStringLiteral("timelineToolbar"));
    auto* bar = new QHBoxLayout(toolbar_);
    bar->setContentsMargins(2, 1, 2, 1);
    bar->setSpacing(1);
    auto tool = [this, bar](const char* name, Picture pic, const QString& tip, bool enabled) {
        auto* b = new QToolButton(toolbar_);
        b->setObjectName(QString::fromLatin1(name));
        b->setAutoRaise(true);
        b->setFixedSize(26, kToolButtonHeight);
        b->setIcon(picture(pic));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setEnabled(enabled);
        bar->addWidget(b);
        return b;
    };
    QToolButton* fast = tool("fastMenu", Picture::FastMenu, tr("Timeline Fast Menu"), true);
    fastMenu_ = new QMenu(fast);
    fast->setPopupMode(QToolButton::InstantPopup);
    fast->setMenu(fastMenu_);
    connect(fastMenu_, &QMenu::aboutToShow, this, &Timeline::fastMenu);
    QToolButton* focus = tool("focus", Picture::Focus, tr("Focus"), true);
    focus->setCheckable(true);
    connect(focus, &QToolButton::clicked, this, &Timeline::toggleFocus);
    QToolButton* toggle = tool("toggleSourceRecord", Picture::SourceRecord, tr("Toggle Source/Record in Timeline"), true);
    toggle->setCheckable(true);
    connect(toggle, &QToolButton::clicked, this, [this](bool on) { setShowSource(on); });
    // These need media, effects or the Search feature (later phases).
    tool("videoQuality", Picture::VideoQuality, tr("Video Quality Menu"), false);
    tool("clientMonitor", Picture::ClientMonitor, tr("Client Monitor"), false);
    tool("stepIn", Picture::StepIn, tr("Step In"), false);
    tool("stepOut", Picture::StepOut, tr("Step Out"), false);
    tool("viewMenu", Picture::ViewMenu, tr("Timeline View"), false);
    search_ = new QLineEdit(toolbar_);
    search_->setObjectName(QStringLiteral("timelineSearch"));
    search_->setFixedSize(110, kToolButtonHeight - 2);
    search_->setEnabled(false);
    search_->setToolTip(tr("Search in visible timeline text"));
    bar->addWidget(search_);
    tool("findLeft", Picture::FindLeft, tr("Find to the left"), false);
    tool("findRight", Picture::FindRight, tr("Find to the right"), false);
    QToolButton* zoomOut = tool("zoomOut", Picture::ZoomOut, tr("Zoom Out"), true);
    scaleBar_ = new QSlider(Qt::Horizontal, toolbar_);
    scaleBar_->setObjectName(QStringLiteral("scaleBar"));
    scaleBar_->setRange(0, 1000);
    scaleBar_->setFixedWidth(120);
    bar->addWidget(scaleBar_);
    QToolButton* zoomIn = tool("zoomIn", Picture::ZoomIn, tr("Zoom In"), true);
    scroll_ = new QScrollBar(Qt::Horizontal, toolbar_);
    scroll_->setObjectName(QStringLiteral("timelineScroll"));
    bar->addWidget(scroll_, 1);
    connect(scaleBar_, &QSlider::valueChanged, this, [this](int v) {
        if (!syncing_) setScale(scaleFor(v));
    });
    connect(zoomOut, &QToolButton::clicked, this, [this] { setScale(scale() / 2); });
    connect(zoomIn, &QToolButton::clicked, this, [this] { setScale(scale() * 2); });
    connect(scroll_, &QScrollBar::valueChanged, this, [this](int v) {
        if (!syncing_) setFirstFrame(v);
    });

    // The Timeline Top toolbar (p.603, picture on p.607): the master
    // timecode, then buttons. The guide shows it but does not list its
    // buttons; ours are the segment tools (p.621) and editing buttons from
    // the Command Palette's Edit and Other tabs.
    topbar_ = new QWidget(this);
    topbar_->setObjectName(QStringLiteral("timelineTopToolbar"));
    auto* top = new QHBoxLayout(topbar_);
    top->setContentsMargins(2, 1, 2, 1);
    top->setSpacing(1);
    timecode_ = new QLabel(topbar_);
    timecode_->setObjectName(QStringLiteral("timelineTimecode"));
    timecode_->setFixedSize(kHeaderWidth - 4, kToolButtonHeight);
    timecode_->setAlignment(Qt::AlignCenter);
    timecode_->setAutoFillBackground(true);
    {
        QPalette pal = timecode_->palette();
        pal.setColor(QPalette::Window, Qt::black);
        pal.setColor(QPalette::WindowText, QColor(0xff4cd98c)); // green, as in the picture
        timecode_->setPalette(pal);
    }
    top->addWidget(timecode_);
    top->addSpacing(4);
    auto command = [this, top](const char* id, Picture pic, const QString& tip, bool checkable) {
        auto* b = new QToolButton(topbar_);
        b->setObjectName(QString::fromLatin1(id));
        b->setAutoRaise(true);
        b->setFixedSize(26, kToolButtonHeight);
        b->setIcon(picture(pic));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setCheckable(checkable);
        top->addWidget(b);
        const QString commandId = QString::fromLatin1(id);
        connect(b, &QToolButton::clicked, this, [this, commandId] { perform(commandId); });
        return b;
    };
    command("smart.extract_splice_in", Picture::SegmentSplice, tr("Extract/Splice-in"), true);
    command("smart.lift_overwrite", Picture::SegmentOverwrite, tr("Lift/Overwrite"), true);
    top->addSpacing(8);
    command("edit.lift", Picture::Lift, tr("Lift"), false);
    command("edit.extract", Picture::Extract, tr("Extract"), false);
    command("edit.copy_to_clipboard", Picture::CopyClipboard, tr("Copy to Clipboard"), false);
    top->addSpacing(8);
    command("edit.add_edit", Picture::AddEdit, tr("Add Edit"), false);
    command("edit.replace_edit", Picture::Replace, tr("Replace Edit"), false);
    command("edit.top", Picture::Top, tr("Top"), false);
    command("edit.tail", Picture::Tail, tr("Tail"), false);
    command("other.match_frame", Picture::MatchFrame, tr("Match Frame"), false);
    top->addStretch(1);

    auto* upper = new QHBoxLayout;
    upper->setContentsMargins(0, 0, 0, 0);
    upper->setSpacing(0);
    upper->addWidget(view_, 1);
    upper->addWidget(vscroll_);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(topbar_);
    v->addLayout(upper, 1);
    v->addWidget(toolbar_);

    connect(&composer_, &Composer::changed, this, &Timeline::sync);
    // Go to Previous/Next Event and Mark Clip use the selected tracks'
    // edits (p.398, p.407).
    composer_.setEventResolver([this](MonitorId monitor) -> QList<std::int64_t> {
        const model::MonitorState& s = composer_.state(monitor);
        if (s.isEmpty()) return {0};
        if (s.current()->item.kind != model::BinItemKind::Sequence) return {0, s.duration()};
        const model::Sequence seq = model::Sequence::fromItem(s.current()->item);
        QStringList tracks = monitor == MonitorId::Record ? patch_.selectedRecordTracks(seq) : QStringList();
        if (tracks.isEmpty()) tracks = seq.trackNames();
        return seq.editPoints(tracks);
    });
    sync();
}

bool Timeline::hasShown() const {
    return !composer_.state(shownMonitor()).isEmpty();
}

std::int64_t Timeline::startFrame() const {
    return composer_.state(shownMonitor()).startFrame();
}

void Timeline::setShowSource(bool on) {
    if (showSource_ == on) return;
    showSource_ = on;
    if (QToolButton* b = toolButton(QStringLiteral("toggleSourceRecord"))) b->setChecked(on);
    shownId_.clear();
    sync();
}

void Timeline::sync() {
    if (syncing_) return;
    const model::MonitorState& s = composer_.state(shownMonitor());
    const model::LoadedClip* clip = s.current();
    if (clip) {
        const model::BinItem& item = clip->item;
        format_ = TimecodeFormat::forRate(item.editRate, false);
        if (item.kind == model::BinItemKind::Sequence) {
            shown_ = model::Sequence::fromItem(item);
        } else {
            // A clip: its tracks, each one segment long.
            shown_ = model::Sequence();
            for (const QString& t : model::sourceTracksOf(item)) {
                if (model::Track* track = shown_.ensureTrack(t)) {
                    model::Segment seg;
                    seg.clipId = item.id;
                    seg.binId = clip->binId;
                    seg.name = item.name;
                    seg.sourceTrack = t;
                    seg.length = item.durationFrames;
                    if (seg.length > 0) track->segments << seg;
                }
            }
        }
    } else {
        shown_ = model::Sequence();
    }
    const model::MonitorState& source = composer_.state(MonitorId::Source);
    patch_.setSourceTracks(source.isEmpty() ? QStringList() : model::sourceTracksOf(source.current()->item));

    // Selected segments that are gone are dropped.
    selection_.removeIf([this](const model::SegmentRef& ref) { return showSource_ || !model::segmentFor(shown_, ref); });
    // The master timecode at the position (picture on p.607).
    timecode_->setText(clip ? s.tracking(model::TrackingFormat::Master) : QString());
    const QString id = clip ? clip->item.id : QString();
    if (id != shownId_) {
        shownId_ = id;
        selection_.clear();
        scale_ = 0;
        first_ = 0;
        focusBack_.reset();
        zoomStack_.clear();
    }
    // Page mode: the view follows the position indicator (p.609).
    if (scale_ > 0 && clip) {
        const double visible = (view_->width() - view_->headerWidth()) / scale_;
        const auto pos = static_cast<double>(s.position());
        if (pos < first_ || pos >= first_ + visible) first_ = std::max(pos - visible * 0.1, 0.0);
    }
    syncScrollBars();
    view_->update();
    emit changed();
}

void Timeline::syncScrollBars() {
    syncing_ = true;
    const int area = std::max(view_->width() - view_->headerWidth(), 1);
    const double sc = scale();
    const auto visible = static_cast<int>(area / std::max(sc, 1e-6));
    const auto total = static_cast<int>(std::min<std::int64_t>(shown_.duration(), 1 << 30));
    scroll_->setRange(0, std::max(total - visible / 2, 0));
    scroll_->setPageStep(std::max(visible, 1));
    scroll_->setSingleStep(std::max(visible / 10, 1));
    scroll_->setValue(static_cast<int>(first_));
    scroll_->setEnabled(scale_ > 0);
    scaleBar_->setValue(sliderFor(sc));
    if (QToolButton* f = toolButton(QStringLiteral("focus"))) f->setChecked(focusBack_.has_value());
    const int extra = std::max(view_->contentHeight() + view_->topOffset() - view_->height(), 0);
    vscroll_->setRange(0, extra);
    vscroll_->setPageStep(std::max(view_->height(), 1));
    vscroll_->setVisible(extra > 0);
    syncing_ = false;
}

double Timeline::fitScale() const {
    // The whole sequence, with a little room after it; at least ten
    // seconds.
    const int area = std::max(view_->width() - view_->headerWidth(), 1);
    const auto fps = static_cast<std::int64_t>(std::max(format_.nominalFps, 1));
    const std::int64_t span = std::max<std::int64_t>(shown_.duration() + shown_.duration() / 20 + 1, 10 * fps);
    return area / static_cast<double>(span);
}

double Timeline::scaleFor(int slider) const {
    const double fit = fitScale();
    if (slider <= 0 || fit >= kMaxScale) return 0;
    return fit * std::pow(kMaxScale / fit, slider / 1000.0);
}

int Timeline::sliderFor(double sc) const {
    const double fit = fitScale();
    if (scale_ <= 0 || fit >= kMaxScale || sc <= fit) return 0;
    return std::clamp(static_cast<int>(std::lround(1000.0 * std::log(sc / fit) / std::log(kMaxScale / fit))), 0, 1000);
}

double Timeline::scale() const {
    return scale_ > 0 ? scale_ : fitScale();
}

void Timeline::setScale(double pixelsPerFrame, std::optional<std::int64_t> anchor) {
    // The scale bar zooms around the position indicator (p.608).
    const std::int64_t at = anchor.value_or(composer_.state(shownMonitor()).position());
    const double oldX = view_->xFor(at) - view_->headerWidth();
    if (pixelsPerFrame <= fitScale() * 1.0001) {
        scale_ = 0;
        first_ = 0;
    } else {
        scale_ = std::min(pixelsPerFrame, kMaxScale);
        const int area = std::max(view_->width() - view_->headerWidth(), 1);
        const double x = oldX >= 0 && oldX <= area ? oldX : area / 2.0;
        first_ = std::max(static_cast<double>(at) - x / scale_, 0.0);
    }
    syncScrollBars();
    view_->update();
}

void Timeline::setFirstFrame(double frame) {
    if (scale_ <= 0) return;
    first_ = std::clamp(frame, 0.0, std::max(static_cast<double>(shown_.duration()), 0.0));
    syncScrollBars();
    view_->update();
}

void Timeline::showEntireSequence() {
    focusBack_.reset();
    setScale(0);
}

void Timeline::toggleFocus() {
    if (focusBack_) {
        const auto [sc, first] = *focusBack_;
        focusBack_.reset();
        scale_ = sc;
        first_ = first;
        syncScrollBars();
        view_->update();
        return;
    }
    focusBack_ = std::make_pair(scale_, first_);
    // 90 pixels per second, the position indicator centered (p.608).
    const double sc = kFocusPixelsPerSecond / static_cast<double>(std::max(format_.nominalFps, 1));
    const int area = std::max(view_->width() - view_->headerWidth(), 1);
    scale_ = std::max(sc, fitScale() * 1.001);
    const auto pos = static_cast<double>(composer_.state(shownMonitor()).position());
    first_ = std::max(pos - area / 2.0 / scale_, 0.0);
    syncScrollBars();
    view_->update();
}

void Timeline::zoomTo(std::int64_t from, std::int64_t to) {
    if (to <= from) return;
    zoomStack_ << std::make_pair(scale_, first_);
    const int area = std::max(view_->width() - view_->headerWidth(), 1);
    scale_ = std::min(area / static_cast<double>(to - from), kMaxScale);
    if (scale_ <= fitScale()) {
        scale_ = 0;
        first_ = 0;
    } else {
        first_ = static_cast<double>(std::max<std::int64_t>(from, 0));
    }
    syncScrollBars();
    view_->update();
}

void Timeline::zoomBack() {
    if (zoomStack_.isEmpty()) return;
    const auto [sc, first] = zoomStack_.takeLast();
    scale_ = sc;
    first_ = first;
    syncScrollBars();
    view_->update();
}

QToolButton* Timeline::toolButton(const QString& name) const {
    return findChild<QToolButton*>(name);
}

QMenu* Timeline::fastMenu() {
    // The Timeline Fast menu (p.592-593). Options that need effects, audio
    // data, markers or media are listed but not available yet.
    QMenu* m = fastMenu_;
    m->clear();
    auto later = [m](const QString& text) { m->addAction(text)->setEnabled(false); };
    auto laterMenu = [](QMenu* parent, const QString& text) { parent->addMenu(text)->setEnabled(false); };
    auto check = [this](QMenu* menu, const QString& text, bool on, std::function<void(bool)> apply) {
        QAction* a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(on);
        connect(a, &QAction::toggled, this, [this, apply = std::move(apply)](bool v) {
            apply(v);
            syncScrollBars();
            view_->update();
        });
    };
    connect(m->addAction(tr("Default Setup")), &QAction::triggered, this, [this] {
        display_ = Display();
        showEntireSequence();
        syncScrollBars();
        view_->update();
    });
    laterMenu(m, tr("View Type"));
    check(m, tr("Track Panel"), display_.trackPanel, [this](bool v) { display_.trackPanel = v; });
    later(tr("Effect Icons"));
    later(tr("Render Ranges"));
    later(tr("Dissolve Icons"));
    later(tr("Clip Frames"));
    QMenu* clipText = m->addMenu(tr("Clip Text"));
    check(clipText, tr("Clip Names"), display_.clipNames, [this](bool v) { display_.clipNames = v; });
    clipText->addAction(tr("Group Clip Name"))->setEnabled(false);
    check(clipText, tr("Clip Tracks"), display_.clipTracks, [this](bool v) { display_.clipTracks = v; });
    for (const QString& t : {tr("Source Track Name")}) clipText->addAction(t)->setEnabled(false);
    check(clipText, tr("Clip Durations"), display_.clipDurations, [this](bool v) { display_.clipDurations = v; });
    for (const QString& t : {tr("Clip Full Durations"), tr("Clip Trans Duration"), tr("Timeline Clip Notes"),
                             tr("Source Names"), tr("Media Full Names"), tr("Clip Resolution")}) {
        clipText->addAction(t)->setEnabled(false);
    }
    laterMenu(m, tr("Sync Breaks"));
    later(tr("Dupe Detection"));
    later(tr("Color Correction"));
    laterMenu(m, tr("Audio Data"));
    later(tr("Clip Color..."));
    later(tr("Segment Selection"));
    later(tr("Mark IO Selection"));
    later(tr("Track Color"));
    laterMenu(m, tr("Show Markers"));
    later(tr("Show Adapters"));
    QMenu* showTrack = m->addMenu(tr("Show Track"));
    check(showTrack, tr("TC1"), display_.timecodeTrack, [this](bool v) { display_.timecodeTrack = v; });
    later(tr("Track Control Panel"));
    QMenu* zoom = m->addMenu(tr("Zoom"));
    connect(zoom->addAction(tr("Zoom In")), &QAction::triggered, this, [this] { view_->beginZoomSelection(); });
    QAction* out = zoom->addAction(tr("Zoom Out"));
    out->setEnabled(!zoomStack_.isEmpty());
    connect(out, &QAction::triggered, this, &Timeline::zoomBack);
    connect(zoom->addAction(tr("Show Entire Sequence")), &QAction::triggered, this, &Timeline::showEntireSequence);
    // Our factor: four times closer.
    connect(zoom->addAction(tr("Zoom into Region")), &QAction::triggered, this, [this] { setScale(scale() * 4); });
    QAction* back = zoom->addAction(tr("Zoom Back to Last View"));
    back->setEnabled(!zoomStack_.isEmpty());
    connect(back, &QAction::triggered, this, &Timeline::zoomBack);
    zoom->addAction(tr("Show Every Frame"))->setEnabled(false);
    return m;
}

QMenu* Timeline::undoRedoMenu() {
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QStringList names = history_.descriptions();
    const int done = history_.undoCount();
    const int redoable = static_cast<int>(names.size()) - done;
    // Undoing back to a command undoes it and everything after it (p.466).
    for (int i = 0; i < names.size(); ++i) {
        const bool isRedo = i < redoable;
        QAction* a = menu->addAction((isRedo ? tr("Redo %1") : tr("Undo %1")).arg(names.at(i)));
        connect(a, &QAction::triggered, this, [this, isRedo, i, redoable] {
            const int times = isRedo ? redoable - i : i - redoable + 1;
            for (int k = 0; k < times; ++k) perform(isRedo ? QStringLiteral("edit.redo") : QStringLiteral("edit.undo"));
        });
    }
    if (names.isEmpty()) menu->addAction(tr("Nothing to undo"))->setEnabled(false);
    return menu;
}

void Timeline::toggleSourceTrack(const QString& track) {
    patch_.setSourceSelected(track, !patch_.isSourceSelected(track));
    view_->update();
    emit changed();
}

void Timeline::toggleRecordTrack(const QString& track) {
    patch_.setRecordSelected(track, !patch_.isRecordSelected(track));
    view_->update();
    emit changed();
}

void Timeline::toggleMonitor(const QString& track) {
    if (unmonitored_.contains(track)) {
        unmonitored_.remove(track);
    } else {
        unmonitored_.insert(track);
    }
    view_->update();
}

bool Timeline::patchTracks(const QString& source, const QString& record) {
    if (!patch_.patch(source, record)) return fail(tr("%1 cannot be patched to %2.").arg(source, record));
    view_->update();
    emit changed();
    return true;
}

bool Timeline::fail(const QString& text) {
    message_ = text;
    emit message(text);
    return false;
}

bool Timeline::handles(const QString& id) {
    static const QStringList ids{
        QStringLiteral("edit.splice_in"),
        QStringLiteral("edit.overwrite"),
        QStringLiteral("edit.lift"),
        QStringLiteral("edit.extract"),
        QStringLiteral("edit.undo"),
        QStringLiteral("edit.redo"),
        QStringLiteral("edit.undo_redo_list"),
        QStringLiteral("edit.select_all_tracks"),
        QStringLiteral("timeline.new.video_track"),
        QStringLiteral("timeline.new.audio_track.mono"),
        QStringLiteral("timeline.new.audio_track.stereo"),
        QStringLiteral("timeline.new.audio_track.5_1_surround"),
        QStringLiteral("timeline.new.audio_track.7_1_surround"),
        QStringLiteral("timeline.new.data_track"),
        QStringLiteral("timeline.restore_default_patch"),
        QStringLiteral("timeline.remove_match_frame_edits"),
        QStringLiteral("edit.copy_to_clipboard"),
        QStringLiteral("edit.clipboard_contents"),
        QStringLiteral("edit.replace_edit"),
        QStringLiteral("edit.add_edit"),
        QStringLiteral("edit.top"),
        QStringLiteral("edit.tail"),
        QStringLiteral("edit.sync_lock_all_tracks"),
        QStringLiteral("edit.move_clip_up"),
        QStringLiteral("edit.move_clip_down"),
        QStringLiteral("edit.copy"),
        QStringLiteral("edit.cut"),
        QStringLiteral("edit.paste"),
        QStringLiteral("edit.delete"),
        QStringLiteral("clip.lock_tracks"),
        QStringLiteral("clip.unlock_tracks"),
        QStringLiteral("other.match_frame"),
        QStringLiteral("smart.lift_overwrite"),
        QStringLiteral("smart.extract_splice_in"),
    };
    return ids.contains(id);
}

bool Timeline::perform(const QString& id) {
    if (!handles(id)) return false;
    message_.clear();
    using model::AudioFormat;
    using model::TrackKind;
    if (id == QLatin1String("edit.splice_in")) {
        edit(model::EditKind::SpliceIn);
    } else if (id == QLatin1String("edit.overwrite")) {
        edit(model::EditKind::Overwrite);
    } else if (id == QLatin1String("edit.lift")) {
        remove(false);
    } else if (id == QLatin1String("edit.extract")) {
        remove(true);
    } else if (id == QLatin1String("edit.undo") || id == QLatin1String("edit.redo")) {
        const bool undo = id == QLatin1String("edit.undo");
        const model::EditStep* step = undo ? history_.undo() : history_.redo();
        if (!step) return fail(undo ? tr("Nothing to undo.") : tr("Nothing to redo."));
        composer_.stop();
        restore(step->binId, undo ? step->before : step->after);
    } else if (id == QLatin1String("edit.undo_redo_list")) {
        undoRedoMenu()->popup(QCursor::pos());
    } else if (id == QLatin1String("edit.select_all_tracks")) {
        // Every track on both sides (p.627).
        for (const QString& t : patch_.sourceTracks()) patch_.setSourceSelected(t, true);
        for (const QString& t : shown_.trackNames()) patch_.setRecordSelected(t, true);
        view_->update();
    } else if (id == QLatin1String("timeline.new.video_track")) {
        addTrack(TrackKind::Video);
    } else if (id == QLatin1String("timeline.new.audio_track.mono")) {
        addTrack(TrackKind::Audio, AudioFormat::Mono);
    } else if (id == QLatin1String("timeline.new.audio_track.stereo")) {
        addTrack(TrackKind::Audio, AudioFormat::Stereo);
    } else if (id == QLatin1String("timeline.new.audio_track.5_1_surround")) {
        addTrack(TrackKind::Audio, AudioFormat::Surround51);
    } else if (id == QLatin1String("timeline.new.audio_track.7_1_surround")) {
        addTrack(TrackKind::Audio, AudioFormat::Surround71);
    } else if (id == QLatin1String("timeline.new.data_track")) {
        addTrack(TrackKind::Data);
    } else if (id == QLatin1String("timeline.restore_default_patch")) {
        patch_.restoreDefault();
        view_->update();
    } else if (id == QLatin1String("timeline.remove_match_frame_edits")) {
        removeMatchFrameEdits();
    } else if (id == QLatin1String("edit.copy_to_clipboard")) {
        copyToClipboard();
    } else if (id == QLatin1String("edit.clipboard_contents")) {
        if (!clipboard_) return fail(tr("The Clipboard is empty."));
        loadClipboard(tr("Clipboard Contents.%1").arg(++clipboardCount_));
    } else if (id == QLatin1String("edit.replace_edit")) {
        replaceEdit();
    } else if (id == QLatin1String("edit.add_edit")) {
        // Alt: on every track with filler (p.640).
        addEdit(QApplication::keyboardModifiers() & Qt::AltModifier);
    } else if (id == QLatin1String("edit.top") || id == QLatin1String("edit.tail")) {
        topOrTail(id == QLatin1String("edit.top"));
    } else if (id == QLatin1String("edit.sync_lock_all_tracks")) {
        toggleSyncLockAll();
    } else if (id == QLatin1String("edit.move_clip_up") || id == QLatin1String("edit.move_clip_down")) {
        moveClips(id == QLatin1String("edit.move_clip_up") ? 1 : -1);
    } else if (id == QLatin1String("edit.copy") || id == QLatin1String("edit.cut")) {
        if (selection_.isEmpty()) {
            if (id == QLatin1String("edit.copy")) copyToClipboard();
        } else {
            cutOrCopySelection(id == QLatin1String("edit.cut"));
        }
    } else if (id == QLatin1String("edit.paste")) {
        pasteClipboard();
    } else if (id == QLatin1String("edit.delete")) {
        deleteSelection();
    } else if (id == QLatin1String("clip.lock_tracks") || id == QLatin1String("clip.unlock_tracks")) {
        if (const model::LoadedClip* rec = recordSequence()) {
            setTracksLocked(patch_.selectedRecordTracks(model::Sequence::fromItem(rec->item)),
                            id == QLatin1String("clip.lock_tracks"));
        }
    } else if (id == QLatin1String("other.match_frame")) {
        matchFrame();
    } else if (id == QLatin1String("smart.lift_overwrite")) {
        setSegmentTool(model::EditKind::Overwrite, !liftTool_);
    } else if (id == QLatin1String("smart.extract_splice_in")) {
        setSegmentTool(model::EditKind::SpliceIn, !spliceTool_);
    }
    return true;
}

void Timeline::edit(model::EditKind kind) {
    composer_.stop();
    const model::MonitorState& source = composer_.state(MonitorId::Source);
    if (source.isEmpty()) {
        fail(tr("Load a clip into the Source monitor first."));
        return;
    }
    model::MonitorState& record = composer_.state(MonitorId::Record);
    if (record.isEmpty()) {
        // The first edit makes the sequence (p.463).
        const auto made = maker_ ? maker_() : std::nullopt;
        if (!made) {
            fail(tr("Open a bin for the new sequence first."));
            return;
        }
        if (store_) store_(made->first, made->second, true);
        composer_.load(MonitorId::Record, made->first, made->second);
        composer_.setActiveMonitor(MonitorId::Source);
    }
    const model::LoadedClip* rec = record.current();
    const model::LoadedClip* src = source.current();
    if (!rec || !src) return;
    if (rec->item.id == src->item.id) {
        fail(tr("A sequence cannot be edited into itself."));
        return;
    }
    const model::BinItem before = rec->item;
    const QString binId = rec->binId;
    model::Sequence seq = model::Sequence::fromItem(before);
    const model::EditMarks sourceMarks{source.in(), source.out(), source.position(), source.duration()};
    const model::EditMarks recordMarks{record.in(), record.out(), record.position(), seq.duration()};
    const model::EditPlan plan = model::planEdit(sourceMarks, recordMarks);
    if (!plan.ok()) {
        fail(plan.error);
        return;
    }
    if (model::applyEdit(seq, kind, plan, src->binId, src->item, patch_).isEmpty()) {
        fail(tr("Select a source track and a record track to edit."));
        return;
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(kind == model::EditKind::SpliceIn ? tr("Splice-in") : tr("Overwrite"), before, after);
    // The Record monitor shows the end of the new edit (p.464); the
    // sequence's marks are used up.
    record.clearBoth();
    composer_.moveTo(MonitorId::Record, plan.recordStart + plan.length);
}

void Timeline::remove(bool extract) {
    composer_.stop();
    model::MonitorState& record = composer_.state(MonitorId::Record);
    const model::LoadedClip* rec = record.current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) {
        fail(tr("Load a sequence into the Record monitor first."));
        return;
    }
    // Between the marks, on the selected tracks (p.478-479).
    if (!record.in() && !record.out()) {
        fail(tr("Mark an IN and an OUT in the sequence first."));
        return;
    }
    const model::BinItem before = rec->item;
    const QString binId = rec->binId;
    model::Sequence seq = model::Sequence::fromItem(before);
    const std::int64_t start = record.markedStart();
    const std::int64_t length = record.markedEnd() - start + 1;
    const QStringList tracks = patch_.selectedRecordTracks(seq);
    if (tracks.isEmpty()) {
        fail(tr("Select the tracks to remove material from."));
        return;
    }
    // The removed material goes into the Clipboard (p.479).
    const model::Sequence removed = seq.excerpt(tracks, start, length);
    if (model::removeMaterial(seq, extract, start, length, tracks).isEmpty()) {
        fail(tr("There is no material between the marks."));
        return;
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    setClipboard(removed, before.name);
    commit(extract ? tr("Extract") : tr("Lift"), before, after);
    // Lift leaves the marked gap; Extract closes it, so only the IN stays.
    if (extract) record.clearOut();
    composer_.moveTo(MonitorId::Record, start);
}

void Timeline::addTrack(model::TrackKind kind, model::AudioFormat format) {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) {
        fail(tr("Load a sequence into the Record monitor first."));
        return;
    }
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const auto name = seq.addTrack(kind, format);
    if (!name) {
        fail(tr("The sequence has all the tracks it can hold."));
        return;
    }
    const model::BinItem before = rec->item;
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    patch_.setRecordSelected(*name, true);
    commit(tr("Add Track"), before, after);
}

void Timeline::commit(const QString& description, const model::BinItem& before, model::BinItem after) {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    const QString binId = rec ? rec->binId : QString();
    history_.push(model::EditStep{description, binId, before, after});
    restore(binId, after);
}

void Timeline::restore(const QString& binId, const model::BinItem& item) {
    if (store_) store_(binId, item, false);
    composer_.updateItem(item);
}

const model::LoadedClip* Timeline::recordSequence() {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) {
        fail(tr("Load a sequence into the Record monitor first."));
        return nullptr;
    }
    return rec;
}

void Timeline::storeLocks(const model::Sequence& sequence) {
    // Locks are part of the sequence but not undo steps.
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec) return;
    model::BinItem item = rec->item;
    sequence.storeIn(item);
    restore(rec->binId, item);
}

void Timeline::setTracksLocked(const QStringList& tracks, bool locked) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    for (const QString& name : tracks) {
        if (model::Track* t = seq.track(name)) t->locked = locked;
    }
    storeLocks(seq);
}

void Timeline::toggleSyncLock(const QString& track) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    if (model::Track* t = seq.track(track)) t->syncLocked = !t->syncLocked;
    storeLocks(seq);
}

void Timeline::toggleSyncLockAll() {
    // All on, or all off if they all are (p.633).
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    bool all = true;
    for (const model::Track& t : seq.tracks()) all = all && t.syncLocked;
    for (const QString& name : seq.trackNames()) seq.track(name)->syncLocked = !all;
    storeLocks(seq);
}

bool Timeline::isSegmentTool(model::EditKind kind) const {
    return kind == model::EditKind::Overwrite ? liftTool_ : spliceTool_;
}

void Timeline::setSegmentTool(model::EditKind kind, bool on) {
    (kind == model::EditKind::Overwrite ? liftTool_ : spliceTool_) = on;
    if (!liftTool_ && !spliceTool_) selection_.clear();
    if (QToolButton* b = toolButton(QStringLiteral("smart.lift_overwrite"))) b->setChecked(liftTool_);
    if (QToolButton* b = toolButton(QStringLiteral("smart.extract_splice_in"))) b->setChecked(spliceTool_);
    view_->update();
}

model::EditKind Timeline::segmentKind() const {
    if (liftTool_ != spliceTool_) return liftTool_ ? model::EditKind::Overwrite : model::EditKind::SpliceIn;
    return model::EditKind::Overwrite;
}

void Timeline::setSelection(const QList<model::SegmentRef>& segments) {
    selection_ = segments;
    view_->update();
    emit changed();
}

void Timeline::clickSegment(const model::SegmentRef& segment, bool toggle) {
    if (!toggle) {
        selection_ = {segment};
    } else if (selection_.contains(segment)) {
        selection_.removeAll(segment);
    } else {
        selection_ << segment;
    }
    view_->update();
    emit changed();
}

void Timeline::selectSegments(SelectWhere where) {
    // To the Left / To the Right of the position, or Reverse; filler is not
    // selected (p.614).
    const std::int64_t pos = composer_.state(MonitorId::Record).position();
    QList<model::SegmentRef> picked;
    for (const model::Track& t : shown_.tracks()) {
        if (!patch_.isRecordSelected(t.name())) continue;
        std::int64_t at = 0;
        for (const model::Segment& seg : t.segments) {
            const model::SegmentRef ref{t.name(), at};
            const bool take = where == SelectWhere::Left    ? at + seg.length <= pos
                              : where == SelectWhere::Right ? at >= pos
                                                            : !selection_.contains(ref);
            if (take && !seg.isFiller()) picked << ref;
            at += seg.length;
        }
    }
    if (!liftTool_ && !spliceTool_) setSegmentTool(model::EditKind::Overwrite, true);
    setSelection(picked);
}

bool Timeline::moveSelection(std::int64_t frames, int tracks, model::EditKind kind, bool copy) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec || selection_.isEmpty()) return false;
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    QString error;
    const QList<model::SegmentRef> moved = model::moveSegments(seq, selection_, frames, tracks, kind, copy, &error);
    if (moved.isEmpty()) return fail(error);
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(copy ? tr("Copy Segment") : kind == model::EditKind::SpliceIn ? tr("Extract/Splice-in") : tr("Lift/Overwrite"),
           before, after);
    // "After dragging segments up or down, the segments remain selected"
    // (p.620).
    selection_ = moved;
    view_->update();
    return true;
}

bool Timeline::deleteSelection() {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    if (selection_.isEmpty()) {
        // The Delete key with track buttons selected: the Delete Track(s)
        // dialog (p.636).
        const QStringList tracks = patch_.selectedRecordTracks(model::Sequence::fromItem(rec->item));
        if (tracks.isEmpty()) return false;
        if (QMessageBox::question(this, tr("Delete Track(s)"),
                                  tr("Delete %1 from the sequence?").arg(tracks.join(QStringLiteral(", "))),
                                  QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok) {
            return false;
        }
        return deleteTracks(tracks);
    }
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    const bool extract = segmentKind() == model::EditKind::SpliceIn;
    if (!model::deleteSegments(seq, selection_, extract)) return fail(tr("The selected segments cannot be deleted."));
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    selection_.clear();
    commit(extract ? tr("Extract") : tr("Lift"), before, after);
    return true;
}

bool Timeline::deleteTracks(const QStringList& tracks) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    bool any = false;
    for (const QString& name : tracks) any = seq.removeTrack(name) || any;
    if (!any) return false;
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(tr("Delete Track"), before, after);
    return true;
}

void Timeline::setClipboard(const model::Sequence& material, const QString& from) {
    model::BinItem clip;
    clip.id = QStringLiteral("clipboard-") + model::newItemId();
    clip.kind = model::BinItemKind::Sequence;
    clip.name = tr("Clipboard Contents");
    clip.created = QDateTime::currentDateTimeUtc();
    clip.modified = clip.created;
    if (const model::LoadedClip* rec = composer_.state(MonitorId::Record).current()) clip.editRate = rec->item.editRate;
    // The clip starts where the material did in the sequence.
    material.storeIn(clip);
    clipboard_ = clip;
    // With Alt, straight into the Source monitor as "<sequence>.Sub" (p.480).
    if (QApplication::keyboardModifiers() & Qt::AltModifier) loadClipboard(from + QStringLiteral(".Sub"));
}

void Timeline::loadClipboard(const QString& name) {
    if (!clipboard_) return;
    model::BinItem clip = *clipboard_;
    clip.id = QStringLiteral("clipboard-") + model::newItemId();
    clip.name = name;
    composer_.load(MonitorId::Source, QString(), clip);
}

void Timeline::copyToClipboard() {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (!record.in() && !record.out()) {
        fail(tr("Mark an IN and an OUT in the sequence first."));
        return;
    }
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const std::int64_t start = record.markedStart();
    setClipboard(seq.excerpt(patch_.selectedRecordTracks(seq), start, record.markedEnd() - start + 1), rec->item.name);
}

void Timeline::cutOrCopySelection(bool cut) {
    // Ctrl+C / Ctrl+X on selected segments (p.622).
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    setClipboard(model::copySegments(seq, selection_), rec->item.name);
    if (cut) deleteSelection();
}

void Timeline::pasteClipboard() {
    // At the IN or the position (p.623), with the segment tool's kind of
    // edit; onto the tracks it came from.
    if (!clipboard_) {
        fail(tr("The Clipboard is empty."));
        return;
    }
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    const std::int64_t at = record.in().value_or(record.position());
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    const model::Sequence clip = model::Sequence::fromItem(*clipboard_);
    const model::EditKind kind = segmentKind();
    if (model::paste(seq, clip, at, kind).isEmpty()) {
        fail(tr("The tracks the Clipboard came from are locked."));
        return;
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(tr("Paste"), before, after);
    composer_.moveTo(MonitorId::Record, at + clip.duration());
}

void Timeline::replaceEdit() {
    composer_.stop();
    const model::MonitorState& source = composer_.state(MonitorId::Source);
    if (source.isEmpty()) {
        fail(tr("Load a clip into the Source monitor first."));
        return;
    }
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    model::MonitorState& record = composer_.state(MonitorId::Record);
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    const auto pairs = patch_.editPairs(seq);
    if (pairs.isEmpty()) {
        fail(tr("Select a source track and a record track to edit."));
        return;
    }
    const model::EditMarks sourceMarks{source.in(), source.out(), source.position(), source.duration()};
    const model::EditMarks recordMarks{record.in(), record.out(), record.position(), seq.duration()};
    const model::EditPlan plan = model::planReplace(seq, pairs.first().second, sourceMarks, recordMarks);
    if (!plan.ok()) {
        fail(plan.error);
        return;
    }
    const model::LoadedClip* src = source.current();
    if (model::applyEdit(seq, model::EditKind::Overwrite, plan, src->binId, src->item, patch_).isEmpty()) {
        fail(tr("The selected tracks are locked."));
        return;
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(tr("Replace Edit"), before, after);
    record.clearBoth();
}

void Timeline::addEdit(bool fillerOnly) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    const std::int64_t pos = composer_.state(MonitorId::Record).position();
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    const model::Sequence original = seq;
    bool any = false;
    for (const model::Track& t : original.tracks()) {
        if (t.locked) continue;
        if (fillerOnly) {
            // Every track with filler there, whatever is selected (p.640).
            const int i = t.segmentAt(pos);
            if (i < 0 || !t.segments.at(i).isFiller()) continue;
        } else if (!patch_.isRecordSelected(t.name())) {
            continue;
        }
        any = seq.addEdit(t.name(), pos) || any;
    }
    if (!any) {
        fail(tr("There is nothing to add an edit to at the position indicator."));
        return;
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(tr("Add Edit"), before, after);
}

void Timeline::removeMatchFrameEdits() {
    // Between the marks, or in the whole sequence (p.640).
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    const bool marked = record.in() || record.out();
    const std::int64_t start = marked ? record.markedStart() : 0;
    const std::int64_t length = marked ? record.markedEnd() - start + 1 : seq.duration() + 1;
    if (seq.removeMatchFrameEdits(patch_.selectedRecordTracks(seq), start, length) == 0) {
        fail(tr("There are no match-frame edits to remove."));
        return;
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(tr("Remove Match Frame Edits"), before, after);
}

void Timeline::topOrTail(bool top) {
    // Top: T-R-X, Mark Clip, Mark OUT, Extract. Tail: T-E-X (p.639).
    if (!recordSequence()) return;
    composer_.setActiveMonitor(MonitorId::Record);
    composer_.perform(QStringLiteral("edit.mark_clip"));
    composer_.perform(top ? QStringLiteral("edit.mark_out") : QStringLiteral("edit.mark_in"));
    remove(true);
}

void Timeline::matchFrame() {
    // From the active monitor's sequence: the clip into the Source monitor
    // at the matching frame, marked IN; Alt keeps its marks (p.432-433).
    const MonitorId monitor = composer_.activeMonitor();
    const model::MonitorState& s = composer_.state(monitor);
    const model::LoadedClip* loaded = s.current();
    if (!loaded || loaded->item.kind != model::BinItemKind::Sequence) {
        fail(tr("Match Frame needs a sequence in the monitor."));
        return;
    }
    const model::Sequence seq = model::Sequence::fromItem(loaded->item);
    const QStringList tracks = monitor == MonitorId::Record ? patch_.selectedRecordTracks(seq) : seq.trackNames();
    const auto match = model::matchFrame(seq, tracks, s.position());
    if (!match) {
        fail(tr("There is no clip under the position indicator on the selected tracks."));
        return;
    }
    const bool keepMarks = QApplication::keyboardModifiers() & Qt::AltModifier;
    composer_.loadFromBin(MonitorId::Source, match->segment.binId, {match->segment.clipId});
    model::MonitorState& source = composer_.state(MonitorId::Source);
    if (!source.current() || source.current()->item.id != match->segment.clipId) {
        fail(tr("The clip %1 is not in an open bin.").arg(match->segment.name));
        return;
    }
    if (!keepMarks) source.clearBoth();
    composer_.moveTo(MonitorId::Source, match->frame);
    if (!keepMarks) source.markIn();
    composer_.setActiveMonitor(MonitorId::Source);
}

void Timeline::moveClips(int tracks) {
    // Move Clip Up / Down: the selected segments one track (p.613).
    if (selection_.isEmpty()) {
        fail(tr("Select the clips to move."));
        return;
    }
    moveSelection(0, tracks, segmentKind(), QApplication::keyboardModifiers() & Qt::AltModifier);
}

QMenu* Timeline::contextMenu(const QString& track) {
    auto* menu = new QMenu(this);
    auto run = [this](QMenu* m, const QString& text, const QString& id) {
        QAction* a = m->addAction(text);
        connect(a, &QAction::triggered, this, [this, id] {
            if (handles(id)) {
                perform(id);
            } else {
                emit commandRequested(id);
            }
        });
        return a;
    };
    if (!track.isEmpty() && track != QLatin1String("TC1")) {
        // On a track button (p.633-636).
        model::Sequence seq;
        if (const model::LoadedClip* rec = composer_.state(MonitorId::Record).current()) seq = model::Sequence::fromItem(rec->item);
        const model::Track* t = seq.track(track);
        QAction* lock = menu->addAction(t && t->locked ? tr("Unlock Track") : tr("Lock Track"));
        lock->setEnabled(t);
        connect(lock, &QAction::triggered, this, [this, track, locked = t && t->locked] { setTracksLocked({track}, !locked); });
        QAction* del = menu->addAction(tr("Delete Track"));
        del->setEnabled(t);
        // No dialog from here (p.636).
        connect(del, &QAction::triggered, this, [this, track] { deleteTracks({track}); });
        QAction* match = menu->addAction(tr("Match Frame Track"));
        match->setEnabled(t);
        connect(match, &QAction::triggered, this, [this, track] {
            // Only this track (p.434).
            const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
            if (!rec) return;
            const auto m = model::matchFrame(model::Sequence::fromItem(rec->item), {track},
                                             composer_.state(MonitorId::Record).position());
            if (!m) {
                fail(tr("There is no clip under the position indicator on %1.").arg(track));
                return;
            }
            composer_.loadFromBin(MonitorId::Source, m->segment.binId, {m->segment.clipId});
            model::MonitorState& source = composer_.state(MonitorId::Source);
            if (!source.current() || source.current()->item.id != m->segment.clipId) return;
            source.clearBoth();
            composer_.moveTo(MonitorId::Source, m->frame);
            source.markIn();
        });
        menu->addAction(tr("Rename Track"))->setEnabled(false);
        return menu;
    }
    // In the Timeline (p.613, p.633, p.635).
    QMenu* add = menu->addMenu(tr("New"));
    run(add, tr("Sequence"), QStringLiteral("timeline.new.sequence"));
    run(add, tr("Video Track"), QStringLiteral("timeline.new.video_track"));
    QMenu* audio = add->addMenu(tr("Audio Track"));
    run(audio, tr("Mono"), QStringLiteral("timeline.new.audio_track.mono"));
    run(audio, tr("Stereo"), QStringLiteral("timeline.new.audio_track.stereo"));
    run(audio, tr("5.1 Surround"), QStringLiteral("timeline.new.audio_track.5_1_surround"));
    run(audio, tr("7.1 Surround"), QStringLiteral("timeline.new.audio_track.7_1_surround"));
    run(add, tr("Data Track"), QStringLiteral("timeline.new.data_track"));
    menu->addSeparator();
    run(menu, tr("Lock Tracks"), QStringLiteral("clip.lock_tracks"));
    run(menu, tr("Unlock Tracks"), QStringLiteral("clip.unlock_tracks"));
    menu->addSeparator();
    QMenu* select = menu->addMenu(tr("Select"));
    connect(select->addAction(tr("To the Left")), &QAction::triggered, this, [this] { selectSegments(SelectWhere::Left); });
    connect(select->addAction(tr("To the Right")), &QAction::triggered, this, [this] { selectSegments(SelectWhere::Right); });
    for (const QString& t : {tr("Clips with Same Source Clip Color"), tr("Clips With No Source Clip Color"),
                             tr("Offline Clips"), tr("Clips With Same Local Clip Color")}) {
        select->addAction(t)->setEnabled(false);
    }
    connect(select->addAction(tr("Reverse")), &QAction::triggered, this, [this] { selectSegments(SelectWhere::Reverse); });
    menu->addAction(tr("Create Sequence Based on Selection"))->setEnabled(false);
    return menu;
}

void Timeline::keyPressEvent(QKeyEvent* event) {
    // The Delete key: segments or tracks (p.621, p.636).
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        perform(QStringLiteral("edit.delete"));
        return;
    }
    if (keys_) {
        const QString id = keys_(QKeySequence(event->keyCombination()));
        if (!id.isEmpty()) {
            if (handles(id)) {
                perform(id);
            } else if (Composer::handles(id)) {
                // Moving and marking act on the monitor the Timeline shows.
                composer_.setActiveMonitor(shownMonitor());
                composer_.perform(id);
            }
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

} // namespace lumenis::ui
