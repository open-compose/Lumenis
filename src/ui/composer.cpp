#include "lumenis/ui/composer.h"

#include "lumenis/core/timecode.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/button_mapping.h"
#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QHash>
#include <QDragEnterEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// The Composer window (Phase 1.4a, guide ch.13): the Source and Record monitors side by side, each a
// "column" of information row (Clip Name menu, tracking display), picture (MonitorPicture), position
// bar (PositionBar), Trim mode counter and two rows of mappable buttons; plus the center duration
// display and the Trim buttons used by Trim mode.
//
// What a monitor holds (the loaded clips, position, marks) lives in model::MonitorState
// (src/model/monitor.*); this file only draws it and runs the transport and marking commands on it
// (Composer::perform). Everything the Composer cannot know by itself comes in through resolvers set
// by MainWindow::createPanels (main_window.cpp): bin items (ItemResolver), pictures (PictureResolver,
// backed by MediaFrames), keys (KeyResolver, the KeyboardMap), the Source Browser's commit
// (Committer) and the audio play clock (PlayClock). The Timeline (timeline.cpp) sets the edit points
// (the event and stop resolvers) and the Trim display. A MarkResolver (Mark Clip on selected
// segments) is declared, but nothing sets one yet. Commands that are not the Composer's own
// (Splice-in, Overwrite, Trim) leave through commandRequested(), which MainWindow routes to the
// Timeline or the menus. Buttons are MappableButtons (button_mapping.*), so the Command Palette can
// map other commands onto them.
namespace lumenis::ui {

namespace {

constexpr int kInfoHeight = 20;      // the information row above a monitor
constexpr int kBarHeight = 14;       // position bar
constexpr int kButtonWidth = 24;
// Ms between playback steps. Short and precise: the picture moves to where
// the sound is at each step, so a late step is a picture behind the sound
// (macOS coalesces coarse timers; ROADMAP Phase 2 gate 4).
constexpr int kTickInterval = 5;
constexpr QRgb kInfoText = 0xff4cd98c;   // tracking and clip names, green as in the pictures (p.380)
constexpr QRgb kIndicator = 0xff33c3ff;  // the blue position indicator (p.396)
constexpr QRgb kSourceBar = 0xff2f8a6c;  // Source/Record colours (p.383)
constexpr QRgb kRecordBar = 0xff2b5576;
constexpr QRgb kPlainBar = 0xff4a4a4a;
constexpr QRgb kMarkedSpan = 0x60ffffff;
constexpr QRgb kPhantomMark = 0xff3a7bd2; // phantom marks, blue (p.501)
constexpr QRgb kSplice = 0xffe6c619;     // Splice-in (yellow) and Overwrite (red)
constexpr QRgb kOverwrite = 0xffd23a32;
constexpr QRgb kMarker = 0xffd23a32;     // red Add Marker
constexpr QRgb kTrimActive = 0xffb070f0; // active trim counter, purple (p.651)
// Shuttle speeds when L or J is pressed again (p.400).
constexpr int kSpeeds[] = {1, 2, 3, 5, 8};

// The buttons below the monitors, drawn in code (our own drawings; the
// guide's pictures only fix where each one sits, p.380).
enum class Glyph {
    Blank,
    StepBack,
    StepForward,
    PrevEvent,
    NextEvent,
    Play,
    MarkIn,
    MarkOut,
    MarkClip,
    ClearBoth,
    GoToIn,
    GoToOut,
    PlayInToOut,
    AddMarker,
    SpliceIn,
    Overwrite,
    Gang,
};

struct ButtonSpec {
    const char* command; // empty: a Blank button (Command palette, p.98)
    Glyph glyph;
};

// The two rows under each monitor, slot by slot as in the picture on p.380.
// Functions are those the guide names for these shapes; unidentified slots
// are Blank (docs/spec/ui-inventory.md, section 10).
const QList<QList<ButtonSpec>>& buttonRows(MonitorId monitor) {
    static const QList<QList<ButtonSpec>> source{
        {{"", Glyph::Blank},
         {"move.step_backward_one_frame", Glyph::StepBack},
         {"move.step_forward_one_frame", Glyph::StepForward},
         {"edit.mark_in", Glyph::MarkIn},
         {"play.play", Glyph::Play},
         {"edit.mark_out", Glyph::MarkOut},
         {"edit.mark_clip", Glyph::MarkClip},
         {"more.add_marker", Glyph::AddMarker},
         {"edit.splice_in", Glyph::SpliceIn},
         {"edit.overwrite", Glyph::Overwrite}},
        {{"other.gang", Glyph::Gang},
         {"move.go_to_previous_event", Glyph::PrevEvent},
         {"move.go_to_next_event", Glyph::NextEvent},
         {"edit.go_to_in_point", Glyph::GoToIn},
         {"play.play_in_to_out", Glyph::PlayInToOut},
         {"edit.go_to_out_point", Glyph::GoToOut},
         {"edit.clear_both_marks", Glyph::ClearBoth},
         {"", Glyph::Blank},
         {"", Glyph::Blank},
         {"", Glyph::Blank}},
    };
    static const QList<QList<ButtonSpec>> record{
        {{"", Glyph::Blank},
         {"", Glyph::Blank},
         {"move.step_backward_one_frame", Glyph::StepBack},
         {"move.step_forward_one_frame", Glyph::StepForward},
         {"edit.mark_in", Glyph::MarkIn},
         {"play.play", Glyph::Play},
         {"edit.mark_out", Glyph::MarkOut},
         {"", Glyph::Blank},
         {"more.add_marker", Glyph::AddMarker},
         {"", Glyph::Blank},
         {"", Glyph::Blank}},
        {{"other.gang", Glyph::Gang},
         {"", Glyph::Blank},
         {"move.go_to_previous_event", Glyph::PrevEvent},
         {"move.go_to_next_event", Glyph::NextEvent},
         {"edit.go_to_in_point", Glyph::GoToIn},
         {"play.play_in_to_out", Glyph::PlayInToOut},
         {"edit.go_to_out_point", Glyph::GoToOut},
         {"", Glyph::Blank},
         {"", Glyph::Blank},
         {"", Glyph::Blank},
         {"", Glyph::Blank}},
    };
    return monitor == MonitorId::Source ? source : record;
}

// Commands shown but not working yet.
bool later(const QString&) {
    return false;
}

// Trim buttons: one or two arrowheads pointing back or forward.
QIcon trimGlyph(const QString& label) {
    QPixmap pm(48, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xc8, 0xc8, 0xc8));
    const bool back = label.startsWith(QLatin1Char('<'));
    const int count = static_cast<int>(label.size());
    for (int i = 0; i < count; ++i) {
        const double x = 24 + (i - (count - 1) / 2.0) * 12;
        const double dir = back ? -1 : 1;
        p.drawPolygon(QPolygonF{QPointF(x - 6 * dir, 8), QPointF(x + 6 * dir, 16), QPointF(x - 6 * dir, 24)});
    }
    p.setPen(QPen(QColor(0xc8, 0xc8, 0xc8), 2));
    p.drawLine(QPointF(4, 28), QPointF(44, 28));
    return QIcon(pm);
}

// Draws a button glyph on a 32 x 32 pixmap shown at 16 x 16 logical pixels (device pixel ratio 2),
// so it stays sharp on high-density screens. The coordinates below are in the 32-pixel space.
QIcon glyph(Glyph g, const QColor& color) {
    constexpr int size = 32;
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const QPen line(color, 2.6, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    auto triangle = [&](qreal x, qreal y, qreal w, qreal h, bool right) {
        QPainterPath path;
        if (right) {
            path.moveTo(x, y);
            path.lineTo(x + w, y + h / 2);
            path.lineTo(x, y + h);
        } else {
            path.moveTo(x + w, y);
            path.lineTo(x, y + h / 2);
            path.lineTo(x + w, y + h);
        }
        path.closeSubpath();
        p.drawPath(path);
    };
    auto bracket = [&](qreal x, bool open) {
        p.setPen(line);
        p.setBrush(Qt::NoBrush);
        const qreal d = open ? 4 : -4;
        p.drawPolyline(QPolygonF{{x + d, 9}, {x, 9}, {x, 23}, {x + d, 23}});
        p.setPen(Qt::NoPen);
        p.setBrush(color);
    };
    auto arrow = [&](qreal x1, qreal x2) {
        p.setPen(line);
        p.drawLine(QPointF(x1, 16), QPointF(x2, 16));
        p.setPen(Qt::NoPen);
        triangle(x2 > x1 ? x2 - 1 : x2 - 4, 12, 5, 8, x2 > x1);
    };
    switch (g) {
    case Glyph::Blank: break;
    case Glyph::StepBack:
        triangle(8, 9, 10, 14, false);
        p.drawRect(QRectF(20, 9, 3, 14));
        break;
    case Glyph::StepForward:
        p.drawRect(QRectF(9, 9, 3, 14));
        triangle(14, 9, 10, 14, true);
        break;
    case Glyph::PrevEvent:
        triangle(5, 9, 10, 14, false);
        p.drawRect(QRectF(17, 9, 3, 14));
        p.drawRect(QRectF(22, 9, 3, 14));
        break;
    case Glyph::NextEvent:
        p.drawRect(QRectF(7, 9, 3, 14));
        p.drawRect(QRectF(12, 9, 3, 14));
        triangle(17, 9, 10, 14, true);
        break;
    case Glyph::Play: triangle(10, 8, 13, 16, true); break;
    // Mark IN is "]" and Mark OUT "[", Go to IN "]" with an arrow pointing at it and Go to OUT
    // the arrow then "[" (the labelled Capture tool buttons, p.117).
    case Glyph::MarkIn: bracket(19, false); break;
    case Glyph::MarkOut: bracket(13, true); break;
    case Glyph::MarkClip:
        p.setPen(QPen(color, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(7, 8, 18, 16), 2, 2);
        bracket(12, true);
        bracket(20, false);
        break;
    case Glyph::ClearBoth:
        bracket(14, false);
        bracket(18, true);
        break;
    case Glyph::GoToIn:
        bracket(12, false);
        arrow(27, 17);
        break;
    case Glyph::GoToOut:
        arrow(5, 13);
        bracket(18, true);
        break;
    case Glyph::PlayInToOut:
        p.drawRect(QRectF(6, 9, 3, 14));
        triangle(11, 9, 10, 14, true);
        p.drawRect(QRectF(23, 9, 3, 14));
        break;
    case Glyph::AddMarker: p.drawRoundedRect(QRectF(13, 9, 6, 14), 3, 3); break;
    case Glyph::SpliceIn:
    case Glyph::Overwrite:
        p.drawRect(QRectF(6, 21, 9, 3));
        p.drawRect(QRectF(17, 21, 9, 3));
        p.setPen(QPen(color, 3));
        p.drawLine(QPointF(8, 13), QPointF(20, 13));
        p.setPen(Qt::NoPen);
        triangle(19, 8, 8, 10, true);
        break;
    case Glyph::Gang:
        p.setPen(QPen(color, 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(5, 11, 11, 10));
        p.drawEllipse(QRectF(16, 11, 11, 10));
        break;
    }
    p.end();
    pm.setDevicePixelRatio(2.0);
    return QIcon(pm);
}

// The short labels before tracking values (p.386).
QString trackingPrefix(model::TrackingFormat f) {
    switch (f) {
    case model::TrackingFormat::Master: return QStringLiteral("Mas");
    case model::TrackingFormat::Duration: return QStringLiteral("Dur");
    case model::TrackingFormat::InOut: return QStringLiteral("I/O");
    case model::TrackingFormat::Absolute: return QStringLiteral("Abs");
    case model::TrackingFormat::Remain: return QStringLiteral("Rem");
    case model::TrackingFormat::ClipName:
    case model::TrackingFormat::None: return {};
    }
    return {};
}

// The edit rate of the monitor's current clip in frames per second; 25 when nothing is loaded.
double framesPerSecond(const model::MonitorState& s) {
    const model::LoadedClip* c = s.current();
    if (!c) return 25.0;
    return static_cast<double>(c->item.editRate.num()) / static_cast<double>(c->item.editRate.den());
}

// A button of the information row (Clip Name menu, tracking display): green text on near-black, a
// popup menu on click, and a width set by the layout, not by its text.
// The middle Composer button panel's built-in commands (ours, p.1159).
const QList<QStringList>& middleDefaults() {
    static const QList<QStringList> rows{
        {QStringLiteral("edit.lift"), QStringLiteral("edit.extract"), QStringLiteral("trim.trim_mode")},
        {QStringLiteral("edit.replace_edit"), QStringLiteral("edit.fit_to_fill"), QStringLiteral("edit.copy_to_clipboard")}};
    return rows;
}
QToolButton* infoBox(QWidget* parent, const QString& name) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setPopupMode(QToolButton::InstantPopup);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setFixedHeight(kInfoHeight);
    b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    QPalette p = b->palette();
    p.setColor(QPalette::ButtonText, QColor(kInfoText));
    p.setColor(QPalette::Button, QColor(10, 10, 10));
    b->setPalette(p);
    b->setAutoFillBackground(true);
    return b;
}

} // namespace

// ---------------------------------------------------------------- Column

// The widgets of one monitor's column, built by buildColumn() and owned by the Composer (the widgets
// by Qt parenting, the Column itself deleted in ~Composer). `buttons` maps a command id to the first
// button that carries it, for Composer::button().
// ---------------------------------------------------------------- PlayState

// The playing monitor's Play button while it plays (Composer::PlayDisplay): a green arrow over the
// button at normal speed, else the rate in a box laid over the button row, the number centred where
// the arrow is and the direction arrow beside it. It takes no clicks.
class PlayState : public QWidget {
public:
    explicit PlayState(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("playState"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }

    // Covers `button` (in the parent's coordinates) for `shown`, wide enough for the box.
    void place(const QRect& button, const Composer::PlayDisplay& shown) {
        shown_ = shown;
        if (shown.kind == Composer::PlayDisplay::Kind::Stopped) {
            hide();
            return;
        }
        QFont f = font();
        f.setBold(true);
        const QFontMetrics fm(f);
        const int number = fm.horizontalAdvance(QString::number(shown.fps));
        const int arrow = fm.horizontalAdvance(QStringLiteral(" >"));
        // Symmetric about the button's centre, so the number sits where the arrow is.
        const int half = shown.kind == Composer::PlayDisplay::Kind::Normal ? button.width() / 2
                                                                            : std::max(button.width() / 2, number / 2 + arrow + 4);
        setGeometry(button.left() + button.width() / 2 - half, button.top(), 2 * half, button.height());
        centre_ = half;
        show();
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        using Kind = Composer::PlayDisplay::Kind;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (shown_.kind == Kind::Normal) {
            // The play arrow, green, where the button draws it (glyph(): 16 x 16 in the middle).
            const QPointF o(width() / 2.0 - 8, height() / 2.0 - 8);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(kPlayGreen));
            p.drawPolygon(QPolygonF{o + QPointF(5, 4), o + QPointF(11.5, 8), o + QPointF(5, 12)});
            return;
        }
        const bool forward = shown_.kind == Kind::Forward;
        const QColor box(forward ? kForwardBox : kReverseBox);
        const QColor ink(forward ? kForwardText : kReverseText);
        p.setPen(QPen(box.darker(115), 1));
        p.setBrush(box);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 1.5, -0.5, -1.5), 3, 3);
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(ink);
        const QFontMetrics fm(f);
        const QString number = QString::number(shown_.fps);
        const int w = fm.horizontalAdvance(number);
        const QRect numberBox(centre_ - w / 2, 0, w, height());
        p.drawText(numberBox, Qt::AlignCenter, number);
        if (forward) {
            p.drawText(QRect(numberBox.right() + 1, 0, width() - numberBox.right() - 1, height()), Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral(" >"));
        } else {
            p.drawText(QRect(0, 0, numberBox.left(), height()), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("< "));
        }
    }

private:
    // The owner's colours: a dark green box with light green text forward, dark amber with light
    // amber backward.
    static constexpr QRgb kPlayGreen = 0xff4fd46a;
    static constexpr QRgb kForwardBox = 0xff1e4a2a;
    static constexpr QRgb kForwardText = 0xff8ff0a4;
    static constexpr QRgb kReverseBox = 0xff5a4008;
    static constexpr QRgb kReverseText = 0xffffc94d;

    Composer::PlayDisplay shown_;
    int centre_ = 0;
};

struct Composer::Column {
    QWidget* widget = nullptr;
    QWidget* controls = nullptr;
    QToolButton* trimCounter = nullptr;
    QToolButton* clipName = nullptr;
    QToolButton* tracking = nullptr;
    // The second row of data (p.385, p.1158): the Fast Menu, the second tracking display and, over the
    // Source monitor, the gang icon.
    QWidget* info2 = nullptr;
    QToolButton* tracking2 = nullptr;
    QMenu* trackingMenu2 = nullptr;
    QToolButton* fastMenu = nullptr;
    QLabel* gangIcon = nullptr;
    MonitorPicture* picture = nullptr;
    PositionBar* bar = nullptr;
    QMenu* clipMenu = nullptr;
    QMenu* trackingMenu = nullptr;
    QHash<QString, QToolButton*> buttons;
    QList<QWidget*> rows; // the button rows
    PlayState* playState = nullptr;
};

// ---------------------------------------------------------------- PositionBar

PositionBar::PositionBar(Composer& composer, MonitorId monitor, QWidget* parent)
    : QWidget(parent), composer_(composer), monitor_(monitor) {
    setObjectName(monitor == MonitorId::Source ? QStringLiteral("sourcePositionBar")
                                               : QStringLiteral("recordPositionBar"));
    setFixedHeight(kBarHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize PositionBar::sizeHint() const {
    return {200, kBarHeight};
}

// The bar spans the whole clip: x = 0 is frame 0 and the right edge the last frame (duration - 1).
// x outside the bar is clamped; frameAt and xFor are each other's inverse up to rounding.
std::int64_t PositionBar::frameAt(int x) const {
    const std::int64_t d = composer_.state(monitor_).duration();
    if (d <= 1 || width() <= 1) return 0;
    const double t = std::clamp(static_cast<double>(x) / static_cast<double>(width() - 1), 0.0, 1.0);
    return std::llround(t * static_cast<double>(d - 1));
}

int PositionBar::xFor(std::int64_t frame) const {
    const std::int64_t d = composer_.state(monitor_).duration();
    if (d <= 1) return 0;
    return static_cast<int>(std::lround(static_cast<double>(frame) / static_cast<double>(d - 1) * (width() - 1)));
}

// Background (Source/Record colour or plain grey) with 24 tick marks, then the marked span, the
// markers and the blue position indicator on top.
void PositionBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const model::MonitorState& s = composer_.state(monitor_);
    QColor base(kPlainBar);
    if (composer_.sourceRecordColors()) base = QColor(monitor_ == MonitorId::Source ? kSourceBar : kRecordBar);
    p.fillRect(rect(), base);
    if (s.isEmpty()) return;
    // Tick Marks in Position Bars (p.1159): "duration indicators that appear incrementally along the
    // position bars". Ours: one every 1, 2, 5, 10 or 30 seconds, or 1, 5, 10 or 30 minutes, the first
    // interval that leaves at least 6 pixels between them.
    if (composer_.display().tickMarks && s.duration() > 1) {
        const std::int64_t fps = std::max(TimecodeFormat::forRate(s.current()->item.editRate, false).nominalFps, 1);
        static constexpr int kSeconds[] = {1, 2, 5, 10, 30, 60, 300, 600, 1800, 3600};
        std::int64_t step = 0;
        for (int sec : kSeconds) {
            step = sec * fps;
            if (static_cast<double>(step) * width() / static_cast<double>(s.duration()) >= 6.0) break;
        }
        p.setPen(base.darker(150));
        for (std::int64_t f = step; f < s.duration(); f += step) {
            const int x = xFor(f);
            p.drawLine(x, height() - 4, x, height() - 1);
        }
    }
    // Marked material, then the indicator.
    if (s.in() || s.out()) p.fillRect(QRect(QPoint(xFor(s.markedStart()), 2), QPoint(xFor(s.markedEnd()), height() - 3)), QColor::fromRgba(kMarkedSpan));
    // Markers: ovals in their colours (p.413); spanned ones as bars.
    p.setRenderHint(QPainter::Antialiasing);
    for (const model::Marker& m : composer_.shownMarkers(monitor_)) {
        const QColor c = QColor::fromRgb(model::markerColor(m.color));
        p.setPen(QPen(c.darker(150), 1));
        p.setBrush(c);
        if (m.isSpanned()) {
            p.drawRoundedRect(QRectF(xFor(m.frame), 2, std::max(xFor(m.end() - 1) - xFor(m.frame), 4), 5), 2, 2);
        } else {
            p.drawEllipse(QRectF(xFor(m.frame) - 4, 1, 8, 6));
        }
    }
    // Phantom marks (p.501): "blue mark In or Out icons in the position bars", as brackets.
    const auto [phantomIn, phantomOut] = composer_.phantomMarks(monitor_);
    p.setPen(QPen(QColor(kPhantomMark), 2));
    p.setBrush(Qt::NoBrush);
    if (phantomIn) {
        const int x = xFor(*phantomIn);
        p.drawPolyline(QPolygon{QPoint(x + 4, 1), QPoint(x, 1), QPoint(x, height() - 2), QPoint(x + 4, height() - 2)});
    }
    if (phantomOut) {
        const int x = xFor(*phantomOut);
        p.drawPolyline(QPolygon{QPoint(x - 4, 1), QPoint(x, 1), QPoint(x, height() - 2), QPoint(x - 4, height() - 2)});
    }
    p.fillRect(QRect(xFor(s.position()) - 1, 0, 2, height()), QColor(kIndicator));
}

// The first shown marker whose start is within 4 pixels of x.
std::optional<model::Marker> PositionBar::markerAt(int x) const {
    const model::MonitorState& s = composer_.state(monitor_);
    if (s.isEmpty()) return std::nullopt;
    for (const model::Marker& m : composer_.shownMarkers(monitor_)) {
        if (std::abs(xFor(m.frame) - x) <= 4) return m;
    }
    return std::nullopt;
}

void PositionBar::mouseDoubleClickEvent(QMouseEvent* event) {
    // Double-clicking a marker opens Edit Marker (p.416).
    if (const auto m = markerAt(event->position().toPoint().x())) emit composer_.markerEditRequested(monitor_, m->id);
}

// Clicking the bar activates its monitor, stops playback and jumps there; dragging with the left
// button held keeps moving (mouseMoveEvent).
void PositionBar::mousePressEvent(QMouseEvent* event) {
    composer_.setActiveMonitor(monitor_);
    composer_.stop();
    composer_.moveTo(monitor_, frameAt(event->position().toPoint().x()));
}

void PositionBar::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) composer_.moveTo(monitor_, frameAt(event->position().toPoint().x()));
}

// ---------------------------------------------------------------- MonitorPicture

MonitorPicture::MonitorPicture(Composer& composer, MonitorId monitor, QWidget* parent)
    : QWidget(parent), composer_(composer), monitor_(monitor) {
    setObjectName(monitor == MonitorId::Source ? QStringLiteral("sourceMonitor") : QStringLiteral("recordMonitor"));
    setAcceptDrops(true);
    setMinimumSize(120, 68);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QRect MonitorPicture::pictureRect() const {
    // The project's aspect ratio, letterboxed or pillarboxed (p.379-381).
    const Rational a = composer_.aspectRatio();
    const double ratio = static_cast<double>(a.num()) / static_cast<double>(a.den());
    int w = width();
    int h = static_cast<int>(std::lround(w / ratio));
    if (h > height()) {
        h = height();
        w = static_cast<int>(std::lround(h * ratio));
    }
    return {(width() - w) / 2, (height() - h) / 2, w, h};
}

// Draws, in order: the dark surround and the black picture area, the frame from the PictureResolver
// (or "Media Offline"), the IN/OUT sawtooth when the position is on a mark, a marker oval, and the
// highlight frame of the active monitor (dual-monitor mode only).
std::optional<model::TrimSide> MonitorPicture::shownTrimSide() const {
    const Composer::TrimDisplay& t = composer_.trimDisplay();
    if (!t.on) return std::nullopt;
    if (trimSide_) return trimSide_;
    if (t.small) return std::nullopt; // the Source monitor stays
    return monitor_ == MonitorId::Source ? model::TrimSide::A : model::TrimSide::B;
}

void MonitorPicture::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 18, 18));
    const QRect pic = pictureRect();
    p.fillRect(pic, Qt::black);
    const Composer::DragDisplay& drag = composer_.dragDisplay();
    if (drag.on && !trimSide_ && (drag.fourFrame || monitor_ == MonitorId::Record)) {
        // Display During Segment Drag (p.617-618): two frames side by side, or the Position Bar frame.
        const double q = composer_.qualityScale();
        auto paintFrame = [&](const QRect& area, std::int64_t frame) {
            const QSize decode(std::max(1, static_cast<int>(area.width() * q)), std::max(1, static_cast<int>(area.height() * q)));
            const QImage image = composer_.pictureAt(MonitorId::Record, frame, decode);
            if (image.isNull()) return;
            const QSize fit = image.size().scaled(area.size(), Qt::KeepAspectRatio);
            p.drawImage(QRect(area.left() + (area.width() - fit.width()) / 2, area.top() + (area.height() - fit.height()) / 2,
                              fit.width(), fit.height()),
                        image);
        };
        if (!drag.fourFrame) {
            paintFrame(pic, drag.frames[0]);
            return;
        }
        const bool single = composer_.isSingleMonitor();
        const std::int64_t left = monitor_ == MonitorId::Source ? drag.frames[0] : single ? drag.frames[0] : drag.frames[2];
        const std::int64_t right = monitor_ == MonitorId::Source ? drag.frames[1] : drag.frames[3];
        const int half = pic.width() / 2;
        const QRect a(pic.left(), pic.top(), half - 1, pic.height());
        const QRect b(pic.left() + half + 1, pic.top(), pic.width() - half - 1, pic.height());
        p.fillRect(QRect(pic.left() + half - 1, pic.top(), 2, pic.height()), QColor(18, 18, 18));
        paintFrame(a, left);
        paintFrame(b, right);
        return;
    }
    if (const std::optional<model::TrimSide> side = shownTrimSide()) {
        // A trim picture: the sequence's frame on that side of the transition, without marks.
        const double q = composer_.qualityScale();
        const QSize decode(std::max(1, static_cast<int>(pic.width() * q)), std::max(1, static_cast<int>(pic.height() * q)));
        bool offline = false;
        const QImage image = composer_.pictureAt(MonitorId::Record, composer_.trimFrame(*side), decode, &offline);
        if (!image.isNull()) {
            const QSize fit = image.size().scaled(pic.size(), Qt::KeepAspectRatio);
            p.setRenderHint(QPainter::SmoothPixmapTransform, composer_.smoothScaling());
            p.drawImage(QRect(pic.left() + (pic.width() - fit.width()) / 2, pic.top() + (pic.height() - fit.height()) / 2,
                              fit.width(), fit.height()),
                        image);
        }
        if (composer_.trimPlaySide() == *side && !composer_.trimDisplay().dualImage) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 1));
            p.drawRect(rect().adjusted(0, 0, -1, -1));
        }
        return;
    }
    const model::MonitorState& s = composer_.state(monitor_);
    if (!s.isEmpty()) {
        bool offline = false;
        // Draft Quality and Best Performance decode fewer pixels (p.403).
        const double q = composer_.qualityScale();
        const QSize decode(std::max(1, static_cast<int>(pic.width() * q)), std::max(1, static_cast<int>(pic.height() * q)));
        const QImage image = composer_.picture(monitor_, decode, &offline);
        if (!image.isNull()) {
            // Scaled to fit, centred: other shapes than the project's are
            // letterboxed or pillarboxed.
            const QSize fit = image.size().scaled(pic.size(), Qt::KeepAspectRatio);
            const QRect at(pic.left() + (pic.width() - fit.width()) / 2, pic.top() + (pic.height() - fit.height()) / 2,
                           fit.width(), fit.height());
            p.setRenderHint(QPainter::SmoothPixmapTransform, composer_.smoothScaling());
            p.drawImage(at, image);
        } else if (offline) {
            // "A black screen appears with the words Media Offline" (p.347).
            QFont f = font();
            f.setPointSizeF(std::max(9.0, pic.height() / 14.0));
            f.setBold(true);
            p.setFont(f);
            p.setPen(palette().color(QPalette::BrightText));
            p.drawText(pic, Qt::AlignCenter, tr("Media Offline"));
        }
        // The sawtooth marks on the IN and OUT frames (p.405-406).
        p.setPen(QPen(QColor(230, 230, 230), 2));
        auto saw = [&](int x, int dir) {
            QPolygon teeth;
            for (int i = 0; i <= 8; ++i) teeth << QPoint(x + ((i % 2) ? dir * 6 : 0), pic.top() + 8 + i * 8);
            p.drawPolyline(teeth);
        };
        if (s.in() && *s.in() == s.position()) saw(pic.left() + 6, 1);
        if (s.out() && *s.out() == s.position()) saw(pic.right() - 6, -1);
        // A marker on this frame: an oval at the bottom of the picture
        // (p.413).
        for (const model::Marker& m : composer_.shownMarkers(monitor_)) {
            if (s.position() < m.frame || s.position() >= m.end()) continue;
            p.setRenderHint(QPainter::Antialiasing);
            const QColor c = QColor::fromRgb(model::markerColor(m.color));
            p.setPen(QPen(c.darker(150), 1));
            p.setBrush(c);
            p.drawEllipse(QRectF(pic.center().x() - 12, pic.bottom() - 18, 24, 12));
            break;
        }
    }
    if (composer_.activeMonitor() == monitor_ && !composer_.isSingleMonitor()) {
        p.setPen(QPen(palette().color(QPalette::Highlight), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void MonitorPicture::mousePressEvent(QMouseEvent* event) {
    if (const std::optional<model::TrimSide> side = shownTrimSide()) {
        // "Click either the outgoing (A-side) or incoming (B-side) monitor to play in real time during
        // the trim" (p.659): the Record monitor plays the sequence; the clicked side follows it.
        composer_.setTrimPlaySide(*side);
        composer_.setActiveMonitor(MonitorId::Record);
        composer_.setFocus();
        composer_.refreshDisplay();
        return;
    }
    composer_.setActiveMonitor(monitor_);
    composer_.setFocus();
    // Clicking the oval of a marker opens Edit Marker (p.416).
    const model::MonitorState& s = composer_.state(monitor_);
    const QRect pic = pictureRect();
    const QRectF oval(pic.center().x() - 12, pic.bottom() - 18, 24, 12);
    if (s.isEmpty() || !oval.contains(event->position())) return;
    for (const model::Marker& m : composer_.shownMarkers(monitor_)) {
        if (s.position() >= m.frame && s.position() < m.end()) {
            emit composer_.markerEditRequested(monitor_, m.id);
            return;
        }
    }
}

void MonitorPicture::contextMenuEvent(QContextMenuEvent* event) {
    composer_.setActiveMonitor(monitor_);
    composer_.contextMenu()->exec(event->globalPos());
}

void MonitorPicture::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(binItemsMime()) || event->mimeData()->hasFormat(mediaFilesMime())) {
        event->acceptProposedAction();
    }
}

void MonitorPicture::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasFormat(mediaFilesMime())) {
        // Files from the Source Browser, to review without putting them in
        // a bin (p.316).
        const QStringList paths =
            QString::fromUtf8(event->mimeData()->data(mediaFilesMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        event->setDropAction(Qt::CopyAction);
        event->accept();
        emit composer_.filesDropped(monitor_, paths);
        return;
    }
    // Clips dragged from a bin (p.394): the bin first, then the items.
    QStringList lines = QString::fromUtf8(event->mimeData()->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2) return;
    const QString bin = lines.takeFirst();
    event->setDropAction(Qt::CopyAction);
    event->accept();
    composer_.loadFromBin(monitor_, bin, lines);
}

// ---------------------------------------------------------------- Composer

// Builds both columns from the default button rows, the center duration label and the hidden Trim
// bar, and the playback timer. The saved button layout and display settings arrive later through
// setButtonLayout() and setDisplay() (MainWindow::applySettings).
Composer::Composer(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("composer"));
    setFocusPolicy(Qt::StrongFocus);

    // The buttons' default commands; their pictures serve every palette
    // that shows these commands.
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        for (const QList<ButtonSpec>& row : buttonRows(m)) {
            QStringList ids;
            for (const ButtonSpec& spec : row) {
                const QString id = QString::fromLatin1(spec.command);
                ids << id;
                if (!id.isEmpty()) {
                    registerCommandIcon(id, glyph(spec.glyph, spec.glyph == Glyph::SpliceIn    ? QColor(kSplice)
                                                              : spec.glyph == Glyph::Overwrite ? QColor(kOverwrite)
                                                              : spec.glyph == Glyph::AddMarker ? QColor(kMarker)
                                                                                               : QColor(0xc8, 0xc8, 0xc8)));
                }
            }
            layout_[m == MonitorId::Source ? 0 : 1] << ids;
        }
    }

    center_ = new QLabel(this);
    center_->setObjectName(QStringLiteral("centerDuration"));
    center_->setAlignment(Qt::AlignCenter);
    center_->setFixedSize(64, kInfoHeight);
    QPalette cp = center_->palette();
    cp.setColor(QPalette::WindowText, QColor(kInfoText));
    cp.setColor(QPalette::Window, QColor(10, 10, 10));
    center_->setPalette(cp);
    center_->setAutoFillBackground(true);

    QWidget* sourceColumn = buildColumn(MonitorId::Source);
    QWidget* recordColumn = buildColumn(MonitorId::Record);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(3);
    auto* layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(sourceColumn, 1);
    // The middle Composer button panel (p.1159): one row of three per button row, level with them.
    middle_ = new QWidget(this);
    middle_->setObjectName(QStringLiteral("middlePanel"));
    auto* middleRows = new QVBoxLayout(middle_);
    middleRows->setContentsMargins(0, 0, 0, 0);
    middleRows->setSpacing(1);
    if (middleLayout_.isEmpty()) middleLayout_ = middleDefaults();
    for (int r = 0; r < middleLayout_.size(); ++r) {
        auto* rowWidget = new QWidget(middle_);
        auto* h = new QHBoxLayout(rowWidget);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);
        for (int i = 0; i < middleLayout_.at(r).size(); ++i) {
            auto* b = new MappableButton(rowWidget);
            b->setFixedSize(kButtonWidth, kToolButtonHeight);
            b->setIconSize(QSize(16, 16));
            b->setProperty("slot", QPoint(r, i));
            const QString id = middleLayout_.at(r).at(i);
            b->setCommand(id);
            b->setObjectName(id.isEmpty() ? QStringLiteral("blankButton") : id);
            connect(b, &MappableButton::commandClicked, this, [this](const QString& command) {
                if (!perform(command)) emit commandRequested(command);
            });
            connect(b, &MappableButton::commandMapped, this, [this, b, r, i](const QString& command) {
                middleLayout_[r][i] = command;
                b->setCommand(command);
                b->setObjectName(command.isEmpty() ? QStringLiteral("blankButton") : command);
                emit buttonsChanged();
            });
            h->addWidget(b);
        }
        middleRows->addWidget(rowWidget);
        middleRows_ << rowWidget;
    }
    middle_->hide();
    layout->addWidget(middle_, 0, Qt::AlignBottom);
    layout->addWidget(recordColumn, 1);
    outer->addLayout(layout, 1);

    // The Trim buttons under the trim display (p.656).
    trimBar_ = new QWidget(this);
    trimBar_->setObjectName(QStringLiteral("trimBar"));
    auto* trims = new QHBoxLayout(trimBar_);
    trims->setContentsMargins(0, 0, 0, 0);
    trims->setSpacing(2);
    trims->addStretch(1);
    const std::pair<const char*, const char*> trimButtons[] = {
        {"trim.trim_backward_10_frames", "<<"}, {"trim.trim_backward_1_frame", "<"},
        {"trim.trim_forward_1_frame", ">"}, {"trim.trim_forward_10_frames", ">>"}};
    for (const auto& [id, label] : trimButtons) {
        auto* b = new QToolButton(trimBar_);
        b->setObjectName(QString::fromLatin1(id));
        b->setAutoRaise(true);
        b->setFixedSize(2 * kButtonWidth, kToolButtonHeight);
        b->setIcon(trimGlyph(QString::fromLatin1(label)));
        b->setIconSize(QSize(24, 16));
        const QString command = QString::fromLatin1(id);
        connect(b, &QToolButton::clicked, this, [this, command] { emit commandRequested(command); });
        trims->addWidget(b);
    }
    // The Play Loop button (p.657), lit while a loop plays; its picture is ours.
    auto* loop = new QToolButton(trimBar_);
    loop->setObjectName(QStringLiteral("trim.play_loop"));
    loop->setToolTip(tr("Play Loop"));
    loop->setAutoRaise(true);
    loop->setCheckable(true);
    loop->setFixedSize(2 * kButtonWidth, kToolButtonHeight);
    loop->setIcon(glyph(Glyph::PlayInToOut, QColor(0xc8, 0xc8, 0xc8)));
    connect(loop, &QToolButton::clicked, this, [this, loop] {
        loop->setChecked(isLooping());
        emit commandRequested(QStringLiteral("trim.play_loop"));
    });
    connect(this, &Composer::loopChanged, loop, &QToolButton::setChecked);
    trims->addSpacing(kButtonWidth);
    trims->addWidget(loop);
    trims->addStretch(1);
    trimBar_->hide();
    outer->addWidget(trimBar_);
    // The four-frame display's offset counter, centred below the monitors (p.617).
    dragCounter_ = new QLabel(this);
    dragCounter_->setObjectName(QStringLiteral("dragCounter"));
    dragCounter_->setAlignment(Qt::AlignCenter);
    dragCounter_->setFixedHeight(kInfoHeight);
    QPalette dp = dragCounter_->palette();
    dp.setColor(QPalette::WindowText, QColor(kTrimActive));
    dragCounter_->setPalette(dp);
    dragCounter_->hide();
    outer->addWidget(dragCounter_);

    // Playback steps: tick() runs every kTickInterval ms while playing (started by play(), stopped by
    // stop()).
    timer_ = new QTimer(this);
    timer_->setInterval(kTickInterval);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &Composer::tick);
    refresh();
}

// The Columns are plain structs, not QObjects; their widgets go with the Composer's children.
Composer::~Composer() {
    delete columns_[0];
    delete columns_[1];
}

Composer::Column& Composer::column(MonitorId monitor) const {
    return *columns_[monitor == MonitorId::Source ? 0 : 1];
}

// Builds one monitor's column, top to bottom: information row, picture, position bar, Trim counter
// (hidden outside Trim mode) and the button rows. The Source column also carries the center duration
// display at the end of its information row.
QWidget* Composer::buildColumn(MonitorId monitor) {
    auto* c = new Column;
    columns_[monitor == MonitorId::Source ? 0 : 1] = c;
    const bool source = monitor == MonitorId::Source;
    c->widget = new QWidget(this);
    c->widget->setObjectName(source ? QStringLiteral("sourceColumn") : QStringLiteral("recordColumn"));

    // Information row: Clip Name menu and tracking display; on the Source
    // side the center duration display follows (p.380, p.384).
    c->clipName = infoBox(c->widget, source ? QStringLiteral("sourceClipName") : QStringLiteral("recordClipName"));
    c->tracking = infoBox(c->widget, source ? QStringLiteral("sourceTracking") : QStringLiteral("recordTracking"));
    c->clipMenu = new QMenu(c->clipName);
    connect(c->clipMenu, &QMenu::aboutToShow, this, [this, monitor] { clipNameMenu(monitor); });
    c->clipName->setMenu(c->clipMenu);
    c->trackingMenu = new QMenu(c->tracking);
    for (model::TrackingFormat f : model::trackingFormats()) {
        QAction* a = c->trackingMenu->addAction(model::trackingLabel(f));
        a->setCheckable(true);
        connect(a, &QAction::triggered, this, [this, monitor, f] { setTracking(monitor, f); });
    }
    connect(c->trackingMenu, &QMenu::aboutToShow, this, [this, c, monitor] {
        for (QAction* a : c->trackingMenu->actions()) a->setChecked(a->text() == model::trackingLabel(tracking(monitor)));
    });
    c->tracking->setMenu(c->trackingMenu);
    auto* info = new QHBoxLayout;
    info->setSpacing(4);
    if (source) {
        info->addWidget(c->clipName, 3);
        info->addWidget(c->tracking, 3);
        info->addWidget(center_);
    } else {
        info->addWidget(c->tracking, 3);
        info->addWidget(c->clipName, 3);
    }

    // The second row of data, above the first (the picture on p.384).
    c->info2 = new QWidget(c->widget);
    c->info2->setObjectName(source ? QStringLiteral("sourceInfo2") : QStringLiteral("recordInfo2"));
    auto* info2 = new QHBoxLayout(c->info2);
    info2->setContentsMargins(0, 0, 0, 0);
    info2->setSpacing(4);
    c->tracking2 = infoBox(c->info2, source ? QStringLiteral("sourceTracking2") : QStringLiteral("recordTracking2"));
    c->trackingMenu2 = new QMenu(c->tracking2);
    for (model::TrackingFormat f : model::trackingFormats()) {
        QAction* a = c->trackingMenu2->addAction(model::trackingLabel(f));
        a->setCheckable(true);
        connect(a, &QAction::triggered, this, [this, monitor, f] { setSecondTracking(monitor, f); });
    }
    connect(c->trackingMenu2, &QMenu::aboutToShow, this, [this, c, monitor] {
        for (QAction* a : c->trackingMenu2->actions()) a->setChecked(a->text() == model::trackingLabel(secondTracking(monitor)));
    });
    c->tracking2->setMenu(c->trackingMenu2);
    // The Fast Menu: the monitor's right-click menu (ours: the guide names "Fast Menu icons" only).
    c->fastMenu = new QToolButton(c->info2);
    c->fastMenu->setObjectName(source ? QStringLiteral("sourceFastMenu") : QStringLiteral("recordFastMenu"));
    c->fastMenu->setAutoRaise(true);
    c->fastMenu->setFixedSize(kInfoHeight + 4, kInfoHeight);
    c->fastMenu->setIcon(glyphIcon(GlyphIcon::FastMenu, QColor(kInfoText)));
    connect(c->fastMenu, &QToolButton::clicked, this, [this, monitor, c] {
        setActiveMonitor(monitor);
        contextMenu()->exec(c->fastMenu->mapToGlobal(QPoint(0, c->fastMenu->height())));
    });
    if (source) {
        c->gangIcon = new QLabel(c->info2);
        c->gangIcon->setObjectName(QStringLiteral("gangIcon"));
        c->gangIcon->setPixmap(glyph(Glyph::Gang, QColor(kInfoText)).pixmap(QSize(16, 16)));
        c->gangIcon->setToolTip(tr("Ganged"));
        info2->addWidget(c->fastMenu);
        info2->addWidget(c->gangIcon);
        info2->addStretch(3);
        info2->addWidget(c->tracking2, 3);
        info2->addSpacing(64 + 4); // under the center duration display
    } else {
        info2->addWidget(c->tracking2, 3);
        info2->addStretch(3);
        info2->addWidget(c->fastMenu);
    }
    c->info2->hide();

    c->picture = new MonitorPicture(*this, monitor, c->widget);
    c->bar = new PositionBar(*this, monitor, c->widget);

    // The button rows sit in one widget, which Trim mode hides.
    c->controls = new QWidget(c->widget);
    c->controls->setObjectName(source ? QStringLiteral("sourceControls") : QStringLiteral("recordControls"));
    auto* rows = new QVBoxLayout(c->controls);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(1);
    for (const QList<ButtonSpec>& row : buttonRows(monitor)) {
        auto* rowWidget = new QWidget(c->controls);
        auto* h = new QHBoxLayout(rowWidget);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(0);
        const int r = static_cast<int>(c->rows.size());
        for (int i = 0; i < row.size(); ++i) {
            // A user-selectable button (p.96): the Command Palette maps
            // other commands, or Blank, onto it.
            auto* b = new MappableButton(rowWidget);
            b->setFixedSize(kButtonWidth, kToolButtonHeight);
            b->setIconSize(QSize(16, 16));
            b->setProperty("slot", QPoint(r, i));
            setupButton(monitor, b, layout_[source ? 0 : 1].value(r).value(i));
            connect(b, &MappableButton::commandClicked, this, [this, monitor](const QString& id) {
                setActiveMonitor(monitor);
                if (!perform(id)) emit commandRequested(id);
            });
            connect(b, &MappableButton::commandMapped, this, [this, monitor, b, r, i](const QString& id) {
                layout_[monitor == MonitorId::Source ? 0 : 1][r][i] = id;
                setupButton(monitor, b, id);
                refresh();
                emit buttonsChanged();
            });
            if (i > 0) h->addStretch(1);
            h->addWidget(b);
        }
        rows->addWidget(rowWidget);
        c->rows << rowWidget;
    }
    c->playState = new PlayState(c->controls);

    auto* v = new QVBoxLayout(c->widget);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    v->addWidget(c->info2);
    v->addLayout(info);
    v->addWidget(c->picture, 1);
    v->addWidget(c->bar);
    // Trim mode: the frame counter of this side of the transition (p.651):
    // the outgoing (A-side) frames on the left, the incoming on the right.
    c->trimCounter = new QToolButton(c->widget);
    c->trimCounter->setObjectName(source ? QStringLiteral("trimCounterA") : QStringLiteral("trimCounterB"));
    c->trimCounter->setFixedHeight(kInfoHeight);
    c->trimCounter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    c->trimCounter->setAutoRaise(true);
    c->trimCounter->hide();
    connect(c->trimCounter, &QToolButton::clicked, this, [this, source] {
        // Shift-click: both sides, a dual-roller trim.
        const bool both = QApplication::keyboardModifiers() & Qt::ShiftModifier;
        emit trimSideRequested(both ? model::TrimSide::Both : source ? model::TrimSide::A : model::TrimSide::B);
    });
    v->addWidget(c->trimCounter);
    v->addWidget(c->controls);
    if (!source) {
        // Small Trim display (p.648): in place of the Record monitor, the outgoing and incoming frames
        // side by side, each with its frame counter beneath.
        smallTrim_ = new QWidget(c->widget);
        smallTrim_->setObjectName(QStringLiteral("smallTrim"));
        auto* grid = new QGridLayout(smallTrim_);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(3);
        for (int i = 0; i < 2; ++i) {
            const bool a = i == 0;
            auto* pic = new MonitorPicture(*this, MonitorId::Record, smallTrim_);
            pic->setObjectName(a ? QStringLiteral("smallTrimA") : QStringLiteral("smallTrimB"));
            pic->setMinimumSize(60, 34);
            pic->setTrimSide(a ? model::TrimSide::A : model::TrimSide::B);
            pic->setAcceptDrops(false);
            grid->addWidget(pic, 0, i);
            auto* counter = new QToolButton(smallTrim_);
            counter->setObjectName(a ? QStringLiteral("smallTrimCounterA") : QStringLiteral("smallTrimCounterB"));
            counter->setFixedHeight(kInfoHeight);
            counter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            counter->setAutoRaise(true);
            connect(counter, &QToolButton::clicked, this, [this, a] {
                const bool both = QApplication::keyboardModifiers() & Qt::ShiftModifier;
                emit trimSideRequested(both ? model::TrimSide::Both : a ? model::TrimSide::A : model::TrimSide::B);
            });
            grid->addWidget(counter, 1, i);
            smallCounters_ << counter;
        }
        grid->setRowStretch(0, 1);
        smallTrim_->hide();
        v->insertWidget(v->indexOf(c->picture) + 1, smallTrim_, 1);
    }
    return c->widget;
}

// Gives a button slot its command (empty: Blank). Called when the column is built, when a command is
// mapped onto the button and when a saved layout is applied.
void Composer::setupButton(MonitorId monitor, MappableButton* b, const QString& id) {
    Column& c = column(monitor);
    // Forget whatever command this button carried before.
    for (auto it = c.buttons.begin(); it != c.buttons.end();) {
        it = it.value() == b ? c.buttons.erase(it) : std::next(it);
    }
    b->setCommand(id);
    b->setCheckable(id == QLatin1String("other.gang"));
    if (id.isEmpty()) {
        // Blank (p.97): does nothing, but takes mapped commands.
        b->setObjectName(QStringLiteral("blankButton"));
        b->setEnabled(true);
        return;
    }
    b->setObjectName(id);
    if (!c.buttons.contains(id)) c.buttons.insert(id, b);
    // The Record monitor is always ganged (p.495).
    b->setEnabled(!later(id) && !(id == QLatin1String("other.gang") && monitor == MonitorId::Record));
}

// The current command of every slot, in the format described in composer.h.
QJsonObject Composer::buttonLayout() const {
    QJsonObject out;
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        QJsonArray rows;
        for (const QStringList& row : layout_[m == MonitorId::Source ? 0 : 1]) rows.append(QJsonArray::fromStringList(row));
        out.insert(m == MonitorId::Source ? QStringLiteral("source") : QStringLiteral("record"), rows);
    }
    QJsonArray middle;
    for (const QStringList& row : middleLayout_) middle.append(QJsonArray::fromStringList(row));
    out.insert(QStringLiteral("middle"), middle);
    return out;
}

// The slots as built in, from buttonRows().
QJsonObject Composer::defaultButtonLayout() {
    QJsonObject out;
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        QJsonArray rows;
        for (const QList<ButtonSpec>& row : buttonRows(m)) {
            QJsonArray ids;
            for (const ButtonSpec& spec : row) ids.append(QString::fromLatin1(spec.command));
            rows.append(ids);
        }
        out.insert(m == MonitorId::Source ? QStringLiteral("source") : QStringLiteral("record"), rows);
    }
    QJsonArray middle;
    for (const QStringList& row : middleDefaults()) middle.append(QJsonArray::fromStringList(row));
    out.insert(QStringLiteral("middle"), middle);
    return out;
}

void Composer::setButtonLayout(const QJsonObject& layout) {
    // Every slot keeps its place; slots the layout does not name have their
    // default command.
    const QJsonObject defaults = defaultButtonLayout();
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        const QString key = m == MonitorId::Source ? QStringLiteral("source") : QStringLiteral("record");
        const QJsonArray given = layout.value(key).toArray();
        const QJsonArray fallback = defaults.value(key).toArray();
        QList<QStringList>& rows = layout_[m == MonitorId::Source ? 0 : 1];
        rows.clear();
        for (int r = 0; r < fallback.size(); ++r) {
            QStringList row;
            const QJsonArray g = given.at(r).toArray();
            const QJsonArray f = fallback.at(r).toArray();
            for (int i = 0; i < f.size(); ++i) row << (g.at(i).isString() ? g.at(i).toString() : f.at(i).toString());
            rows << row;
        }
        if (!columns_[m == MonitorId::Source ? 0 : 1]) continue;
        for (MappableButton* b : column(m).controls->findChildren<MappableButton*>()) {
            const QPoint slot = b->property("slot").toPoint();
            const QString id = rows.value(slot.x()).value(slot.y());
            if (id != b->command()) setupButton(m, b, id);
        }
    }
    const QJsonArray middle = layout.value(QStringLiteral("middle")).toArray();
    middleLayout_.clear();
    for (int r = 0; r < middleDefaults().size(); ++r) {
        QStringList row;
        for (int i = 0; i < middleDefaults().at(r).size(); ++i) {
            const QJsonValue v = middle.at(r).toArray().at(i);
            row << (v.isString() ? v.toString() : middleDefaults().at(r).at(i));
        }
        middleLayout_ << row;
    }
    if (middle_) {
        for (MappableButton* b : middle_->findChildren<MappableButton*>()) {
            const QPoint slot = b->property("slot").toPoint();
            const QString id = middleLayout_.value(slot.x()).value(slot.y());
            b->setCommand(id);
            b->setObjectName(id.isEmpty() ? QStringLiteral("blankButton") : id);
        }
    }
    refresh();
}

// Shows or hides the information row, the button rows and the center duration display. The button
// rows stay hidden in Trim mode whatever the setting.
void Composer::setDisplay(const Display& display) {
    display_ = display;
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        Column& c = column(m);
        c.clipName->setVisible(display.infoRow);
        c.tracking->setVisible(display.infoRow);
        for (int i = 0; i < c.rows.size(); ++i) c.rows.at(i)->setVisible(i < display.buttonRows);
        const bool trimHere = trim_.on && (!trim_.small || m == MonitorId::Record);
        c.controls->setVisible(!trimHere && display.buttonRows > 0);
    }
    center_->setVisible(display.infoRow && display.centerDuration);
    applyRows();
    refresh();
}

bool Composer::showsSecondRow() const {
    if (!display_.infoRow) return false;
    return display_.twoRows || (display_.flowData && height() >= kFlowHeight);
}

// Shows or hides the second row of data and the middle panel's rows after a setting or a resize.
void Composer::applyRows() {
    const bool second = showsSecondRow();
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        if (!columns_[m == MonitorId::Source ? 0 : 1]) continue;
        column(m).info2->setVisible(second);
    }
    if (middle_) {
        for (int i = 0; i < middleRows_.size(); ++i) middleRows_.at(i)->setVisible(i < display_.buttonRows);
        middle_->setVisible(display_.middlePanel && display_.buttonRows > 0 && !trim_.on);
    }
}

void Composer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (display_.flowData) applyRows();
}

void Composer::setSecondTracking(MonitorId monitor, model::TrackingFormat format) {
    (monitor == MonitorId::Source ? sourceTracking2_ : recordTracking2_) = format;
    refresh();
}

model::TrackingFormat Composer::secondTracking(MonitorId monitor) const {
    return monitor == MonitorId::Source ? sourceTracking2_ : recordTracking2_;
}

QString Composer::secondTrackingText(MonitorId monitor) const {
    const model::TrackingFormat f = secondTracking(monitor);
    const QString value = state(monitor).tracking(f);
    if (value.isEmpty()) return {};
    const QString prefix = trackingPrefix(f);
    return prefix.isEmpty() ? value : prefix + QStringLiteral("  ") + value;
}

// The current clip's markers, filtered by the Show Composer Markers and spanned-marker settings.
QList<model::Marker> Composer::shownMarkers(MonitorId monitor) const {
    const model::MonitorState& s = state(monitor);
    if (s.isEmpty() || !display_.markers) return {};
    QList<model::Marker> out = model::markersOf(s.current()->item);
    if (!display_.spannedMarkers) out.removeIf([](const model::Marker& m) { return m.isSpanned(); });
    return out;
}

model::MonitorState& Composer::state(MonitorId monitor) {
    return monitor == MonitorId::Source ? source_ : record_;
}

const model::MonitorState& Composer::state(MonitorId monitor) const {
    return monitor == MonitorId::Source ? source_ : record_;
}

void Composer::setAspectRatio(const Rational& aspect) {
    aspect_ = aspect;
    refresh();
}

// Returns true when there was nothing to commit, false only when the Committer could not place it.
bool Composer::commitSource() {
    const model::LoadedClip* c = source_.current();
    // Only clips from the Source Browser: they are linked and in no bin
    // (Clipboard Contents is in no bin either, and stays so, p.480).
    if (!c || !c->binId.isEmpty() || c->item.link.isEmpty()) return true;
    const QString bin = committer_ ? committer_(c->item) : QString();
    if (bin.isEmpty()) return false;
    source_.setCurrentBin(bin);
    emit changed();
    return true;
}

void Composer::clearUncommitted() {
    const int removed = source_.removeUncommitted() + record_.removeUncommitted();
    if (removed > 0) refresh();
}

QImage Composer::picture(MonitorId monitor, QSize size, bool* offline) const {
    return pictureAt(monitor, state(monitor).position(), size, offline);
}

QImage Composer::pictureAt(MonitorId monitor, std::int64_t frame, QSize size, bool* offline) const {
    if (offline) *offline = false;
    if (!pictures_ || state(monitor).isEmpty()) return {};
    return pictures_(monitor, frame, size, offline);
}

std::int64_t Composer::trimFrame(model::TrimSide side) const {
    const bool a = side == model::TrimSide::A;
    if (isPlaying() && active_ == MonitorId::Record) {
        const std::int64_t pos = record_.position();
        // The side that plays follows the position; with Dual Image Play both do, the outgoing frame
        // just before the incoming one.
        if (trim_.dualImage || trimPlaySide_ == side) return a ? std::max<std::int64_t>(pos - 1, 0) : pos;
    }
    return a ? trim_.aFrame : trim_.bFrame;
}

void Composer::setTrimPlaySide(model::TrimSide side) {
    trimPlaySide_ = side == model::TrimSide::A ? model::TrimSide::A : model::TrimSide::B;
}

// Loading stops playback and makes the monitor active.
void Composer::load(MonitorId monitor, const QString& binId, const model::BinItem& item) {
    stop();
    state(monitor).load(binId, item);
    setActiveMonitor(monitor);
    refresh();
}

void Composer::loadDefault(const QString& binId, const model::BinItem& item) {
    // Sequences to the Record monitor, everything else to the Source monitor.
    load(item.kind == model::BinItemKind::Sequence ? MonitorId::Record : MonitorId::Source, binId, item);
}

void Composer::loadFromBin(MonitorId monitor, const QString& binId, const QStringList& itemIds) {
    if (!resolver_) return;
    for (const QString& id : itemIds) {
        if (const auto item = resolver_(binId, id)) load(monitor, binId, *item);
    }
}

void Composer::updateItem(const model::BinItem& item) {
    const bool source = source_.replaceItem(item);
    const bool record = record_.replaceItem(item);
    if (source || record) refresh();
}

void Composer::clear() {
    stop();
    source_ = {};
    record_ = {};
    refresh();
}

void Composer::setActiveMonitor(MonitorId monitor) {
    // A single monitor is always the Record monitor (p.381).
    if (single_) monitor = MonitorId::Record;
    if (active_ == monitor) return;
    active_ = monitor;
    refresh();
}

void Composer::setSingleMonitor(bool single) {
    if (single_ == single) return;
    single_ = single;
    column(MonitorId::Source).widget->setVisible(!single);
    if (single) active_ = MonitorId::Record;
    refresh();
    emit singleMonitorChanged(single);
}

void Composer::setTrimDisplay(const TrimDisplay& display) {
    // Big Trim display (p.648): both monitors show the transition, the
    // outgoing frames on the left and the incoming on the right, with their
    // frame counters and the Trim buttons; the position bars and button rows
    // give way.
    // Small Trim display keeps the Source monitor with its controls and puts the two trim pictures in
    // place of the Record monitor (p.648); single-monitor view has no Source monitor to keep, so it
    // stays with Big Trim (ours).
    if (!display.on) trimPlaySide_ = model::TrimSide::B;
    trim_ = display;
    const bool small = display.on && display.small && !single_;
    trim_.small = small;
    // "The counter is purple when the side is active" (p.651).
    auto paintCounter = [&](QToolButton* counter, bool a) {
        counter->setText(display.on ? QString::number(a ? display.aFrames : display.bFrames) : QString());
        QPalette pal = counter->palette();
        pal.setColor(QPalette::ButtonText, (a ? display.aActive : display.bActive) ? QColor(kTrimActive) : QColor(kInfoText));
        counter->setPalette(pal);
        counter->setAutoFillBackground(true);
    };
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        Column& c = column(m);
        const bool trimHere = display.on && (!small || m == MonitorId::Record);
        c.bar->setVisible(!trimHere);
        c.controls->setVisible(!trimHere && display_.buttonRows > 0);
        c.trimCounter->setVisible(trimHere && !small);
        c.picture->setVisible(!(small && m == MonitorId::Record));
        paintCounter(c.trimCounter, m == MonitorId::Source);
    }
    smallTrim_->setVisible(small);
    applyRows();
    for (int i = 0; i < smallCounters_.size(); ++i) paintCounter(smallCounters_.at(i), i == 0);
    column(MonitorId::Source).widget->setVisible(display.on || !single_);
    trimBar_->setVisible(display.on);
    refresh();
}

void Composer::setDragDisplay(const DragDisplay& display) {
    drag_ = display;
    const bool four = display.on && display.fourFrame;
    dragCounter_->setVisible(four);
    dragCounter_->setText(four ? QString::number(display.offset) : QString());
    refresh();
}

QWidget* Composer::dragCounter() const {
    return dragCounter_;
}

void Composer::setNameFont(const QString& family, int pointSize) {
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        QToolButton* b = column(m).clipName;
        QFont f = column(m).tracking->font(); // the information row's own font
        if (!family.isEmpty()) f.setFamily(family);
        if (pointSize > 0) f.setPointSize(pointSize);
        b->setFont(f);
    }
}

QFont Composer::nameFont() const {
    return column(MonitorId::Source).clipName->font();
}

QToolButton* Composer::trimCounter(MonitorId monitor) const {
    return column(monitor).trimCounter;
}

void Composer::setTracking(MonitorId monitor, model::TrackingFormat format) {
    (monitor == MonitorId::Source ? sourceTracking_ : recordTracking_) = format;
    refresh();
}

model::TrackingFormat Composer::tracking(MonitorId monitor) const {
    return monitor == MonitorId::Source ? sourceTracking_ : recordTracking_;
}

QString Composer::trackingText(MonitorId monitor) const {
    const model::TrackingFormat f = tracking(monitor);
    const QString value = state(monitor).tracking(f);
    if (value.isEmpty()) return {};
    const QString prefix = trackingPrefix(f);
    return prefix.isEmpty() ? value : prefix + QStringLiteral("  ") + value;
}

QString Composer::centerDuration() const {
    // The IN to OUT duration of the active monitor.
    const model::MonitorState& s = state(active_);
    return s.isEmpty() ? QString() : s.durationText(s.inOutDuration());
}

void Composer::setSourceRecordColors(bool on) {
    colors_ = on;
    refresh();
}

void Composer::setGanged(bool ganged) {
    ganged_ = ganged;
    refresh();
}

// The position is clamped by MonitorState::setPosition. A ganged monitor moves by the same number of
// frames the first one actually moved, not to the same frame.
void Composer::moveTo(MonitorId monitor, std::int64_t frame) {
    model::MonitorState& s = state(monitor);
    const std::int64_t before = s.position();
    s.setPosition(frame);
    // Ganged monitors move together (p.495).
    if (ganged_) {
        model::MonitorState& other = state(monitor == MonitorId::Source ? MonitorId::Record : MonitorId::Source);
        other.step(s.position() - before);
    }
    refresh();
}

// Starts or changes playback of the active monitor at `speed` times normal (negative: reverse).
// Speed 0, or an empty monitor, stops instead. The stats are reset only when play starts from a stop;
// playStart_ and clock_ restart at every speed change, which is what the play clock counts from.
void Composer::play(int speed) {
    const model::MonitorState& s = state(active_);
    if (s.isEmpty() || speed == 0) {
        stop();
        return;
    }
    if (speed_ == 0) stats_ = {};
    if (loop_ && speed != 1) {
        stopAt_.reset();
        endLoop();
    }
    speed_ = speed;
    slow_ = false;
    fraction_ = 0;
    dynamicDirection_ = 0;
    carry_ = 0;
    playStart_ = s.position();
    clock_.restart();
    timer_->start();
    refresh();
    emit playStarted(active_, speed);
}

void Composer::setDynamicPlay(int acceleration, int slowStartPresses) {
    dynamicAcceleration_ = std::clamp(acceleration, 1, 12);
    dynamicSlowStart_ = std::clamp(slowStartPresses, 0, 12);
}

void Composer::playDynamic(int direction, bool slowStart) {
    const model::MonitorState& s = state(active_);
    if (s.isEmpty() || direction == 0) return;
    const int dir = direction > 0 ? 1 : -1;
    int steps = 0;
    if (dynamicDirection_ == dir && isPlaying()) {
        // "A total of 12 steps will double the play speed" (p.402); ours: no faster than 8x (36 steps).
        steps = std::min(dynamicSteps_ + dynamicAcceleration_, 36);
    } else if (slowStart) {
        steps = -dynamicSlowStart_ * dynamicAcceleration_;
    }
    const double multiple = std::pow(2.0, steps / 12.0);
    const double whole = std::round(multiple);
    if (std::abs(multiple - whole) < 1e-9) {
        play(dir * static_cast<int>(whole)); // with sound, as J and L
    } else {
        if (speed_ == 0) stats_ = {};
        if (loop_) {
            stopAt_.reset();
            endLoop();
        }
        speed_ = dir;
        slow_ = false;
        fraction_ = multiple;
        carry_ = 0;
        playStart_ = s.position();
        clock_.restart();
        timer_->start();
        refresh();
        emit playStarted(active_, 0); // no sound between whole speeds
    }
    dynamicDirection_ = dir;
    dynamicSteps_ = steps;
}

double Composer::rate() const {
    if (speed_ == 0) return 0;
    if (fraction_ > 0) return speed_ * fraction_;
    if (slow_) {
        const model::MonitorState& s = state(active_);
        const int nominal = s.isEmpty() ? 25 : TimecodeFormat::forRate(s.current()->item.editRate, false).nominalFps;
        return speed_ * static_cast<double>(slowFps(nominal)) / std::max(nominal, 1);
    }
    return speed_;
}

// Slow play runs on the wall clock, without sound: playStarted() says speed 0 so none is started.
void Composer::playSlow(int direction) {
    const model::MonitorState& s = state(active_);
    if (s.isEmpty() || direction == 0) {
        stop();
        return;
    }
    if (speed_ == 0) stats_ = {};
    if (loop_) {
        stopAt_.reset();
        endLoop();
    }
    speed_ = direction > 0 ? 1 : -1;
    slow_ = true;
    fraction_ = 0;
    dynamicDirection_ = 0;
    carry_ = 0;
    playStart_ = s.position();
    clock_.restart();
    timer_->start();
    refresh();
    emit playStarted(active_, 0);
}

int Composer::slowFps(int nominalFps) {
    if (nominalFps <= 0) return 6;
    if (nominalFps % 30 == 0) return 8 * nominalFps / 30;
    if (nominalFps % 25 == 0) return 6 * nominalFps / 25;
    if (nominalFps % 24 == 0) return 6 * nominalFps / 24;
    return std::max(1, (nominalFps + 2) / 4);
}

Composer::PlayDisplay Composer::playDisplay() const {
    PlayDisplay d;
    if (speed_ == 0) return d;
    const model::MonitorState& s = state(active_);
    const int nominal = s.isEmpty() ? 25 : TimecodeFormat::forRate(s.current()->item.editRate, false).nominalFps;
    d.fps = slow_          ? slowFps(nominal)
            : fraction_ > 0 ? static_cast<int>(std::lround(nominal * fraction_))
                            : nominal * std::abs(speed_);
    d.kind = speed_ < 0                               ? PlayDisplay::Kind::Reverse
             : speed_ == 1 && !slow_ && fraction_ == 0 ? PlayDisplay::Kind::Normal
                                      : PlayDisplay::Kind::Forward;
    return d;
}

// Stops playback and forgets a Play In to Out end point; emits playStopped() only if it was playing.
void Composer::stop() {
    endLoop();
    if (!startingLoopClip_) endClipLoop();
    if (speed_ == 0 && !timer_->isActive()) return;
    stopAt_.reset();
    speed_ = 0;
    slow_ = false;
    fraction_ = 0;
    dynamicDirection_ = 0;
    timer_->stop();
    refresh();
    emit playStopped();
}

void Composer::playLoop(MonitorId monitor, std::int64_t start, std::int64_t end) {
    const model::MonitorState& s = state(monitor);
    if (s.isEmpty() || end < start) return;
    stop();
    setActiveMonitor(monitor);
    moveTo(monitor, start);
    stopAt_ = end;
    play(1);
    loop_ = start;
    emit loopChanged(true);
}

void Composer::playLoop(MonitorId monitor, LoopResolver range, int pauseMs) {
    const std::optional<LoopRange> first = range ? range() : std::nullopt;
    if (!first) return;
    playLoop(monitor, first->start, first->end);
    if (!loop_) return;
    loopRange_ = std::move(range);
    loopPauseMs_ = std::max(0, pauseMs);
}

bool Composer::isLoopPaused() const {
    return loopPause_ && loopPause_->isActive();
}

void Composer::endLoop() {
    if (!loop_) return;
    loop_.reset();
    loopRange_ = {};
    loopPauseMs_ = 0;
    if (loopPause_) loopPause_->stop();
    emit loopChanged(false);
}

void Composer::playClipLoop(const QList<LoopClip>& clips, bool whole) {
    stop();
    if (clips.isEmpty()) return;
    clipLoop_ = clips;
    clipLoopWhole_ = whole;
    qApp->installEventFilter(this);
    startLoopClip(0);
}

void Composer::stepClipLoop(int delta) {
    if (clipLoop_.isEmpty()) return;
    const auto n = static_cast<int>(clipLoop_.size());
    startLoopClip(((clipLoopIndex_ + delta) % n + n) % n);
}

// Loads the clip into the Source monitor and plays its IN to OUT (or all of it); reachedEnd() goes on
// to the next.
void Composer::startLoopClip(int index) {
    clipLoopIndex_ = index;
    const LoopClip& c = clipLoop_.at(index);
    startingLoopClip_ = true;
    load(MonitorId::Source, c.binId, c.item);
    model::MonitorState& s = state(MonitorId::Source);
    const std::int64_t last = std::max<std::int64_t>(s.duration() - 1, 0);
    const std::int64_t from = clipLoopWhole_ ? 0 : s.in().value_or(0);
    const std::int64_t to = clipLoopWhole_ ? last : s.out().value_or(last);
    moveTo(MonitorId::Source, from);
    stopAt_ = to;
    play(1);
    startingLoopClip_ = false;
}

void Composer::endClipLoop() {
    if (clipLoop_.isEmpty()) return;
    clipLoop_.clear();
    qApp->removeEventFilter(this);
}

bool Composer::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress && !clipLoop_.isEmpty()) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            stepClipLoop(key->key() == Qt::Key_Backtab || (key->modifiers() & Qt::ShiftModifier) ? -1 : 1);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// The next pass starts like play(1) from the loop's start, so the sound restarts there. In a clip
// loop the next clip starts.
void Composer::reachedEnd() {
    if (!clipLoop_.isEmpty()) {
        startLoopClip((clipLoopIndex_ + 1) % static_cast<int>(clipLoop_.size()));
        return;
    }
    if (!loop_) {
        stop();
        return;
    }
    // Trim mode's Play Loop: the Intermission holds the last frame before the next pass (p.1223).
    if (loopPauseMs_ > 0) {
        if (!loopPause_) {
            loopPause_ = new QTimer(this);
            loopPause_->setSingleShot(true);
            connect(loopPause_, &QTimer::timeout, this, [this] {
                if (!loop_) return;
                timer_->start();
                nextLoopPass();
            });
        }
        timer_->stop();
        loopPause_->start(loopPauseMs_);
        return;
    }
    nextLoopPass();
}

// Starts the next pass of a loop from its (re-resolved) start.
void Composer::nextLoopPass() {
    if (loopRange_) {
        if (const std::optional<LoopRange> r = loopRange_()) {
            loop_ = r->start;
            stopAt_ = r->end;
        }
    }
    moveTo(active_, *loop_);
    carry_ = 0;
    playStart_ = *loop_;
    clock_.restart();
    emit playStarted(active_, speed_);
}

// One playback step, run by timer_. Moves the active monitor to where the sound (or the wall clock)
// says it should be, stops at the end (or the OUT of Play In to Out, or the start when playing
// backward) and counts shown and dropped frames.
void Composer::tick() {
    const qint64 ms = clock_.restart();
    const std::int64_t before = state(active_).position();
    const int speed = speed_;
    // Up to three times normal speed the sound sets the pace (A/V sync,
    // smooth audio scrub p.675); faster, or with no sound, the wall clock
    // does.
    std::optional<double> played;
    if (!slow_ && fraction_ == 0 && std::abs(speed_) <= 3 && playClock_) played = playClock_();
    // The play clock gives seconds of program played since playStart_ (negative going backward). The
    // picture only ever moves in the direction of play, so a clock that lags a tick holds the frame.
    if (played) {
        model::MonitorState& s = state(active_);
        const auto target = playStart_ + static_cast<std::int64_t>(*played * framesPerSecond(s));
        const std::int64_t end = stopAt_.value_or(s.lastPosition());
        if (speed_ > 0 && target >= end) {
            moveTo(active_, end);
            reachedEnd();
        } else if (speed_ < 0 && target <= 0) {
            moveTo(active_, 0);
            stop();
        } else if ((speed_ > 0 && target > s.position()) || (speed_ < 0 && target < s.position())) {
            moveTo(active_, target);
        }
    } else {
        advance(ms);
    }
    // Frames shown and skipped: a tick moves |speed| frames on time; more
    // means the picture fell behind (the real-time gate, ROADMAP 2.5).
    const std::int64_t moved = std::abs(state(active_).position() - before);
    if (moved > 0 && speed != 0) {
        ++stats_.shown;
        stats_.dropped += std::max<std::int64_t>(0, moved - std::abs(speed));
    }
}

void Composer::setVideoQuality(VideoQuality quality) {
    if (quality_ == quality) return;
    quality_ = quality;
    refresh();
    emit videoQualityChanged(quality);
}

void Composer::setHighQualityScaling(bool on) {
    smoothScaling_ = on;
    refresh();
}

double Composer::qualityScale() const {
    switch (quality_) {
    case VideoQuality::Full: return 1.0;
    case VideoQuality::Draft: return 0.5;
    case VideoQuality::Best: return 0.25;
    }
    return 1.0;
}

// Wall-clock playback: `ms` milliseconds at the current speed, keeping the fraction of a frame in
// carry_ for the next call. Reaching the end (or the Play In to Out end) or the start stops play.
void Composer::advance(qint64 ms) {
    if (speed_ == 0) return;
    model::MonitorState& s = state(active_);
    const double rate = slow_            ? static_cast<double>(playDisplay().fps) * (speed_ > 0 ? 1.0 : -1.0)
                        : fraction_ > 0 ? framesPerSecond(s) * fraction_ * speed_
                                        : framesPerSecond(s) * speed_;
    carry_ += static_cast<double>(ms) * rate / 1000.0;
    const auto frames = static_cast<std::int64_t>(carry_);
    carry_ -= static_cast<double>(frames);
    if (frames == 0) return;
    const std::int64_t target = s.position() + frames;
    const std::int64_t end = stopAt_.value_or(s.lastPosition());
    if (target >= end || target <= 0) {
        moveTo(active_, std::clamp<std::int64_t>(target, 0, end));
        if (target >= end) {
            reachedEnd();
        } else {
            stop();
        }
        return;
    }
    moveTo(active_, target);
}

// The commands perform() carries out itself. MainWindow::installPanelHandlers routes each of them to
// perform(); every other button or key leaves as commandRequested().
bool Composer::handles(const QString& id) {
    static const QStringList ids{
        QStringLiteral("play.play"),
        QStringLiteral("play.play_reverse"),
        QStringLiteral("play.pause"),
        QStringLiteral("play.play_forward"),
        QStringLiteral("play.dynamic_play_forward"),
        QStringLiteral("play.dynamic_play_reverse"),
        QStringLiteral("play.play_in_to_out"),
        QStringLiteral("move.step_backward_10_frames"),
        QStringLiteral("move.step_forward_10_frames"),
        QStringLiteral("move.step_backward_one_frame"),
        QStringLiteral("move.step_forward_one_frame"),
        QStringLiteral("move.go_to_start"),
        QStringLiteral("move.go_to_end"),
        QStringLiteral("move.go_to_previous_event"),
        QStringLiteral("move.go_to_next_event"),
        QStringLiteral("edit.mark_in"),
        QStringLiteral("edit.mark_out"),
        QStringLiteral("edit.mark_clip"),
        QStringLiteral("edit.clear_in_mark"),
        QStringLiteral("edit.clear_out_mark"),
        QStringLiteral("edit.clear_both_marks"),
        QStringLiteral("edit.go_to_in_point"),
        QStringLiteral("edit.go_to_out_point"),
        QStringLiteral("other.gang"),
        QStringLiteral("composer.show_single_dual_monitor"),
    };
    return ids.contains(id);
}

// Runs one of the commands in handles() on the active monitor, then refreshes and emits performed().
// The play commands change speed; the move and mark commands stop playback first.
bool Composer::perform(const QString& id) {
    if (!handles(id)) return false;
    const MonitorId m = active_;
    model::MonitorState& s = state(m);
    const bool progressive = !s.isEmpty() && TimecodeFormat::forRate(s.current()->item.editRate, false).nominalFps == 24;
    // 10 frames, or 8 in 24-frame formats (p.398).
    const std::int64_t big = progressive ? 8 : 10;
    auto nextSpeed = [this](int sign) {
        // Pressing again plays faster: 1, 2, 3, 5, 8 times (p.400).
        if (speed_ == 0 || (speed_ > 0) != (sign > 0)) return sign;
        const int current = std::abs(speed_);
        for (int sp : kSpeeds) {
            if (sp > current) return sign * sp;
        }
        return sign * kSpeeds[std::size(kSpeeds) - 1];
    };
    if (id == QLatin1String("composer.show_single_dual_monitor")) {
        setSingleMonitor(!single_);
    } else if (id == QLatin1String("other.gang")) {
        if (m == MonitorId::Source || single_) setGanged(!ganged_);
    } else if (id == QLatin1String("play.play")) {
        // Start and stop playback (p.400).
        if (isPlaying()) {
            stop();
        } else {
            play(1);
        }
    } else if (id == QLatin1String("play.play_forward")) {
        play(nextSpeed(1));
    } else if (id == QLatin1String("play.play_reverse")) {
        play(nextSpeed(-1));
    } else if (id == QLatin1String("play.dynamic_play_forward") || id == QLatin1String("play.dynamic_play_reverse")) {
        // "If you press Alt ... while you tap the Dynamic Play keys, start play starts at a lower
        // speed" (p.402).
        playDynamic(id.endsWith(QLatin1String("forward")) ? 1 : -1, QApplication::keyboardModifiers() & Qt::AltModifier);
    } else if (id == QLatin1String("play.pause")) {
        stop();
    } else if (id == QLatin1String("play.play_in_to_out")) {
        if (s.isEmpty()) return true;
        moveTo(m, s.markedStart());
        stopAt_ = s.markedEnd();
        play(1);
    } else {
        stop();
        // Moves and marks; positions are clamped by MonitorState.
        if (id == QLatin1String("move.step_backward_one_frame")) {
            moveTo(m, s.position() - 1);
        } else if (id == QLatin1String("move.step_forward_one_frame")) {
            moveTo(m, s.position() + 1);
        } else if (id == QLatin1String("move.step_backward_10_frames")) {
            moveTo(m, s.position() - big);
        } else if (id == QLatin1String("move.step_forward_10_frames")) {
            moveTo(m, s.position() + big);
        } else if (id == QLatin1String("move.go_to_start")) {
            moveTo(m, 0);
        } else if (id == QLatin1String("move.go_to_end")) {
            moveTo(m, s.lastPosition());
        } else if (id == QLatin1String("move.go_to_previous_event")) {
            // The previous edit on the selected tracks (p.398); a clip's
            // only events are its start and end.
            std::int64_t to = 0;
            for (std::int64_t e : stops(m)) {
                if (e < s.position()) to = e;
            }
            moveTo(m, to);
        } else if (id == QLatin1String("move.go_to_next_event")) {
            // The first stop after the position; the end when there is none.
            std::int64_t to = s.lastPosition();
            for (std::int64_t e : stops(m)) {
                if (e > s.position()) {
                    to = std::min(e, to);
                    break;
                }
            }
            moveTo(m, to);
        } else if (id == QLatin1String("edit.mark_in")) {
            s.markIn(); // "mark an IN point and stop playback" (p.405)
        } else if (id == QLatin1String("edit.mark_out")) {
            s.markOut();
        } else if (id == QLatin1String("edit.mark_clip")) {
            // The entire clip, or in a sequence the segment under the
            // position on the selected tracks (p.407).
            const std::int64_t at = s.position();
            std::int64_t from = 0;
            std::int64_t to = std::max<std::int64_t>(s.duration() - 1, 0);
            for (std::int64_t e : events(m)) {
                if (e <= at) {
                    from = e;
                } else {
                    to = std::min(to, e - 1);
                    break;
                }
            }
            // A MarkResolver's range, when it gives one, wins over the segment under the position.
            if (const auto range = marks_ ? marks_(m) : std::nullopt) {
                from = range->first;
                to = range->second;
            }
            // Mark IN and OUT through the position, then put the position back.
            if (!s.isEmpty() && from <= to) {
                s.setPosition(from);
                s.markIn();
                s.setPosition(to);
                s.markOut();
                s.setPosition(at);
            }
        } else if (id == QLatin1String("edit.clear_in_mark")) {
            s.clearIn();
        } else if (id == QLatin1String("edit.clear_out_mark")) {
            s.clearOut();
        } else if (id == QLatin1String("edit.clear_both_marks")) {
            s.clearBoth();
        } else if (id == QLatin1String("edit.go_to_in_point")) {
            if (s.in()) moveTo(m, *s.in());
        } else if (id == QLatin1String("edit.go_to_out_point")) {
            if (s.out()) moveTo(m, *s.out());
        }
    }
    refresh();
    emit performed(id);
    return true;
}

// Keys reach the Composer when it has focus: K is tracked for K+J / K+L, and every other key is
// looked up in the Keyboard settings (KeyResolver) and run as a command.
void Composer::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat() && event->key() == Qt::Key_K) return;
    if (event->key() == Qt::Key_K) kHeld_ = true;
    // Holding K and tapping J or L steps one frame; holding both plays slowly (p.401): the key's
    // repeat, which starts once it has been held a moment, starts slow play.
    if (kHeld_ && (event->key() == Qt::Key_J || event->key() == Qt::Key_L)) {
        const int direction = event->key() == Qt::Key_J ? -1 : 1;
        if (!event->isAutoRepeat()) {
            if (kStep_ && kStep_(direction)) return;
            perform(direction < 0 ? QStringLiteral("move.step_backward_one_frame") : QStringLiteral("move.step_forward_one_frame"));
        } else if (!slow_ || speed_ != direction) {
            playSlow(direction);
        }
        return;
    }
    if (keys_) {
        const QString id = keys_(QKeySequence(event->keyCombination()));
        if (!id.isEmpty()) {
            // Commands that are not the Composer's (Splice-in, ...) go to
            // whoever handles them.
            if (!perform(id)) emit commandRequested(id);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void Composer::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_K && !event->isAutoRepeat()) kHeld_ = false;
    // Letting go of L or J (or K) ends slow play.
    if (slow_ && !event->isAutoRepeat() &&
        (event->key() == Qt::Key_K || event->key() == Qt::Key_J || event->key() == Qt::Key_L)) {
        stop();
    }
    QWidget::keyReleaseEvent(event);
}

// Rebuilt each time the Clip Name menu is about to show, so it reads the Alt key held at that moment.
QMenu* Composer::clipNameMenu(MonitorId monitor) {
    // Loaded clips, alphabetically (load order with Alt), then Clear Monitor
    // and Clear Menu (p.394-395).
    QMenu* menu = column(monitor).clipMenu;
    menu->clear();
    const bool loadOrder = QApplication::keyboardModifiers() & Qt::AltModifier;
    const QStringList names = state(monitor).menuNames(loadOrder);
    for (int i = 0; i < names.size(); ++i) {
        connect(menu->addAction(names.at(i)), &QAction::triggered, this, [this, monitor, i, loadOrder] {
            stop();
            state(monitor).select(i, loadOrder);
            setActiveMonitor(monitor);
            refresh();
        });
    }
    if (!names.isEmpty()) menu->addSeparator();
    // "Click above the Source monitor, and select Load Filler from the Clip Name menu" (p.462).
    if (monitor == MonitorId::Source) {
        connect(menu->addAction(tr("Load Filler")), &QAction::triggered, this,
                [this] { emit commandRequested(QStringLiteral("composer.load_filler")); });
    }
    QAction* clearMonitor = menu->addAction(tr("Clear Monitor"));
    QAction* clearMenu = menu->addAction(tr("Clear Menu"));
    clearMonitor->setEnabled(!state(monitor).isEmpty());
    clearMenu->setEnabled(state(monitor).loadedCount() > 0);
    connect(clearMonitor, &QAction::triggered, this, [this, monitor] {
        stop();
        state(monitor).clearMonitor();
        refresh();
    });
    connect(clearMenu, &QAction::triggered, this, [this, monitor] {
        state(monitor).clearMenu();
        refresh();
    });
    return menu;
}

// A new menu each time, deleted when it closes; the caller shows it with exec().
QMenu* Composer::contextMenu() {
    // Right-click in a monitor (p.381).
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* single = menu->addAction(tr("Show Single/Dual Monitor"));
    connect(single, &QAction::triggered, this, [this] { perform(QStringLiteral("composer.show_single_dual_monitor")); });
    // The Markers window (p.420).
    QAction* markers = menu->addAction(tr("Markers Window"));
    connect(markers, &QAction::triggered, this, [this] { emit commandRequested(QStringLiteral("tools.markers")); });
    // The Clipboard into the Source monitor (p.480).
    QAction* clipboard = menu->addAction(tr("Clipboard Contents"));
    connect(clipboard, &QAction::triggered, this, [this] { emit commandRequested(QStringLiteral("edit.clipboard_contents")); });
    return menu;
}

QToolButton* Composer::button(MonitorId monitor, const QString& commandId) const {
    return column(monitor).buttons.value(commandId);
}

QToolButton* Composer::clipNameButton(MonitorId monitor) const {
    return column(monitor).clipName;
}

QToolButton* Composer::trackingButton(MonitorId monitor) const {
    return column(monitor).tracking;
}

PositionBar* Composer::positionBar(MonitorId monitor) const {
    return column(monitor).bar;
}

MonitorPicture* Composer::picture(MonitorId monitor) const {
    return column(monitor).picture;
}

// Brings every widget up to date with the monitor states (clip names, tracking, Gang button, picture,
// bars, center duration) and emits changed(). Most setters end here, so changed() fires often:
// listeners (the Timeline, digital audio scrub in MainWindow) must be cheap.
void Composer::refresh() {
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        if (!columns_[m == MonitorId::Source ? 0 : 1]) continue;
        Column& c = column(m);
        const model::LoadedClip* clip = state(m).current();
        c.clipName->setText(clip ? clip->item.name + QStringLiteral("  ") : QString());
        c.tracking->setText(trackingText(m));
        c.tracking2->setText(secondTrackingText(m));
        if (c.gangIcon) c.gangIcon->setVisible(ganged_);
        if (QToolButton* gang = c.buttons.value(QStringLiteral("other.gang"))) gang->setChecked(ganged_);
        // The playing monitor's Play button shows how it plays.
        QToolButton* playButton = c.buttons.value(QStringLiteral("play.play"));
        if (playButton && m == active_ && isPlaying() && playButton->isVisible()) {
            c.playState->place(QRect(playButton->mapTo(c.controls, QPoint(0, 0)), playButton->size()), playDisplay());
        } else {
            c.playState->hide();
        }
        c.picture->update();
        c.bar->update();
        if (smallTrim_) smallTrim_->update();
    }
    if (center_) center_->setText(centerDuration());
    emit changed();
}

// Without a stop resolver, Go to Previous/Next Event stop at the events.
QList<std::int64_t> Composer::stops(MonitorId monitor) const {
    return stops_ ? stops_(monitor) : events(monitor);
}

// Without an event resolver, a clip's only events are its start and its end (duration).
QList<std::int64_t> Composer::events(MonitorId monitor) const {
    if (events_) return events_(monitor);
    const model::MonitorState& s = state(monitor);
    return {0, s.duration()};
}

} // namespace lumenis::ui
