#include "lumenis/ui/composer.h"

#include "lumenis/core/timecode.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QHash>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {

constexpr int kInfoHeight = 20;      // the information row above a monitor
constexpr int kBarHeight = 14;       // position bar
constexpr int kButtonWidth = 24;
constexpr int kTickInterval = 20;    // ms between playback steps
constexpr QRgb kInfoText = 0xff4cd98c;   // tracking and clip names, green as in the pictures (p.380)
constexpr QRgb kIndicator = 0xff33c3ff;  // the blue position indicator (p.396)
constexpr QRgb kSourceBar = 0xff2f8a6c;  // Source/Record colours (p.383)
constexpr QRgb kRecordBar = 0xff2b5576;
constexpr QRgb kPlainBar = 0xff4a4a4a;
constexpr QRgb kMarkedSpan = 0x60ffffff;
constexpr QRgb kSplice = 0xffe6c619;     // Splice-in (yellow) and Overwrite (red)
constexpr QRgb kOverwrite = 0xffd23a32;
constexpr QRgb kMarker = 0xffd23a32;     // red Add Marker
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
         {"edit.mark_out", Glyph::MarkOut},
         {"play.play", Glyph::Play},
         {"edit.mark_in", Glyph::MarkIn},
         {"edit.mark_clip", Glyph::MarkClip},
         {"more.add_marker", Glyph::AddMarker},
         {"edit.splice_in", Glyph::SpliceIn},
         {"edit.overwrite", Glyph::Overwrite}},
        {{"other.gang", Glyph::Gang},
         {"move.go_to_previous_event", Glyph::PrevEvent},
         {"move.go_to_next_event", Glyph::NextEvent},
         {"edit.go_to_out_point", Glyph::GoToOut},
         {"play.play_in_to_out", Glyph::PlayInToOut},
         {"edit.go_to_in_point", Glyph::GoToIn},
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
         {"edit.mark_out", Glyph::MarkOut},
         {"play.play", Glyph::Play},
         {"edit.mark_in", Glyph::MarkIn},
         {"", Glyph::Blank},
         {"more.add_marker", Glyph::AddMarker},
         {"", Glyph::Blank},
         {"", Glyph::Blank}},
        {{"other.gang", Glyph::Gang},
         {"", Glyph::Blank},
         {"move.go_to_previous_event", Glyph::PrevEvent},
         {"move.go_to_next_event", Glyph::NextEvent},
         {"edit.go_to_out_point", Glyph::GoToOut},
         {"play.play_in_to_out", Glyph::PlayInToOut},
         {"edit.go_to_in_point", Glyph::GoToIn},
         {"", Glyph::Blank},
         {"", Glyph::Blank},
         {"", Glyph::Blank},
         {"", Glyph::Blank}},
    };
    return monitor == MonitorId::Source ? source : record;
}

// Commands that need the Timeline (1.4b) or markers, shown but not working yet.
bool later(const QString& id) {
    return id == QLatin1String("edit.splice_in") || id == QLatin1String("edit.overwrite") ||
           id == QLatin1String("more.add_marker");
}

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
    case Glyph::MarkIn: bracket(13, true); break;
    case Glyph::MarkOut: bracket(19, false); break;
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
        arrow(5, 13);
        bracket(18, true);
        break;
    case Glyph::GoToOut:
        bracket(12, false);
        arrow(27, 17);
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

double framesPerSecond(const model::MonitorState& s) {
    const model::LoadedClip* c = s.current();
    if (!c) return 25.0;
    return static_cast<double>(c->item.editRate.num()) / static_cast<double>(c->item.editRate.den());
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

struct Composer::Column {
    QWidget* widget = nullptr;
    QToolButton* clipName = nullptr;
    QToolButton* tracking = nullptr;
    MonitorPicture* picture = nullptr;
    PositionBar* bar = nullptr;
    QMenu* clipMenu = nullptr;
    QMenu* trackingMenu = nullptr;
    QHash<QString, QToolButton*> buttons;
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

void PositionBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const model::MonitorState& s = composer_.state(monitor_);
    QColor base(kPlainBar);
    if (composer_.sourceRecordColors()) base = QColor(monitor_ == MonitorId::Source ? kSourceBar : kRecordBar);
    p.fillRect(rect(), base);
    p.setPen(base.darker(150));
    for (int i = 1; i < 24; ++i) {
        const int x = width() * i / 24;
        p.drawLine(x, height() - 4, x, height() - 1);
    }
    if (s.isEmpty()) return;
    // Marked material, then the indicator.
    if (s.in() || s.out()) p.fillRect(QRect(QPoint(xFor(s.markedStart()), 2), QPoint(xFor(s.markedEnd()), height() - 3)), QColor::fromRgba(kMarkedSpan));
    p.fillRect(QRect(xFor(s.position()) - 1, 0, 2, height()), QColor(kIndicator));
}

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

void MonitorPicture::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 18, 18));
    const QRect pic = pictureRect();
    p.fillRect(pic, Qt::black);
    const model::MonitorState& s = composer_.state(monitor_);
    if (!s.isEmpty()) {
        // The sawtooth marks on the IN and OUT frames (p.405-406).
        p.setPen(QPen(QColor(230, 230, 230), 2));
        auto saw = [&](int x, int dir) {
            QPolygon teeth;
            for (int i = 0; i <= 8; ++i) teeth << QPoint(x + ((i % 2) ? dir * 6 : 0), pic.top() + 8 + i * 8);
            p.drawPolyline(teeth);
        };
        if (s.in() && *s.in() == s.position()) saw(pic.left() + 6, 1);
        if (s.out() && *s.out() == s.position()) saw(pic.right() - 6, -1);
    }
    if (composer_.activeMonitor() == monitor_ && !composer_.isSingleMonitor()) {
        p.setPen(QPen(palette().color(QPalette::Highlight), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void MonitorPicture::mousePressEvent(QMouseEvent*) {
    composer_.setActiveMonitor(monitor_);
    composer_.setFocus();
}

void MonitorPicture::contextMenuEvent(QContextMenuEvent* event) {
    composer_.setActiveMonitor(monitor_);
    composer_.contextMenu()->exec(event->globalPos());
}

void MonitorPicture::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat(binItemsMime())) event->acceptProposedAction();
}

void MonitorPicture::dropEvent(QDropEvent* event) {
    // Clips dragged from a bin (p.394): the bin first, then the items.
    QStringList lines = QString::fromUtf8(event->mimeData()->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2) return;
    const QString bin = lines.takeFirst();
    event->setDropAction(Qt::CopyAction);
    event->accept();
    composer_.loadFromBin(monitor_, bin, lines);
}

// ---------------------------------------------------------------- Composer

Composer::Composer(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("composer"));
    setFocusPolicy(Qt::StrongFocus);

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
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);
    layout->addWidget(sourceColumn, 1);
    layout->addWidget(recordColumn, 1);

    timer_ = new QTimer(this);
    timer_->setInterval(kTickInterval);
    connect(timer_, &QTimer::timeout, this, &Composer::tick);
    refresh();
}

Composer::~Composer() {
    delete columns_[0];
    delete columns_[1];
}

Composer::Column& Composer::column(MonitorId monitor) const {
    return *columns_[monitor == MonitorId::Source ? 0 : 1];
}

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

    c->picture = new MonitorPicture(*this, monitor, c->widget);
    c->bar = new PositionBar(*this, monitor, c->widget);

    auto* rows = new QVBoxLayout;
    rows->setSpacing(1);
    for (const QList<ButtonSpec>& row : buttonRows(monitor)) {
        auto* h = new QHBoxLayout;
        h->setSpacing(0);
        for (int i = 0; i < row.size(); ++i) {
            const ButtonSpec& spec = row.at(i);
            auto* b = new QToolButton(c->widget);
            b->setAutoRaise(true);
            b->setFixedSize(kButtonWidth, kToolButtonHeight);
            b->setIcon(glyph(spec.glyph, spec.glyph == Glyph::SpliceIn    ? QColor(kSplice)
                                         : spec.glyph == Glyph::Overwrite ? QColor(kOverwrite)
                                         : spec.glyph == Glyph::AddMarker ? QColor(kMarker)
                                                                          : QColor(0xc8, 0xc8, 0xc8)));
            b->setIconSize(QSize(16, 16));
            const QString id = QString::fromLatin1(spec.command);
            if (id.isEmpty()) {
                b->setObjectName(QStringLiteral("blankButton"));
                b->setEnabled(false);
            } else {
                b->setObjectName(id);
                c->buttons.insert(id, b);
                if (id == QLatin1String("other.gang")) b->setCheckable(true);
                b->setEnabled(!later(id) && !(id == QLatin1String("other.gang") && !source));
                connect(b, &QToolButton::clicked, this, [this, monitor, id] {
                    setActiveMonitor(monitor);
                    perform(id);
                });
            }
            if (i > 0) h->addStretch(1);
            h->addWidget(b);
        }
        rows->addLayout(h);
    }

    auto* v = new QVBoxLayout(c->widget);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(3);
    v->addLayout(info);
    v->addWidget(c->picture, 1);
    v->addWidget(c->bar);
    v->addLayout(rows);
    return c->widget;
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

void Composer::play(int speed) {
    const model::MonitorState& s = state(active_);
    if (s.isEmpty() || speed == 0) {
        stop();
        return;
    }
    speed_ = speed;
    carry_ = 0;
    clock_.restart();
    timer_->start();
    refresh();
}

void Composer::stop() {
    if (speed_ == 0 && !timer_->isActive()) return;
    stopAt_.reset();
    speed_ = 0;
    timer_->stop();
    refresh();
}

void Composer::tick() {
    advance(clock_.restart());
}

void Composer::advance(qint64 ms) {
    if (speed_ == 0) return;
    model::MonitorState& s = state(active_);
    carry_ += static_cast<double>(ms) * framesPerSecond(s) * speed_ / 1000.0;
    const auto frames = static_cast<std::int64_t>(carry_);
    carry_ -= static_cast<double>(frames);
    if (frames == 0) return;
    const std::int64_t target = s.position() + frames;
    const std::int64_t end = stopAt_.value_or(s.duration() - 1);
    if (target >= end || target <= 0) {
        moveTo(active_, std::clamp<std::int64_t>(target, 0, end));
        stop();
        return;
    }
    moveTo(active_, target);
}

bool Composer::handles(const QString& id) {
    static const QStringList ids{
        QStringLiteral("play.play"),
        QStringLiteral("play.play_reverse"),
        QStringLiteral("play.pause"),
        QStringLiteral("play.play_forward"),
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
    } else if (id == QLatin1String("play.pause")) {
        stop();
    } else if (id == QLatin1String("play.play_in_to_out")) {
        if (s.isEmpty()) return true;
        moveTo(m, s.markedStart());
        stopAt_ = s.markedEnd();
        play(1);
    } else {
        stop();
        if (id == QLatin1String("move.step_backward_one_frame")) {
            moveTo(m, s.position() - 1);
        } else if (id == QLatin1String("move.step_forward_one_frame")) {
            moveTo(m, s.position() + 1);
        } else if (id == QLatin1String("move.step_backward_10_frames")) {
            moveTo(m, s.position() - big);
        } else if (id == QLatin1String("move.step_forward_10_frames")) {
            moveTo(m, s.position() + big);
        } else if (id == QLatin1String("move.go_to_start") || id == QLatin1String("move.go_to_previous_event")) {
            // A clip has no transitions until it is in a sequence (1.4b).
            moveTo(m, 0);
        } else if (id == QLatin1String("move.go_to_end") || id == QLatin1String("move.go_to_next_event")) {
            moveTo(m, s.duration() - 1);
        } else if (id == QLatin1String("edit.mark_in")) {
            s.markIn(); // "mark an IN point and stop playback" (p.405)
        } else if (id == QLatin1String("edit.mark_out")) {
            s.markOut();
        } else if (id == QLatin1String("edit.mark_clip")) {
            // The entire clip (p.407).
            const std::int64_t at = s.position();
            s.goToStart();
            s.markIn();
            s.goToEnd();
            s.markOut();
            s.setPosition(at);
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
    emit changed();
    return true;
}

void Composer::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat() && event->key() == Qt::Key_K) return;
    if (event->key() == Qt::Key_K) kHeld_ = true;
    // Holding K and tapping J or L steps one frame (p.401).
    if (kHeld_ && (event->key() == Qt::Key_J || event->key() == Qt::Key_L)) {
        perform(event->key() == Qt::Key_J ? QStringLiteral("move.step_backward_one_frame")
                                          : QStringLiteral("move.step_forward_one_frame"));
        return;
    }
    if (keys_) {
        const QString id = keys_(QKeySequence(event->keyCombination()));
        if (!id.isEmpty() && perform(id)) return;
    }
    QWidget::keyPressEvent(event);
}

void Composer::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_K && !event->isAutoRepeat()) kHeld_ = false;
    QWidget::keyReleaseEvent(event);
}

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

QMenu* Composer::contextMenu() {
    // Right-click in a monitor (p.381).
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction* single = menu->addAction(tr("Show Single/Dual Monitor"));
    connect(single, &QAction::triggered, this, [this] { perform(QStringLiteral("composer.show_single_dual_monitor")); });
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

void Composer::refresh() {
    for (MonitorId m : {MonitorId::Source, MonitorId::Record}) {
        if (!columns_[m == MonitorId::Source ? 0 : 1]) continue;
        Column& c = column(m);
        const model::LoadedClip* clip = state(m).current();
        c.clipName->setText(clip ? clip->item.name + QStringLiteral("  ") : QString());
        c.tracking->setText(trackingText(m));
        if (QToolButton* gang = c.buttons.value(QStringLiteral("other.gang"))) gang->setChecked(ganged_);
        c.picture->update();
        c.bar->update();
    }
    if (center_) center_->setText(centerDuration());
}

} // namespace lumenis::ui
