#include "lumenis/ui/timeline.h"

#include "lumenis/media/audio_fx.h"
#include "lumenis/media/link.h"
#include "lumenis/model/audio_eq.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/effect_render.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/motion_effect.h"
#include "lumenis/model/nest.h"
#include "lumenis/media/surround.h"
#include "lumenis/model/multichannel.h"
#include "lumenis/model/surround_pan.h"
#include "lumenis/model/sync_dupes.h"
#include "lumenis/model/titler.h"
#include "lumenis/model/tracking.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/button_mapping.h"
#include "lumenis/ui/clip_color_dialog.h"
#include "lumenis/ui/marker_dialogs.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/sequence_map.h"
#include "lumenis/ui/theme.h"

#include <QActionGroup>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <limits>
#include <cmath>
#include <memory>

// The Timeline window (Editing Guide ch. 15, 19, 20) and its tracks area.
//
// - TimelineView (first half) paints the Track Selector panel, the ruler, the tracks with their
//   segments, effects, marks, markers, trim rollers and drag outlines, and maps mouse input to
//   Timeline calls: scrubbing, segment selection and drags, lassos, trim rollers, patching and
//   the Track Selector's buttons. Coordinates: x = headerWidth() + (frame - firstFrame) * scale,
//   with the scale in pixels per frame; rows stack below the ruler, offset by the vertical scroll.
// - Timeline (second half) builds the toolbars, keeps the editing state and carries out the
//   commands listed in Timeline::handles(). Every change to the record sequence follows one
//   pattern: read model::Sequence::fromItem() from the Record monitor's BinItem, change it with a
//   function from the model, storeIn() a copy of the item, then commit() it, which pushes an undo
//   step and hands the item to the bin (SequenceStore) and the Composer.
//
// The editing algorithms live in the model (src/model/include/lumenis/model/sequence.h: edits,
// trims, slips, segment moves, effects; marker.h: markers); the monitors and their marks in
// the Composer (composer.h, src/model/include/lumenis/model/monitor.h). MainWindow
// (main_window.cpp) creates the Timeline; the Effect Editor (effect_editor.cpp) and Audio Tools
// (audio_tools.cpp) call its effect and audio functions. Tests: tests/ui.
namespace lumenis::ui {

namespace {

// Sizes. The guide's pictures show the layout but not the sizes.
constexpr int kRulerHeight = 20;
constexpr int kVideoRow = 30;
constexpr int kAudioRow = 26;
constexpr int kAudioDataRow = 64; // ours: audio rows while Audio Data shows a graph or line
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
// The Track Control panel (p.602), right of the Track Selector: two lines of buttons.
constexpr int kControlWidth = 92;
constexpr int kControlButton = 20;  // Waveform, Clip Volume/Pan, Inactive, Solo, Mute
constexpr int kInsertButton = 13;   // inserts a-e
constexpr int kControlLine = 12;    // the height of one line of buttons
// Enlarge Track and Reduce Track (p.594): pixels a step (ours), and the lowest a row can be.
constexpr int kTrackStep = 8;
constexpr int kMinRow = 12;
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
// Show Source/Record colors in Timeline (p.383): selected source buttons green, record ones blue, as
// the Composer's position bars.
constexpr QRgb kSourceButtonOn = 0xff2f8a6c;
constexpr QRgb kRecordButtonOn = 0xff2b5576;
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
constexpr QRgb kDualRoller = 0xffe87fb8;     // dual-roller trims are pink (p.651)
constexpr QRgb kSyncRoller = 0xff9a9a9a;     // sync rollers are gray (p.656); the shade is ours

// The Record monitor's sequence, or an empty one.
model::Sequence recordSequenceOf(const Composer& composer) {
    const model::LoadedClip* rec = composer.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return {};
    return model::Sequence::fromItem(rec->item);
}

// Timeline Text Color (p.1219): white (the palette's BrightText), black, or "either white or black text
// on unselected clips, depending upon the clip's segment color" (ours: black on light colours). Black
// keeps BrightText's alpha, so text-free screenshots hide it too.
QColor clipTextColor(const QColor& fill, const QColor& bright, Timeline::Options::TextColor choice) {
    QColor black(Qt::black);
    black.setAlpha(bright.alpha());
    switch (choice) {
    case Timeline::Options::TextColor::White: return bright;
    case Timeline::Options::TextColor::Black: return black;
    case Timeline::Options::TextColor::Both: return fill.lightness() > 150 ? black : bright;
    }
    return bright;
}

// A row's height for a track name; anything that is not a known track gets the TC1 height.
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
    SegmentSplice, SegmentOverwrite, Lift, Extract, CopyClipboard, AddEdit, Replace, Top, Tail, MatchFrame,
    TrimMode, RippleTrim, OverwriteTrim, SourceRecordMode, EffectMode, Titler
};

// The Video Quality Menu button's picture for each quality (p.404): green
// for Full Quality, green and yellow for Draft Quality, yellow for Best
// Performance. The guide names the colours; the drawing is ours.
QIcon qualityIcon(Composer::VideoQuality quality) {
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor green(0x3c, 0xb0, 0x4a);
    const QColor yellow(0xe6, 0xc6, 0x19);
    const QRectF circle(6, 6, 20, 20);
    p.setPen(Qt::NoPen);
    p.setBrush(quality == Composer::VideoQuality::Best ? yellow : green);
    p.drawEllipse(circle);
    if (quality == Composer::VideoQuality::Draft) {
        p.setBrush(yellow);
        p.drawPie(circle, -90 * 16, 180 * 16);
    }
    p.end();
    pm.setDevicePixelRatio(2.0);
    return QIcon(pm);
}

// Draws one toolbar picture at 32 pixels square, in the glyph colour (or the tool's colour).
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
    case Picture::TrimMode:
    case Picture::RippleTrim:
    case Picture::OverwriteTrim: {
        // A roller: a pink pair for Trim mode, a yellow or red single one
        // for the trim tools (p.651).
        const QColor rc(which == Picture::TrimMode ? kDualRoller : which == Picture::RippleTrim ? kSpliceColor : kOverwriteColor);
        p.setPen(QPen(rc, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (which != Picture::RippleTrim && which != Picture::OverwriteTrim) {
            p.drawLine(QPointF(13, 6), QPointF(13, 26));
            p.drawLine(QPointF(13, 6), QPointF(7, 6));
            p.drawLine(QPointF(13, 26), QPointF(7, 26));
        }
        p.drawLine(QPointF(19, 6), QPointF(19, 26));
        p.drawLine(QPointF(19, 6), QPointF(25, 6));
        p.drawLine(QPointF(19, 26), QPointF(25, 26));
        break;
    }
    case Picture::SourceRecordMode:
        // Two monitors side by side (ours).
        p.drawRect(QRectF(4, 9, 11, 14));
        p.drawRect(QRectF(17, 9, 11, 14));
        break;
    case Picture::EffectMode: {
        // A monitor with an effect's purple square (ours).
        p.drawRect(QRectF(5, 7, 22, 18));
        p.fillRect(QRectF(11, 11, 10, 10), QColor(0x8e, 0x5c, 0xd6));
        break;
    }
    case Picture::Titler:
        // A T with a plus (ours).
        p.drawLine(QPointF(5, 7), QPointF(19, 7));
        p.drawLine(QPointF(12, 7), QPointF(12, 25));
        p.drawLine(QPointF(19, 19), QPointF(27, 19));
        p.drawLine(QPointF(23, 15), QPointF(23, 23));
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
    setAcceptDrops(true);
    // Clip Frames are decoded off the UI thread and drawn when ready.
    if (MediaFrames* frames = MediaFrames::instance()) {
        connect(frames, &MediaFrames::thumbnailReady, this, [this] {
            if (timeline_.display().clipFrames) update();
        });
    }
}

int TimelineView::rulerHeight() const {
    return kRulerHeight;
}

int TimelineView::headerWidth() const {
    const auto& d = timeline_.display();
    if (!d.trackPanel) return 0;
    return kHeaderWidth + (d.trackControlPanel ? kControlWidth : 0);
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
        r.height = timeline_.trackHeight(n);
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

// Clamps to 0 at the top and, at the bottom, to the offset that puts the last row's bottom
// edge at the view's bottom edge. contentHeight() does not depend on the offset, so the limit
// is contentHeight() - height(); syncScrollBars() computes the vertical range the same way.
void TimelineView::setTopOffset(int offset) {
    top_ = std::clamp(offset, 0, std::max(contentHeight() - height(), 0));
    update();
}

// x to frame and back: frame = firstFrame + (x - headerWidth) / scale.
std::int64_t TimelineView::frameAt(int x) const {
    const double scale = timeline_.scale();
    if (scale <= 0) return 0;
    return static_cast<std::int64_t>(std::floor(timeline_.firstFrame() + (x - headerWidth()) / scale));
}

double TimelineView::xFor(std::int64_t frame) const {
    return headerWidth() + (static_cast<double>(frame) - timeline_.firstFrame()) * timeline_.scale();
}

// The buttons are fixed columns of the header, inset 2 pixels in the row and at least 12 tall.
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

// Our layout of the panel in the picture on p.602: [Waveform][Clip Volume/Pan][Inactive][Solo] on
// top, [a][b][c][d][e][Mute] below, the two lines centred in the row.
QRect TimelineView::controlButton(const Row& row, int control) const {
    if (!timeline_.display().trackPanel || !timeline_.display().trackControlPanel || !row.exists ||
        !row.track.startsWith(QLatin1Char('A')) || control < 0 || control > InsertA + model::kTrackInserts - 1) {
        return {};
    }
    const bool two = row.height >= 2 * kControlLine + 4;
    const int top = two ? row.top + (row.height - 2 * kControlLine - 1) / 2 : row.top + (row.height - kControlLine) / 2;
    const int x = kHeaderWidth + 2;
    if (control <= Solo) return QRect(x + control * (kControlButton + 2), top, kControlButton, kControlLine);
    if (!two) return {};
    const int bottom = top + kControlLine + 1;
    if (control == Mute) return QRect(x + 3 * (kControlButton + 2), bottom, kControlButton, kControlLine);
    return QRect(x + (control - InsertA) * (kInsertButton + 1), bottom, kInsertButton, kControlLine);
}

QRect TimelineView::controlPanelButton() const {
    if (!timeline_.display().trackPanel) return {};
    return QRect(kHeaderWidth - 18, 3, 14, kRulerHeight - 6);
}

std::optional<TimelineView::Row> TimelineView::rowAt(const QPoint& pos) const {
    if (pos.y() < kRulerHeight) return std::nullopt;
    for (const Row& r : rows()) {
        if (pos.y() >= r.top && pos.y() < r.top + r.height) return r;
    }
    return std::nullopt;
}

// A new height changes how far the rows can scroll, so the Timeline's vertical scroll bar is
// set again too.
void TimelineView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    setTopOffset(top_);
    timeline_.syncScrollBars();
    timeline_.placeSequenceMap();
}

// Paints in layers, back to front: the tracks and their segments and effects, the IN to OUT
// highlight, the ruler with its marks, the position indicator, markers, trim rollers, segment
// drag outlines, the lasso, the zoom area, then the Track Selector panel over the left edge and
// a patch arrow being dragged. firstVisible and lastVisible bound the frames worth drawing.
void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), timeline_.backgroundColor());
    if (timeline_.showsHeads()) {
        paintHeads(p);
        return;
    }
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

    // What Sync Breaks and Dupe Detection mark (p.491, p.641), worked out once a paint.
    const QList<model::SyncBreak> breakList =
        timeline_.display().syncBreaks != Timeline::SyncBreaks::None ? model::syncBreaks(seq) : QList<model::SyncBreak>{};
    QSet<model::TrackKind> dupeKinds;
    if (timeline_.display().dupeVideo) dupeKinds.insert(model::TrackKind::Video);
    if (timeline_.display().dupeAudio) dupeKinds.insert(model::TrackKind::Audio);
    if (timeline_.display().dupeData) dupeKinds.insert(model::TrackKind::Data);
    const QList<model::Dupe> dupeList =
        dupeKinds.isEmpty() ? QList<model::Dupe>{} : model::dupes(seq, dupeKinds, timeline_.options().dupeHandles);

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
            // The playback performance indicators (FX Guide p.136): red where frames could not be shown,
            // yellow where play had difficulty, along the foot of the row.
            for (const Composer::PlaybackRange& b : timeline_.playbackIndicators()) {
                const double x0 = std::max(xFor(b.start), static_cast<double>(header));
                const double x1 = std::min(xFor(b.start + b.length), static_cast<double>(width()));
                if (x1 <= x0) continue;
                p.fillRect(QRectF(x0, r.top + r.height - 5, std::max(x1 - x0, 2.0), 4),
                           QColor(b.dropped ? 0xffe03030 : 0xffe0c030));
            }
            p.setPen(text);
            for (std::int64_t f = (firstVisible / interval) * interval; f <= lastVisible; f += interval) {
                if (f < 0) continue;
                const int x = static_cast<int>(xFor(f));
                p.drawText(QRect(x + 3, r.top, kLabelSpacing, r.height), Qt::AlignVCenter | Qt::AlignLeft, tc(f));
            }
        } else if (const model::Track* t = r.exists ? seq.track(r.track) : nullptr) {
            // An expanded nest's tracks hold its material only over the segment (pictures on p.221): grey
            // outside it.
            if (timeline_.isNestedRow(r.track)) {
                if (const auto frames = timeline_.expandedFrames()) {
                    const double x0 = std::clamp(xFor(frames->first), static_cast<double>(header), static_cast<double>(width()));
                    const double x1 = std::clamp(xFor(frames->second), static_cast<double>(header), static_cast<double>(width()));
                    const QColor outside(0x88, 0x88, 0x88);
                    p.fillRect(QRectF(header, r.top, x0 - header, r.height), outside);
                    p.fillRect(QRectF(x1, r.top, width() - x1, r.height), outside);
                }
            }
            // Track Color (p.597) takes the place of the kind's colour; an inactive track is
            // drawn darker (ours).
            const QColor fill = Timeline::trackColor(*t);
            const QString selectionColor = timeline_.display().segmentSelectionColor;
            if (t->locked) p.fillRect(rowRect, QColor(kTimecodeRowColor));
            std::int64_t at = 0;
            const model::Segment* previous = nullptr;
            for (const model::Segment& s : t->segments) {
                const std::int64_t end = at + s.length;
                const bool selected = !timeline_.showsSource() &&
                                      timeline_.selection().contains(model::SegmentRef{r.track, at});
                // Sync Breaks also governs the match-frame signs (p.491).
                if (end >= firstVisible && at <= lastVisible && previous && timeline_.showsSyncBreaks(t->kind) &&
                    model::Sequence::isMatchFrame(*previous, s)) {
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
                    // Clip colours (p.594-598) take the place of the track's.
                    const QString clipColor = timeline_.segmentColor(s);
                    const QColor segFill = clipColor.isEmpty() ? fill : QColor(clipColor);
                    // Segment Selection (p.593): the chosen colour, else a lighter fill.
                    p.fillRect(box, !selected                  ? segFill
                                    : selectionColor.isEmpty() ? segFill.lighter(135)
                                                               : QColor(selectionColor));
                    p.setPen(segFill.darker(160));
                    p.drawRect(box.adjusted(0, 0, -1, -1));
                    if (selected) {
                        p.setPen(QPen(QColor(kSelected), 2));
                        p.drawRect(box.adjusted(1, 1, -1, -1));
                    }
                    // Audio waveforms (p.677-680).
                    if (t->kind == model::TrackKind::Audio && timeline_.showsWaveform(*t) && !fastScrubbing()) {
                        paintWaveform(p, box, *t, s, at);
                    }
                    // Clip Frames (Fast menu, p.592): the start frame of each
                    // segment on video tracks, at its left end.
                    QRectF textArea = box;
                    if (timeline_.display().clipFrames && t->kind == model::TrackKind::Video && xFor(at) >= x0 - 1 &&
                        !fastScrubbing()) {
                        const int h = std::max(r.height - 4, 8);
                        const QSize size(h * 16 / 9, h);
                        if (box.width() > size.width() + 4) {
                            const QRectF frame(box.left() + 2, box.top() + 1, size.width(), size.height());
                            p.fillRect(frame, Qt::black);
                            if (MediaFrames* frames = MediaFrames::instance()) {
                                const auto clip = timeline_.composer().resolveForSegment(s);
                                const QImage image = clip ? frames->thumbnail(*clip, s.sourceIn, size) : QImage();
                                if (!image.isNull()) {
                                    const QSize fit = image.size().scaled(size, Qt::KeepAspectRatio);
                                    p.drawImage(QRectF(frame.left() + (size.width() - fit.width()) / 2.0,
                                                       frame.top() + (size.height() - fit.height()) / 2.0, fit.width(),
                                                       fit.height()),
                                                image);
                                }
                            }
                            textArea.setLeft(frame.right() + 2);
                        }
                    }
                    // Clip text (Fast menu, p.592).
                    const QString clipText = textArea.width() > 12 ? timeline_.clipText(*t, s) : QString();
                    if (!clipText.isEmpty()) {
                        p.save();
                        p.setFont(timeline_.clipFont());
                        p.setPen(clipTextColor(segFill, bright, timeline_.options().textColor));
                        const QRectF textBox = textArea.adjusted(4, 0, -3, 0);
                        p.drawText(textBox, Qt::AlignVCenter | Qt::AlignLeft,
                                   p.fontMetrics().elidedText(clipText, Qt::ElideRight, static_cast<int>(textBox.width())));
                        p.restore();
                    }
                }
                previous = &s;
                at = end;
            }
            if (t->kind == model::TrackKind::Audio && !timeline_.showsSource()) paintAudioData(p, r, *t);
            // Dupe Detection's bars above the frames (p.641).
            for (const model::Dupe& dupe : dupeList) {
                if (dupe.track != r.track) continue;
                const QRectF bar(xFor(dupe.start), r.top + 1, xFor(dupe.start + dupe.length) - xFor(dupe.start), 4);
                p.fillRect(bar, QColor(model::dupeColor(dupe.set)));
            }
            // Sync breaks: "white numbers indicating negative or positive offset values relative to
            // zero" at the break (p.491).
            for (const model::SyncBreak& b : breakList) {
                if (b.track != r.track || !timeline_.showsSyncBreaks(t->kind)) continue;
                const double x = xFor(b.start);
                if (x > width() || xFor(b.start + b.length) < header) continue;
                const QString number = b.offset > 0 ? QStringLiteral("+%1").arg(b.offset) : QString::number(b.offset);
                p.setPen(bright);
                p.drawText(QRectF(x + 3, r.top, std::max(xFor(b.start + b.length) - x - 6, 24.0), r.height),
                           Qt::AlignVCenter | Qt::AlignRight, number);
            }
            if (timeline_.display().effectIcons) paintEffects(p, r, *t);
        }
        p.setPen(QColor(kRowLine));
        p.drawLine(header, r.top + r.height - 1, width(), r.top + r.height - 1);
    }
    // IN to OUT highlighting (p.638), on the selected tracks; Show Marked
    // Region (p.1218) turns it off.
    if (timeline_.options().markedRegion && !state.isEmpty() && (state.in() || state.out()) && !fastScrubbing()) {
        const double x0 = xFor(state.markedStart());
        const double x1 = xFor(state.markedEnd() + 1);
        for (const Row& r : rowList) {
            const bool selected = timeline_.showsSource() ? timeline_.patch().isSourceSelected(r.track)
                                                          : timeline_.patch().isRecordSelected(r.track);
            if (r.exists && selected && r.track != QLatin1String("TC1")) {
                // Mark IO Selection (p.593): the chosen colour, see-through.
                QColor highlight = QColor::fromRgba(kInOut);
                if (!timeline_.display().markIOColor.isEmpty()) {
                    highlight = QColor(timeline_.display().markIOColor);
                    highlight.setAlpha(0x60);
                }
                p.fillRect(QRectF(x0, r.top, x1 - x0, r.height - 1), highlight);
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

    // Markers: ovals on their tracks, spanned markers along TC1 (p.413-415).
    if (!state.isEmpty() && !fastScrubbing()) {
        p.save();
        p.setClipRect(tracksArea);
        p.setRenderHint(QPainter::Antialiasing);
        for (const model::Marker& m : model::markersOf(state.current()->item)) {
            if (!timeline_.showsMarkers(m.color)) continue; // Show Markers (p.599)
            for (const Row& r : rowList) {
                if (r.track != m.track) continue;
                const QColor c = QColor::fromRgb(model::markerColor(m.color));
                p.setPen(QPen(c.darker(150), 1));
                p.setBrush(c);
                if (m.isSpanned()) {
                    p.drawRoundedRect(QRectF(xFor(m.frame), r.top + 3, xFor(m.end()) - xFor(m.frame), r.height - 6), 3, 3);
                } else {
                    const double x = xFor(m.frame) + std::max(scale, 1.0) / 2;
                    p.drawEllipse(QRectF(x - 5, r.top + 2, 10, 7));
                }
            }
        }
        p.restore();
    }

    // Trim rollers (p.650-651): red for Overwrite, yellow for Ripple, pink
    // for dual-roller trims; slipped segments get rollers at both ends.
    if (timeline_.isTrimMode()) {
        p.save();
        p.setClipRect(tracksArea);
        const std::int64_t offset = trimFrom_ ? frameAt(trimPos_.x()) - frameAt(trimFrom_->x()) : 0;
        auto roller = [&](const Row& r, std::int64_t frame, model::TrimSide side, const QColor& c) {
            const double x = xFor(frame);
            p.setPen(QPen(c, 2));
            p.setBrush(Qt::NoBrush);
            const double top = r.top + 2;
            const double bottom = r.top + r.height - 3;
            if (side != model::TrimSide::B) {
                p.drawLine(QPointF(x - 1, top), QPointF(x - 1, bottom));
                p.drawLine(QPointF(x - 1, top), QPointF(x - 6, top));
                p.drawLine(QPointF(x - 1, bottom), QPointF(x - 6, bottom));
            }
            if (side != model::TrimSide::A) {
                p.drawLine(QPointF(x + 1, top), QPointF(x + 1, bottom));
                p.drawLine(QPointF(x + 1, top), QPointF(x + 6, top));
                p.drawLine(QPointF(x + 1, bottom), QPointF(x + 6, bottom));
            }
        };
        for (const model::TrimRoller& t : timeline_.rollers()) {
            const QColor c(t.side == model::TrimSide::Both                 ? kDualRoller
                           : timeline_.trimKind() == model::TrimKind::Ripple ? kSpliceColor
                                                                           : kOverwriteColor);
            for (const Row& r : rowList) {
                if (r.track != t.track) continue;
                roller(r, t.frame, t.side, c);
                if (offset != 0) {
                    // Where the drag will put it.
                    p.setPen(QPen(c, 1, Qt::DashLine));
                    p.drawLine(QPointF(xFor(t.frame + offset), r.top), QPointF(xFor(t.frame + offset), r.top + r.height));
                }
            }
        }
        // Sync rollers, gray, on the sync-locked tracks the trim moves (p.656).
        for (const model::TrimRoller& t : timeline_.syncRollers()) {
            for (const Row& r : rowList) {
                if (r.track != t.track) continue;
                roller(r, t.frame, t.side, QColor(kSyncRoller));
                if (offset != 0) {
                    p.setPen(QPen(QColor(kSyncRoller), 1, Qt::DashLine));
                    p.drawLine(QPointF(xFor(t.frame + offset), r.top), QPointF(xFor(t.frame + offset), r.top + r.height));
                }
            }
        }
        for (const model::SegmentRef& ref : timeline_.slipSelection()) {
            const auto seg = model::segmentFor(seq, ref);
            if (!seg) continue;
            for (const Row& r : rowList) {
                if (r.track != ref.track) continue;
                roller(r, ref.start, model::TrimSide::B, QColor(kDualRoller));
                roller(r, ref.start + seg->length, model::TrimSide::A, QColor(kDualRoller));
            }
        }
        p.restore();
    }

    // A bin clip being dragged in: "a white outline of the clip indicates the segment position" (p.624).
    // Ours: on the selected record tracks.
    if (binDrop_) {
        p.save();
        p.setClipRect(tracksArea);
        p.setPen(QPen(QColor(Qt::white), 2));
        p.setBrush(Qt::NoBrush);
        const QStringList on = timeline_.patch().selectedRecordTracks(seq);
        for (const Row& r : rowList) {
            if (!on.contains(r.track)) continue;
            p.drawRect(QRectF(xFor(binDrop_->frame), r.top + 2, xFor(binDrop_->frame + binDrop_->length) - xFor(binDrop_->frame), r.height - 4));
        }
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
            const QRectF box(xFor(ref.start + frames), top + 2, xFor(ref.start + frames + seg->length) - xFor(ref.start + frames),
                             h - 4);
            if (!timeline_.options().wireframeDragging && !seg->isFiller()) {
                // Without Wireframe Dragging (p.1219) "the clip" is seen moving: drawn whole, with its name.
                const model::Track* t = seq.track(ref.track);
                const QString clipColor = timeline_.segmentColor(*seg);
                const QColor fill = !clipColor.isEmpty() ? QColor(clipColor) : t ? Timeline::trackColor(*t) : QColor(kVideoSegment);
                p.fillRect(box, fill);
                p.save();
                p.setPen(clipTextColor(fill, palette().color(QPalette::BrightText), timeline_.options().textColor));
                p.drawText(box.adjusted(4, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft,
                           p.fontMetrics().elidedText(seg->name, Qt::ElideRight, static_cast<int>(box.width()) - 7));
                p.restore();
            }
            p.drawRect(box);
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
        const bool colors = timeline_.sourceRecordColors();
        auto button = [&](const QRect& box, const QString& label, bool on, bool dim, bool isSource) {
            const QRgb lit = !colors ? kButtonOn : isSource ? kSourceButtonOn : kRecordButtonOn;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(dim ? kButtonDim : on ? lit : kButtonOff));
            p.drawRoundedRect(box, 2, 2);
            p.setPen(dim ? text.darker(150) : text);
            p.drawText(box, Qt::AlignCenter, label);
        };
        for (const Row& r : rowList) {
            if (r.track == QLatin1String("TC1")) {
                button(recordButton(r), r.track, false, true, false);
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
            if (!source.isEmpty()) button(sourceButton(r), source, patch.isSourceSelected(source), false, true);
            if (r.exists && !sourceView) {
                const auto ref = model::parseTrackName(r.track);
                const bool cannotTake = dragKind && (!ref || ref->kind != *dragKind || ref->kind == model::TrackKind::Data);
                const model::Track* t = seq.track(r.track);
                // A renamed track shows its own name (p.635).
                const QString label = t && !t->customName.isEmpty() ? t->customName : r.track;
                button(recordButton(r), p.fontMetrics().elidedText(label, Qt::ElideRight, kRecordWidth - 2),
                       patch.isRecordSelected(r.track), cannotTake, false);
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
            // The Track Control panel (p.602).
            const model::Track* audio = r.exists && !sourceView ? seq.track(r.track) : nullptr;
            if (audio && audio->kind == model::TrackKind::Audio && !controlButton(r, Waveform).isEmpty()) {
                paintControls(p, r, *audio);
            }
        }
        p.restore();
        if (sourceView) {
            p.fillRect(QRect(0, 0, header, kRulerHeight), QColor(kSourceView).darker(180));
        }
        // The Track Control Panel button (p.602): an arrow pointing the way a click moves the panel.
        const QRectF cb = controlPanelButton();
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(timeline_.display().trackControlPanel ? kGlyph : QColor(kButtonOff).lighter(150).rgb()));
        const QPointF cc = cb.center();
        if (timeline_.display().trackControlPanel) {
            p.drawPolygon(QPolygonF{QPointF(cc.x() + 3, cc.y() - 4), QPointF(cc.x() + 3, cc.y() + 4), QPointF(cc.x() - 3, cc.y())});
        } else {
            p.drawPolygon(QPolygonF{QPointF(cc.x() - 3, cc.y() - 4), QPointF(cc.x() - 3, cc.y() + 4), QPointF(cc.x() + 3, cc.y())});
        }
        p.restore();
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

// The Track Control panel's buttons for an audio track (p.602), lit while on: Waveform a small
// plot, Clip Volume/Pan a line with a keyframe, Inactive a power symbol, S and M Solo and Mute, and
// each insert the first two letters of its effect (ours; the picture on p.602 shows "EQ", "Dy" ...).
void TimelineView::paintControls(QPainter& p, const Row& r, const model::Track& t) const {
    const QColor text = palette().color(QPalette::WindowText);
    auto box = [&](const QRect& b, bool on) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(on ? kButtonOn : kButtonOff));
        p.drawRoundedRect(b, 2, 2);
        p.setPen(QPen(QColor(kGlyph), 1));
        p.setBrush(Qt::NoBrush);
    };
    const bool perTrack = timeline_.display().perTrack;
    QRect b = controlButton(r, Waveform);
    box(b, perTrack && t.showWaveform);
    for (int i = 0; i < 5; ++i) {
        const int x = b.left() + 4 + i * 3;
        const int h = 1 + (i * 2) % 4;
        p.drawLine(x, b.center().y() - h, x, b.center().y() + h);
    }
    b = controlButton(r, AudioData);
    box(b, perTrack && (t.showClipVolume || t.showVolume || t.showPan));
    p.drawLine(b.left() + 3, b.center().y() + 2, b.center().x(), b.center().y() - 2);
    p.drawLine(b.center().x(), b.center().y() - 2, b.right() - 3, b.center().y() - 2);
    p.drawRect(QRect(b.center().x() - 1, b.center().y() - 3, 2, 2));
    b = controlButton(r, Inactive);
    box(b, t.inactive);
    p.drawArc(QRectF(b.center().x() - 3.5, b.center().y() - 3, 7, 7), 120 * 16, 300 * 16);
    p.drawLine(b.center().x(), b.center().y() - 4, b.center().x(), b.center().y());
    for (const auto& [control, solo] : {std::pair{static_cast<int>(Solo), true}, std::pair{static_cast<int>(Mute), false}}) {
        b = controlButton(r, control);
        if (b.isEmpty()) continue;
        box(b, timeline_.isSoloMuted(r.track, solo));
        p.setPen(text);
        p.drawText(b, Qt::AlignCenter, solo ? QStringLiteral("S") : QStringLiteral("M"));
    }
    for (int slot = 0; slot < model::kTrackInserts; ++slot) {
        b = controlButton(r, InsertA + slot);
        if (b.isEmpty()) continue;
        const auto& in = t.inserts[static_cast<std::size_t>(slot)];
        box(b, in && !in->bypass);
        if (in) {
            p.setPen(text);
            p.drawText(b, Qt::AlignCenter, in->name.left(2));
        }
    }
}

// ---------------------------------------------------------------- Heads and Heads Tails views

// Ours: the boxes are 16:9 frames 56 pixels high (two side by side in Heads Tails), 6 pixels apart,
// in one row under the ruler, made narrower when they would not all fit.
QRect TimelineView::headsBox(int index, int count) const {
    const int header = headerWidth();
    const bool tails = timeline_.display().viewType == Timeline::ViewType::HeadsTails;
    const int frames = tails ? 2 : 1;
    int h = 56;
    const int available = width() - header - 8;
    if (count > 0) {
        const int fit = available / count - 6;
        h = std::clamp(fit * 9 / (16 * frames), 12, h);
    }
    const int w = h * 16 / 9 * frames;
    return QRect(header + 6 + index * (w + 6), kRulerHeight + 6, w, h);
}

int TimelineView::headsAt(const QPoint& pos, bool boundary) const {
    const int count = static_cast<int>(timeline_.headsSpans().size());
    if (count == 0 || pos.x() < headerWidth()) return -1;
    const QRect first = headsBox(0, count);
    const int step = (count > 1 ? headsBox(1, count).left() : first.right() + 7) - first.left();
    const double at = static_cast<double>(pos.x() - first.left() + 3) / std::max(step, 1);
    if (boundary) return std::clamp(static_cast<int>(std::lround(at)), 0, count);
    if (pos.y() < first.top() || pos.y() > first.bottom()) return -1;
    const int index = static_cast<int>(std::floor(at));
    return index >= 0 && index < count && headsBox(index, count).contains(pos) ? index : -1;
}

// "Heads view shows the Head frame for each clip. Heads Tails view shows both the head and tail frames
// for each clip" (p.639). The frame is the top selected video track's; a clip with no picture there
// shows its name instead. The Track Selector shows "H" (the picture on p.639).
void TimelineView::paintHeads(QPainter& p) {
    const int header = headerWidth();
    const QColor text = palette().color(QPalette::WindowText);
    const QColor bright = palette().color(QPalette::BrightText);
    const auto spans = timeline_.headsSpans();
    const int count = static_cast<int>(spans.size());
    const model::Sequence& seq = timeline_.shown();
    const model::MonitorState& state = timeline_.composer().state(MonitorId::Record);
    QStringList video;
    for (const QString& t : timeline_.headsTracks()) {
        if (t.startsWith(QLatin1Char('V'))) video.prepend(t); // the top track first
    }
    const bool tails = timeline_.display().viewType == Timeline::ViewType::HeadsTails;
    QFont small = font();
    small.setPointSizeF(std::max(small.pointSizeF() - 1, 6.0));
    p.setFont(small);
    auto frameAtTime = [&](std::int64_t frame, const QRect& box) {
        p.fillRect(box, Qt::black);
        for (const QString& name : video) {
            const model::Track* t = seq.track(name);
            if (!t) continue;
            std::int64_t segStart = 0;
            const int i = t->segmentAt(frame, &segStart);
            if (i < 0 || t->segments.at(i).isFiller()) continue;
            const model::Segment& s = t->segments.at(i);
            if (MediaFrames* frames = MediaFrames::instance()) {
                const auto clip = timeline_.composer().resolveForSegment(s);
                const QImage image = clip ? frames->thumbnail(*clip, s.sourceIn + frame - segStart, box.size()) : QImage();
                if (!image.isNull()) {
                    const QSize fit = image.size().scaled(box.size(), Qt::KeepAspectRatio);
                    p.drawImage(QRect(box.left() + (box.width() - fit.width()) / 2, box.top() + (box.height() - fit.height()) / 2,
                                      fit.width(), fit.height()),
                                image);
                    return;
                }
            }
            p.setPen(bright);
            p.drawText(box.adjusted(3, 0, -3, 0), Qt::AlignCenter | Qt::TextWordWrap, s.name);
            return;
        }
    };
    p.save();
    p.setClipRect(QRect(header, kRulerHeight, width() - header, height() - kRulerHeight));
    for (int i = 0; i < count; ++i) {
        const QRect box = headsBox(i, count);
        if (box.left() > width()) break;
        const auto [start, end] = spans.at(i);
        if (tails) {
            const QRect head(box.left(), box.top(), box.width() / 2 - 1, box.height());
            frameAtTime(start, head);
            frameAtTime(end - 1, QRect(head.right() + 2, box.top(), box.width() - head.width() - 2, box.height()));
        } else {
            frameAtTime(start, box);
        }
        // The clip under the position indicator is outlined.
        const bool current = state.position() >= start && state.position() < end;
        p.setPen(QPen(current ? QColor(kIndicator) : QColor(kRowLine), current ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box.adjusted(0, 0, -1, -1));
    }
    // Where an Alt+drag will put the clip.
    if (headsFrom_) {
        const int to = headsAt(headsPos_, true);
        if (to >= 0) {
            const int x = to < count ? headsBox(to, count).left() - 3 : headsBox(count - 1, count).right() + 4;
            p.setPen(QPen(QColor(kSpliceColor), 2));
            p.drawLine(x, kRulerHeight + 2, x, headsBox(0, count).bottom() + 4);
        }
    }
    p.restore();
    if (header > 0) {
        p.fillRect(QRect(0, 0, header, height()), QColor(kHeader));
        const QRect row(0, kRulerHeight + 6, header, headsBox(0, std::max(count, 1)).height());
        p.setPen(text);
        p.drawText(row.adjusted(kRecordX, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("H"));
        p.setPen(QColor(kRowLine));
        p.drawLine(header - 1, 0, header - 1, height());
    }
}

// Moves the shown monitor's position to the frame under x (a click or scrub in the tracks),
// stopping playback and making that monitor active.
void TimelineView::moveIndicator(const QPoint& pos, Qt::KeyboardModifiers modifiers, bool click) {
    std::int64_t frame = std::max<std::int64_t>(frameAt(pos.x()), 0);
    // Position Bar Snap (p.1220): "clicking in the Timeline will snap the position indicator to the
    // nearest event, such as IN and OUT points, markers, and segment start and end points" (p.397: "to
    // the nearest transition"). Ours: an event within kSnapPixels of the click.
    if (click && timeline_.options().positionBarSnap && !(modifiers & Qt::ControlModifier)) {
        const MonitorId monitor = timeline_.shownMonitor();
        const model::MonitorState& state = timeline_.composer().state(monitor);
        QList<std::int64_t> points = timeline_.composer().events(monitor);
        if (state.in()) points << *state.in();
        if (state.out()) points << *state.out();
        for (const model::Marker& m : timeline_.composer().shownMarkers(monitor)) points << m.frame;
        constexpr double kSnapPixels = 8;
        double best = kSnapPixels;
        for (std::int64_t e : points) {
            const double d = std::abs(xFor(e) - pos.x());
            if (d <= best) {
                best = d;
                frame = e;
            }
        }
    }
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

bool TimelineView::fastScrubbing() const {
    return scrubbing_ && timeline_.options().fastScrub;
}

bool TimelineView::selectable(const model::SegmentRef& ref) const {
    if (timeline_.options().selectFiller) return true;
    const auto seg = model::segmentFor(timeline_.shown(), ref);
    return seg && !seg->isFiller();
}

double TimelineView::exactFrameAt(double x) const {
    const double scale = timeline_.scale();
    return scale > 0 ? timeline_.firstFrame() + (x - headerWidth()) / scale : 0.0;
}

// Each pixel column draws the lowest to the highest sample of the frames under it, read from the
// waveform's peaks (media::Waveform::range) at the file's own rate. Output Levels scales by what plays
// there (clip gain and volume automation, model::volumeAt); Full Height spreads 48 dB over the half
// height (ours: the guide only says it "visually extends audio peaks"); the plot zoom doubles or halves.
void TimelineView::paintWaveform(QPainter& p, const QRectF& box, const model::Track& t, const model::Segment& s,
                                 std::int64_t at) const {
    const auto clip = timeline_.composer().resolveForSegment(s);
    const auto link = clip ? media::linkOf(*clip) : std::nullopt;
    const auto ref = model::parseTrackName(s.sourceTrack);
    if (!clip || !link || link->unlinked || !ref || clip->editRate.num() <= 0) return;
    const Timeline::Display& d = timeline_.display();
    // Show Marked Waveforms: only between the marks (p.1218).
    double left = box.left();
    double right = box.right();
    const model::MonitorState& marks = timeline_.composer().state(timeline_.shownMonitor());
    if (timeline_.options().markedWaveforms && !marks.isEmpty() && (marks.in() || marks.out())) {
        left = std::max(left, xFor(marks.markedStart()));
        right = std::min(right, xFor(marks.markedEnd() + 1));
    }
    if (right <= left) return;
    const QList<int> order = timeline_.waveformChannels(t.format);
    const int bands = std::max(static_cast<int>(order.size()), 1);
    const double bandHeight = (box.height() - 2) / bands;
    const double zoom = std::pow(2.0, d.waveformZoom);
    const double clipRate = clip->editRate.toDouble();
    const double first = static_cast<double>(link->first) / std::max(link->rate.toDouble(), 1.0); // seconds
    p.save();
    p.setClipRect(QRectF(left, box.top(), right - left, box.height()), Qt::IntersectClip);
    for (int b = 0; b < bands; ++b) {
        const double top = box.top() + 1 + b * bandHeight;
        if (b > 0) {
            // "Separated by a horizontal divider" (p.677).
            p.setPen(QColor(kRowLine));
            p.drawLine(QPointF(left, top), QPointF(right, top));
        }
        const int track = ref->number - 1 + order.value(b, 0);
        const auto channel = link->channelOf(track);
        if (!channel) continue;
        const auto waveform = timeline_.waveforms().get({link->audioFile(track), channel->first, channel->second});
        if (!waveform || waveform->sampleRate <= 0) continue;
        const bool mismatched = timeline_.showsMismatchedRates() && waveform->sampleRate != timeline_.projectSampleRate();
        p.setPen(QColor::fromRgba(mismatched ? Timeline::kMismatchedWaveformColor : Timeline::kWaveformColor));
        const double middle = top + bandHeight / 2;
        const double half = bandHeight / 2 - 1;
        auto sampleAt = [&](double record) {
            const double source = static_cast<double>(s.sourceIn) + (record - static_cast<double>(at));
            return static_cast<std::int64_t>(std::floor((first + source / clipRate) * waveform->sampleRate));
        };
        auto shape = [&](float v) {
            double a = std::abs(static_cast<double>(v)) * zoom;
            if (d.fullHeight) a = a > 0 ? std::max(0.0, 1.0 + 20.0 * std::log10(a) / 48.0) : 0.0;
            return std::copysign(std::min(a, 1.0), static_cast<double>(v));
        };
        for (int x = static_cast<int>(std::floor(left)); x < static_cast<int>(std::ceil(right)); ++x) {
            const double r0 = exactFrameAt(x);
            const double r1 = exactFrameAt(x + 1);
            const std::int64_t from = sampleAt(r0);
            auto [lo, hi] = waveform->range(from, std::max(sampleAt(r1), from + 1));
            if (d.outputLevels) {
                // What plays there: the clip gain and the volume automation.
                const double db = model::volumeAt(s, static_cast<double>(s.sourceIn) + (r0 - static_cast<double>(at)));
                const auto g = static_cast<float>(db <= model::kGainOff ? 0.0 : std::pow(10.0, db / 20.0));
                lo *= g;
                hi *= g;
            }
            const double y0 = middle - shape(hi) * half;
            const double y1 = middle - shape(lo) * half;
            p.drawLine(QPointF(x + 0.5, y0), QPointF(x + 0.5, std::max(y1, y0 + 1)));
        }
    }
    p.restore();
}

void TimelineView::paintEffects(QPainter& p, const Row& r, const model::Track& t) const {
    // Transitions shade their span with the effect icon on the cut;
    // segment effects show their icon in the segment. Real-time effects
    // have a green dot, others a blue one until rendered (FX guide p.36); "All colored dots disappear in
    // the Timeline when you render effects" (p.36): a render in date has none.
    const int iconHeight = std::min(r.height - 4, 14);
    if (iconHeight < 8) return;
    const int iconWidth = iconHeight * 3 / 2;
    // `realTime` overrides the Effect Palette's dot (a motion effect clip's own, FX Guide p.172).
    auto icon = [&](double centre, const model::Effect& e, bool transition, bool rendered, std::optional<bool> realTime = {}) {
        const EffectInfo* info = EffectCatalog::builtin().find(e.category, e.name);
        const QRectF box(centre - iconWidth / 2.0, r.top + (r.height - iconHeight) / 2.0, iconWidth, iconHeight);
        p.setPen(QPen(QColor(0xff4b2f72), 1));
        p.setBrush(QColor(0xff8f5fc8));
        p.drawRoundedRect(box, 2, 2);
        p.setBrush(QColor(0xff4b2f72));
        if (transition) {
            p.drawPolygon(QPolygonF{box.topRight(), box.bottomRight(), box.bottomLeft() + QPointF(iconWidth / 4.0, 0)});
        } else {
            p.drawRect(box.adjusted(iconWidth / 4.0, iconHeight / 4.0, -iconWidth / 4.0, -iconHeight / 4.0));
        }
        if (rendered) return;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(realTime.value_or(!info || info->realTime) ? 0xff3cc85a : 0xff3c78dc));
        p.drawEllipse(QRectF(box.left() - 5, box.center().y() - 2, 4, 4));
    };
    // "Traditional motion effects display the effect speed in parentheses beside the effect icon in the
    // Timeline" (FX Guide p.175): the rate in frames per second, after the icon.
    auto speedText = [&](double centre, double speed, const Rational& rate, double right) {
        const QString text = QStringLiteral("(%1 FPS)").arg(QString::number(model::motionFps(speed, rate), 'g', 4));
        const QFontMetrics fm(font());
        const double x = centre + iconWidth / 2.0 + 3;
        if (x + fm.horizontalAdvance(text) > right) return;
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QPointF(x, r.top + (r.height + fm.ascent() - fm.descent()) / 2.0), text);
    };
    const model::Sequence& seq = timeline_.shown();
    // Whether segment `i` (starting at `start`)'s effect, or its transition, has a render in date.
    auto rendered = [&](int i, std::int64_t start, bool transition) {
        const model::Segment& s = t.segments.at(i);
        if (transition ? !(s.transition && s.transitionRender) : !s.render) return false;
        const model::RenderTarget target = transition ? model::RenderTarget{t.name(), i, true, start - s.transition->before(), s.transition->duration}
                                                      : model::RenderTarget{t.name(), i, false, start, s.length};
        return model::isRendered(seq, target);
    };
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    // Render Ranges (p.150): "a red line on the portion of any partially rendered effect that is
    // currently unrendered" and, with All, "on all completely unrendered effects", along the row's foot.
    auto renderRange = [&](const model::RenderTarget& target) {
        const Timeline::RenderRanges mode = timeline_.display().renderRanges;
        if (mode == Timeline::RenderRanges::None || t.kind != model::TrackKind::Video) return;
        std::int64_t from = 0;
        if (model::isRendered(seq, target)) return;
        if (model::isPartlyRendered(seq, target)) {
            from = model::renderedFrames(*model::renderOf(seq, target));
        } else if (mode != Timeline::RenderRanges::All) {
            return;
        }
        const double x0 = std::max(xFor(target.start + from), static_cast<double>(headerWidth()));
        const double x1 = std::min(xFor(target.end()), static_cast<double>(width()));
        if (x1 <= x0) return;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xffe03030));
        p.drawRect(QRectF(x0, r.top + r.height - 4, x1 - x0, 2));
    };
    // ExpertRender's choice (p.142): "highlights the effects that require rendering in the Timeline".
    for (const model::RenderTarget& target : timeline_.expertSelection()) {
        if (target.track != t.name()) continue;
        const double x0 = std::max(xFor(target.start), static_cast<double>(headerWidth()));
        const double x1 = std::min(xFor(target.end()), static_cast<double>(width()));
        if (x1 <= x0) continue;
        p.setPen(QPen(QColor(0xffffd040), 2));
        p.setBrush(QColor(0x40ffd040));
        p.drawRect(QRectF(x0 + 1, r.top + 1, x1 - x0 - 2, r.height - 2));
    }
    std::int64_t at = 0;
    for (int index = 0; index < t.segments.size(); ++index) {
        const model::Segment& s = t.segments.at(index);
        if (s.transition && s.transition->duration > 0) {
            renderRange(model::RenderTarget{t.name(), index, true, at - s.transition->before(), s.transition->duration});
        }
        if (s.effect) renderRange(model::RenderTarget{t.name(), index, false, at, s.length});
        if (s.effect) {
            const double x0 = std::max(xFor(at), static_cast<double>(headerWidth()));
            const double x1 = std::min(xFor(at + s.length), static_cast<double>(width()));
            if (x1 - x0 > iconWidth + 8) {
                icon((x0 + x1) / 2, *s.effect, false, rendered(index, at, false));
                if (const auto speed = model::motionSpeed(*s.effect)) {
                    // The rate of the clip the speed applies to (the sequence's when it cannot be found).
                    const auto clip = timeline_.composer().resolveForSegment(s);
                    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
                    const Rational rate = clip ? clip->editRate : rec ? rec->item.editRate : Rational(25);
                    speedText((x0 + x1) / 2, *speed, rate, x1);
                }
            }
        } else if (!s.isFiller() && t.kind == model::TrackKind::Video) {
            // A motion effect clip: "Freeze Frame ... No icon appears on a Freeze Frame clip in the
            // Timeline. Variable Speed, forward motion ... a motion effect icon and a green dot ...
            // reverse motion [and] Strobe Motion ... a motion effect icon and a blue dot" (p.172); none
            // once rendered.
            const auto clip = timeline_.composer().resolveForSegment(s);
            const auto motion = clip ? model::motionEffectOf(*clip) : std::nullopt;
            const double x0 = std::max(xFor(at), static_cast<double>(headerWidth()));
            const double x1 = std::min(xFor(at + s.length), static_cast<double>(width()));
            if (motion && !motion->isFreeze() && x1 - x0 > iconWidth + 8) {
                icon((x0 + x1) / 2, model::motionEffect(motion->speed), false, media::isLinked(*clip), motion->isRealTime());
                speedText((x0 + x1) / 2, motion->speed, clip->editRate, x1);
            }
        }
        // A mixed rate clip's Motion Adapter: "Mixed rate clips that are unrendered always appear with a
        // green dot that represents the Motion Adapter effect" (Editing Guide p.470). With Show Adapters >
        // Motion its icon, a T, shows too (p.599), left of an effect icon or in the middle; "If there is a
        // render dot on the clip it will appear on the effect icon. If there is no effect icon, the render
        // dot will appear on the adapters." A render in date of the segment's effect has none.
        if (s.adapter && t.kind == model::TrackKind::Video && model::adapterShown(*s.adapter) &&
            !(s.effect && rendered(index, at, false))) {
            const double x0 = std::max(xFor(at), static_cast<double>(headerWidth()));
            const double x1 = std::min(xFor(at + s.length), static_cast<double>(width()));
            const bool effectIcon = s.effect.has_value();
            const double centre = (x0 + x1) / 2;
            const double y = r.top + r.height / 2.0;
            p.setPen(Qt::NoPen);
            if (timeline_.display().motionAdapters && x1 - x0 > (effectIcon ? 3 : 1) * iconWidth + 8) {
                const double c = effectIcon ? centre - iconWidth - 8 : centre;
                const QRectF box(c - iconHeight / 2.0, r.top + (r.height - iconHeight) / 2.0, iconHeight, iconHeight);
                p.setPen(QPen(QColor(0xff3a3a3a), 1));
                p.setBrush(QColor(0xffb8b8b8));
                p.drawRoundedRect(box, 2, 2);
                // The T, drawn (not a letter, so the text-free screenshots keep it).
                p.setPen(QPen(QColor(0xff2a2a2a), 1.5));
                p.drawLine(QPointF(box.left() + 2.5, box.top() + 3), QPointF(box.right() - 2.5, box.top() + 3));
                p.drawLine(QPointF(box.center().x(), box.top() + 3), QPointF(box.center().x(), box.bottom() - 2.5));
                if (!effectIcon) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(0xff3cc85a));
                    p.drawEllipse(QRectF(box.left() - 5, y - 2, 4, 4));
                }
            } else if (!effectIcon && x1 - x0 > 10) {
                p.setBrush(QColor(0xff3cc85a));
                p.drawEllipse(QRectF(centre - 2, y - 2, 4, 4));
            }
        }
        if (s.eq) {
            // Audio EQ (p.740): a teal icon with three bars for the bands. Without text, so the
            // text-free screenshots keep it.
            const double x0 = std::max(xFor(at), static_cast<double>(headerWidth()));
            const double x1 = std::min(xFor(at + s.length), static_cast<double>(width()));
            if (x1 - x0 > iconWidth + 8) {
                const double centre = (x0 + x1) / 2;
                const QRectF box(centre - iconWidth / 2.0, r.top + (r.height - iconHeight) / 2.0, iconWidth, iconHeight);
                p.setPen(QPen(QColor(0xff1f5a5a), 1));
                p.setBrush(s.eq->in ? QColor(0xff4fb0b0) : QColor(0xff7a8a8a));
                p.drawRoundedRect(box, 2, 2);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0xff1f5a5a));
                const double bar = iconWidth / 7.0;
                const double heights[] = {0.6, 0.3, 0.5};
                for (int b = 0; b < 3; ++b) {
                    const double h = (iconHeight - 4) * heights[b];
                    p.drawRect(QRectF(box.left() + bar * (1 + 2 * b), box.bottom() - 2 - h, bar, h));
                }
            }
        }
        if (s.audioSuite && !s.eq) {
            // AudioSuite: a plum icon with a plug's two prongs; a blue dot until it is rendered, as
            // effects that do not play in real time have. With an EQ too, only the EQ's icon shows
            // ("The AudioSuite plug-in still applies even though the icon is not visible", p.700).
            const double x0 = std::max(xFor(at), static_cast<double>(headerWidth()));
            const double x1 = std::min(xFor(at + s.length), static_cast<double>(width()));
            if (x1 - x0 > iconWidth + 8) {
                const double centre = (x0 + x1) / 2;
                const QRectF box(centre - iconWidth / 2.0, r.top + (r.height - iconHeight) / 2.0, iconWidth, iconHeight);
                p.setPen(QPen(QColor(0xff4a2a5a), 1));
                p.setBrush(QColor(0xffa070c0));
                p.drawRoundedRect(box, 2, 2);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0xff4a2a5a));
                const double prong = iconWidth / 7.0;
                p.drawRect(QRectF(box.left() + prong * 2, box.top() + 2, prong, iconHeight / 2.0));
                p.drawRect(QRectF(box.left() + prong * 4, box.top() + 2, prong, iconHeight / 2.0));
                p.drawRect(QRectF(box.left() + prong * 1.5, box.top() + 2 + iconHeight / 2.0 - 1, prong * 4, prong));
                if (model::needsRender(s)) {
                    p.setBrush(QColor(0xff3c78dc));
                    p.drawEllipse(QRectF(box.left() - 5, box.center().y() - 2, 4, 4));
                }
            }
        }
        if (s.transition) {
            const double x0 = xFor(at - s.transition->before());
            const double x1 = xFor(at + s.transition->after());
            p.fillRect(QRectF(x0, r.top + 1, std::max(x1 - x0, 1.0), r.height - 2), QColor::fromRgba(0x50000000));
            const bool dissolve = s.transition->effect.name == QLatin1String("Dissolve");
            if (!dissolve || timeline_.display().dissolveIcons) icon(xFor(at), s.transition->effect, true, rendered(index, at, true));
        }
        at += s.length;
    }
    p.restore();
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

// Tracks moved by a segment drag: the track number difference between the row it started on
// and the row under the pointer, or 0 when they are tracks of different kinds.
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
            const bool filler = t->segments.at(i).isFiller();
            if (at >= from && end <= to + 1 && !(filler && (edge || !timeline_.options().selectFiller))) {
                picked << model::SegmentRef{r.track, at};
            }
            at = end;
        }
    }
    // A lasso with no segment tool turns on Lift/Overwrite (p.612), or the
    // Default Segment Tool of the Timeline settings (p.1220).
    if (!picked.isEmpty() && !timeline_.isSegmentTool(model::EditKind::Overwrite) &&
        !timeline_.isSegmentTool(model::EditKind::SpliceIn)) {
        timeline_.setSegmentTool(timeline_.options().defaultSegmentTool, true);
    }
    timeline_.setSelection(picked);
}

std::optional<model::TrimRoller> TimelineView::transitionAt(const QPoint& pos) const {
    if (pos.x() < headerWidth() || timeline_.showsSource()) return std::nullopt;
    const auto row = rowAt(pos);
    if (!row || !row->exists) return std::nullopt;
    // Within a few pixels of an edit: on it a dual roller, either side a
    // single roller (p.650).
    constexpr double kNear = 9;
    constexpr double kOn = 2.5;
    std::optional<model::TrimRoller> best;
    double distance = kNear;
    QList<std::int64_t> points = model::trimPoints(timeline_.shown(), row->track);
    for (std::int64_t e : points) {
        const double d = pos.x() - xFor(e);
        if (std::abs(d) >= distance) continue;
        distance = std::abs(d);
        const bool end = e == points.last();
        const model::TrimSide side = end || d < -kOn ? model::TrimSide::A : d > kOn ? model::TrimSide::B : model::TrimSide::Both;
        best = model::TrimRoller{row->track, e, side};
    }
    return best;
}

model::TrimKind TimelineView::trimKindAt(const QPoint& pos) const {
    const bool overwrite = timeline_.isTrimTool(model::TrimKind::Overwrite);
    const bool ripple = timeline_.isTrimTool(model::TrimKind::Ripple);
    if (overwrite != ripple) return overwrite ? model::TrimKind::Overwrite : model::TrimKind::Ripple;
    if (overwrite && ripple) {
        if (const auto row = rowAt(pos)) {
            return pos.y() < row->top + row->height / 2 ? model::TrimKind::Overwrite : model::TrimKind::Ripple;
        }
    }
    return timeline_.trimKind();
}

// A left press decides what the drag that follows does, in priority order: in the tracks area
// a zoom selection, a trim roller or slip, leaving Trim mode, a lasso (Alt), a segment selection
// and drag with a segment tool, else scrubbing; in the Track Selector, a patch drag from a
// source button or a click on a record, sync or monitor button. The state set here (zoomFrom_,
// trimFrom_, lassoFrom_, dragFrom_, scrubbing_, patchFrom_) is finished in mouseReleaseEvent().
void TimelineView::mouseDoubleClickEvent(QMouseEvent* event) {
    // Expanding a nest, or closing it (FX Guide p.220-221).
    const QPoint pos = event->position().toPoint();
    const auto row = rowAt(pos);
    if (event->button() == Qt::LeftButton && row && row->exists && pos.y() >= kRulerHeight &&
        timeline_.doubleClickTrack(row->track, pos.x() >= headerWidth() ? frameAt(pos.x()) : -1)) {
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TimelineView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const QPoint pos = event->position().toPoint();
    // Heads views (p.639): a click goes to the clip's head; "Press and hold the Alt key ... click the
    // frames representing the clip you want to move, and drag the clip to its new position."
    if (timeline_.showsHeads()) {
        const int index = headsAt(pos, false);
        if (index < 0) return;
        if (event->modifiers() & Qt::AltModifier) {
            headsFrom_ = index;
            headsPos_ = pos;
            update();
        } else {
            Composer& composer = timeline_.composer();
            composer.stop();
            composer.setActiveMonitor(MonitorId::Record);
            composer.moveTo(MonitorId::Record, timeline_.headsSpans().at(index).first);
        }
        return;
    }
    // ExpertRender's Modify Selection (FX Guide p.145): "Shift+click effects in the Timeline to add them
    // or remove them from the set of effects to render"; "Click in the timecode track" cancels.
    if (timeline_.isModifyingExpertSelection() && pos.x() >= headerWidth()) {
        const auto row = rowAt(pos);
        if (row && row->track == QLatin1String("TC1")) {
            timeline_.cancelExpertSelection();
        } else if (row && row->exists && (event->modifiers() & Qt::ShiftModifier)) {
            timeline_.toggleExpertTarget(row->track, frameAt(pos.x()));
            return;
        }
    }
    if (pos.x() >= headerWidth()) {
        if (zoomSelecting_) {
            zoomFrom_ = pos.x();
            zoomTo_ = pos.x();
            update();
            return;
        }
        // Keyframes of the graph shown (p.716, p.721-722): click, Shift-click and drag one;
        // Ctrl+Shift+click adds one (with Alt between frames); clicking elsewhere deselects.
        // A graph shows on this track, by the Audio Data menu or its own setting (p.679).
        const auto graphRow = rowAt(pos);
        const model::Track* graphTrack = graphRow && graphRow->exists ? timeline_.shown().track(graphRow->track) : nullptr;
        const bool graph = timeline_.display().automation || (graphTrack && timeline_.automationShown(*graphTrack));
        if (pos.y() >= kRulerHeight && graph && !timeline_.showsSource()) {
            const Qt::KeyboardModifiers mods = event->modifiers();
            if (const auto key = keyframeAt(pos)) {
                const bool shift = mods & Qt::ShiftModifier;
                if (shift || !timeline_.keySelection().contains(*key)) timeline_.clickKeyframe(*key, shift);
                if (!shift && timeline_.keySelection().contains(*key)) {
                    keyFrom_ = pos;
                    keyPos_ = pos;
                    keyGrab_ = *key;
                    const auto row = rowAt(pos);
                    keyGrabValue_ = row ? valueForY(*row, timeline_.automationParam(), pos.y()) : 0;
                }
                return;
            }
            const auto row = rowAt(pos);
            if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) && row && row->exists &&
                row->track.startsWith(QLatin1Char('A'))) {
                const double exact = timeline_.firstFrame() + (pos.x() - headerWidth()) / std::max(timeline_.scale(), 1e-9);
                timeline_.addKeyframeAt(row->track, (mods & Qt::AltModifier) ? exact : std::floor(exact));
                return;
            }
            if (!timeline_.keySelection().isEmpty() && !(mods & Qt::ShiftModifier)) timeline_.setKeySelection({});
        }
        // Trim mode, or a trim tool: transitions take rollers (p.650-655).
        const bool trimming = timeline_.isTrimMode() || timeline_.isTrimTool(model::TrimKind::Overwrite) ||
                              timeline_.isTrimTool(model::TrimKind::Ripple);
        if (trimming && pos.y() >= kRulerHeight && !(event->modifiers() & Qt::AltModifier)) {
            if (const auto roller = transitionAt(pos)) {
                const bool shift = event->modifiers() & Qt::ShiftModifier;
                const auto& rollers = timeline_.rollers();
                const bool selected = std::any_of(rollers.begin(), rollers.end(), [&](const model::TrimRoller& r) {
                    return r.track == roller->track && r.frame == roller->frame;
                });
                if (!selected || shift || !timeline_.isTrimMode()) timeline_.clickTransition(*roller, trimKindAt(pos), shift);
                trimFrom_ = pos;
                trimPos_ = pos;
                return;
            }
            if (timeline_.isTrimMode() && !timeline_.slipSelection().isEmpty()) {
                // Dragging a slipped segment slips it.
                if (const auto seg = segmentAt(pos); seg && timeline_.slipSelection().contains(*seg)) {
                    trimFrom_ = pos;
                    trimPos_ = pos;
                    return;
                }
            }
            if (timeline_.isTrimMode()) return;
        }
        // "Clicking the TC Track or ruler Disables Smart Tools" (Timeline settings, p.1220): every edit
        // tool of the Smart Tool goes off.
        if (timeline_.options().tcDisablesSmartTools) {
            const auto row = rowAt(pos);
            if (pos.y() < kRulerHeight || (row && row->track == QLatin1String("TC1"))) timeline_.disableSmartTools();
        }
        // Clicking the ruler or TC1 leaves Trim mode (p.651).
        if (timeline_.isTrimMode()) {
            const auto row = rowAt(pos);
            if (pos.y() < kRulerHeight || (row && row->track == QLatin1String("TC1"))) timeline_.exitTrimMode();
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
            if (const auto seg = segmentAt(pos); seg && selectable(*seg)) {
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
        moveIndicator(pos, event->modifiers(), true);
        return;
    }
    // The Track Control Panel button (p.602).
    if (controlPanelButton().contains(pos)) {
        timeline_.setTrackControlPanel(!timeline_.display().trackControlPanel);
        return;
    }
    const auto row = rowAt(pos);
    if (!row) return;
    // The Track Control panel (p.602): Alt+click on Waveform or Clip Volume/Pan does every track
    // (p.677-680).
    const bool alt = event->modifiers() & Qt::AltModifier;
    const model::Track* track = row->exists ? timeline_.shown().track(row->track) : nullptr;
    if (track && !controlButton(*row, Waveform).isEmpty()) {
        for (int control = Waveform; control < InsertA + model::kTrackInserts; ++control) {
            const QRect b = controlButton(*row, control);
            if (b.isEmpty() || !b.contains(pos)) continue;
            const bool perTrack = timeline_.display().perTrack;
            if (control == Waveform) {
                timeline_.setTrackWaveform(row->track, !(perTrack && track->showWaveform), alt);
            } else if (control == AudioData) {
                std::unique_ptr<QMenu> menu(timeline_.trackAudioDataMenu(row->track, alt, this));
                menu->exec(mapToGlobal(b.bottomLeft()));
            } else if (control == Inactive) {
                timeline_.setTrackInactive(row->track, !track->inactive);
            } else if (control == Solo || control == Mute) {
                emit timeline_.soloMuteRequested(row->track, control == Solo);
            } else {
                // An insert: a click opens the Audio Track Effect tool on it (p.602, p.792), a drag
                // moves or copies it (p.795); see mouseReleaseEvent().
                insertFrom_ = InsertPress{row->track, control - InsertA, pos};
            }
            return;
        }
    }
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

// Updates whichever drag mousePressEvent() started. A segment drag counts as dragging only
// once it passes the platform's drag distance, so a click does not move segments.
void TimelineView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (headsFrom_) {
        headsPos_ = pos;
        update();
        return;
    }
    if (keyFrom_) {
        keyPos_ = pos;
        update();
    } else if (zoomFrom_) {
        zoomTo_ = pos.x();
        update();
    } else if (lassoFrom_) {
        lassoTo_ = pos;
        update();
    } else if (trimFrom_) {
        trimPos_ = pos;
        update();
    } else if (dragFrom_) {
        dragPos_ = pos;
        dragging_ = dragging_ || (pos - *dragFrom_).manhattanLength() >= QApplication::startDragDistance();
        if (dragging_) timeline_.showSegmentDrag(dragFrames(event->modifiers()));
        update();
    } else if (scrubbing_) {
        moveIndicator(pos, event->modifiers());
    } else if (!patchFrom_.isEmpty()) {
        patchPos_ = pos;
        update();
    }
}

// Finishes the drag mousePressEvent() started; a plain click ends scrubbing or toggles a source
// track button.
std::optional<std::pair<QString, int>> TimelineView::insertAt(const QPoint& pos) const {
    const auto row = rowAt(pos);
    if (!row) return std::nullopt;
    for (int slot = 0; slot < model::kTrackInserts; ++slot) {
        if (controlButton(*row, InsertA + slot).contains(pos)) return std::pair{row->track, slot};
    }
    return std::nullopt;
}

void TimelineView::mouseReleaseEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (headsFrom_) {
        const int from = *headsFrom_;
        headsFrom_.reset();
        const int to = headsAt(pos, true);
        if (to >= 0 && to != from && to != from + 1) timeline_.moveHeads(from, to);
        update();
        return;
    }
    if (insertFrom_) {
        const InsertPress press = *insertFrom_;
        insertFrom_.reset();
        const auto to = insertAt(pos);
        if ((pos - press.from).manhattanLength() < QApplication::startDragDistance() || !to) {
            if (!to || (to->first == press.track && to->second == press.slot)) {
                emit timeline_.trackInsertRequested(press.track, press.slot);
            }
            return;
        }
        // "Click an Audio Track Effect insert button and drag it to an insert button on a new track
        // or to a new insert button on the same track"; Alt+drag copies (p.795).
        if (to->first != press.track || to->second != press.slot) {
            timeline_.moveTrackInsert(press.track, press.slot, to->first, to->second, event->modifiers() & Qt::AltModifier);
        }
        return;
    }
    if (keyFrom_) {
        // Up or down changes the value; with Alt it moves in time too, by whole frames, or with
        // Ctrl+Alt between them (p.716, p.721). Ctrl alone snaps to the scale's lines.
        const QPoint from = *keyFrom_;
        keyFrom_.reset();
        if ((pos - from).manhattanLength() >= QApplication::startDragDistance()) {
            const Qt::KeyboardModifiers mods = event->modifiers();
            const bool alt = mods & Qt::AltModifier;
            const bool ctrl = mods & Qt::ControlModifier;
            double frames = 0;
            if (alt) {
                frames = (pos.x() - from.x()) / std::max(timeline_.scale(), 1e-9);
                if (!ctrl) frames = std::round(frames);
            }
            double by = 0;
            if (const auto row = rowAt(from)) by = valueForY(*row, timeline_.automationParam(), pos.y()) - keyGrabValue_;
            if (alt) by = 0;
            if (frames != 0 || by != 0 || (ctrl && !alt)) timeline_.dragKeyframes(keyGrab_, frames, by, ctrl && !alt);
        }
        update();
        return;
    }
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
    if (trimFrom_) {
        // A roller or slip drag: the frames moved, Ctrl snapping to edits
        // (p.656).
        std::int64_t frames = frameAt(pos.x()) - frameAt(trimFrom_->x());
        trimFrom_.reset();
        if (!timeline_.slipSelection().isEmpty()) {
            if (frames != 0) timeline_.trimBy(frames);
        } else if (!timeline_.rollers().isEmpty()) {
            if (event->modifiers() & Qt::ControlModifier) {
                const std::int64_t at = timeline_.rollers().first().frame + frames;
                std::int64_t best = at;
                std::int64_t distance = -1;
                for (std::int64_t e : timeline_.shown().editPoints()) {
                    if (e == timeline_.rollers().first().frame) continue;
                    if (distance < 0 || std::abs(e - at) < distance) {
                        distance = std::abs(e - at);
                        best = e;
                    }
                }
                frames = best - timeline_.rollers().first().frame;
            }
            if (frames != 0) timeline_.trimBy(frames);
        }
        update();
        return;
    }
    if (lassoFrom_) {
        const QRect area = QRect(*lassoFrom_, pos).normalized();
        const bool rightToLeft = pos.x() < lassoFrom_->x();
        lassoFrom_.reset();
        selectLasso(area);
        if (rightToLeft && !timeline_.selection().isEmpty()) {
            // Right to left: slip, or slide with Shift+Alt (p.663).
            const QList<model::SegmentRef> picked = timeline_.selection();
            timeline_.setSelection({});
            timeline_.setSlip(picked, event->modifiers() & Qt::ShiftModifier);
        } else if (timeline_.selection().isEmpty() && timeline_.options().lassoSegmentMode) {
            // "Lassoing transitions enters: Segment Mode" (p.1221): the segments either side of the
            // transitions inside, with the Default Segment Tool when no segment tool is on (ours).
            QList<model::SegmentRef> picked;
            const std::int64_t from = frameAt(area.left());
            const std::int64_t to = frameAt(area.right());
            for (const Row& r : rows()) {
                if (!r.exists || r.top + r.height < area.top() || r.top > area.bottom()) continue;
                const model::Track* t = timeline_.shown().track(r.track);
                if (!t) continue;
                std::int64_t at = 0;
                for (const model::Segment& seg : t->segments) {
                    const std::int64_t end = at + seg.length;
                    const model::SegmentRef ref{r.track, at};
                    if (end > from && at <= to && selectable(ref)) picked << ref;
                    at = end;
                }
            }
            if (!picked.isEmpty() && !timeline_.isSegmentTool(model::EditKind::Overwrite) &&
                !timeline_.isSegmentTool(model::EditKind::SpliceIn)) {
                timeline_.setSegmentTool(timeline_.options().defaultSegmentTool, true);
            }
            timeline_.setSelection(picked);
        } else if (timeline_.selection().isEmpty()) {
            // Only transitions inside: both sides of them (p.651).
            QList<model::TrimRoller> rollers;
            for (const Row& r : rows()) {
                if (!r.exists || r.top + r.height < area.top() || r.top > area.bottom()) continue;
                const QList<std::int64_t> points = model::trimPoints(timeline_.shown(), r.track);
                for (int i = 0; i + 1 < points.size(); ++i) {
                    const double x = xFor(points.at(i));
                    if (x >= area.left() && x <= area.right()) rollers << model::TrimRoller{r.track, points.at(i), model::TrimSide::Both};
                }
            }
            if (!rollers.isEmpty()) timeline_.setRollers(rollers, timeline_.trimKind());
        }
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
        timeline_.showSegmentDrag(std::nullopt);
        // Dropped outside the Timeline: cancelled (p.620). Alt copies.
        if (moved && rect().contains(pos) && (frames != 0 || tracks != 0)) {
            timeline_.moveSelection(frames, tracks, dragKind_, event->modifiers() & Qt::AltModifier);
        }
        update();
        return;
    }
    // Fast Scrub's hidden marks come back.
    if (scrubbing_ && timeline_.options().fastScrub) update();
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

// Over the Track Selector the menu is the track's; elsewhere the Timeline's.
void TimelineView::contextMenuEvent(QContextMenuEvent* event) {
    QString track;
    if (const auto row = rowAt(event->pos()); row && event->pos().x() < headerWidth()) track = row->track;
    QMenu* menu = timeline_.contextMenu(track, track.isEmpty() ? segmentAt(event->pos()) : std::nullopt);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

// A horizontal wheel or Shift scrolls through time, about 40 pixels per notch (120 units);
// a vertical wheel scrolls the rows and moves the Timeline's vertical scroll bar to match.
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

// Builds the window: the top toolbar (timecode and command buttons), the tracks view with its
// vertical scroll bar, and the bottom toolbar; then connects to the Composer and installs the
// resolvers the Composer uses for events and stops in the shown items.
Timeline::Timeline(Composer& composer, QWidget* parent) : QWidget(parent), composer_(composer) {
    setObjectName(QStringLiteral("Timeline"));
    view_ = new TimelineView(*this, this);
    // Waveforms are worked out on the cache's thread; the view draws them once they are ready.
    waveforms_ = std::make_unique<media::WaveformCache>([this] {
        QMetaObject::invokeMethod(this, [this] { view_->update(); }, Qt::QueuedConnection);
    });
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
    // "Right click the Timeline Fast Menu and select Set Local Clip Color" (p.598).
    fast->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fast, &QToolButton::customContextMenuRequested, this, [this, fast](const QPoint& at) {
        QMenu menu(this);
        addClipColorMenu(&menu, tr("Set Local Clip Color"), [this](const QString& hex) { setLocalClipColor(hex); })
            ->setEnabled(!selection_.isEmpty());
        menu.exec(fast->mapToGlobal(at));
    });
    QToolButton* focus = tool("focus", Picture::Focus, tr("Focus"), true);
    focus->setCheckable(true);
    connect(focus, &QToolButton::clicked, this, &Timeline::toggleFocus);
    QToolButton* toggle = tool("toggleSourceRecord", Picture::SourceRecord, tr("Toggle Source/Record in Timeline"), true);
    toggle->setCheckable(true);
    connect(toggle, &QToolButton::clicked, this, [this](bool on) { setShowSource(on); });
    // These need media, effects or the Search feature (later phases).
    // The Video Quality Menu button (p.403-404): a click goes to the next
    // quality; right-click lists them.
    QToolButton* quality = tool("videoQuality", Picture::VideoQuality, tr("Video Quality Menu"), true);
    quality->setIcon(qualityIcon(composer_.videoQuality()));
    quality->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(quality, &QToolButton::clicked, this, [this] {
        const auto q = composer_.videoQuality();
        composer_.setVideoQuality(q == Composer::VideoQuality::Full    ? Composer::VideoQuality::Draft
                                  : q == Composer::VideoQuality::Draft ? Composer::VideoQuality::Best
                                                                       : Composer::VideoQuality::Full);
    });
    connect(quality, &QWidget::customContextMenuRequested, this, [this, quality](const QPoint& pos) {
        QMenu menu(quality);
        const std::pair<Composer::VideoQuality, QString> choices[] = {
            {Composer::VideoQuality::Full, tr("Full Quality")},
            {Composer::VideoQuality::Draft, tr("Draft Quality")},
            {Composer::VideoQuality::Best, tr("Best Performance")},
        };
        for (const auto& [q, text] : choices) {
            QAction* a = menu.addAction(qualityIcon(q), text);
            a->setCheckable(true);
            a->setChecked(composer_.videoQuality() == q);
            connect(a, &QAction::triggered, this, [this, choice = q] { composer_.setVideoQuality(choice); });
        }
        menu.addSeparator();
        // The Project's Video Display setting (p.405).
        connect(menu.addAction(tr("Video Display Settings...")), &QAction::triggered, this,
                &Timeline::videoDisplaySettingsRequested);
        menu.exec(quality->mapToGlobal(pos));
    });
    connect(&composer_, &Composer::videoQualityChanged, quality,
            [quality](Composer::VideoQuality q) { quality->setIcon(qualityIcon(q)); });
    tool("clientMonitor", Picture::ClientMonitor, tr("Client Monitor"), false);
    // "The Step In and Step Out buttons appear by default in the Timeline top toolbar" (FX Guide p.220).
    connect(tool("stepIn", Picture::StepIn, tr("Step In"), true), &QToolButton::clicked, this, [this] { perform(QStringLiteral("fx.step_in")); });
    connect(tool("stepOut", Picture::StepOut, tr("Step Out"), true), &QToolButton::clicked, this, [this] { perform(QStringLiteral("fx.step_out")); });
    // The View Menu button (p.604): the Timeline view in use, and a menu of the views; with Alt, the
    // views to replace.
    QToolButton* viewButton = tool("viewMenu", Picture::ViewMenu, tr("Timeline View"), true);
    viewButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    viewButton->setFixedWidth(104);
    viewButton->setText(tr("Untitled"));
    connect(viewButton, &QToolButton::clicked, this, [this, viewButton] {
        std::unique_ptr<QMenu> menu(viewMenu(QApplication::keyboardModifiers() & Qt::AltModifier, viewButton));
        menu->exec(viewButton->mapToGlobal(QPoint(0, -menu->sizeHint().height())));
    });
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
        // The same picture on Command Palette and Tool Palette buttons.
        registerCommandIcon(QString::fromLatin1(id), picture(pic));
        top->addWidget(b);
        const QString commandId = QString::fromLatin1(id);
        connect(b, &QToolButton::clicked, this, [this, commandId] { perform(commandId); });
        return b;
    };
    command("smart.extract_splice_in", Picture::SegmentSplice, tr("Extract/Splice-in"), true);
    command("smart.lift_overwrite", Picture::SegmentOverwrite, tr("Lift/Overwrite"), true);
    command("smart.ripple_trim", Picture::RippleTrim, tr("Ripple Trim"), true);
    command("smart.overwrite_trim", Picture::OverwriteTrim, tr("Overwrite Trim"), true);
    top->addSpacing(8);
    // The mode buttons: "Source/Record Mode button (left) and Effect Mode button (right)" (p.652) and
    // "The Trim Mode button in the Timeline" (p.644).
    command("edit.source_record_mode", Picture::SourceRecordMode, tr("Source/Record Mode"), true)->setChecked(true);
    command("trim.trim_mode", Picture::TrimMode, tr("Trim Mode"), true);
    command("fx.effect_mode", Picture::EffectMode, tr("Effect Mode"), true);
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
    top->addSpacing(8);
    // "Click the Avid Titler+ icon at the top of the Timeline" (FX Guide p.303).
    command("tools.titler", Picture::Titler, tr("Titler+"), false);
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
    composer_.setPhantomResolver([this](MonitorId monitor) { return phantomMarks(monitor); });
    // The Sequence Map follows the sequence, the position and the selection.
    auto mapUpdate = [this] {
        if (map_) map_->update();
    };
    connect(&composer_, &Composer::changed, this, mapUpdate);
    connect(this, &Timeline::changed, this, mapUpdate);
    connect(&composer_, &Composer::performed, this, &Timeline::composerPerformed);
    // The Composer's buttons for the Timeline's commands (Splice-in, the Trim
    // buttons, Add Marker, ...).
    connect(&composer_, &Composer::commandRequested, this, [this](const QString& id) {
        if (handles(id)) perform(id);
    });
    connect(&composer_, &Composer::trimSideRequested, this, &Timeline::setTrimSide);
    connect(&composer_, &Composer::loopChanged, this, [this](bool looping) {
        if (!looping) trimLooping_ = false;
    });
    // J-K-L Trim (p.658-659): slow play (K with J or L held) trims like J and L; when play stops the
    // transition moves to the frame shown. Holding K and tapping J or L trims one frame.
    connect(&composer_, &Composer::playStarted, this, [this](MonitorId monitor, int speed) {
        if (speed == 0 && monitor == MonitorId::Record && trimMode_ && options_.jklTrim && !rollers_.isEmpty()) jklTrimming_ = true;
    });
    connect(&composer_, &Composer::playStopped, this, [this] {
        if (!jklTrimming_) return;
        jklTrimming_ = false;
        if (!trimMode_ || rollers_.isEmpty()) return;
        const std::int64_t frames = composer_.state(MonitorId::Record).position() - playLoopCut();
        if (frames != 0) trimBy(frames);
    });
    composer_.setKStepHandler([this](int direction) {
        if (!trimMode_ || !options_.jklTrim || rollers_.isEmpty()) return false;
        trimBy(direction);
        return true;
    });
    connect(&composer_, &Composer::markerEditRequested, this, [this](MonitorId monitor, const QString& id) {
        const model::LoadedClip* loaded = composer_.state(monitor).current();
        if (!loaded) return;
        for (const model::Marker& m : model::markersOf(loaded->item)) {
            if (m.id != id) continue;
            MarkerDialog dialog(m, trackChoices(loaded->item), this);
            if (dialog.exec() == QDialog::Accepted) changeMarker(monitor, dialog.marker());
            return;
        }
    });
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
    composer_.setStopResolver([this](MonitorId monitor) -> QList<std::int64_t> {
        const model::MonitorState& s = composer_.state(monitor);
        if (s.isEmpty()) return {0};
        const bool sequence = s.current()->item.kind == model::BinItemKind::Sequence;
        // Composer settings, Move tab (p.1160): head frames, tail frames,
        // markers, on the selected tracks or all of them.
        QList<std::int64_t> edits{0, s.duration()};
        if (sequence) {
            const model::Sequence seq = model::Sequence::fromItem(s.current()->item);
            QStringList tracks = monitor == MonitorId::Record && !options_.ignoreTrackSelectors
                                     ? patch_.selectedRecordTracks(seq)
                                     : QStringList();
            if (tracks.isEmpty()) tracks = seq.trackNames();
            edits = seq.editPoints(tracks);
        }
        QList<std::int64_t> out{0};
        for (std::int64_t e : edits) {
            if (options_.stopAtHead) out << e;
            if (options_.stopAtTail && e > 0) out << e - 1;
        }
        if (!edits.isEmpty()) out << edits.constLast(); // the end
        if (options_.stopAtMarkers) {
            for (const model::Marker& m : model::markersOf(s.current()->item)) out << m.frame;
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    });
    sync();
}

void Timeline::setOptions(const Options& options) {
    options_ = options;
    topbar_->setVisible(options.toolbar);
    if (options.onlyOneSegmentTool && liftTool_ && spliceTool_) setSegmentTool(model::EditKind::SpliceIn, false);
    ButtonMapper::instance().setMarked(QStringLiteral("edit.overwrite"), options.syncPointEditing);
    view_->update();
    if (map_) map_->update();
    composer_.refreshDisplay();
}

void Timeline::setSyncPointEditing(bool on) {
    options_.syncPointEditing = on;
    // "The orange mark on the Overwrite button signals Sync Point editing is active" (p.496).
    ButtonMapper::instance().setMarked(QStringLiteral("edit.overwrite"), on);
    emit changed();
}

void Timeline::loadFiller() {
    // "The system loads a 2-minute clip of filler into the Source monitor" (p.462): ours, a sequence of
    // filler on the record sequence's tracks (V1, A1 and A2 without one), in no bin, as Clipboard
    // Contents is.
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    const bool sequence = rec && rec->item.kind == model::BinItemKind::Sequence;
    const Rational rate = rec ? rec->item.editRate : Rational(std::max(format_.nominalFps, 1));
    const std::int64_t length = 120 * std::max<std::int64_t>(format_.nominalFps, 1);
    model::Sequence filler = model::Sequence::withDefaultTracks();
    if (sequence) {
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        filler = model::Sequence();
        for (const QString& name : seq.trackNames()) {
            const model::Track* t = seq.track(name);
            if (t && t->kind != model::TrackKind::Data) filler.ensureTrack(name, t->format);
        }
    }
    for (const QString& name : filler.trackNames()) filler.track(name)->segments = {model::Segment::filler(length)};
    model::BinItem clip;
    clip.id = QStringLiteral("filler-") + model::newItemId();
    clip.name = tr("Filler");
    clip.kind = model::BinItemKind::Sequence;
    clip.editRate = rate;
    filler.storeIn(clip);
    composer_.load(MonitorId::Source, QString(), clip);
}

std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>> Timeline::phantomMarks(MonitorId monitor) const {
    // Phantom Marks (p.501): the marks the next Splice-in or Overwrite would use, where none is set.
    if (!options_.phantomMarks) return {};
    const model::MonitorState& source = composer_.state(MonitorId::Source);
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (source.isEmpty() || record.isEmpty()) return {};
    const model::EditMarks sourceMarks{source.in(), source.out(), source.position(), source.duration()};
    const model::EditMarks recordMarks{record.in(), record.out(), record.position(), record.duration()};
    const model::EditPlan plan = options_.syncPointEditing ? model::planSyncPoint(sourceMarks, recordMarks)
                                                           : model::planEdit(sourceMarks, recordMarks, options_.singleMarkEditing);
    if (!plan.ok()) return {};
    const model::MonitorState& s = monitor == MonitorId::Source ? source : record;
    const std::int64_t start = monitor == MonitorId::Source ? plan.sourceStart : plan.recordStart;
    std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>> out;
    if (!s.in()) out.first = start;
    if (!s.out()) out.second = start + plan.length - 1;
    return out;
}

bool Timeline::hasShown() const {
    return !composer_.state(shownMonitor()).isEmpty();
}

std::int64_t Timeline::startFrame() const {
    return composer_.state(shownMonitor()).startFrame();
}

// Clearing shownId_ makes sync() treat the item as new: the view zooms out and the selection
// and Trim mode end.
void Timeline::setShowSource(bool on) {
    if (showSource_ == on) return;
    showSource_ = on;
    if (QToolButton* b = toolButton(QStringLiteral("toggleSourceRecord"))) b->setChecked(on);
    shownId_.clear();
    sync();
}

// Rebuilds shown_ from the shown monitor whenever the Composer changes, and with it the source
// tracks of the patch, the selection, the timecode display and the horizontal view.
void Timeline::sync() {
    if (syncing_) return;
    // Another item in the Record monitor than the nest stepped into: the levels are left.
    if (!nests_.isEmpty()) {
        const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
        if (!rec || rec->item.id != nests_.last().itemId) nests_.clear();
    }
    // Likewise an expanded nest.
    if (expanded_) {
        const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
        if (!rec || rec->item.id != expanded_->itemId) expanded_.reset();
    }
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
                    seg.sourceMob = item.mobId;
                    seg.binId = clip->binId;
                    seg.name = item.name;
                    seg.sourceTrack = t;
                    seg.length = item.durationFrames;
                    // "If you use the Toggle Source/Record button to view the Timeline for a mixed rate
                    // clip loaded in the Source monitor, you also see the green dot" (p.470).
                    seg.adapter = model::adapterOf(item);
                    if (seg.length > 0) track->segments << seg;
                }
            }
        }
    } else {
        shown_ = model::Sequence();
    }
    const model::MonitorState& source = composer_.state(MonitorId::Source);
    const QStringList sourceBefore = patch_.sourceTracks();
    patch_.setSourceTracks(source.isEmpty() ? QStringList() : model::sourceTracksOf(source.current()->item));
    // Auto-enable Source Tracks (p.1160): new source material starts with all its tracks enabled.
    const QString sourceId = source.isEmpty() ? QString() : source.current()->item.id;
    if (sourceId != lastSourceId_) {
        lastSourceId_ = sourceId;
        if (options_.autoEnableSourceTracks) {
            for (const QString& t : patch_.sourceTracks()) patch_.setSourceSelected(t, true);
        }
    }
    // Auto-Patching (p.1220): another source clip is patched to the enabled record tracks.
    if (options_.autoPatching && patch_.sourceTracks() != sourceBefore) autoPatch();

    // Selected segments that are gone are dropped.
    selection_.removeIf([this](const model::SegmentRef& ref) { return showSource_ || !model::segmentFor(shown_, ref); });
    // The master timecode at the position (picture on p.607).
    // In a nest, its level before the timecode (p.220).
    const QString level = model::nestLevelLabel(nestLevel());
    timecode_->setText(clip ? (level.isEmpty() ? s.tracking(model::TrackingFormat::Master)
                                               : level + QLatin1Char(' ') + s.tracking(model::TrackingFormat::Master))
                            : QString());
    const QString id = clip ? clip->item.id : QString();
    if (id != shownId_) {
        shownId_ = id;
        selection_.clear();
        // Another sequence: Trim mode ends.
        if (trimMode_) {
            trimMode_ = false;
            rollers_.clear();
            slip_.clear();
            QMetaObject::invokeMethod(this, &Timeline::syncTrimDisplay, Qt::QueuedConnection);
        }
        scale_ = 0;
        first_ = 0;
        focusBack_.reset();
        zoomStack_.clear();
    }
    // Movement During Play (p.1219). Page: the view follows the position
    // indicator a page at a time (p.609); Scroll: the view moves under the
    // indicator; None: the view stays while playing.
    if (scale_ > 0 && clip) {
        const double visible = (view_->width() - view_->headerWidth()) / scale_;
        const auto pos = static_cast<double>(s.position());
        const bool playing = composer_.isPlaying();
        if (playing && options_.movement == Options::Movement::Scroll) {
            first_ = std::max(pos - visible * 0.5, 0.0);
        } else if (!(playing && options_.movement == Options::Movement::None) &&
                   (pos < first_ || pos >= first_ + visible)) {
            first_ = std::max(pos - visible * 0.1, 0.0);
        }
    }
    syncScrollBars();
    view_->update();
    emit changed();
}

// Sets the scroll bars and scale bar from the view without feeding back: syncing_ makes their
// valueChanged handlers ignore the change. The horizontal bar's range is in frames.
void Timeline::syncScrollBars() {
    if (!scroll_ || !vscroll_ || !scaleBar_) return; // still being built
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
    const int extra = std::max(view_->contentHeight() - view_->height(), 0);
    vscroll_->setRange(0, extra);
    vscroll_->setValue(view_->topOffset());
    vscroll_->setPageStep(std::max(view_->height(), 1));
    vscroll_->setVisible(extra > 0);
    syncing_ = false;
    if (map_) map_->update();
}

double Timeline::fitScale() const {
    // The whole sequence, with a little room after it; at least ten
    // seconds.
    const int area = std::max(view_->width() - view_->headerWidth(), 1);
    const auto fps = static_cast<std::int64_t>(std::max(format_.nominalFps, 1));
    const std::int64_t span = std::max<std::int64_t>(shown_.duration() + shown_.duration() / 20 + 1, 10 * fps);
    return area / static_cast<double>(span);
}

// The scale bar (0..1000) is logarithmic between fitScale() at 0 and kMaxScale at 1000; 0 means
// fit (a scale of 0). sliderFor() is the inverse.
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

// Clamped to the sequence's duration; ignored while the whole sequence fits.
void Timeline::setFirstFrame(double frame) {
    if (scale_ <= 0) return;
    first_ = std::clamp(frame, 0.0, std::max(static_cast<double>(shown_.duration()), 0.0));
    syncScrollBars();
    view_->update();
}

void Timeline::scrollTo(double firstFrame, int topOffset) {
    if (scale_ > 0) first_ = std::clamp(firstFrame, 0.0, std::max(static_cast<double>(shown_.duration()), 0.0));
    view_->setTopOffset(topOffset);
    syncScrollBars();
    view_->update();
}

void Timeline::placeSequenceMap() {
    if (!map_ || mapInWindow_) return;
    const int w = std::clamp(view_->width() * 2 / 5, 160, 480);
    const int h = std::clamp(view_->height() * 2 / 5, 60, 220);
    map_->setGeometry(std::max(view_->width() - w - 8, 0), std::max(view_->height() - h - 8, 0), w, h);
    map_->raise();
}

void Timeline::showSequenceMap(bool on) {
    if (!on) {
        mapShown_ = false;
        if (map_) map_->hide();
        if (mapWindow_) mapWindow_->hide();
        return;
    }
    // Shown afresh, it opens where the setting says (p.1219).
    if (!mapShown_) mapInWindow_ = !options_.mapOverlay;
    setSequenceMapInWindow(mapInWindow_);
}

void Timeline::setSequenceMapInWindow(bool inWindow) {
    if (!map_) map_ = new SequenceMap(*this, view_);
    mapInWindow_ = inWindow;
    mapShown_ = true;
    map_->hide(); // shown again below, so it sets up its click filter for the new place
    if (inWindow) {
        if (!mapWindow_) {
            mapWindow_ = new SequenceMapWindow(this);
            connect(mapWindow_, &SequenceMapWindow::closed, this, [this] {
                if (mapInWindow_) showSequenceMap(false);
            });
        }
        if (map_->parentWidget() != mapWindow_) {
            map_->setParent(mapWindow_);
            mapWindow_->layout()->addWidget(map_);
        }
        map_->show();
        mapWindow_->show();
        mapWindow_->raise();
    } else {
        if (mapWindow_) mapWindow_->hide();
        if (map_->parentWidget() != view_) {
            if (mapWindow_) mapWindow_->layout()->removeWidget(map_);
            map_->setParent(view_);
        }
        placeSequenceMap();
        map_->show();
    }
}

void Timeline::showEntireSequence() {
    focusBack_.reset();
    setScale(0);
}

// focusBack_ keeps the scale and first frame to go back to.
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

// The old view goes on zoomStack_ for zoomBack(); a range wider than the whole sequence fits it.
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
    // clear() leaves the submenus of the last build: delete them, so they do not pile up.
    for (QMenu* sub : m->findChildren<QMenu*>(Qt::FindDirectChildrenOnly)) delete sub;
    auto later = [m](const QString& text) { m->addAction(text)->setEnabled(false); };
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
        setDisplay(Display());
        showEntireSequence();
    });
    // View Type (p.592, p.638-639): Heads or Heads Tails; choosing the one that is on goes back to
    // the tracks (ours: the guide names no item for that). Dimmed while the Timeline shows a source
    // (p.603).
    QMenu* viewType = m->addMenu(tr("View Type"));
    viewType->setObjectName(QStringLiteral("viewType"));
    viewType->setEnabled(!showSource_);
    for (const auto& [type, text] : {std::pair{ViewType::Heads, tr("Heads")}, std::pair{ViewType::HeadsTails, tr("Heads Tails")}}) {
        check(viewType, text, display_.viewType == type, [this, t = type](bool v) { setViewType(v ? t : ViewType::Standard); });
    }
    check(m, tr("Track Panel"), display_.trackPanel, [this](bool v) { display_.trackPanel = v; });
    check(m, tr("Effect Icons"), display_.effectIcons, [this](bool v) {
        display_.effectIcons = v;
        // Dissolve Icons is dimmed without them (FX guide p.40).
        for (QAction* a : fastMenu_->actions()) {
            if (a->text() == tr("Dissolve Icons")) a->setEnabled(v);
        }
    });
    // Render Ranges (FX Guide p.150): None, Partial Only (the default) or All.
    QMenu* rangesMenu = m->addMenu(tr("Render Ranges"));
    rangesMenu->setObjectName(QStringLiteral("renderRanges"));
    auto* rangesGroup = new QActionGroup(rangesMenu);
    for (const auto& [which, text] : {std::pair{RenderRanges::None, tr("None")}, std::pair{RenderRanges::PartialOnly, tr("Partial Only")},
                                      std::pair{RenderRanges::All, tr("All")}}) {
        check(rangesMenu, text, display_.renderRanges == which, [this, w = which](bool v) {
            if (v) display_.renderRanges = w;
        });
        rangesGroup->addAction(rangesMenu->actions().constLast());
    }
    check(m, tr("Dissolve Icons"), display_.dissolveIcons, [this](bool v) { display_.dissolveIcons = v; });
    m->actions().constLast()->setEnabled(display_.effectIcons);
    check(m, tr("Clip Frames"), display_.clipFrames, [this](bool v) { display_.clipFrames = v; });
    // Clip Text (p.592). Group Clip Name needs groups (multicamera editing) and Source Track Name the
    // per-channel track names of production sound metadata (p.593-594); neither is kept yet.
    QMenu* clipTextMenu = m->addMenu(tr("Clip Text"));
    clipTextMenu->setObjectName(QStringLiteral("clipText"));
    check(clipTextMenu, tr("Clip Names"), display_.clipNames, [this](bool v) { display_.clipNames = v; });
    clipTextMenu->addAction(tr("Group Clip Name"))->setEnabled(false);
    check(clipTextMenu, tr("Clip Tracks"), display_.clipTracks, [this](bool v) { display_.clipTracks = v; });
    clipTextMenu->addAction(tr("Source Track Name"))->setEnabled(false);
    check(clipTextMenu, tr("Clip Durations"), display_.clipDurations, [this](bool v) { display_.clipDurations = v; });
    check(clipTextMenu, tr("Clip Full Durations"), display_.clipFullDurations, [this](bool v) { display_.clipFullDurations = v; });
    check(clipTextMenu, tr("Clip Trans Duration"), display_.clipTransDuration, [this](bool v) { display_.clipTransDuration = v; });
    check(clipTextMenu, tr("Timeline Clip Notes"), display_.clipNotes, [this](bool v) { display_.clipNotes = v; });
    check(clipTextMenu, tr("Title Text"), display_.titleText, [this](bool v) { display_.titleText = v; });
    check(clipTextMenu, tr("Source Names"), display_.sourceNames, [this](bool v) { display_.sourceNames = v; });
    check(clipTextMenu, tr("Media Full Names"), display_.mediaFullNames, [this](bool v) { display_.mediaFullNames = v; });
    check(clipTextMenu, tr("Clip Resolution"), display_.clipResolution, [this](bool v) { display_.clipResolution = v; });
    // Sync Breaks (p.491-492): "display sync breaks and match-frame edits in video tracks only, audio
    // tracks only, or neither"; the item names are ours.
    QMenu* syncMenu = m->addMenu(tr("Sync Breaks"));
    syncMenu->setObjectName(QStringLiteral("syncBreaks"));
    auto* syncGroup = new QActionGroup(syncMenu);
    for (const auto& [which, text] : {std::pair{SyncBreaks::All, tr("All Tracks")},
                                      std::pair{SyncBreaks::Video, tr("Video Tracks Only")},
                                      std::pair{SyncBreaks::Audio, tr("Audio Tracks Only")},
                                      std::pair{SyncBreaks::None, tr("None")}}) {
        check(syncMenu, text, display_.syncBreaks == which, [this, w = which](bool v) {
            if (v) setSyncBreaks(w);
        });
        syncGroup->addAction(syncMenu->actions().constLast());
    }
    // Dupe Detection (p.641): "You can choose to turn on Dupe Detection for Video, Audio, and Data
    // tracks."
    QMenu* dupeMenu = m->addMenu(tr("Dupe Detection"));
    dupeMenu->setObjectName(QStringLiteral("dupeDetection"));
    for (const auto& [kind, text, on] : {std::tuple{model::TrackKind::Video, tr("Video"), display_.dupeVideo},
                                         std::tuple{model::TrackKind::Audio, tr("Audio"), display_.dupeAudio},
                                         std::tuple{model::TrackKind::Data, tr("Data"), display_.dupeData}}) {
        check(dupeMenu, text, on, [this, k = kind](bool v) { setDupeDetection(k, v); });
    }
    later(tr("Color Correction"));
    // Audio Data (p.677-680): the clip gain's line, and the Volume or Pan graph, one at a time.
    QMenu* audioData = m->addMenu(tr("Audio Data"));
    audioData->setObjectName(QStringLiteral("audioData"));
    check(audioData, tr("Waveform"), display_.waveform, [this](bool v) { setWaveform(v); });
    check(audioData, tr("Clip Volume"), display_.clipVolume, [this](bool v) { setAudioData(v, display_.automation); });
    check(audioData, tr("Volume"), display_.automation == model::AutomationParam::Volume, [this](bool v) {
        setAudioData(display_.clipVolume, v ? std::optional(model::AutomationParam::Volume) : std::nullopt);
    });
    check(audioData, tr("Pan"), display_.automation == model::AutomationParam::Pan, [this](bool v) {
        setAudioData(display_.clipVolume, v ? std::optional(model::AutomationParam::Pan) : std::nullopt);
    });
    // Allow Per Track Settings (p.677-679): the tracks' own Waveform and Clip Volume/Pan settings.
    check(audioData, tr("Allow Per Track Settings"), display_.perTrack, [this](bool v) { setPerTrackSettings(v); });
    // Waveform Options (p.679): Source Levels or Output Levels, and Full Height.
    QMenu* waveformOptions = m->addMenu(tr("Waveform Options"));
    waveformOptions->setObjectName(QStringLiteral("waveformOptions"));
    auto* levels = new QActionGroup(waveformOptions);
    check(waveformOptions, tr("Source Levels"), !display_.outputLevels, [this](bool v) { if (v) setWaveformLevels(false, display_.fullHeight); });
    levels->addAction(waveformOptions->actions().constLast());
    check(waveformOptions, tr("Output Levels"), display_.outputLevels, [this](bool v) { if (v) setWaveformLevels(true, display_.fullHeight); });
    levels->addAction(waveformOptions->actions().constLast());
    waveformOptions->addSeparator();
    check(waveformOptions, tr("Full Height"), display_.fullHeight, [this](bool v) { setWaveformLevels(display_.outputLevels, v); });
    connect(m->addAction(tr("Clip Color...")), &QAction::triggered, this, &Timeline::openClipColorDialog);
    // Segment Selection and Mark IO Selection (p.593): Default, or a colour from the palette.
    addClipColorMenu(m, tr("Segment Selection"), [this](const QString& hex) {
        setSelectionColors(hex, display_.markIOColor);
    }, tr("Default"))->setObjectName(QStringLiteral("segmentSelection"));
    addClipColorMenu(m, tr("Mark IO Selection"), [this](const QString& hex) {
        setSelectionColors(display_.segmentSelectionColor, hex);
    }, tr("Default"))->setObjectName(QStringLiteral("markIOSelection"));
    // Track Color (p.597): on the selected record tracks; "A track must be selected in the Timeline
    // for the Track Color command to appear" (p.593), so it is dimmed without one. Other... is the
    // custom colour the guide gives with Alt (ours: an item of its own).
    const model::Sequence recordSeq = recordSequenceOf(composer_);
    const QStringList selectedTracks = patch_.selectedRecordTracks(recordSeq);
    QMenu* trackColor = addClipColorMenu(m, tr("Track Color"), [this, selectedTracks](const QString& hex) {
        setTrackColor(selectedTracks, hex);
    }, tr("Default"));
    trackColor->setObjectName(QStringLiteral("trackColor"));
    trackColor->setEnabled(!selectedTracks.isEmpty() && !showSource_);
    // Show Markers (p.599): All, None, and each colour.
    QMenu* markers = m->addMenu(tr("Show Markers"));
    markers->setObjectName(QStringLiteral("showMarkers"));
    connect(markers->addAction(tr("All")), &QAction::triggered, this, [this] { setAllMarkersShown(true); });
    connect(markers->addAction(tr("None")), &QAction::triggered, this, [this] { setAllMarkersShown(false); });
    markers->addSeparator();
    for (const model::MarkerColor& c : model::markerColors()) {
        check(markers, c.name, showsMarkers(c.name), [this, name = c.name](bool v) { setMarkersShown(name, v); });
    }
    // Show Adapters (p.599): the adapters whose icons are drawn. Only Motion adapters are kept yet.
    QMenu* adapters = m->addMenu(tr("Show Adapters"));
    adapters->setObjectName(QStringLiteral("showAdapters"));
    check(adapters, tr("Motion"), display_.motionAdapters, [this](bool v) { display_.motionAdapters = v; });
    adapters->addAction(tr("Spatial"))->setEnabled(false);
    adapters->addAction(tr("Color"))->setEnabled(false);
    QMenu* showTrack = m->addMenu(tr("Show Track"));
    check(showTrack, tr("TC1"), display_.timecodeTrack, [this](bool v) { display_.timecodeTrack = v; });
    check(m, tr("Track Control Panel"), display_.trackControlPanel, [this](bool v) { setTrackControlPanel(v); });
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
    if (options_.autoPatching) autoPatch();
    view_->update();
    emit changed();
}

void Timeline::toggleRecordTrack(const QString& track) {
    patch_.setRecordSelected(track, !patch_.isRecordSelected(track));
    if (options_.autoPatching) autoPatch();
    view_->update();
    emit changed();
}

// "Automatically patches the enabled source tracks to the tracks enabled in the Timeline sequence"
// (p.1220): of each kind, the enabled source tracks in order to the enabled record tracks in order;
// an audio track only to a record track of its format (p.629). Source tracks left over keep their
// patch.
void Timeline::autoPatch() {
    const model::LoadedClip* src = composer_.state(MonitorId::Source).current();
    if (!src) return;
    const model::Sequence seq = recordSequenceOf(composer_);
    const QStringList records = patch_.selectedRecordTracks(seq);
    QSet<QString> taken;
    for (const QString& source : patch_.sourceTracks()) {
        const auto ref = model::parseTrackName(source);
        if (!patch_.isSourceSelected(source) || !ref || ref->kind == model::TrackKind::Data) continue;
        const model::AudioFormat format = model::sourceFormat(src->item, source);
        for (const QString& record : records) {
            const model::Track* t = seq.track(record);
            if (!t || t->kind != ref->kind || taken.contains(record)) continue;
            if (t->kind == model::TrackKind::Audio && t->format != format) continue;
            taken.insert(record);
            if (patch_.recordFor(source) != record) movePatch(source, record);
            break;
        }
    }
}

// Auto-Monitoring (p.629): "When you patch from one video track to another, the Video Track Monitor
// icon moves to the track you are patching to."
bool Timeline::movePatch(const QString& source, const QString& record) {
    const QString old = patch_.recordFor(source);
    if (!patch_.patch(source, record)) return false;
    if (options_.autoMonitoring && source.startsWith(QLatin1Char('V')) && old != record && isMonitored(old)) {
        unmonitored_.insert(old);
        unmonitored_.remove(record);
    }
    return true;
}

void Timeline::toggleMonitor(const QString& track) {
    if (isNestedRow(track)) return; // "You cannot monitor individual tracks" of an expanded nest (p.220)
    if (unmonitored_.contains(track)) {
        unmonitored_.remove(track);
    } else {
        unmonitored_.insert(track);
    }
    view_->update();
}

bool Timeline::patchTracks(const QString& source, const QString& record) {
    // "You can only patch multichannel audio tracks to multichannel audio tracks, or mono tracks to
    // mono tracks" (p.629): a record track that exists must have the source track's format.
    const model::LoadedClip* src = composer_.state(MonitorId::Source).current();
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (src && rec && rec->item.kind == model::BinItemKind::Sequence) {
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        if (const model::Track* t = seq.track(record); t && t->kind == model::TrackKind::Audio) {
            const model::AudioFormat format = model::sourceFormat(src->item, source);
            if (format != t->format) {
                return fail(tr("%1 is %2 and cannot be patched to %3, which is %4.")
                                .arg(source, model::formatName(format).toLower(), record, model::formatName(t->format).toLower()));
            }
        }
    }
    if (!movePatch(source, record)) return fail(tr("%1 cannot be patched to %2.").arg(source, record));
    view_->update();
    emit changed();
    return true;
}

bool Timeline::splitTracksToMono(const QString& track) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    QString error;
    const bool ok = track.isEmpty() ? model::splitAllToMono(seq, &error) > 0 : model::splitToMono(seq, track, &error);
    if (!ok) return fail(error);
    // "A copy of your original sequence is saved to your bin as [sequence_name].Copy.[number]"
    // (p.735-736).
    emit sequenceCopyRequested(rec->binId, rec->item);
    return commitSequence(track.isEmpty() ? tr("Split All Tracks to Mono") : tr("Split Track to Mono"), seq);
}

bool Timeline::fail(const QString& text) {
    message_ = text;
    emit message(text);
    return false;
}

bool Timeline::handles(const QString& id) {
    static const QStringList ids{
        QStringLiteral("timeline.enlarge_waveform"),
        QStringLiteral("timeline.reduce_waveform"),
        QStringLiteral("fx.remove_effect"),
        QStringLiteral("tools.titler"),
        QStringLiteral("edit.splice_in"),
        QStringLiteral("edit.overwrite"),
        QStringLiteral("edit.fit_to_fill"),
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
        QStringLiteral("timeline.split_all_tracks"),
        QStringLiteral("clip.split_all_tracks"),
        QStringLiteral("clip.audio.split_all_tracks"),
        QStringLiteral("timeline.restore_default_patch"),
        QStringLiteral("timeline.add_filler_at_start"),
        QStringLiteral("timeline.show_sequence_map"),
        QStringLiteral("composer.load_filler"),
        QStringLiteral("composer.sync_point_overwrite"),
        QStringLiteral("timeline.find_black_hole"),
        QStringLiteral("timeline.find_flash_frame"),
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
        QStringLiteral("other.reverse_match_frame"),
        QStringLiteral("smart.lift_overwrite"),
        QStringLiteral("smart.extract_splice_in"),
        QStringLiteral("smart.overwrite_trim"),
        QStringLiteral("smart.ripple_trim"),
        QStringLiteral("trim.trim_mode"),
        QStringLiteral("fx.effect_mode"),
        QStringLiteral("fx.step_in"),
        QStringLiteral("fx.step_out"),
        QStringLiteral("fx.collapse"),
        QStringLiteral("edit.source_record_mode"),
        QStringLiteral("trim.trim_backward_10_frames"),
        QStringLiteral("trim.trim_backward_1_frame"),
        QStringLiteral("trim.trim_forward_1_frame"),
        QStringLiteral("trim.trim_forward_10_frames"),
        QStringLiteral("trim.cycle_trim_sides"),
        QStringLiteral("trim.play_loop"),
        QStringLiteral("play.edit_review"),
        QStringLiteral("trim.select_nearest_transition"),
        QStringLiteral("trim.select_previous_transition"),
        QStringLiteral("trim.select_next_transition"),
        QStringLiteral("trim.extend"),
        QStringLiteral("trim.slip_left"),
        QStringLiteral("trim.slip_right"),
        QStringLiteral("more.add_marker"),
        QStringLiteral("move.go_to_previous_marker"),
        QStringLiteral("move.go_to_next_marker"),
        QStringLiteral("edit.mark_markers"),
        QStringLiteral("fx.add_keyframe"),
        QStringLiteral("fx.fade_effect"),
        QStringLiteral("edit.paste_audio_keyframes"),
        QStringLiteral("play.audio_loop_play"),
    };
    return ids.contains(id);
}

// Dispatches a command ID from Timeline::handles() to the function that does it. Failures go
// through fail(), which sets message_ and emits message().
bool Timeline::perform(const QString& id) {
    if (!handles(id)) return false;
    message_.clear();
    using model::AudioFormat;
    using model::TrackKind;
    if (id == QLatin1String("timeline.enlarge_waveform") || id == QLatin1String("timeline.reduce_waveform")) {
        // "To enlarge the size of the waveform plot image without enlarging its track" (p.678).
        return zoomWaveform(id == QLatin1String("timeline.enlarge_waveform") ? 1 : -1);
    }
    if (id == QLatin1String("timeline.add_filler_at_start")) return addFillerAtStart();
    if (id == QLatin1String("composer.load_filler")) {
        loadFiller();
        return true;
    }
    if (id == QLatin1String("composer.sync_point_overwrite")) {
        setSyncPointEditing(!options_.syncPointEditing);
        return true;
    }
    if (id == QLatin1String("timeline.show_sequence_map")) {
        showSequenceMap(!mapShown_);
        return true;
    }
    if (id == QLatin1String("timeline.find_black_hole")) return findGap(false);
    if (id == QLatin1String("timeline.find_flash_frame")) return findGap(true);
    if (id == QLatin1String("play.audio_loop_play")) {
        toggleAudioLoopPlay();
    } else if (id == QLatin1String("fx.fade_effect")) {
        // "The Fade Effect dialog box opens. Type the number of frames to fade up and fade down" (p.47).
        std::optional<std::pair<std::int64_t, std::int64_t>> frames;
        if (fadeAsk_) {
            frames = fadeAsk_();
        } else {
            QDialog dialog(this);
            dialog.setObjectName(QStringLiteral("fadeEffectDialog"));
            dialog.setWindowTitle(tr("Fade Effect"));
            auto* form = new QFormLayout(&dialog);
            auto* upBox = new QSpinBox(&dialog);
            auto* downBox = new QSpinBox(&dialog);
            for (QSpinBox* b : {upBox, downBox}) {
                b->setRange(0, 99999);
                b->setSuffix(tr(" frames"));
            }
            upBox->setObjectName(QStringLiteral("fadeUp"));
            downBox->setObjectName(QStringLiteral("fadeDown"));
            form->addRow(tr("Fade Up:"), upBox);
            form->addRow(tr("Fade Down:"), downBox);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addRow(buttons);
            if (dialog.exec() == QDialog::Accepted) frames = std::pair{std::int64_t{upBox->value()}, std::int64_t{downBox->value()}};
        }
        if (frames) fadeEffects(frames->first, frames->second);
        return true;
    } else if (id == QLatin1String("fx.remove_effect")) {
        removeEffectAtPosition();
        return true;
    } else if (id == QLatin1String("tools.titler")) {
        applyTitler();
        return true;
    }
    if (id == QLatin1String("edit.splice_in")) {
        edit(model::EditKind::SpliceIn);
    } else if (id == QLatin1String("edit.overwrite")) {
        edit(model::EditKind::Overwrite);
    } else if (id == QLatin1String("edit.fit_to_fill")) {
        edit(model::EditKind::Overwrite, true);
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
    } else if (id == QLatin1String("timeline.split_all_tracks") || id == QLatin1String("clip.split_all_tracks") ||
               id == QLatin1String("clip.audio.split_all_tracks")) {
        splitTracksToMono({});
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
    } else if (id == QLatin1String("fx.add_keyframe")) {
        addKeyframe();
    } else if (id == QLatin1String("edit.paste_audio_keyframes")) {
        pasteAudioKeyframes();
    } else if (id == QLatin1String("edit.copy") && copyKeyframes()) {
        // Selected keyframes were copied, not the segments (p.721).
    } else if (id == QLatin1String("edit.copy") || id == QLatin1String("edit.cut")) {
        if (selection_.isEmpty()) {
            if (id == QLatin1String("edit.copy")) copyToClipboard();
        } else {
            cutOrCopySelection(id == QLatin1String("edit.cut"));
        }
    } else if (id == QLatin1String("edit.paste")) {
        // After copying individual keyframes, Ctrl+V pastes them (p.722).
        if (keysCopiedLast_ && !keyClipboard_.isEmpty()) {
            pasteKeyframes(true, true);
        } else {
            pasteClipboard();
        }
    } else if (id == QLatin1String("edit.delete")) {
        if (deleteHook_ && deleteHook_()) {
            // deleted by the hook's owner
        } else if (!keySelection_.isEmpty()) {
            const QList<model::KeyRef> keys = keySelection_;
            if (const model::LoadedClip* rec = recordSequence()) {
                model::Sequence seq = model::Sequence::fromItem(rec->item);
                if (model::deleteKeyframes(seq, keys, automationParam()) > 0) {
                    keySelection_.clear();
                    commitSequence(tr("Delete Keyframes"), seq);
                }
            }
        } else if (effectMode_ && selection_.isEmpty()) {
            // "In Effect mode, select the effect and press Delete" (FX p.47).
            perform(QStringLiteral("fx.remove_effect"));
        } else {
            deleteSelection();
        }
    } else if (id == QLatin1String("clip.lock_tracks") || id == QLatin1String("clip.unlock_tracks")) {
        if (const model::LoadedClip* rec = recordSequence()) {
            setTracksLocked(patch_.selectedRecordTracks(model::Sequence::fromItem(rec->item)),
                            id == QLatin1String("clip.lock_tracks"));
        }
    } else if (id == QLatin1String("other.match_frame")) {
        matchFrame();
    } else if (id == QLatin1String("other.reverse_match_frame")) {
        reverseMatchFrame();
    } else if (id == QLatin1String("smart.lift_overwrite")) {
        setSegmentTool(model::EditKind::Overwrite, !liftTool_);
    } else if (id == QLatin1String("smart.extract_splice_in")) {
        setSegmentTool(model::EditKind::SpliceIn, !spliceTool_);
    } else if (id == QLatin1String("smart.overwrite_trim")) {
        setTrimTool(model::TrimKind::Overwrite, !overwriteTrimTool_);
    } else if (id == QLatin1String("smart.ripple_trim")) {
        setTrimTool(model::TrimKind::Ripple, !rippleTrimTool_);
    } else if (id == QLatin1String("fx.step_in")) {
        stepIn();
    } else if (id == QLatin1String("fx.step_out")) {
        stepOut();
    } else if (id == QLatin1String("fx.collapse")) {
        collapse();
    } else if (id == QLatin1String("fx.effect_mode")) {
        // A second click leaves it (ours; FX p.684 says only that it cannot close the Effect Editor in
        // Color Correction mode).
        setEffectMode(!effectMode_);
    } else if (id == QLatin1String("edit.source_record_mode")) {
        // Alt: between the single Record monitor and the Source/Record monitors (p.381).
        if (QApplication::keyboardModifiers() & Qt::AltModifier) {
            composer_.setSingleMonitor(!composer_.isSingleMonitor());
        } else {
            // "To enter Source/Record mode from another mode" (p.457); it clears trim selections (p.652).
            exitTrimMode();
            setEffectMode(false);
            syncTrimDisplay();
        }
    } else if (id == QLatin1String("trim.trim_mode")) {
        if (trimMode_) {
            exitTrimMode();
        } else if (QApplication::keyboardModifiers() & Qt::AltModifier && !lastRollers_.isEmpty()) {
            // Alt: the last roller configuration (p.651).
            setRollers(lastRollers_, trimKind_);
        } else {
            enterTrimMode(0);
        }
    } else if (id.startsWith(QLatin1String("trim.trim_"))) {
        // 10 frames, or 8 in 24-frame formats (p.656).
        const std::int64_t big = format_.nominalFps == 24 ? 8 : 10;
        const std::int64_t frames = id.endsWith(QLatin1String("10_frames")) ? big : 1;
        const bool back = id.contains(QLatin1String("backward"));
        if (!trimMode_) return fail(tr("Enter Trim mode first."));
        trimBy(back ? -frames : frames);
    } else if (id == QLatin1String("trim.play_loop")) {
        playTrimLoop();
    } else if (id == QLatin1String("play.edit_review")) {
        editReview();
    } else if (id == QLatin1String("trim.cycle_trim_sides")) {
        // A-side, B-side, both (p.651).
        if (!trimMode_ || rollers_.isEmpty()) return fail(tr("Enter Trim mode first."));
        const model::TrimSide now = rollers_.first().side;
        setTrimSide(now == model::TrimSide::Both ? model::TrimSide::A
                    : now == model::TrimSide::A  ? model::TrimSide::B
                                                 : model::TrimSide::Both);
    } else if (id == QLatin1String("trim.select_nearest_transition")) {
        enterTrimMode(0);
    } else if (id == QLatin1String("trim.select_previous_transition")) {
        enterTrimMode(-1);
    } else if (id == QLatin1String("trim.select_next_transition")) {
        enterTrimMode(1);
    } else if (id == QLatin1String("trim.extend")) {
        extend();
    } else if (id == QLatin1String("trim.slip_left") || id == QLatin1String("trim.slip_right")) {
        // In Source/Record mode: the segment under the position on the
        // selected tracks; Slip Left shows later material; Alt 10 (8)
        // frames (p.665).
        const model::LoadedClip* rec = recordSequence();
        if (!rec) return false;
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        const std::int64_t pos = composer_.state(MonitorId::Record).position();
        QList<model::SegmentRef> under;
        for (const QString& name : patch_.selectedRecordTracks(seq)) {
            std::int64_t start = 0;
            const int i = seq.track(name)->segmentAt(pos, &start);
            if (i >= 0 && !seq.track(name)->segments.at(i).isFiller()) under << model::SegmentRef{name, start};
        }
        if (under.isEmpty()) return fail(tr("There is no clip under the position indicator to slip."));
        const std::int64_t big = format_.nominalFps == 24 ? 8 : 10;
        const std::int64_t frames = (QApplication::keyboardModifiers() & Qt::AltModifier) ? big : 1;
        const QList<model::SegmentRef> keep = slip_;
        const bool keepSlide = slide_;
        slip_ = under;
        slide_ = false;
        slip(id == QLatin1String("trim.slip_left") ? frames : -frames);
        slip_ = keep;
        slide_ = keepSlide;
    } else if (id == QLatin1String("more.add_marker")) {
        // Alt: a spanned marker over the IN to OUT (p.414).
        addMarker(QStringLiteral("Red"), QApplication::keyboardModifiers() & Qt::AltModifier);
    } else if (id == QLatin1String("move.go_to_previous_marker") || id == QLatin1String("move.go_to_next_marker")) {
        const MonitorId m = composer_.activeMonitor();
        const model::MonitorState& s = composer_.state(m);
        if (s.isEmpty()) return fail(tr("There is nothing loaded."));
        const auto to = id == QLatin1String("move.go_to_next_marker") ? model::nextMarker(s.current()->item, s.position())
                                                                        : model::previousMarker(s.current()->item, s.position());
        if (!to) return fail(tr("There is no marker that way."));
        composer_.moveTo(m, *to);
    } else if (id == QLatin1String("edit.mark_markers")) {
        const MonitorId m = composer_.activeMonitor();
        model::MonitorState& s = composer_.state(m);
        const auto range = s.isEmpty() ? std::nullopt : model::markMarkers(s.current()->item, s.position());
        if (!range) return fail(tr("The position indicator must be between two markers."));
        const std::int64_t at = s.position();
        s.setPosition(range->first);
        s.markIn();
        s.setPosition(range->second);
        s.markOut();
        composer_.moveTo(m, at);
    }
    return true;
}

// Splice-in or Overwrite from the Source monitor into the record sequence (ch. 15): the plan
// comes from the two monitors' marks (three-point editing, model::planEdit) and the patch picks
// the tracks (model::applyEdit). Fit to Fill plans from all four marks instead and puts a motion
// effect on the new segments.
bool Timeline::edit(model::EditKind kind, bool fitToFill) {
    composer_.stop();
    const model::MonitorState& source = composer_.state(MonitorId::Source);
    if (source.isEmpty()) {
        fail(tr("Load a clip into the Source monitor first."));
        return false;
    }
    // A clip from the Source Browser goes into a bin as it is cut in (p.316).
    if (!composer_.commitSource()) {
        fail(tr("Open a bin for the clip first."));
        return false;
    }
    model::MonitorState& record = composer_.state(MonitorId::Record);
    if (record.isEmpty()) {
        // The first edit makes the sequence (p.463).
        const auto made = maker_ ? maker_() : std::nullopt;
        if (!made) {
            fail(tr("Open a bin for the new sequence first."));
            return false;
        }
        if (store_) store_(made->first, made->second, true);
        composer_.load(MonitorId::Record, made->first, made->second);
        composer_.setActiveMonitor(MonitorId::Source);
    }
    const model::LoadedClip* rec = record.current();
    const model::LoadedClip* src = source.current();
    if (!rec || !src) return false;
    if (rec->item.id == src->item.id) {
        fail(tr("A sequence cannot be edited into itself."));
        return false;
    }
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    const model::EditMarks sourceMarks{source.in(), source.out(), source.position(), source.duration()};
    const model::EditMarks recordMarks{record.in(), record.out(), record.position(), seq.duration()};
    double speed = 1.0;
    // Sync Point editing overwrites only (p.1159).
    const bool syncPoint = !fitToFill && kind == model::EditKind::Overwrite && options_.syncPointEditing;
    const model::EditPlan plan = fitToFill   ? model::planFitToFill(sourceMarks, recordMarks, &speed)
                                 : syncPoint ? model::planSyncPoint(sourceMarks, recordMarks)
                                             : model::planEdit(sourceMarks, recordMarks, options_.singleMarkEditing);
    if (!plan.ok()) {
        fail(plan.error);
        return false;
    }
    if (fitToFill && src->item.kind == model::BinItemKind::Sequence) {
        fail(tr("Fit to Fill needs a clip in the Source monitor."));
        return false;
    }
    // Multichannel tracks edit only onto tracks of their format (p.629).
    if (const QString mismatch = model::editFormatMismatch(seq, src->item, patch_); !mismatch.isEmpty()) {
        fail(mismatch);
        return false;
    }
    const QStringList edited = model::applyEdit(seq, kind, plan, src->binId, src->item, patch_, options_.autoCreateTracks);
    if (edited.isEmpty()) {
        fail(tr("Select a source track and a record track to edit."));
        return false;
    }
    if (fitToFill && speed != 1.0) {
        // The overwrite made one segment per edited track at the record IN.
        for (const QString& name : edited) {
            model::Track* track = seq.track(name);
            std::int64_t start = 0;
            const int at = track ? track->segmentAt(plan.recordStart, &start) : -1;
            if (at >= 0 && start == plan.recordStart) track->segments[at].effect = model::motionEffect(speed);
        }
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    // Copy Source Markers (p.1160): the markers within the source material go with it, onto the record
    // tracks their source tracks are patched to, cut short at the edit's end.
    if (options_.copySourceMarkers) {
        QList<model::Marker> markers = model::markersOf(after);
        const std::int64_t sourceEnd = plan.sourceStart + plan.length;
        bool copied = false;
        for (model::Marker m : model::markersOf(src->item)) {
            if (m.frame < plan.sourceStart || m.frame >= sourceEnd) continue;
            const QString to = patch_.recordFor(m.track);
            if (!edited.contains(to)) continue;
            m.id = model::newItemId();
            m.length = std::min(m.length, sourceEnd - m.frame);
            m.frame = plan.recordStart + (m.frame - plan.sourceStart);
            m.track = to;
            markers << m;
            copied = true;
        }
        if (copied) model::setMarkers(after, markers);
    }
    commit(syncPoint   ? tr("Sync Point Overwrite")
           : fitToFill ? tr("Fit to Fill")
           : kind == model::EditKind::SpliceIn ? tr("Splice-in")
                                               : tr("Overwrite"),
           before, after);
    // The Record monitor shows the end of the new edit (p.464); the
    // sequence's marks are used up.
    record.clearBoth();
    composer_.moveTo(MonitorId::Record, plan.recordStart + plan.length);
    if (options_.activateRecordAfterEdit) composer_.setActiveMonitor(MonitorId::Record);
    return true;
}

// Lift (extract false) or Extract between the record marks.
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

// Adds the next track of a kind (timeline.new.*) and selects it, as one undo step.
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
    // Default Sync Locks On (p.1220): new tracks too (ours).
    if (options_.defaultSyncLocks && kind != model::TrackKind::Data) seq.track(*name)->syncLocked = true;
    const model::BinItem before = rec->item;
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    patch_.setRecordSelected(*name, true);
    commit(tr("Add Track"), before, after);
}

// Every undoable change ends here: an undo step for the Record monitor's bin, then the new item
// is stored. restore() stores an item in its bin and updates the Composer's loaded copy; undo
// and redo use it directly.
void Timeline::commit(const QString& description, const model::BinItem& before, model::BinItem after) {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    const QString binId = rec ? rec->binId : QString();
    history_.push(model::EditStep{description, binId, before, after});
    restore(binId, after);
}

void Timeline::restore(const QString& binId, const model::BinItem& item) {
    // A nest's item is no bin's: it goes back into its segment (stepIn()).
    if (std::any_of(nests_.cbegin(), nests_.cend(), [&](const NestLevel& n) { return n.itemId == item.id; })) {
        storeNest(item);
        composer_.updateItem(item);
        emit sequenceEdited();
        return;
    }
    // Nor is an expanded nest's (expandNest()).
    if (expanded_ && item.id == expanded_->itemId) {
        storeExpanded(item);
        emit sequenceEdited();
        return;
    }
    // Nor those of nests the Timeline has left (an undo after Step Out or closing an expanded nest).
    if (storeDetached(binId, item)) {
        composer_.updateItem(item);
        emit sequenceEdited();
        return;
    }
    if (store_) store_(binId, item, false);
    composer_.updateItem(item);
    emit sequenceEdited();
}

void Timeline::storeNest(const model::BinItem& item) {
    for (qsizetype k = nests_.size() - 1; k >= 0; --k) {
        NestLevel& level = nests_[k];
        if (level.itemId != item.id) continue;
        model::Sequence parent = model::Sequence::fromItem(level.parent);
        model::Track* track = parent.track(level.track);
        const int i = track ? track->segmentAt(level.start) : -1;
        if (i < 0) return;
        model::setNest(track->segments[i], model::Sequence::fromItem(item));
        parent.storeIn(level.parent);
        level.parent.modified = QDateTime::currentDateTimeUtc();
        // Up a level: another nest's item, or the sequence in its bin.
        if (k > 0) {
            storeNest(level.parent);
        } else if (store_) {
            store_(level.binId, level.parent, false);
        }
        composer_.updateItem(level.parent);
        return;
    }
}

namespace {

// The parts of a nest or expanded nest item's id (Timeline::storeDetached): the parent's id, whether it is an
// expanded nest, the track and the segment's first frame. Nothing for another id.
struct DetachedId {
    QString parent;
    bool expanded = false;
    QString track;
    std::int64_t start = 0;
};
std::optional<DetachedId> detachedId(const QString& id) {
    const qsizetype nest = id.lastIndexOf(QLatin1String("/nest:"));
    const qsizetype expand = id.lastIndexOf(QLatin1String("/expand:"));
    const qsizetype at = std::max(nest, expand);
    if (at < 0) return std::nullopt;
    DetachedId d;
    d.parent = id.left(at);
    d.expanded = expand > nest;
    const QStringList parts = id.mid(at + 1).split(QLatin1Char(':'));
    if (parts.size() != 3) return std::nullopt;
    d.track = parts.at(1);
    bool ok = false;
    d.start = parts.at(2).toLongLong(&ok);
    if (!ok) return std::nullopt;
    return d;
}

} // namespace

std::optional<model::BinItem> Timeline::currentItem(const QString& binId, const QString& id) const {
    const auto d = detachedId(id);
    if (!d) return composer_.resolve(binId, id);
    auto parent = currentItem(binId, d->parent);
    if (!parent) return std::nullopt;
    const model::Sequence seq = model::Sequence::fromItem(*parent);
    model::BinItem item = *parent;
    item.id = id;
    if (d->expanded) {
        const auto expanded = model::expandNest(seq, d->track, d->start);
        if (!expanded) return std::nullopt;
        expanded->first.storeIn(item);
        return item;
    }
    const model::Track* t = seq.track(d->track);
    const int i = t ? t->segmentAt(d->start) : -1;
    if (i < 0 || !t->segments.at(i).hasNest()) return std::nullopt;
    model::nestOf(t->segments.at(i)).storeIn(item);
    return item;
}

bool Timeline::storeDetached(const QString& binId, const model::BinItem& item) {
    const auto d = detachedId(item.id);
    if (!d) return false;
    auto parent = currentItem(binId, d->parent);
    if (!parent) return true; // nowhere to go: dropped rather than stored as a bin item
    model::Sequence seq = model::Sequence::fromItem(*parent);
    if (d->expanded) {
        if (!pastExpansions_.contains(item.id)) return true;
        std::optional<model::NestExpansion> e = pastExpansions_.value(item.id);
        seq = model::collapseExpansion(model::Sequence::fromItem(item), e);
    } else {
        model::Track* t = seq.track(d->track);
        const int i = t ? t->segmentAt(d->start) : -1;
        if (i < 0) return true;
        model::setNest(t->segments[i], model::Sequence::fromItem(item));
    }
    seq.storeIn(*parent);
    parent->modified = QDateTime::currentDateTimeUtc();
    // On out: another nest, an expanded nest, or the bin.
    restore(binId, *parent);
    return true;
}

void Timeline::storeExpanded(const model::BinItem& item) {
    if (!expanded_) return;
    std::optional<model::NestExpansion> e = expanded_->expansion;
    const model::Sequence outer = model::collapseExpansion(model::Sequence::fromItem(item), e);
    outer.storeIn(expanded_->parent);
    expanded_->parent.modified = QDateTime::currentDateTimeUtc();
    if (store_) store_(expanded_->binId, expanded_->parent, false);
    composer_.updateItem(expanded_->parent);
    const auto again = e ? model::expandNest(outer, e->track, e->start) : std::nullopt;
    if (!again) {
        // The segment is gone: the sequence is shown again.
        const Expanded gone = *expanded_;
        expanded_.reset();
        const std::int64_t position = composer_.state(MonitorId::Record).position();
        composer_.load(MonitorId::Record, gone.binId, gone.parent);
        composer_.state(MonitorId::Record).remove(gone.itemId);
        composer_.moveTo(MonitorId::Record, position);
        sync();
        emit changed();
        return;
    }
    expanded_->expansion = again->second;
    pastExpansions_.insert(expanded_->itemId, again->second);
    model::BinItem shown = item;
    again->first.storeIn(shown);
    composer_.updateItem(shown);
}

std::optional<std::pair<std::int64_t, std::int64_t>> Timeline::expandedFrames() const {
    if (!expanded_) return std::nullopt;
    return std::pair{expanded_->expansion.start, expanded_->expansion.start + expanded_->expansion.length};
}

std::optional<model::Sequence> Timeline::collapsedSequence() const {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!expanded_ || !rec || rec->item.id != expanded_->itemId) return std::nullopt;
    std::optional<model::NestExpansion> e = expanded_->expansion;
    return model::collapseExpansion(model::Sequence::fromItem(rec->item), e);
}

bool Timeline::expandNest(const QString& track, std::int64_t frame) {
    message_.clear();
    if (expanded_) closeNest();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::Track* t = seq.track(track);
    std::int64_t start = 0;
    const int i = t && t->kind == model::TrackKind::Video ? t->segmentAt(frame, &start) : -1;
    if (i < 0 || !t->segments.at(i).effect) return fail(tr("There is no segment effect there to expand."));
    // "You can expand any segment effect in this way" (p.220).
    if (!t->segments.at(i).hasNest()) {
        changeRecordSequence(tr("Expand Nest"), [&](model::Sequence& s) {
            model::Track* x = s.track(track);
            return model::ensureNest(x->segments[x->segmentAt(start)]);
        });
        rec = composer_.state(MonitorId::Record).current();
        if (!rec) return false;
        seq = model::Sequence::fromItem(rec->item);
    }
    const auto expanded = model::expandNest(seq, track, start);
    if (!expanded) return fail(tr("The sequence has no room for the nested tracks."));
    model::BinItem item = rec->item;
    item.id = QStringLiteral("%1/expand:%2:%3").arg(rec->item.id, track).arg(start);
    expanded->first.storeIn(item);
    expanded_ = Expanded{rec->binId, rec->item, item.id, expanded->second};
    pastExpansions_.insert(item.id, expanded->second);
    const std::int64_t position = composer_.state(MonitorId::Record).position();
    composer_.load(MonitorId::Record, rec->binId, item);
    composer_.moveTo(MonitorId::Record, position);
    sync();
    emit changed();
    return true;
}

bool Timeline::closeNest() {
    if (!expanded_) return fail(tr("No nest is expanded."));
    const Expanded e = *expanded_;
    expanded_.reset();
    const std::int64_t position = composer_.state(MonitorId::Record).position();
    composer_.load(MonitorId::Record, e.binId, e.parent);
    composer_.state(MonitorId::Record).remove(e.itemId);
    composer_.moveTo(MonitorId::Record, position);
    sync();
    emit changed();
    return true;
}

bool Timeline::doubleClickTrack(const QString& track, std::int64_t frame) {
    if (showSource_) return false;
    // "Double-click the main track for the nested effect ... to close the nested tracks again" (p.221), or
    // "Double-click the effect's icon in the Timeline" (p.220).
    if (expanded_ && track == expanded_->expansion.track) return closeNest();
    const bool tools = isSegmentTool(model::EditKind::Overwrite) || isSegmentTool(model::EditKind::SpliceIn);
    if (!effectMode_ && !(tools && options_.doubleClickNesting)) return false;
    const model::Track* t = shown_.track(track);
    const int i = t ? t->segmentAt(frame) : -1;
    if (i < 0 || !t->segments.at(i).effect || t->kind != model::TrackKind::Video) return false;
    return expandNest(track, frame);
}

bool Timeline::stepIn() {
    // Step In works on the sequence itself: an expanded nest is closed first.
    if (expanded_) closeNest();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const std::int64_t position = composer_.state(MonitorId::Record).position();
    // The video tracks to look on, highest first.
    QStringList tracks;
    for (const QString& t : patch_.selectedRecordTracks(seq)) {
        if (t.startsWith(QLatin1Char('V'))) tracks << t;
    }
    if (tracks.isEmpty()) {
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Video) tracks << t.name();
        }
    }
    std::sort(tracks.begin(), tracks.end(), [](const QString& a, const QString& b) {
        return model::parseTrackName(a)->number > model::parseTrackName(b)->number;
    });
    QString name;
    std::int64_t start = 0;
    for (const QString& t : tracks) {
        std::int64_t at = 0;
        const model::Track* track = seq.track(t);
        const int i = track ? track->segmentAt(position, &at) : -1;
        if (i < 0 || !track->segments.at(i).effect) continue;
        name = t;
        start = at;
        break;
    }
    if (name.isEmpty()) return fail(tr("There is no segment effect to step into at the position."));
    if (!seq.track(name)->segments.at(seq.track(name)->segmentAt(start)).hasNest()) {
        const QString track = name;
        changeRecordSequence(tr("Step In"), [&](model::Sequence& s) {
            model::Track* t = s.track(track);
            return model::ensureNest(t->segments[t->segmentAt(start)]);
        });
        rec = composer_.state(MonitorId::Record).current();
        if (!rec) return false;
    }
    const model::Sequence current = model::Sequence::fromItem(rec->item);
    const model::Segment& segment = current.track(name)->segments.at(current.track(name)->segmentAt(start));
    model::BinItem nestItem = rec->item;
    nestItem.id = QStringLiteral("%1/nest:%2:%3").arg(rec->item.id, name).arg(start);
    model::nestOf(segment).storeIn(nestItem);
    NestLevel level{rec->binId, rec->item, name, start, segment.nestIn, nestItem.id};
    nests_ << level;
    composer_.load(MonitorId::Record, rec->binId, nestItem);
    composer_.moveTo(MonitorId::Record, segment.nestIn + position - start);
    sync();
    emit changed();
    return true;
}

bool Timeline::stepOut() {
    if (nests_.isEmpty()) return fail(tr("The Timeline is not inside a nest."));
    const NestLevel level = nests_.takeLast();
    const std::int64_t position = composer_.state(MonitorId::Record).position();
    composer_.load(MonitorId::Record, level.binId, level.parent);
    composer_.state(MonitorId::Record).remove(level.itemId);
    composer_.moveTo(MonitorId::Record, level.start + position - level.nestIn);
    sync();
    emit changed();
    return true;
}

bool Timeline::collapse() {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    QStringList tracks;
    std::int64_t from = 0;
    std::int64_t to = 0;
    if (!selection_.isEmpty()) {
        // "Select the segments you want to collapse" (p.227): their tracks, from the first to the end of the last.
        bool first = true;
        for (const model::SegmentRef& ref : selection_) {
            const auto segment = model::segmentFor(seq, ref);
            if (!segment || !ref.track.startsWith(QLatin1Char('V'))) continue;
            if (!tracks.contains(ref.track)) tracks << ref.track;
            from = first ? ref.start : std::min(from, ref.start);
            to = first ? ref.start + segment->length : std::max(to, ref.start + segment->length);
            first = false;
        }
    } else {
        const model::MonitorState& state = composer_.state(MonitorId::Record);
        if (!state.in() || !state.out()) return fail(tr("Mark an IN point and an OUT point around the area to collapse."));
        from = *state.in();
        to = *state.out() + 1;
        for (const QString& t : patch_.selectedRecordTracks(seq)) {
            if (t.startsWith(QLatin1Char('V'))) tracks << t;
        }
    }
    QString error;
    model::Sequence changed = seq;
    if (!model::collapseTracks(changed, tracks, from, to, &error)) return fail(error);
    selection_.clear();
    return commitSequence(tr("Collapse"), changed);
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

// EditKind::Overwrite stands for Lift/Overwrite and SpliceIn for Extract/Splice-in. Turning
// both off drops the selection.
void Timeline::disableSmartTools() {
    setSegmentTool(model::EditKind::Overwrite, false);
    setSegmentTool(model::EditKind::SpliceIn, false);
    setTrimTool(model::TrimKind::Overwrite, false);
    setTrimTool(model::TrimKind::Ripple, false);
}

void Timeline::setSegmentTool(model::EditKind kind, bool on) {
    (kind == model::EditKind::Overwrite ? liftTool_ : spliceTool_) = on;
    // Only One Segment Tool Can Be Enabled At A Time (p.1220).
    if (on && options_.onlyOneSegmentTool) (kind == model::EditKind::Overwrite ? spliceTool_ : liftTool_) = false;
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

// A click selects only that segment; Shift (toggle) adds or removes it.
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
    // selected (p.614). The colour choices first gather the colours of what is selected.
    const std::int64_t pos = composer_.state(MonitorId::Record).position();
    QSet<QString> sourceColors;
    QSet<QString> localColors;
    for (const model::SegmentRef& ref : selection_) {
        const auto seg = model::segmentFor(shown_, ref);
        if (!seg || seg->isFiller()) continue;
        if (const auto facts = clipFacts(*seg); facts && !facts->sourceColor.isEmpty()) sourceColors.insert(facts->sourceColor.toLower());
        if (!seg->localColor.isEmpty()) localColors.insert(seg->localColor.toLower());
    }
    auto byColor = [&](const model::Segment& seg) {
        const auto facts = clipFacts(seg);
        const QString source = facts ? facts->sourceColor.toLower() : QString();
        switch (where) {
        case SelectWhere::SameSourceColor: return !source.isEmpty() && sourceColors.contains(source);
        case SelectWhere::NoSourceColor: return source.isEmpty();
        case SelectWhere::Offline: return !facts || facts->offline;
        case SelectWhere::SameLocalColor: return !seg.localColor.isEmpty() && localColors.contains(seg.localColor.toLower());
        default: return false;
        }
    };
    QList<model::SegmentRef> picked;
    for (const model::Track& t : shown_.tracks()) {
        if (!patch_.isRecordSelected(t.name())) continue;
        std::int64_t at = 0;
        for (const model::Segment& seg : t.segments) {
            const model::SegmentRef ref{t.name(), at};
            const bool take = where == SelectWhere::Left      ? at + seg.length <= pos
                              : where == SelectWhere::Right   ? at >= pos
                              : where == SelectWhere::Reverse ? !selection_.contains(ref)
                                                              : !seg.isFiller() && byColor(seg);
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
    const QList<model::SegmentRef> moved = model::moveSegments(seq, selection_, frames, tracks, kind, copy, &error, options_.segmentDragSyncLocks);
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

// Deleting segments lifts them, or extracts them when the segment tool is Extract/Splice-in.
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

// Stores `material` as the Clipboard sequence (an item in no bin); `from` names the sequence it
// came from, for the Alt subclip name.
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

// A fresh copy of the Clipboard, with a new ID, into the Source monitor under `name`.
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

// Replace Edit (p.467): the record segment under the position on the first patched record track
// (or the record IN to OUT) is overwritten with source material lined up on the two positions
// (model::planReplace).
void Timeline::replaceEdit() {
    composer_.stop();
    const model::MonitorState& source = composer_.state(MonitorId::Source);
    if (source.isEmpty()) {
        fail(tr("Load a clip into the Source monitor first."));
        return;
    }
    // A clip from the Source Browser goes into a bin as it is cut in (p.316).
    if (!composer_.commitSource()) {
        fail(tr("Open a bin for the clip first."));
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
    // Without Show Marked Region, Replace Edit ignores the sequence's marks
    // (p.1218).
    const bool marks = options_.markedRegion;
    const model::EditMarks recordMarks{marks ? record.in() : std::nullopt, marks ? record.out() : std::nullopt,
                                       record.position(), seq.duration()};
    const model::EditPlan plan = model::planReplace(seq, pairs.first().second, sourceMarks, recordMarks);
    if (!plan.ok()) {
        fail(plan.error);
        return;
    }
    const model::LoadedClip* src = source.current();
    if (const QString mismatch = model::editFormatMismatch(seq, src->item, patch_); !mismatch.isEmpty()) {
        fail(mismatch);
        return;
    }
    if (model::applyEdit(seq, model::EditKind::Overwrite, plan, src->binId, src->item, patch_).isEmpty()) {
        fail(tr("The selected tracks are locked."));
        return;
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(tr("Replace Edit"), before, after);
    record.clearBoth();
    if (options_.activateRecordAfterEdit) composer_.setActiveMonitor(MonitorId::Record);
}

// Add Edit at the Record position: on the selected unlocked tracks, or with fillerOnly on every
// unlocked track that has filler there.
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

// The Source monitor's frame found in the Record monitor's sequence, on the selected tracks of both
// sides (p.433): the first place it is shown, then each later one at each click. Ours: after the
// last place it starts again at the first.
void Timeline::reverseMatchFrame() {
    const model::MonitorState& source = composer_.state(MonitorId::Source);
    const model::LoadedClip* src = source.current();
    model::MonitorState& record = composer_.state(MonitorId::Record);
    const model::LoadedClip* rec = record.current();
    if (!src || !rec || rec->item.kind != model::BinItemKind::Sequence) {
        fail(tr("Reverse Match Frame needs a clip in the Source monitor and a sequence in the Record monitor."));
        return;
    }
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    QString clipId = src->item.id;
    std::int64_t frame = source.position();
    QStringList sourceTracks;
    if (src->item.kind == model::BinItemKind::Sequence) {
        // A sequence in the Source monitor: its clip under the position.
        const auto match = model::matchFrame(model::Sequence::fromItem(src->item), {}, frame);
        if (!match) {
            fail(tr("There is no clip under the Source monitor's position indicator."));
            return;
        }
        clipId = match->segment.clipId;
        frame = match->frame;
        sourceTracks = {match->segment.sourceTrack};
    } else {
        for (const QString& t : patch_.sourceTracks()) {
            if (patch_.isSourceSelected(t)) sourceTracks << t;
        }
    }
    const QList<std::int64_t> found =
        model::reverseMatchFrames(seq, patch_.selectedRecordTracks(seq), clipId, sourceTracks, frame);
    if (found.isEmpty()) {
        fail(tr("The frame is not in the sequence on the selected tracks."));
        return;
    }
    std::int64_t at = found.first();
    if (reverseMatchAt_ && record.position() == *reverseMatchAt_) {
        const auto next = std::upper_bound(found.cbegin(), found.cend(), record.position());
        if (next != found.cend()) at = *next;
    }
    reverseMatchAt_ = at;
    composer_.moveTo(MonitorId::Record, at);
    composer_.setActiveMonitor(MonitorId::Record);
}

void Timeline::moveClips(int tracks) {
    // Move Clip Up / Down: the selected segments one track (p.613).
    if (selection_.isEmpty()) {
        fail(tr("Select the clips to move."));
        return;
    }
    moveSelection(0, tracks, segmentKind(), QApplication::keyboardModifiers() & Qt::AltModifier);
}

QMenu* Timeline::contextMenu(const QString& track, const std::optional<model::SegmentRef>& segment) {
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
        // Rename Track (p.635).
        QAction* rename = menu->addAction(tr("Rename Track"));
        rename->setEnabled(t && !showSource_);
        connect(rename, &QAction::triggered, this, [this, track] { renameTrack(track); });
        if (t && t->kind == model::TrackKind::Audio) {
            // Split Track to Mono (p.735), for multichannel tracks.
            QAction* split = menu->addAction(tr("Split Track to Mono"));
            split->setEnabled(t->format != model::AudioFormat::Mono);
            connect(split, &QAction::triggered, this, [this, track] { splitTracksToMono(track); });
            // Audio Track Effects [n] > Insert [a-e] > [effect] (p.791), and the tool (p.793).
            menu->addSeparator();
            QMenu* fx = menu->addMenu(tr("Audio Track Effects %1").arg(t->number));
            fx->setObjectName(QStringLiteral("audioTrackEffects"));
            fillTrackEffectsMenu(fx, track);
            QAction* tool = menu->addAction(tr("Audio Track Effect Tool"));
            connect(tool, &QAction::triggered, this, [this, track] { emit trackEffectToolRequested(track); });
        }
        return menu;
    }
    // In the Timeline (p.613, p.633, p.635).
    if (segment) addChangeSourceTrack(menu, *segment);
    QMenu* add = menu->addMenu(tr("New"));
    run(add, tr("Sequence"), QStringLiteral("timeline.new.sequence"));
    run(add, tr("Video Track"), QStringLiteral("timeline.new.video_track"));
    QMenu* audio = add->addMenu(tr("Audio Track"));
    run(audio, tr("Mono"), QStringLiteral("timeline.new.audio_track.mono"));
    run(audio, tr("Stereo"), QStringLiteral("timeline.new.audio_track.stereo"));
    run(audio, tr("5.1 Surround"), QStringLiteral("timeline.new.audio_track.5_1_surround"));
    run(audio, tr("7.1 Surround"), QStringLiteral("timeline.new.audio_track.7_1_surround"));
    run(add, tr("Data Track"), QStringLiteral("timeline.new.data_track"));
    // "Right-click in the Timeline, and select Split All Tracks to Mono" (p.637).
    run(menu, tr("Split All Tracks to Mono"), QStringLiteral("timeline.split_all_tracks"));
    menu->addSeparator();
    run(menu, tr("Lock Tracks"), QStringLiteral("clip.lock_tracks"));
    run(menu, tr("Unlock Tracks"), QStringLiteral("clip.unlock_tracks"));
    menu->addSeparator();
    QMenu* select = menu->addMenu(tr("Select"));
    connect(select->addAction(tr("To the Left")), &QAction::triggered, this, [this] { selectSegments(SelectWhere::Left); });
    connect(select->addAction(tr("To the Right")), &QAction::triggered, this, [this] { selectSegments(SelectWhere::Right); });
    const std::pair<QString, SelectWhere> byColor[] = {{tr("Clips with Same Source Clip Color"), SelectWhere::SameSourceColor},
                                                         {tr("Clips With No Source Clip Color"), SelectWhere::NoSourceColor},
                                                         {tr("Offline Clips"), SelectWhere::Offline},
                                                         {tr("Clips With Same Local Clip Color"), SelectWhere::SameLocalColor}};
    for (const auto& [text, where] : byColor) {
        connect(select->addAction(text), &QAction::triggered, this, [this, w = where] { selectSegments(w); });
    }
    connect(select->addAction(tr("Reverse")), &QAction::triggered, this, [this] { selectSegments(SelectWhere::Reverse); });
    // "Right click and select Add Timeline Clip Note" (p.483). Ours: near the end of the menu.
    QAction* note = menu->addAction(tr("Add Timeline Clip Note"));
    bool onClip = !selection_.isEmpty();
    if (!onClip && segment) {
        const auto seg = model::segmentFor(recordSequenceOf(composer_), *segment);
        onClip = seg && !seg->isFiller();
    }
    note->setEnabled(!showSource_ && onClip);
    connect(note, &QAction::triggered, this, [this, segment] { addClipNote(segment); });
    // "Right-click and choose Create Sequence Based on Selection" (p.614).
    QAction* fromSelection = menu->addAction(tr("Create Sequence Based on Selection"));
    fromSelection->setEnabled(!selection_.isEmpty() && !showSource_);
    connect(fromSelection, &QAction::triggered, this, &Timeline::createSequenceFromSelection);
    // "Right-click in the Timeline, and select Add Filler at Start" (p.462).
    run(menu, tr("Add Filler at Start"), QStringLiteral("timeline.add_filler_at_start"));
    return menu;
}

// Change Source Track (p.595; 2026.8 guide p.507; What's New 2026.8 p.7): the clip's audio
// tracks, the segment's own checked; for a subclip, More lists the master clip's audio tracks the
// subclip does not have ("audio tracks from the master clip can be accessed directly"). Only the
// segment changes, as one undo step; the clip in the bin does not.
void Timeline::addChangeSourceTrack(QMenu* menu, const model::SegmentRef& ref) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec || showsSource()) return;
    const auto seg = model::segmentFor(model::Sequence::fromItem(rec->item), ref);
    const auto kind = model::parseTrackName(ref.track);
    if (!seg || seg->isFiller() || !kind || kind->kind != model::TrackKind::Audio) return;
    const auto clip = composer_.resolve(seg->binId, seg->clipId);
    auto audioOf = [](const model::BinItem& item) {
        QStringList audio;
        for (const QString& t : model::sourceTracksOf(item)) {
            if (const auto r = model::parseTrackName(t); r && r->kind == model::TrackKind::Audio) audio << t;
        }
        return audio;
    };
    QMenu* change = menu->addMenu(tr("Change Source Track"));
    change->setObjectName(QStringLiteral("changeSourceTrack"));
    const QStringList own = clip ? audioOf(*clip) : QStringList{};
    for (const QString& t : own) {
        QAction* a = change->addAction(t);
        a->setCheckable(true);
        a->setChecked(t == seg->sourceTrack);
        connect(a, &QAction::triggered, this, [this, ref, t] {
            changeRecordSequence(tr("Change Source Track"),
                                 [&](model::Sequence& s) { return model::changeSourceTrack(s, ref, t); });
        });
    }
    // A subclip's master clip, by the MobID of its Clip source.
    std::optional<std::pair<QString, model::BinItem>> master;
    std::int64_t offset = 0;
    if (clip && clip->kind == model::BinItemKind::Subclip) {
        for (const model::ClipSource& from : clip->sources) {
            if (from.kind != model::ClipSource::Kind::Clip) continue;
            master = composer_.resolveMob(from.mob);
            offset = from.offset;
            break;
        }
    }
    QStringList more;
    if (master) {
        for (const QString& t : audioOf(master->second)) {
            if (!own.contains(t)) more << t;
        }
    }
    if (clip && clip->kind == model::BinItemKind::Subclip) {
        QMenu* m = change->addMenu(tr("More"));
        m->setObjectName(QStringLiteral("changeSourceTrackMore"));
        m->setEnabled(!more.isEmpty());
        for (const QString& t : more) {
            connect(m->addAction(t), &QAction::triggered, this, [this, ref, t, master, offset] {
                changeRecordSequence(tr("Change Source Track"), [&](model::Sequence& s) {
                    return model::changeSourceTrack(s, ref, master->second, master->first, offset, t);
                });
            });
        }
    }
    change->setEnabled(!own.isEmpty() || !more.isEmpty());
    menu->addSeparator();
}

model::SourceLength Timeline::sourceLength() const {
    return [this](const model::Segment& seg) -> std::optional<std::int64_t> {
        const auto item = composer_.resolve(seg.binId, seg.clipId);
        if (!item) return std::nullopt;
        return item->durationFrames;
    };
}

bool Timeline::isTrimTool(model::TrimKind kind) const {
    return kind == model::TrimKind::Overwrite ? overwriteTrimTool_ : rippleTrimTool_;
}

void Timeline::setTrimTool(model::TrimKind kind, bool on) {
    (kind == model::TrimKind::Overwrite ? overwriteTrimTool_ : rippleTrimTool_) = on;
    if (QToolButton* b = toolButton(QStringLiteral("smart.overwrite_trim"))) b->setChecked(overwriteTrimTool_);
    if (QToolButton* b = toolButton(QStringLiteral("smart.ripple_trim"))) b->setChecked(rippleTrimTool_);
    view_->update();
}

// Picks the edit nearest the position (direction 0), or the nearest before (-1) or after (1)
// it, among the selected unlocked tracks' trim points; the last trim point, the end of the
// track, is not one.
bool Timeline::enterTrimMode(int direction) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    composer_.stop();
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const std::int64_t pos = composer_.state(MonitorId::Record).position();
    // Edits with a segment on both sides, on the selected unlocked tracks.
    QHash<QString, QList<std::int64_t>> edits;
    std::optional<std::int64_t> best;
    for (const QString& name : patch_.selectedRecordTracks(seq)) {
        if (seq.track(name)->locked) continue;
        QList<std::int64_t> points = model::trimPoints(seq, name);
        if (!points.isEmpty()) points.removeLast();
        edits.insert(name, points);
        for (std::int64_t e : points) {
            const bool fits = direction < 0 ? e < pos : direction > 0 ? e > pos : true;
            if (!fits) continue;
            // Nearest in every case (p.655): direction only filters the side. On a tie the
            // edit found first wins, the earlier one on a track.
            if (!best || std::abs(e - pos) < std::abs(*best - pos)) best = e;
        }
    }
    if (!best) return fail(tr("There is no transition to trim on the selected tracks."));
    // Every selected track with an edit there: a straight cut (p.655).
    QList<model::TrimRoller> rollers;
    for (auto it = edits.cbegin(); it != edits.cend(); ++it) {
        if (it.value().contains(*best)) rollers << model::TrimRoller{it.key(), *best, model::TrimSide::Both};
    }
    std::sort(rollers.begin(), rollers.end(), [&](const model::TrimRoller& a, const model::TrimRoller& b) {
        return seq.trackNames().indexOf(a.track) < seq.trackNames().indexOf(b.track);
    });
    setRollers(rollers, trimKind_);
    composer_.moveTo(MonitorId::Record, *best);
    // Trim settings, Auto focus when entering Trim mode (p.1223): with no
    // trim tool on, the Timeline zooms in on the transition.
    if (options_.trimAutoFocus && !rippleTrimTool_ && !overwriteTrimTool_ && !focusBack_) {
        toggleFocus();
        trimFocused_ = true;
    }
    return true;
}

void Timeline::setEffectMode(bool on) {
    if (on == effectMode_) {
        syncTrimDisplay();
        return;
    }
    if (on) {
        composer_.stop();
        exitTrimMode();
    }
    effectMode_ = on;
    syncTrimDisplay();
    emit effectModeChanged(on);
}

void Timeline::exitTrimMode() {
    if (!trimMode_) return;
    if (trimFocused_) {
        trimFocused_ = false;
        if (focusBack_) toggleFocus();
    }
    if (!rollers_.isEmpty()) lastRollers_ = rollers_;
    goToTrim_ = false;
    trimMode_ = false;
    rollers_.clear();
    slip_.clear();
    trimA_ = 0;
    trimB_ = 0;
    syncTrimDisplay();
}

// Every way into Trim mode with rollers ends here; the A and B counters restart from 0.
void Timeline::setRollers(const QList<model::TrimRoller>& rollers, model::TrimKind kind) {
    trimMode_ = true;
    rollers_ = rollers;
    trimKind_ = kind;
    slip_.clear();
    selection_.clear();
    trimA_ = 0;
    trimB_ = 0;
    syncTrimDisplay();
}

void Timeline::clickTransition(const model::TrimRoller& roller, model::TrimKind kind, bool add) {
    // "When you <shift> select a transition on a track with a sync roller, the trim is changed to the
    // selected transition instead of adding another roller" (p.656).
    const QList<model::TrimRoller> sync = syncRollers();
    const bool onSync = std::any_of(sync.begin(), sync.end(), [&](const model::TrimRoller& r) { return r.track == roller.track; });
    if (!trimMode_ || !add || onSync) {
        setRollers({roller}, kind);
        return;
    }
    // Shift-click adds or removes; A- and B-sides together trim in two
    // directions (p.652).
    QList<model::TrimRoller> rollers = rollers_;
    const auto same = [&](const model::TrimRoller& r) { return r.track == roller.track && r.frame == roller.frame; };
    if (std::any_of(rollers.begin(), rollers.end(), same)) {
        rollers.removeIf(same);
    } else {
        rollers << roller;
    }
    rollers_ = rollers;
    slip_.clear();
    syncTrimDisplay();
}

void Timeline::setTrimSide(model::TrimSide side) {
    if (!trimMode_) return;
    for (model::TrimRoller& r : rollers_) r.side = side;
    syncTrimDisplay();
}

bool Timeline::trimBy(std::int64_t frames) {
    if (trimMode_ && !slip_.isEmpty()) {
        // Forward (or dragging right) slides the segments right, or slips
        // in earlier material, like Slip Right (p.665).
        slip(slide_ ? frames : -frames);
        return true;
    }
    if (!trimMode_ || rollers_.isEmpty()) return fail(tr("Select a transition to trim."));
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    std::int64_t applied = 0;
    const QList<model::TrimRoller> moved = model::trim(seq, rollers_, trimKind_, frames, sourceLength(), &applied);
    if (applied == 0) {
        // The trim stops at the end of the material (p.659).
        QApplication::beep();
        return fail(tr("The trim cannot go further."));
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(tr("Trim"), before, after);
    rollers_ = moved;
    bool a = false;
    bool b = false;
    for (const model::TrimRoller& r : rollers_) {
        a = a || r.side != model::TrimSide::B;
        b = b || r.side != model::TrimSide::A;
    }
    if (a) trimA_ += applied;
    if (b) trimB_ += applied;
    if (frames != applied) {
        QApplication::beep();
        fail(tr("The trim stopped at the end of the material."));
    }
    // During the Play Loop the next pass shows the trim (p.660); the position is the loop's.
    if (!trimLooping_) composer_.moveTo(MonitorId::Record, rollers_.first().frame);
    syncTrimDisplay();
    return true;
}

QList<model::TrimRoller> Timeline::syncRollers() const {
    QList<model::TrimRoller> out;
    if (!options_.syncRollers || !trimMode_ || rollers_.isEmpty() || trimKind_ != model::TrimKind::Ripple) return out;
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return out;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    bool locked = false;
    for (const model::TrimRoller& r : rollers_) {
        if (r.side == model::TrimSide::Both) return out; // single-roller trims only
        const model::Track* t = seq.track(r.track);
        locked = locked || (t && t->syncLocked);
    }
    if (!locked) return out;
    const model::TrimRoller& first = rollers_.first();
    for (const QString& name : seq.trackNames()) {
        const model::Track* t = seq.track(name);
        if (!t || !t->syncLocked) continue;
        if (std::any_of(rollers_.begin(), rollers_.end(), [&](const model::TrimRoller& r) { return r.track == name; })) continue;
        out << model::TrimRoller{name, first.frame, first.side};
    }
    return out;
}

void Timeline::showSegmentDrag(std::optional<std::int64_t> frames) {
    Composer::DragDisplay d;
    const model::LoadedClip* rec = recordSequence();
    if (frames && rec && !selection_.isEmpty() && options_.segmentDragDisplay != Options::DragDisplay::None) {
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        std::int64_t head = std::numeric_limits<std::int64_t>::max();
        std::int64_t end = 0;
        for (const model::SegmentRef& ref : selection_) {
            const auto seg = model::segmentFor(seq, ref);
            if (!seg) continue;
            head = std::min(head, ref.start);
            end = std::max(end, ref.start + seg->length);
        }
        if (end > head) {
            const std::int64_t last = std::max<std::int64_t>(composer_.state(MonitorId::Record).duration() - 1, 0);
            const std::int64_t to = head + *frames;
            d.on = true;
            d.fourFrame = options_.segmentDragDisplay == Options::DragDisplay::FourFrame;
            d.offset = *frames;
            if (d.fourFrame) {
                // "The two outer frames indicate ending and beginning frames of clips before and after
                // the segment. The two inner frames represent the start and end frames of the segment"
                // (p.618). Ours: the outer frames are those of the sequence as it is, around where the
                // segment would go.
                d.frames[0] = std::clamp<std::int64_t>(to - 1, 0, last);
                d.frames[1] = head;
                d.frames[2] = end - 1;
                d.frames[3] = std::clamp<std::int64_t>(to + (end - head), 0, last);
            } else {
                // Position Bar: "shows the frame at the blue position bar when dragging a segment"
                // (p.1219). Ours: the frame where the segment's head would go.
                d.frames[0] = std::clamp<std::int64_t>(to, 0, last);
            }
        }
    }
    composer_.setDragDisplay(d);
}

std::int64_t Timeline::playLoopCut() const {
    return rollers_.isEmpty() ? 0 : rollers_.first().frame;
}

bool Timeline::playTrimLoop() {
    if (trimLooping_) {
        composer_.stop();
        return true;
    }
    if (!trimMode_ || rollers_.isEmpty()) {
        // From Source/Record mode: "The action takes you to the last trimmed transition" (p.656).
        if (!lastRollers_.isEmpty()) {
            setRollers(lastRollers_, trimKind_);
        } else if (!enterTrimMode(0)) {
            return false;
        }
    }
    if (!recordSequence() || rollers_.isEmpty()) return fail(tr("Select a transition to trim."));
    const auto fps = static_cast<std::int64_t>(std::max(format_.nominalFps, 1));
    // "This loop begins at a preroll point before the transition and ends at a postroll point" (p.657).
    Composer::LoopResolver range = [this, fps]() -> std::optional<Composer::LoopRange> {
        if (!trimMode_ || rollers_.isEmpty()) return std::nullopt;
        const std::int64_t cut = playLoopCut();
        const std::int64_t last = std::max<std::int64_t>(composer_.state(MonitorId::Record).duration() - 1, 0);
        const std::int64_t start = std::clamp<std::int64_t>(cut - options_.trimPreroll * fps, 0, last);
        const std::int64_t end = std::clamp<std::int64_t>(cut + options_.trimPostroll * fps - 1, start, last);
        return Composer::LoopRange{start, end};
    };
    trimLooping_ = true;
    composer_.playLoop(MonitorId::Record, std::move(range), options_.trimIntermission * 1000);
    if (!composer_.isLooping()) trimLooping_ = false;
    return trimLooping_;
}

bool Timeline::editReview() {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return fail(tr("Load a sequence first."));
    const auto fps = static_cast<std::int64_t>(std::max(format_.nominalFps, 1));
    std::optional<std::int64_t> cut;
    if (trimMode_ && !rollers_.isEmpty()) {
        // "Using the Edit Review command causes Media Composer to deselect trim rollers" (p.657).
        cut = playLoopCut();
        exitTrimMode();
    } else {
        // "before the first set of edits that line up on all the selected tracks" (p.658), at or
        // before the position.
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        const std::int64_t pos = composer_.state(MonitorId::Record).position();
        std::optional<QSet<std::int64_t>> common;
        for (const QString& name : patch_.selectedRecordTracks(seq)) {
            QList<std::int64_t> points = model::trimPoints(seq, name);
            if (!points.isEmpty()) points.removeLast();
            if (points.isEmpty()) continue; // an empty track has no edit to line up
            const QSet<std::int64_t> set(points.cbegin(), points.cend());
            common = common ? common->intersect(set) : set;
        }
        if (common) {
            for (std::int64_t e : *common) {
                if (e <= pos && (!cut || e > *cut)) cut = e;
            }
        }
    }
    if (!cut) return fail(tr("There is no transition to review on the selected tracks."));
    composer_.stop();
    composer_.setActiveMonitor(MonitorId::Record);
    composer_.moveTo(MonitorId::Record, std::max<std::int64_t>(*cut - options_.trimPreroll * fps, 0));
    composer_.play(1);
    return true;
}

void Timeline::setSlip(const QList<model::SegmentRef>& segments, bool slide) {
    trimMode_ = !segments.isEmpty();
    rollers_.clear();
    slip_ = segments;
    slide_ = slide;
    selection_.clear();
    trimA_ = 0;
    trimB_ = 0;
    syncTrimDisplay();
}

void Timeline::slip(std::int64_t frames) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec || slip_.isEmpty()) return;
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    QList<model::SegmentRef> segments = slip_;
    const std::int64_t applied = slide_ ? model::slideSegments(seq, segments, frames, sourceLength())
                                        : model::slipSegments(seq, segments, frames, sourceLength());
    if (applied == 0) {
        QApplication::beep();
        fail(tr("The slip cannot go further."));
        return;
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(slide_ ? tr("Slide") : tr("Slip"), before, after);
    if (trimMode_) slip_ = segments;
    trimA_ += applied;
    trimB_ += applied;
    syncTrimDisplay();
}

void Timeline::extend() {
    // The edit nearest the mark moves to it: an IN pulls the next edit back,
    // an OUT pushes the previous one forward (p.661).
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    if (!record.in() && !record.out()) {
        fail(tr("Mark an IN or an OUT to extend the edit to."));
        return;
    }
    const bool toIn = record.in().has_value();
    const model::BinItem before = rec->item;
    model::Sequence seq = model::Sequence::fromItem(before);
    if (model::extendEdits(seq, patch_.selectedRecordTracks(seq), toIn ? *record.in() : *record.out(), toIn,
                           sourceLength())
            .isEmpty()) {
        fail(tr("There is no edit to extend to the mark."));
        return;
    }
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(tr("Extend"), before, after);
}

// Tells the Composer what the trim counters show (frames trimmed on each side, and which sides
// are active) and updates the Trim Mode button and the view.
void Timeline::syncTrimDisplay() {
    Composer::TrimDisplay d;
    d.on = trimMode_;
    d.aFrames = trimA_;
    d.bFrames = trimB_;
    for (const model::TrimRoller& r : rollers_) {
        d.aActive = d.aActive || r.side != model::TrimSide::B;
        d.bActive = d.bActive || r.side != model::TrimSide::A;
    }
    if (!slip_.isEmpty()) d.aActive = d.bActive = true;
    // The outgoing and incoming frames of the transition (p.648, p.655).
    if (!rollers_.isEmpty()) {
        d.bFrame = rollers_.first().frame;
        d.aFrame = std::max<std::int64_t>(d.bFrame - 1, 0);
    } else {
        d.aFrame = d.bFrame = composer_.state(MonitorId::Record).position();
    }
    d.small = options_.smallTrim == Options::SmallTrim::Always || (options_.smallTrim == Options::SmallTrim::GoTo && goToTrim_);
    d.dualImage = options_.dualImagePlay;
    composer_.setTrimDisplay(d);
    // Trim mode and Effect mode exclude each other.
    if (trimMode_ && effectMode_) {
        effectMode_ = false;
        emit effectModeChanged(false);
    }
    if (QToolButton* b = toolButton(QStringLiteral("trim.trim_mode"))) b->setChecked(trimMode_);
    if (QToolButton* b = toolButton(QStringLiteral("fx.effect_mode"))) b->setChecked(effectMode_);
    if (QToolButton* b = toolButton(QStringLiteral("edit.source_record_mode"))) b->setChecked(!trimMode_ && !effectMode_);
    view_->update();
    emit changed();
}

void Timeline::composerPerformed(const QString& id) {
    if (!trimMode_) return;
    // J-K-L Trim: J and L play the trim; stopping completes it (p.658-659).
    if ((id == QLatin1String("play.play_forward") || id == QLatin1String("play.play_reverse")) && options_.jklTrim &&
        !rollers_.isEmpty() && composer_.isPlaying() && composer_.activeMonitor() == MonitorId::Record) {
        jklTrimming_ = true;
        return;
    }
    // A frame step leaves Trim mode (p.651); Go to Previous/Next Event
    // selects the transition there (p.655).
    if (id.startsWith(QLatin1String("move.step_"))) {
        exitTrimMode();
    } else if (id == QLatin1String("move.go_to_previous_event") || id == QLatin1String("move.go_to_next_event")) {
        // "Go to Transition" uses Small Trim mode: "Media Composer enters Small Trim mode only when you
        // click the Go to Previous or Go to Next button" (p.1223).
        goToTrim_ = true;
        enterTrimMode(0);
        syncTrimDisplay();
    }
}

// `change` edits a copy of the monitor's item and returns false to leave it unchanged.
bool Timeline::changeItem(MonitorId monitor, const QString& description,
                          const std::function<bool(model::BinItem&)>& change) {
    const model::LoadedClip* loaded = composer_.state(monitor).current();
    if (!loaded) return fail(tr("There is nothing loaded."));
    const model::BinItem before = loaded->item;
    model::BinItem after = before;
    if (!change(after)) return false;
    after.modified = QDateTime::currentDateTimeUtc();
    if (monitor == MonitorId::Record && after.kind == model::BinItemKind::Sequence) {
        commit(description, before, after);
    } else {
        restore(loaded->binId, after);
    }
    return true;
}

bool Timeline::addMarker(const QString& color, bool spanned) {
    // On the active monitor's frame and its first selected track (p.413).
    const MonitorId m = composer_.activeMonitor();
    const model::MonitorState& s = composer_.state(m);
    const model::LoadedClip* loaded = s.current();
    if (!loaded) return fail(tr("Load a clip or sequence first."));
    model::Marker marker;
    marker.color = color;
    marker.user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
    marker.frame = std::min(s.position(), std::max<std::int64_t>(s.duration() - 1, 0));
    QStringList tracks;
    if (m == MonitorId::Record) {
        tracks = patch_.selectedRecordTracks(model::Sequence::fromItem(loaded->item));
    } else {
        for (const QString& t : patch_.sourceTracks()) {
            if (patch_.isSourceSelected(t)) tracks << t;
        }
    }
    marker.track = tracks.value(0, QStringLiteral("V1"));
    if (spanned) {
        // Between the IN and OUT; on TC1 in a sequence (p.414-415).
        if (!s.in() || !s.out()) return fail(tr("Mark an IN and an OUT for a spanned marker."));
        marker.frame = *s.in();
        marker.length = *s.out() - *s.in() + 1;
        if (loaded->item.kind == model::BinItemKind::Sequence) marker.track = QStringLiteral("TC1");
    }
    if (markerDialog_) {
        MarkerDialog dialog(marker, trackChoices(loaded->item), this);
        if (dialog.exec() != QDialog::Accepted) return false;
        marker = dialog.marker();
    }
    QString error;
    const bool ok = changeItem(m, tr("Add Marker"), [&](model::BinItem& item) {
        if (model::addMarker(item, marker)) return true;
        error = tr("Spanned markers cannot overlap.");
        return false;
    });
    if (!ok && !error.isEmpty()) return fail(error);
    return ok;
}

bool Timeline::changeMarker(MonitorId monitor, const model::Marker& marker) {
    return changeItem(monitor, tr("Edit Marker"), [&](model::BinItem& item) { return model::updateMarker(item, marker); });
}

bool Timeline::deleteMarker(MonitorId monitor, const QString& id) {
    return changeItem(monitor, tr("Delete Marker"), [&](model::BinItem& item) { return model::removeMarker(item, id); });
}

void Timeline::keyPressEvent(QKeyEvent* event) {
    // Escape leaves Trim mode and slip (p.664).
    if (event->key() == Qt::Key_Escape && trimMode_) {
        exitTrimMode();
        return;
    }
    // The Delete key: the keyframe under the pointer (p.717), else selected keyframes, segments or
    // tracks (p.621, p.636).
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (const auto key = view_->keyframeAt(view_->mapFromGlobal(QCursor::pos()))) {
            deleteKeyframeAt(*key);
            return;
        }
        perform(QStringLiteral("edit.delete"));
        return;
    }
    // Ctrl+Shift+arrows nudge the selected keyframes: 1 dB up or down, a frame left or right (p.723).
    if (!keySelection_.isEmpty() && (event->modifiers() & Qt::ControlModifier) && (event->modifiers() & Qt::ShiftModifier)) {
        const int key = event->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right) {
            nudgeKeyframes(key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0,
                           key == Qt::Key_Up ? 1 : key == Qt::Key_Down ? -1 : 0);
            return;
        }
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

// ---------------------------------------------------------------- effects

void TimelineView::dragEnterEvent(QDragEnterEvent* event) {
    if ((effectFromMime(event->mimeData()) || timeline_.droppedEq(event->mimeData()) || timeline_.droppedTemplate(event->mimeData())) &&
        !timeline_.showsSource()) {
        event->acceptProposedAction();
        return;
    }
    // A clip from a bin, with Enable edit from bin (p.624).
    if (timeline_.options().editFromBin && !timeline_.showsSource() && timeline_.droppedClip(event->mimeData())) {
        event->acceptProposedAction();
    }
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent* event) {
    binDrop_.reset();
    update();
    QWidget::dragLeaveEvent(event);
}

std::int64_t TimelineView::binDropFrame(int x) const {
    std::int64_t frame = std::max<std::int64_t>(frameAt(x), 0);
    if (!timeline_.options().segmentDragSnap) return frame;
    // "clips snap to an existing transition endpoint or to the position bar" (p.1220). Ours: within
    // kSnapPixels.
    const MonitorId monitor = timeline_.shownMonitor();
    QList<std::int64_t> points = timeline_.composer().events(monitor);
    points << timeline_.composer().state(monitor).position();
    constexpr double kSnapPixels = 12;
    double best = kSnapPixels;
    for (std::int64_t e : points) {
        const double d = std::abs(xFor(e) - x);
        if (d <= best) {
            best = d;
            frame = e;
        }
    }
    return frame;
}

void TimelineView::dragMoveEvent(QDragMoveEvent* event) {
    const auto row = rowAt(event->position().toPoint());
    // An effect template goes on a video track (FX Guide p.53).
    if (timeline_.droppedTemplate(event->mimeData())) {
        if (row && row->exists && row->track.startsWith(QLatin1Char('V')) && !timeline_.showsSource()) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
        return;
    }
    if (!effectFromMime(event->mimeData()) && !timeline_.droppedEq(event->mimeData())) {
        // "As you drag, a white outline of the clip indicates the segment position" (p.624).
        const auto clip = timeline_.options().editFromBin && !timeline_.showsSource()
                              ? timeline_.droppedClip(event->mimeData())
                              : std::nullopt;
        if (!clip) {
            event->ignore();
            return;
        }
        binDrop_ = BinDrop{binDropFrame(event->position().toPoint().x()), std::max<std::int64_t>(clip->second.durationFrames, 1)};
        update();
        event->acceptProposedAction();
        return;
    }
    // An EQ goes on an audio segment (p.740).
    if (timeline_.droppedEq(event->mimeData())) {
        if (row && row->exists && row->track.startsWith(QLatin1Char('A')) && !timeline_.showsSource()) {
            event->acceptProposedAction();
        } else {
            event->ignore();
        }
        return;
    }
    const auto effect = effectFromMime(event->mimeData());
    // Video effects go on video tracks; Audio Track Effects on audio tracks (p.792).
    const QChar kind = effect && !effect->audioEffect.isEmpty() ? QLatin1Char('A') : QLatin1Char('V');
    const bool fits = row && row->exists && row->track.startsWith(kind);
    if (fits && effect && !timeline_.showsSource()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TimelineView::dropEvent(QDropEvent* event) {
    const auto effect = effectFromMime(event->mimeData());
    const QPoint pos = event->position().toPoint();
    const auto row = rowAt(pos);
    binDrop_.reset();
    update();
    if (const auto t = timeline_.droppedTemplate(event->mimeData())) {
        if (!row || !row->exists || timeline_.showsSource()) return;
        std::optional<std::int64_t> cut;
        if (const auto roller = transitionAt(pos)) cut = roller->frame;
        if (timeline_.dropTemplate(row->track, frameAt(pos.x()), *t, cut)) event->acceptProposedAction();
        return;
    }
    if (!effect && !timeline_.droppedEq(event->mimeData())) {
        // A bin clip, edited in with the segment tool's edit (p.624).
        const auto clip = timeline_.options().editFromBin && !timeline_.showsSource()
                              ? timeline_.droppedClip(event->mimeData())
                              : std::nullopt;
        if (clip && timeline_.editClip(timeline_.binEditKind(), clip->first, clip->second, binDropFrame(pos.x()))) {
            event->acceptProposedAction();
        }
        return;
    }
    if (const auto eq = timeline_.droppedEq(event->mimeData())) {
        if (row && row->exists && !timeline_.showsSource() && timeline_.dropEq(row->track, frameAt(pos.x()), *eq)) {
            event->acceptProposedAction();
        }
        return;
    }
    if (!effect || !row || !row->exists || timeline_.showsSource()) return;
    if (!effect->audioEffect.isEmpty()) {
        if (timeline_.dropTrackEffect(row->track, effect->audioEffect)) event->acceptProposedAction();
        return;
    }
    // Near a cut, a transition effect goes on it; elsewhere on the
    // segment (p.44).
    std::optional<std::int64_t> cut;
    if (const auto roller = transitionAt(pos)) cut = roller->frame;
    if (timeline_.dropEffect(row->track, frameAt(pos.x()), *effect, cut)) event->acceptProposedAction();
}

QColor Timeline::backgroundColor() const {
    const QColor base(kBackground);
    const QColor& c = options_.background;
    if (!c.isValid()) return base;
    if (!options_.blendBackground) return c;
    // "blended with the current skin's background gray for a softer look" (p.1202): half and half.
    return QColor((c.red() + base.red()) / 2, (c.green() + base.green()) / 2, (c.blue() + base.blue()) / 2);
}

model::EditKind Timeline::binEditKind() const {
    // "If you do not select a segment tool, or if you select both tools, Media Composer defaults to the
    // segment tool specified in the Timeline Settings dialog box" (p.624).
    if (liftTool_ != spliceTool_) return liftTool_ ? model::EditKind::Overwrite : model::EditKind::SpliceIn;
    return options_.defaultSegmentTool;
}

std::optional<std::pair<QString, model::BinItem>> Timeline::droppedClip(const QMimeData* mime) const {
    if (!mime || !mime->hasFormat(binItemsMime())) return std::nullopt;
    const QStringList lines = QString::fromUtf8(mime->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    // "You can edit only one clip at a time" (p.624).
    if (lines.size() != 2) return std::nullopt;
    const auto item = composer_.resolve(lines.at(0), lines.at(1));
    if (!item) return std::nullopt;
    switch (item->kind) {
    case model::BinItemKind::MasterClip:
    case model::BinItemKind::Subclip:
    case model::BinItemKind::AudioClip:
    case model::BinItemKind::Group:
    case model::BinItemKind::Sequence:
        break;
    default:
        return std::nullopt;
    }
    if (model::eqTemplateOf(*item)) return std::nullopt;
    return std::pair{lines.at(0), *item};
}

bool Timeline::editClip(model::EditKind kind, const QString& binId, const model::BinItem& item,
                        std::optional<std::int64_t> recordAt) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (rec && rec->item.id == item.id) {
        fail(tr("A sequence cannot be edited into itself."));
        return false;
    }
    composer_.load(MonitorId::Source, binId, item);
    model::MonitorState& source = composer_.state(MonitorId::Source);
    // Without marks, all of it (p.624).
    if (!source.in() && !source.out()) composer_.moveTo(MonitorId::Source, 0);
    if (recordAt) {
        model::MonitorState& record = composer_.state(MonitorId::Record);
        record.clearBoth();
        composer_.moveTo(MonitorId::Record, *recordAt);
    }
    return edit(kind);
}

std::int64_t Timeline::defaultTransitionLength() const {
    return std::max(format_.nominalFps, 1);
}

// Commits `seq` as the new record sequence, with an undo step.
bool Timeline::changeRecordSequence(const QString& description, const std::function<bool(model::Sequence&)>& change) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    if (!change(seq)) return false;
    return commitSequence(description, seq);
}

bool Timeline::commitSequence(const QString& description, const model::Sequence& seq) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    const model::BinItem before = rec->item;
    model::BinItem after = before;
    seq.storeIn(after);
    after.modified = QDateTime::currentDateTimeUtc();
    commit(description, before, after);
    return true;
}

// Puts `effect` as a transition of the default length, centred on each cut, on `seq` (not yet
// committed). Cuts without room are skipped; it fails if none takes the effect, or when the
// user cancels the Insufficient Source question.
bool Timeline::addTransitions(model::Sequence& seq, const QList<model::TrimRoller>& cuts, const EffectInfo& effect) {
    return addTransitions(seq, cuts, model::Transition{effect.effect(), defaultTransitionLength(), model::TransitionAlignment::CenteredOnCut, 0});
}

bool Timeline::addTransitions(model::Sequence& seq, const QList<model::TrimRoller>& cuts, const model::Transition& t) {
    const model::SourceLength length = sourceLength();
    bool asked = false;
    bool fit = false;
    int applied = 0;
    for (const model::TrimRoller& cut : cuts) {
        const auto room = model::transitionRoom(seq, cut.track, cut.frame, length);
        if (!room) continue;
        model::Transition here = t;
        QString which;
        if (!model::transitionFits(here, *room, &which)) {
            // The Insufficient Source dialog (p.48), once for all the cuts.
            if (!asked) {
                asked = true;
                if (insufficient_) {
                    fit = insufficient_(which);
                } else {
                    QMessageBox box(QMessageBox::Warning, tr("Insufficient Source"),
                                    tr("There is not enough source material for the effect (%1).").arg(which),
                                    QMessageBox::Cancel, this);
                    QPushButton* size = box.addButton(tr("Size To Fit"), QMessageBox::AcceptRole);
                    box.exec();
                    fit = box.clickedButton() == size;
                }
            }
            if (!fit) return false;
            here = model::sizeToFit(here, *room);
        }
        if (model::applyTransition(seq, cut.track, cut.frame, here, length)) ++applied;
    }
    if (applied == 0) return fail(tr("There is no room for a transition effect there."));
    return true;
}

bool Timeline::dropEffect(const QString& track, std::int64_t frame, const EffectInfo& effect,
                          std::optional<std::int64_t> cut) {
    message_.clear();
    if (showSource_ || !recordSequence()) return false;
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    const model::Track* t = seq.track(track);
    if (!t) return false;
    // Not on data tracks (p.44); the palette's effects are video effects.
    if (t->kind != model::TrackKind::Video) return fail(tr("Video effects go on video tracks."));
    if (t->locked) return fail(tr("The track is locked."));
    std::int64_t start = 0;
    const int i = t->segmentAt(frame, &start);
    std::optional<std::pair<QString, std::int64_t>> stabilize; // AutoStabilize's segment
    if (effect.transition && (cut || !effect.segment)) {
        std::int64_t at = cut.value_or(start);
        if (!cut && i >= 0) {
            // The segment's nearer cut that has a segment after it.
            const std::int64_t end = start + t->segments.at(i).length;
            if (frame - start > end - frame && i + 1 < t->segments.size()) at = end;
        }
        if (!addTransitions(seq, {model::TrimRoller{track, at, model::TrimSide::Both}}, effect)) return false;
    } else if (effect.segment) {
        // Alt+drag nests it inside the segment's effect (FX Guide p.216).
        const bool nest = QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier);
        if (i < 0 || !model::applySegmentEffect(seq, track, frame, effect.effect(), nest)) {
            return fail(tr("There is no segment there."));
        }
        if (!nest && model::isStabilizeEffect(effect.effect())) stabilize = std::pair{track, start};
    } else {
        return fail(tr("This effect cannot be applied there."));
    }
    if (!commitSequence(tr("Apply Effect"), seq)) return false;
    emit effectApplied();
    if (stabilize) emit autoStabilizeRequested(stabilize->first, stabilize->second);
    return true;
}

bool Timeline::applyEffectToSelection(const EffectInfo& effect) {
    message_.clear();
    if (!recordSequence()) return false;
    if (!effect.audioEffect.isEmpty()) {
        // An Audio Track Effect double-clicked: the first empty insert of each selected audio track
        // (ours; the guide drags them, p.792).
        const model::Sequence now = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
        const QStringList selected = patch_.selectedRecordTracks(now);
        bool full = false;
        const bool ok = changeRecordSequence(tr("Audio Track Effect"), [&](model::Sequence& seq) {
            int placed = 0;
            for (const QString& name : selected) {
                model::Track* t = seq.track(name);
                if (!t || t->kind != model::TrackKind::Audio || t->locked || t->format != model::AudioFormat::Mono) continue;
                auto empty = std::find_if(t->inserts.begin(), t->inserts.end(), [](const auto& in) { return !in; });
                if (empty == t->inserts.end()) {
                    full = true;
                    continue;
                }
                *empty = newInsert(effect.audioEffect);
                ++placed;
            }
            return placed > 0;
        });
        if (!ok) return fail(full ? tr("The selected audio tracks have no empty insert.") : tr("Select an audio track first."));
        return true;
    }
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    std::optional<std::pair<QString, std::int64_t>> stabilize; // AutoStabilize's segment, when it is the only one
    auto video = [&](const QString& name) {
        const model::Track* t = seq.track(name);
        return t && t->kind == model::TrackKind::Video && !t->locked;
    };
    if (trimMode_ && !rollers_.isEmpty()) {
        // Selected transitions (p.44-45).
        if (!effect.transition) return fail(tr("This effect cannot be applied to a transition."));
        QList<model::TrimRoller> cuts;
        for (const model::TrimRoller& r : rollers_) {
            if (video(r.track)) cuts << r;
        }
        if (cuts.isEmpty() || !addTransitions(seq, cuts, effect)) return false;
    } else if (!selection_.isEmpty()) {
        int applied = 0;
        if (effect.segment) {
            for (const model::SegmentRef& ref : selection_) {
                // Alt+double-click nests it inside the segment's effect (FX Guide p.276).
                const bool nest = QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier);
                if (video(ref.track) && model::applySegmentEffect(seq, ref.track, ref.start, effect.effect(), nest)) {
                    ++applied;
                    if (!nest && model::isStabilizeEffect(effect.effect())) stabilize = std::pair{ref.track, ref.start};
                }
            }
            if (applied != 1) stabilize.reset();
        } else {
            // A transition effect goes on the selected segments' head cuts.
            QList<model::TrimRoller> cuts;
            for (const model::SegmentRef& ref : selection_) {
                if (video(ref.track)) cuts << model::TrimRoller{ref.track, ref.start, model::TrimSide::Both};
            }
            if (!cuts.isEmpty() && !addTransitions(seq, cuts, effect)) return false;
            applied = static_cast<int>(cuts.size());
        }
        if (applied == 0) return fail(tr("Select segments on video tracks first."));
    } else {
        return fail(tr("Select segments or transitions in the Timeline first."));
    }
    if (!commitSequence(tr("Apply Effect"), seq)) return false;
    emit effectApplied();
    if (stabilize) emit autoStabilizeRequested(stabilize->first, stabilize->second);
    return true;
}

// ---------------------------------------------------------------- effect templates

std::optional<model::EffectTemplate> Timeline::droppedTemplate(const QMimeData* mime) const {
    if (!mime) return std::nullopt;
    if (mime->hasFormat(effectTemplateMime())) {
        const QJsonObject o = QJsonDocument::fromJson(mime->data(effectTemplateMime())).object();
        return model::EffectTemplate::fromJson(o.value(QStringLiteral("template")).toObject());
    }
    if (!mime->hasFormat(binItemsMime())) return std::nullopt;
    const QStringList lines = QString::fromUtf8(mime->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() != 2) return std::nullopt;
    const auto item = composer_.resolve(lines.at(0), lines.at(1));
    return item ? model::effectTemplateOf(*item) : std::nullopt;
}

bool Timeline::dropTemplate(const QString& track, std::int64_t frame, const model::EffectTemplate& t,
                            std::optional<std::int64_t> cut) {
    message_.clear();
    if (showSource_ || !recordSequence()) return false;
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    const model::Track* trk = seq.track(track);
    if (!trk) return false;
    if (trk->kind != model::TrackKind::Video) return fail(tr("Video effects go on video tracks."));
    if (trk->locked) return fail(tr("The track is locked."));
    const Rational rate = composer_.projectRate();
    if (t.transition && !t.hasSource()) {
        std::int64_t start = 0;
        const int i = trk->segmentAt(frame, &start);
        std::int64_t at = cut.value_or(start);
        if (!cut && i >= 0) {
            // The segment's nearer cut that has a segment after it, as for palette effects.
            const std::int64_t end = start + trk->segments.at(i).length;
            if (frame - start > end - frame && i + 1 < trk->segments.size()) at = end;
        }
        model::Transition transition{model::adaptedEffect(t, rate), std::max<std::int64_t>(t.duration, 1), t.alignment, t.start};
        if (!addTransitions(seq, {model::TrimRoller{track, at, model::TrimSide::Both}}, transition)) return false;
    } else {
        // Alt+drag nests it inside the segment's effect, as with palette effects (FX Guide p.216).
        const bool nest = QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier);
        if (!model::applyTemplate(seq, track, frame, t, rate, nest, false, sourceLength())) {
            return fail(t.hasSource() ? tr("The template cannot be edited in there.") : tr("There is no segment there."));
        }
    }
    if (!commitSequence(tr("Apply Effect Template"), seq)) return false;
    emit effectApplied();
    return true;
}

bool Timeline::applyTemplateToSelection(const model::EffectTemplate& t) {
    message_.clear();
    if (!recordSequence()) return false;
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    const Rational rate = composer_.projectRate();
    auto video = [&](const QString& name) {
        const model::Track* trk = seq.track(name);
        return trk && trk->kind == model::TrackKind::Video && !trk->locked;
    };
    if (t.hasSource()) {
        // Edited in at the position on the top selected video track.
        QString top;
        for (const QString& name : patch_.selectedRecordTracks(seq)) {
            if (video(name) && (top.isEmpty() || model::parseTrackName(name)->number > model::parseTrackName(top)->number)) top = name;
        }
        if (top.isEmpty()) return fail(tr("Select a video track first."));
        if (!model::applyTemplate(seq, top, composer_.state(MonitorId::Record).position(), t, rate)) {
            return fail(tr("The template cannot be edited in there."));
        }
    } else if (t.transition) {
        QList<model::TrimRoller> cuts;
        if (trimMode_ && !rollers_.isEmpty()) {
            for (const model::TrimRoller& r : rollers_) {
                if (video(r.track)) cuts << r;
            }
        } else {
            for (const model::SegmentRef& ref : selection_) {
                if (video(ref.track)) cuts << model::TrimRoller{ref.track, ref.start, model::TrimSide::Both};
            }
        }
        if (cuts.isEmpty()) return fail(tr("Select segments or transitions in the Timeline first."));
        model::Transition transition{model::adaptedEffect(t, rate), std::max<std::int64_t>(t.duration, 1), t.alignment, t.start};
        if (!addTransitions(seq, cuts, transition)) return false;
    } else {
        if (selection_.isEmpty()) return fail(tr("Select segments in the Timeline first."));
        const bool nest = QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier);
        int applied = 0;
        for (const model::SegmentRef& ref : selection_) {
            if (video(ref.track) && model::applyTemplate(seq, ref.track, ref.start, t, rate, nest)) ++applied;
        }
        if (applied == 0) return fail(tr("Select segments on video tracks first."));
    }
    if (!commitSequence(tr("Apply Effect Template"), seq)) return false;
    emit effectApplied();
    return true;
}

bool Timeline::applyTitler() {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return fail(tr("There is no sequence in the Record monitor."));
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    // The highest selected video track (ours, as the Effect Editor's effect is found top down, p.84).
    QString track;
    for (const QString& t : patch_.selectedRecordTracks(seq)) {
        const model::Track* candidate = seq.track(t);
        if (candidate && candidate->kind == model::TrackKind::Video && !candidate->locked) track = t;
    }
    // "Select the clip or an empty track segment where you want to add a title. If you want to create a new clip
    // containing your title, set in and out marks in the Timeline to highlight a portion of the track" (p.303).
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    std::optional<std::pair<std::int64_t, std::int64_t>> marks;
    if (record.in() && record.out() && *record.out() >= *record.in()) marks = std::pair{*record.in(), *record.out() + 1};
    QList<model::SegmentRef> targets;
    for (const model::SegmentRef& ref : selection_) {
        const model::Track* t = seq.track(ref.track);
        if (t && t->kind == model::TrackKind::Video) targets << ref;
    }
    if (targets.isEmpty() && !track.isEmpty()) {
        std::int64_t start = 0;
        if (const model::Track* t = seq.track(track); t && t->segmentAt(record.position(), &start) >= 0) {
            targets << model::SegmentRef{track, start};
        }
    }
    if ((marks && track.isEmpty()) || (!marks && targets.isEmpty())) return fail(tr("Select a clip or an empty track segment first."));
    if (model::applyTitler(seq, track, marks, marks ? QList<model::SegmentRef>{} : targets) == 0) {
        return fail(tr("Select a clip or an empty track segment first."));
    }
    if (!commitSequence(tr("Titler+"), seq)) return false;
    // The position on a new title, so that the Effect Editor shows it (ours).
    const std::int64_t first = marks ? marks->first
                                     : std::min_element(targets.begin(), targets.end(), [](const model::SegmentRef& a, const model::SegmentRef& b) {
                                           return a.start < b.start;
                                       })->start;
    const model::Track* titledTrack = seq.track(marks ? track : targets.first().track);
    std::int64_t at = 0;
    const int here = titledTrack ? titledTrack->segmentAt(record.position(), &at) : -1;
    const bool onTitle = here >= 0 && titledTrack->segments.at(here).effect && model::isTitlerEffect(*titledTrack->segments.at(here).effect);
    if (!onTitle) composer_.moveTo(MonitorId::Record, first);
    emit effectApplied();
    // "The Avid Titler+ Dashboard and the Effect Editor open" (p.303).
    emit titlerApplied();
    return true;
}

std::optional<model::EffectRef> Timeline::currentEffect() const {
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    const model::LoadedClip* rec = record.current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return std::nullopt;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    // Top down: the highest video track first (p.84).
    QStringList tracks;
    for (const QString& t : patch_.selectedRecordTracks(seq)) {
        if (t.startsWith(QLatin1Char('V'))) {
            tracks.prepend(t);
        } else {
            tracks << t;
        }
    }
    return model::effectAt(seq, tracks, record.position());
}

int Timeline::fadeEffects(std::int64_t up, std::int64_t down) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) {
        fail(tr("There is no sequence in the Record monitor."));
        return 0;
    }
    const model::Sequence now = model::Sequence::fromItem(rec->item);
    // "To fade multiple segment effects ... Shift+click the segments"; "To fade a single segment effect,
    // move the position indicator to the segment" (p.46).
    QList<model::SegmentRef> targets;
    for (const model::SegmentRef& ref : selection_) {
        if (const auto seg = model::segmentFor(now, ref); seg && seg->effect) targets << ref;
    }
    if (targets.isEmpty()) {
        if (const auto ref = currentEffect(); ref && ref->target == model::EffectTarget::Segment) {
            targets << model::SegmentRef{ref->track, ref->frame};
        }
    }
    if (targets.isEmpty()) {
        fail(tr("Select a segment with an effect to fade."));
        return 0;
    }
    int count = 0;
    changeRecordSequence(tr("Fade Effect"), [&](model::Sequence& seq) {
        for (const model::SegmentRef& ref : targets) {
            model::Track* t = seq.track(ref.track);
            std::int64_t start = 0;
            const int i = t ? t->segmentAt(ref.start, &start) : -1;
            if (i < 0 || start != ref.start || !t->segments[i].effect || t->locked) continue;
            model::fadeEffect(*t->segments[i].effect, t->segments[i].length, up, down);
            ++count;
        }
        return count > 0;
    });
    if (count == 0) fail(tr("The selected effects are on locked tracks."));
    return count;
}

bool Timeline::removeEffectAtPosition() {
    message_.clear();
    const auto ref = currentEffect();
    if (!ref) {
        // No video effect: the Audio EQ (p.741) and AudioSuite effects there on the selected audio
        // tracks.
        if (!recordSequence()) return fail(tr("There is no effect at the position on the selected tracks."));
        const model::MonitorState& record = composer_.state(MonitorId::Record);
        model::Sequence seq = model::Sequence::fromItem(record.current()->item);
        int removed = 0;
        for (const QString& t : patch_.selectedRecordTracks(seq)) {
            removed += model::setEq(seq, t, record.position(), record.position() + 1, std::nullopt);
            removed += model::setAudioSuite(seq, t, record.position(), record.position() + 1, std::nullopt);
        }
        if (removed == 0) return fail(tr("There is no effect at the position on the selected tracks."));
        return commitSequence(tr("Remove Effect"), seq);
    }
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    if (model::removeEffect(seq, ref->track, ref->frame, ref->target).isEmpty()) {
        return fail(tr("The track is locked."));
    }
    return commitSequence(tr("Remove Effect"), seq);
}

// Each cut keeps its transition's effect, or gets a Dissolve; the duration, alignment and start
// are replaced. Locked tracks and cuts without room are skipped.
bool Timeline::setTransitions(const QList<model::TrimRoller>& cuts, std::int64_t duration,
                              model::TransitionAlignment alignment, std::int64_t start) {
    message_.clear();
    if (!recordSequence()) return false;
    if (duration <= 0) return fail(tr("Type a duration for the transition."));
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    const model::SourceLength length = sourceLength();
    const EffectInfo* dissolve = EffectCatalog::builtin().find(QStringLiteral("Blend"), QStringLiteral("Dissolve"));
    bool asked = false;
    bool fit = false;
    int changed = 0;
    for (const model::TrimRoller& cut : cuts) {
        const model::Track* t = seq.track(cut.track);
        if (!t || t->locked) continue;
        const auto room = model::transitionRoom(seq, cut.track, cut.frame, length);
        if (!room) continue;
        model::Transition transition{dissolve ? dissolve->effect() : model::Effect{}, duration, alignment, start};
        const model::EffectRef ref{cut.track, cut.frame, model::EffectTarget::Transition};
        if (const model::Transition* existing = model::transitionFor(seq, ref)) transition.effect = existing->effect;
        QString which;
        if (!model::transitionFits(transition, *room, &which)) {
            if (!asked) {
                asked = true;
                fit = insufficient_ ? insufficient_(which) : QMessageBox::question(this, tr("Insufficient Source"),
                    tr("There is not enough source material for the effect (%1). Size it to fit?").arg(which)) ==
                    QMessageBox::Yes;
            }
            if (!fit) return false;
            transition = model::sizeToFit(transition, *room);
        }
        if (model::applyTransition(seq, cut.track, cut.frame, transition, length)) ++changed;
    }
    if (changed == 0) return fail(tr("There is no room for a transition effect there."));
    return commitSequence(tr("Transition Duration"), seq);
}

// The range is one frame at the position, or with onTrack from the IN (else 0) to the OUT
// (else the end of the track); model::setClipAudio sets the whole segments it touches.
bool Timeline::setClipAudio(const QString& track, std::optional<double> gain, std::optional<int> pan, bool onTrack,
                            const QString& description) {
    message_.clear();
    if (!recordSequence()) return false;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    model::Sequence seq = model::Sequence::fromItem(record.current()->item);
    std::int64_t start = record.position();
    std::int64_t length = 1;
    if (onTrack) {
        const model::Track* t = seq.track(track);
        const std::int64_t end = t ? t->length() : 0;
        start = record.in().value_or(0);
        length = (record.out() ? *record.out() + 1 : end) - start;
    }
    if (model::setClipAudio(seq, track, start, length, gain, pan) == 0) {
        return fail(tr("There is no audio segment there to adjust."));
    }
    return commitSequence(description, seq);
}

std::optional<std::pair<std::int64_t, std::int64_t>> Timeline::audioLoopRange() const {
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    const model::LoadedClip* rec = record.current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return std::nullopt;
    const auto in = record.in();
    const auto out = record.out();
    if (in && out) return std::pair{*in, *out};
    // The shortest audio segment under the position.
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const std::int64_t at = record.position();
    auto shortest = [&](bool selectedOnly) -> std::optional<std::pair<std::int64_t, std::int64_t>> {
        std::optional<std::pair<std::int64_t, std::int64_t>> best;
        for (const model::Track& t : seq.tracks()) {
            if (t.kind != model::TrackKind::Audio || (selectedOnly && !patch_.isRecordSelected(t.name()))) continue;
            std::int64_t start = 0;
            const int i = t.segmentAt(at, &start);
            if (i < 0 || t.segments.at(i).isFiller()) continue;
            const std::int64_t end = start + t.segments.at(i).length - 1;
            if (!best || end - start < best->second - best->first) best = std::pair{start, end};
        }
        return best;
    };
    auto segment = shortest(true);
    if (!segment) segment = shortest(false);
    if (!segment) return std::nullopt;
    if (in && *in <= segment->second) return std::pair{*in, segment->second};
    if (out && *out >= segment->first) return std::pair{segment->first, *out};
    if (in || out) return std::nullopt;
    return segment;
}

QToolButton* Timeline::makeAudioLoopPlayButton(QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setText(tr("Loop"));
    b->setObjectName(QStringLiteral("audioLoopPlay"));
    b->setToolTip(tr("Audio Loop Play"));
    b->setCheckable(true);
    b->setMinimumWidth(22);
    connect(b, &QToolButton::clicked, this, [this, b] {
        toggleAudioLoopPlay();
        b->setChecked(composer_.isLooping());
    });
    connect(&composer_, &Composer::loopChanged, b, &QToolButton::setChecked);
    return b;
}

bool Timeline::toggleAudioLoopPlay() {
    message_.clear();
    if (composer_.isLooping()) {
        composer_.stop();
        return true;
    }
    const auto range = audioLoopRange();
    if (!range) return fail(tr("There is no audio to loop: mark IN and OUT or place the position on an audio segment."));
    composer_.playLoop(MonitorId::Record, range->first, range->second);
    return true;
}

bool Timeline::adjustClipAudio(const QStringList& tracks, double byDb, int byPan) {
    message_.clear();
    if (!recordSequence()) return false;
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    int changed = 0;
    for (const QString& t : tracks) {
        const model::Track* track = seq.track(t);
        if (!track) continue;
        const int side = track->format == model::AudioFormat::Mono ? model::sidePan(track->number, alternatingPan_) : 0;
        const auto [from, to] = markedRange(t);
        changed += model::adjustClipAudio(seq, t, from, to - from, byDb, byPan, side);
    }
    if (changed == 0) return fail(tr("There is no audio segment there to adjust."));
    return commitSequence(tr("Adjust Pan/Vols on Track"), seq);
}

bool Timeline::setDefaultClipPan(const QStringList& tracks, bool commit) {
    message_.clear();
    if (!recordSequence()) return false;
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    int changed = 0;
    for (const QString& t : tracks) {
        const model::Track* track = seq.track(t);
        if (!track) continue;
        const int side = track->format == model::AudioFormat::Mono ? model::sidePan(track->number, alternatingPan_) : 0;
        const auto [from, to] = markedRange(t);
        changed += model::setDefaultClipPan(seq, t, from, to - from, side, commit);
    }
    if (changed == 0) return fail(commit ? tr("Every segment there has a pan of its own.") : tr("No segment there has the default pan."));
    return commitSequence(commit ? tr("Commit Default Clip Pan on Track") : tr("Remove Default Clip Pan on Track"), seq);
}

bool Timeline::setLiveMixAsAutomation(const QStringList& tracks, const QHash<QString, std::pair<double, int>>& live) {
    message_.clear();
    if (!recordSequence()) return false;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    model::Sequence seq = model::Sequence::fromItem(record.current()->item);
    const auto [from, to] = markedRange(seq);
    const bool ramps = record.in() || record.out();
    int changed = 0;
    for (const QString& t : tracks) {
        const auto [db, pan] = live.value(t, {0.0, 0});
        changed += model::setAutomationTo(seq, t, from, to, model::AutomationParam::Volume, db, ramps, alternatingPan_);
        changed += model::setAutomationTo(seq, t, from, to, model::AutomationParam::Pan, pan, ramps, alternatingPan_);
    }
    if (changed == 0) return fail(tr("There is no audio segment there to set."));
    keySelection_.clear();
    return commitSequence(tr("Set Live Mix as Automation"), seq);
}

bool Timeline::setSurroundPan(const QString& track, int channel, const model::SurroundPan& pan,
                              const std::optional<model::SurroundPan>& other) {
    message_.clear();
    if (!recordSequence()) return false;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    const model::Sequence before = model::Sequence::fromItem(record.current()->item);
    model::Sequence seq = before;
    if (model::setSurroundPan(seq, track, record.position(), 1, channel, pan, other) == 0) {
        return fail(tr("There is no mono or stereo audio segment there to pan."));
    }
    if (seq == before) return true;
    return commitSequence(tr("Surround Pan"), seq);
}

bool Timeline::setLfeOnly(const QString& track, bool on) {
    message_.clear();
    if (!recordSequence()) return false;
    const model::Sequence before = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    model::Sequence seq = before;
    if (!model::setLfeOnly(seq, track, on)) return fail(tr("Only a mono audio track can be LFE only."));
    if (seq == before) return true;
    return commitSequence(on ? tr("LFE Only") : tr("Not LFE Only"), seq);
}

bool Timeline::setEq(const QStringList& tracks, const std::optional<model::AudioEq>& eq, bool onTrack,
                     const QString& description) {
    message_.clear();
    if (!recordSequence()) return false;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    model::Sequence seq = model::Sequence::fromItem(record.current()->item);
    int changed = 0;
    for (const QString& track : tracks) {
        std::int64_t from = record.position();
        std::int64_t to = from + 1;
        if (onTrack) {
            // IN to OUT, IN to the end, the start to OUT, or the whole track (p.740).
            const model::Track* t = seq.track(track);
            from = record.in().value_or(0);
            to = record.out() ? *record.out() + 1 : (t ? t->length() : 0);
        }
        changed += model::setEq(seq, track, from, to, eq);
    }
    if (changed == 0) {
        // Nothing to do is not an error when the segments already have this EQ.
        bool any = false;
        for (const QString& track : tracks) {
            const model::Track* t = seq.track(track);
            if (!t || t->kind != model::TrackKind::Audio || t->locked) continue;
            if (onTrack) {
                any = any || std::any_of(t->segments.begin(), t->segments.end(), [](const model::Segment& g) { return !g.isFiller(); });
            } else {
                const int i = t->segmentAt(record.position());
                any = any || (i >= 0 && !t->segments.at(i).isFiller());
            }
        }
        return any ? true : fail(tr("There is no audio segment there to equalize."));
    }
    return commitSequence(description, seq);
}

bool Timeline::setAudioSuite(const QStringList& tracks, const std::optional<model::AudioSuiteEffect>& effect, bool onTrack,
                             const QString& description) {
    message_.clear();
    if (!recordSequence()) return false;
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    model::Sequence seq = model::Sequence::fromItem(record.current()->item);
    int changed = 0;
    bool any = false;
    for (const QString& track : tracks) {
        std::int64_t from = record.position();
        std::int64_t to = from + 1;
        if (onTrack) std::tie(from, to) = markedRange(track);
        changed += model::setAudioSuite(seq, track, from, to, effect);
        // Nothing to do is not an error where the segments already are so.
        const model::Track* t = seq.track(track);
        if (!t || t->kind != model::TrackKind::Audio || t->locked) continue;
        std::int64_t at = 0;
        for (const model::Segment& g : t->segments) {
            any = any || (!g.isFiller() && at + g.length > from && at < to);
            at += g.length;
        }
    }
    if (changed == 0) return any ? true : fail(tr("There is no audio segment there for the AudioSuite plug-in."));
    return commitSequence(description, seq);
}

std::pair<std::int64_t, std::int64_t> Timeline::markedRange(const QString& track) const {
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    std::int64_t end = 0;
    if (const model::LoadedClip* rec = composer_.state(MonitorId::Record).current(); rec && rec->item.kind == model::BinItemKind::Sequence) {
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        if (const model::Track* t = seq.track(track)) end = t->length();
    }
    return {record.in().value_or(0), record.out() ? *record.out() + 1 : end};
}

std::optional<model::AudioSuiteEffect> Timeline::audioSuiteAt(const QString& track) const {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return std::nullopt;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::Track* t = seq.track(track);
    if (!t) return std::nullopt;
    const int i = t->segmentAt(composer_.state(MonitorId::Record).position());
    if (i < 0) return std::nullopt;
    return t->segments.at(i).audioSuite;
}

int Timeline::audioSuiteToRender(const QStringList& tracks, std::int64_t from, std::int64_t to) const {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return 0;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    int n = 0;
    for (const QString& track : tracks) {
        const model::Track* t = seq.track(track);
        if (!t || t->kind != model::TrackKind::Audio) continue;
        std::int64_t at = 0;
        for (const model::Segment& g : t->segments) {
            if (at + g.length > from && at < to && model::needsRender(g)) ++n;
            at += g.length;
        }
    }
    return n;
}

std::optional<int> Timeline::renderAudioSuite(const QStringList& tracks, std::int64_t from, std::int64_t to,
                                              const AudioSuiteRenderer& render) {
    message_.clear();
    if (!recordSequence()) return std::nullopt;
    model::Sequence seq = model::Sequence::fromItem(recordSequence()->item);
    int rendered = 0;
    QString error;
    for (const QString& track : tracks) {
        model::Track* t = seq.track(track);
        if (!t || t->kind != model::TrackKind::Audio || !error.isEmpty()) continue;
        std::int64_t at = 0;
        for (model::Segment& g : t->segments) {
            const std::int64_t start = at;
            at += g.length;
            if (start + g.length <= from || start >= to || !model::needsRender(g)) continue;
            const auto made = render(g, &error);
            if (!made) {
                if (error.isEmpty()) error = tr("The AudioSuite effect could not be rendered.");
                break;
            }
            g.audioSuite = made;
            ++rendered;
        }
    }
    if (rendered > 0) commitSequence(tr("Render AudioSuite"), seq);
    if (!error.isEmpty()) {
        fail(error);
        return std::nullopt;
    }
    return rendered;
}

std::optional<QStringList> Timeline::punchIn(const QString& binId, const model::BinItem& clip, const QStringList& targets,
                                             const QList<PunchIn>& punches) {
    message_.clear();
    if (!recordSequence()) return std::nullopt;
    if (targets.isEmpty() || punches.isEmpty()) {
        fail(tr("There is nothing to edit in."));
        return std::nullopt;
    }
    {
        // "You can only use mono audio tracks for punch-in. You cannot select stereo tracks or
        // locked tracks" (p.749).
        const model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
        QStringList seen;
        for (const QString& t : targets) {
            if (t.isEmpty()) continue;
            const model::Track* track = seq.track(t);
            if (!track || track->kind != model::TrackKind::Audio || track->format != model::AudioFormat::Mono) {
                fail(tr("%1 is not a mono audio track.").arg(t));
                return std::nullopt;
            }
            if (track->locked) {
                fail(tr("%1 is locked.").arg(t));
                return std::nullopt;
            }
            if (seen.contains(t)) {
                fail(tr("Two channels cannot record to %1.").arg(t));
                return std::nullopt;
            }
            seen << t;
        }
    }
    QStringList used = targets;
    bool first = true;
    for (const PunchIn& p : punches) {
        if (p.length <= 0) continue;
        const bool ok = changeRecordSequence(tr("Audio Punch-In"), [&](model::Sequence& s) {
            for (qsizetype k = 0; k < used.size(); ++k) {
                if (used[k].isEmpty()) {
                    if (!first) return false;
                    const auto added = s.addTrack(model::TrackKind::Audio);
                    if (!added) return false;
                    used[k] = *added;
                }
                model::Segment seg;
                seg.clipId = clip.id;
                seg.binId = binId;
                seg.sourceMob = clip.mobId;
                seg.name = clip.name;
                seg.sourceTrack = QStringLiteral("A%1").arg(k + 1);
                seg.sourceIn = p.source;
                seg.length = p.length;
                s.overwrite(used[k], p.at, {seg});
            }
            return true;
        });
        if (!ok) {
            if (first) {
                fail(tr("No audio track can be added."));
                return std::nullopt;
            }
            break;
        }
        first = false;
    }
    if (first) {
        fail(tr("There is nothing to edit in."));
        return std::nullopt;
    }
    return used;
}

std::optional<model::TrackInsert> Timeline::trackInsert(const QString& track, int slot) const {
    const model::LoadedClip* rec = composer_.state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence || slot < 0 || slot >= model::kTrackInserts) return std::nullopt;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::Track* t = seq.track(track);
    if (!t) return std::nullopt;
    return t->inserts[static_cast<std::size_t>(slot)];
}

model::TrackInsert Timeline::newInsert(const QString& effectId) {
    model::TrackInsert in;
    in.effect = effectId;
    if (const media::AudioEffectInfo* info = media::findAudioEffect(effectId)) {
        in.name = info->name;
        in.params = info->defaults();
    }
    return in;
}

bool Timeline::setTrackInsert(const QString& track, int slot, const std::optional<model::TrackInsert>& insert,
                              const QString& description) {
    message_.clear();
    if (!recordSequence()) return false;
    if (slot < 0 || slot >= model::kTrackInserts) return fail(tr("There is no such insert."));
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    model::Track* t = seq.track(track);
    if (!t || t->kind != model::TrackKind::Audio) return fail(tr("Audio Track Effects go on audio tracks."));
    // "You can only apply mono plug-ins to mono audio tracks" (p.790); ours are all mono.
    if (insert && t->format != model::AudioFormat::Mono) return fail(tr("%1 is not a mono track.").arg(track));
    if (t->locked) return fail(tr("%1 is locked.").arg(track));
    auto& place = t->inserts[static_cast<std::size_t>(slot)];
    if (place == insert) return true;
    place = insert;
    return commitSequence(description, seq);
}

bool Timeline::moveTrackInsert(const QString& fromTrack, int fromSlot, const QString& toTrack, int toSlot, bool copy) {
    message_.clear();
    if (!recordSequence()) return false;
    if (fromSlot < 0 || fromSlot >= model::kTrackInserts || toSlot < 0 || toSlot >= model::kTrackInserts) {
        return fail(tr("There is no such insert."));
    }
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    model::Track* from = seq.track(fromTrack);
    model::Track* to = seq.track(toTrack);
    if (!from || !to || to->kind != model::TrackKind::Audio) return fail(tr("Audio Track Effects go on audio tracks."));
    const auto insert = from->inserts[static_cast<std::size_t>(fromSlot)];
    if (!insert) return fail(tr("There is no insert to move."));
    if (to->format != from->format) return fail(tr("Mono inserts go on mono tracks, stereo on stereo (p.794)."));
    if (to->locked || (!copy && from->locked)) return fail(tr("The track is locked."));
    if (fromTrack == toTrack && fromSlot == toSlot) return true;
    if (!copy) from->inserts[static_cast<std::size_t>(fromSlot)].reset();
    seq.track(toTrack)->inserts[static_cast<std::size_t>(toSlot)] = insert;
    return commitSequence(copy ? tr("Copy Audio Track Effect") : tr("Move Audio Track Effect"), seq);
}

void Timeline::fillTrackEffectsMenu(QMenu* fx, const QString& track) {
    for (int slot = 0; slot < model::kTrackInserts; ++slot) {
        const auto in = trackInsert(track, slot);
        QMenu* insert = fx->addMenu(tr("Insert %1").arg(QChar(u'a' + slot)) + (in ? QStringLiteral(" (%1)").arg(in->name) : QString()));
        insert->setObjectName(QStringLiteral("insert:%1").arg(slot));
        insert->addAction(tr("No Insert"), this, [this, track, slot] {
            setTrackInsert(track, slot, std::nullopt, tr("Remove Audio Track Effect"));
        })->setEnabled(in.has_value());
        QMap<QString, QMenu*> categories;
        for (const media::AudioEffectInfo& info : media::audioEffects()) {
            QMenu*& c = categories[info.category];
            if (!c) c = insert->addMenu(info.category);
            c->addAction(info.name, this, [this, track, slot, id = info.id] {
                setTrackInsert(track, slot, newInsert(id), tr("Audio Track Effect"));
            });
        }
    }
}

// The Select Insert dialog (picture on p.792): a button per insert, "[Empty]" or the effect's name.
std::optional<model::AudioEq> Timeline::droppedEq(const QMimeData* mime) const {
    if (!mime) return std::nullopt;
    if (mime->hasFormat(eqTemplateMime())) {
        const QJsonObject o = QJsonDocument::fromJson(mime->data(eqTemplateMime())).object();
        if (!o.value(QStringLiteral("eq")).isObject()) return std::nullopt;
        return model::audioEqFromJson(o.value(QStringLiteral("eq")).toObject());
    }
    if (!mime->hasFormat(binItemsMime())) return std::nullopt;
    const QStringList lines = QString::fromUtf8(mime->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() != 2) return std::nullopt;
    const auto item = composer_.resolve(lines.at(0), lines.at(1));
    return item ? model::eqTemplateOf(*item) : std::nullopt;
}

bool Timeline::dropEq(const QString& track, std::int64_t frame, const model::AudioEq& eq) {
    message_.clear();
    if (!track.startsWith(QLatin1Char('A'))) return false;
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    // Already so: true without an undo step.
    const model::Sequence now = model::Sequence::fromItem(rec->item);
    if (const model::Track* t = now.track(track)) {
        const int i = t->segmentAt(frame);
        if (i >= 0 && !t->segments.at(i).isFiller() && t->segments.at(i).eq == model::clampEq(eq)) return true;
    }
    return changeRecordSequence(tr("Audio EQ"), [&](model::Sequence& seq) { return model::setEq(seq, track, frame, frame + 1, eq) > 0; });
}

bool Timeline::dropTrackEffect(const QString& track, const QString& effectId) {
    message_.clear();
    std::optional<int> slot;
    if (selectInsertAsk_) {
        slot = selectInsertAsk_(track);
    } else {
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("selectInsert"));
        dialog.setWindowTitle(tr("Select Insert"));
        auto* layout = new QVBoxLayout(&dialog);
        for (int i = 0; i < model::kTrackInserts; ++i) {
            const auto in = trackInsert(track, i);
            auto* b = new QPushButton(QStringLiteral("%1: %2").arg(QChar(u'a' + i), in ? in->name : tr("[Empty]")), &dialog);
            connect(b, &QPushButton::clicked, &dialog, [&dialog, &slot, i] {
                slot = i;
                dialog.accept();
            });
            layout->addWidget(b);
        }
        dialog.exec();
    }
    if (!slot) return false;
    return setTrackInsert(track, *slot, newInsert(effectId), tr("Audio Track Effect"));
}

std::optional<model::Segment> Timeline::audioSegmentAt(const QString& track) const {
    const model::MonitorState& record = composer_.state(MonitorId::Record);
    const model::LoadedClip* rec = record.current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return std::nullopt;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const model::Track* t = seq.track(track);
    if (!t) return std::nullopt;
    const int i = t->segmentAt(record.position());
    if (i < 0 || t->segments.at(i).isFiller()) return std::nullopt;
    return t->segments.at(i);
}

bool Timeline::changeEffect(const model::EffectRef& ref, const QString& description,
                            const std::function<void(model::Effect&, model::Transition*)>& change) {
    return changeItem(MonitorId::Record, description, [&](model::BinItem& item) {
        model::Sequence seq = model::Sequence::fromItem(item);
        model::Effect* e = model::effectFor(seq, ref);
        if (!e) return false;
        const model::Track* t = seq.track(ref.track);
        if (t && t->locked) return false;
        model::Transition* transition = model::transitionFor(seq, ref);
        change(*e, transition);
        seq.storeIn(item);
        return true;
    });
}

bool Timeline::changeNestedEffect(const model::EffectRef& ref, std::int64_t offset, int level, const QString& description,
                                  const std::function<void(model::Effect&)>& change) {
    if (level == 0) return changeEffect(ref, description, [&](model::Effect& e, model::Transition*) { change(e); });
    if (ref.target != model::EffectTarget::Segment) return false;
    return changeItem(MonitorId::Record, description, [&](model::BinItem& item) {
        model::Sequence seq = model::Sequence::fromItem(item);
        model::Track* t = seq.track(ref.track);
        if (!t || t->locked) return false;
        const int i = t->segmentAt(ref.frame);
        if (i < 0) return false;
        model::Segment& segment = t->segments[i];
        const QList<model::NestedEffect> chain = model::nestedEffects(segment, offset);
        if (level < 0 || level >= chain.size()) return false;
        model::Effect e = chain.at(level).effect;
        change(e);
        if (e == chain.at(level).effect || !model::setNestedEffect(segment, offset, level, e)) return false;
        seq.storeIn(item);
        return true;
    });
}

bool Timeline::moveNestedEffect(const model::EffectRef& ref, std::int64_t offset, int from, int to) {
    message_.clear();
    if (ref.target != model::EffectTarget::Segment) return false;
    QString error;
    const bool ok = changeItem(MonitorId::Record, tr("Change Order of Nested Effects"), [&](model::BinItem& item) {
        model::Sequence seq = model::Sequence::fromItem(item);
        model::Track* t = seq.track(ref.track);
        if (!t) return false;
        if (t->locked) {
            error = tr("The track is locked.");
            return false;
        }
        const int i = t->segmentAt(ref.frame);
        if (i < 0 || from == to) return false;
        if (!model::moveNestedEffect(t->segments[i], offset, from, to, &error)) return false;
        seq.storeIn(item);
        return true;
    });
    if (!ok && !error.isEmpty()) return fail(error);
    return ok;
}

} // namespace lumenis::ui

namespace lumenis::ui {

// ---------------------------------------------------------------- clip colours (p.594-598)

void Timeline::setClipColorOptions(const model::ClipColorOptions& options) {
    if (options == clipColors_) return;
    clipColors_ = options;
    view_->update();
    emit clipColorOptionsChanged(clipColors_);
}

void Timeline::openClipColorDialog() {
    ClipColorDialog dialog(clipColors_, this);
    const bool ok = clipColorAsk_ ? clipColorAsk_(dialog) : dialog.exec() == QDialog::Accepted;
    if (ok) setClipColorOptions(dialog.options());
}

bool Timeline::addFillerAtStart() {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    const std::int64_t length = static_cast<std::int64_t>(std::max(options_.startFillerSeconds, 0)) *
                                std::max<std::int64_t>(format().nominalFps, 1);
    if (length == 0) return fail(tr("Set a Start Filler Duration in the Timeline settings first."));
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    if (!seq.addFillerAtStart(length)) {
        return fail(seq.duration() == 0 ? tr("You cannot add filler to an empty sequence.")
                                        : tr("A track is locked."));
    }
    return commitSequence(tr("Add Filler at Start"), seq);
}

bool Timeline::findGap(bool flash) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const QStringList tracks = patch_.selectedRecordTracks(seq);
    if (tracks.isEmpty()) return fail(tr("Select the tracks to search."));
    const std::int64_t position = composer_.state(MonitorId::Record).position();
    // Find again goes to the next one: from the frame after the position, or from before frame 0.
    const std::int64_t after = position == 0 ? -1 : position;
    const auto at = flash ? seq.findFlashFrame(tracks, after, std::max(options_.flashFrames, 1)) : seq.findBlackHole(tracks, after);
    if (!at) return fail(flash ? tr("There are no more flash frames.") : tr("There are no more black holes."));
    composer_.stop();
    composer_.setActiveMonitor(MonitorId::Record);
    composer_.moveTo(MonitorId::Record, *at);
    return true;
}

void Timeline::renameTrack(const QString& track) {
    const model::Sequence seq = recordSequenceOf(composer_);
    const model::Track* t = seq.track(track);
    const QString current = t ? t->customName : QString();
    std::optional<QString> name;
    if (renameAsk_) {
        name = renameAsk_(current);
    } else {
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("renameTrack"));
        dialog.setWindowTitle(tr("Comments"));
        auto* edit = new QLineEdit(current, &dialog);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        QPushButton* remove = buttons->addButton(tr("Remove"), QDialogButtonBox::DestructiveRole);
        bool removed = false;
        connect(remove, &QPushButton::clicked, &dialog, [&] {
            removed = true;
            dialog.accept();
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        auto* v = new QVBoxLayout(&dialog);
        v->addWidget(new QLabel(tr("Name for %1:").arg(track), &dialog));
        v->addWidget(edit);
        v->addWidget(buttons);
        if (dialog.exec() == QDialog::Accepted) name = removed ? QString() : edit->text();
    }
    if (name) setTrackName(track, name->trimmed());
}

void Timeline::setTrackName(const QString& track, const QString& name) {
    changeTracks({track}, [&name](model::Track& t) { t.customName = name.trimmed(); });
    // The Audio Mixer shows the name with the track number (p.635).
    emit sequenceEdited();
}

bool Timeline::createSequenceFromSelection() {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    if (selection_.isEmpty()) return fail(tr("Select clips in the Timeline first."));
    const model::Sequence copy = model::Sequence::fromItem(rec->item).selectionCopy(selection_);
    if (copy.tracks().isEmpty()) return fail(tr("Select clips in the Timeline first."));
    model::BinItem item = rec->item;
    copy.storeIn(item);
    emit sequenceFromSelectionRequested(rec->binId, item);
    return true;
}

bool Timeline::setClipNote(const QList<model::SegmentRef>& segments, const QString& note) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    int changed = 0;
    for (const model::SegmentRef& ref : segments) {
        model::Track* t = seq.track(ref.track);
        if (!t) continue;
        std::int64_t at = 0;
        for (model::Segment& seg : t->segments) {
            if (at == ref.start && !seg.isFiller() && seg.note != note) {
                seg.note = note;
                ++changed;
            }
            at += seg.length;
        }
    }
    if (changed == 0) return false;
    const QList<model::SegmentRef> keep = selection_;
    const bool ok = commitSequence(note.isEmpty() ? tr("Delete Timeline Clip Note") : tr("Add Timeline Clip Note"), seq);
    selection_ = keep;
    view_->update();
    return ok;
}

bool Timeline::addClipNote(const std::optional<model::SegmentRef>& segment) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    QList<model::SegmentRef> refs = selection_;
    if (refs.isEmpty() && segment) refs << *segment;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    QString current;
    bool any = false;
    for (const model::SegmentRef& ref : refs) {
        const auto seg = model::segmentFor(seq, ref);
        if (!seg || seg->isFiller()) continue;
        if (!any) current = seg->note;
        any = true;
    }
    if (!any) return fail(tr("Select the clips to add a note to with a segment tool."));
    std::optional<QString> text;
    if (noteAsk_) {
        text = noteAsk_(current);
    } else {
        bool ok = false;
        const QString typed = QInputDialog::getMultiLineText(this, tr("Timeline Clip Note"), tr("Note:"), current, &ok);
        if (ok) text = typed;
    }
    if (!text) return false;
    return setClipNote(refs, text->trimmed());
}

bool Timeline::setLocalClipColor(const QString& hex) {
    message_.clear();
    if (!recordSequence()) return false;
    if (selection_.isEmpty()) return fail(tr("Select the clips to color with a segment tool."));
    model::Sequence seq = model::Sequence::fromItem(composer_.state(MonitorId::Record).current()->item);
    int changed = 0;
    for (const model::SegmentRef& ref : selection_) {
        model::Track* t = seq.track(ref.track);
        if (!t) continue;
        std::int64_t at = 0;
        for (model::Segment& seg : t->segments) {
            if (at == ref.start && !seg.isFiller() && seg.localColor != hex) {
                seg.localColor = hex;
                ++changed;
            }
            at += seg.length;
        }
    }
    if (changed == 0) return false;
    const QList<model::SegmentRef> keep = selection_;
    const bool ok = commitSequence(tr("Set Local Clip Color"), seq);
    selection_ = keep;
    view_->update();
    return ok;
}

// The clip is looked up as the monitors do; whether its media is online is kept for two seconds, so
// painting does not look at the disk for every segment every time.
std::optional<model::ClipFacts> Timeline::clipFacts(const model::Segment& segment) const {
    if (segment.isFiller()) return std::nullopt;
    const auto clip = composer_.resolve(segment.binId, segment.clipId);
    if (!clip) return std::nullopt;
    model::ClipFacts f;
    f.kind = clip->kind;
    f.sourceColor = clip->color;
    const bool hasMedia = clip->kind == model::BinItemKind::MasterClip || clip->kind == model::BinItemKind::Subclip ||
                          clip->kind == model::BinItemKind::AudioClip;
    if (hasMedia) {
        if (!onlineClock_.isValid()) onlineClock_.start();
        const qint64 now = onlineClock_.elapsed();
        const QString key = segment.binId + QLatin1Char('|') + segment.clipId;
        auto it = online_.find(key);
        if (it == online_.end() || now - it->second > 2000) {
            // A clip with no media to point at, or unlinked, is offline as well as one whose files are gone.
            const auto link = media::linkOf(*clip);
            bool online = link && !link->unlinked;
            if (online) {
                for (const QString& file : link->files()) online = online && QFileInfo::exists(file);
            }
            it = online_.insert(key, {online, now});
        }
        f.offline = !it->first;
        const auto link = media::linkOf(*clip);
        f.linked = link && !link->isManaged();
    }
    // Mixed rates: the clip's FPS column against the sequence's edit rate.
    const QString fps = clip->fields.value(QStringLiteral("FPS"));
    const model::LoadedClip* rec = composer_.state(shownMonitor()).current();
    if (!fps.isEmpty() && rec) {
        bool ok = false;
        const double rate = fps.toDouble(&ok);
        if (ok && std::abs(rate - rec->item.editRate.toDouble()) > 0.01) {
            for (const QString& r : model::mixedRates()) {
                if (std::abs(r.toDouble() - rate) < 0.01) f.rate = r;
            }
        }
    }
    // SD/HD: the clip's Format column ("1080p/25") against the project's raster.
    const QString format = clip->fields.value(QStringLiteral("Format"));
    if (!format.isEmpty()) {
        int lines = 0;
        for (const QChar c : format) {
            if (!c.isDigit()) break;
            lines = lines * 10 + c.digitValue();
        }
        if (lines > 0) f.otherDefinition = (lines <= 576) != (projectHeight_ <= 576);
    }
    return f;
}

QColor Timeline::trackColor(const model::Track& track) {
    QColor fill(track.kind == model::TrackKind::Video   ? kVideoSegment
                : track.kind == model::TrackKind::Audio ? kAudioSegment
                                                        : kDataSegment);
    if (!track.color.isEmpty()) fill = QColor(track.color);
    if (track.inactive) fill = fill.darker(170);
    return fill;
}

QString Timeline::segmentColor(const model::Segment& segment) const {
    if (segment.isFiller()) return {};
    return model::timelineClipColor(clipColors_, segment, clipFacts(segment));
}

void Timeline::setProjectHeight(int height) {
    if (height <= 0 || height == projectHeight_) return;
    projectHeight_ = height;
    view_->update();
}

void Timeline::setWaveformFolder(const QString& folder) {
    waveforms_->setFolder(folder);
}

void Timeline::setWaveform(bool on) {
    display_.waveform = on;
    view_->update();
}

void Timeline::setWaveformLevels(bool output, bool fullHeight) {
    display_.outputLevels = output;
    display_.fullHeight = fullHeight;
    view_->update();
}

bool Timeline::zoomWaveform(int steps) {
    const int to = std::clamp(display_.waveformZoom + steps, -3, 4);
    if (to == display_.waveformZoom) return false;
    display_.waveformZoom = to;
    view_->update();
    return true;
}

// ---------------------------------------------------------------- per track settings (U1b)

bool Timeline::showsWaveform(const model::Track& track) const {
    if (track.kind != model::TrackKind::Audio) return false;
    return display_.waveform || (display_.perTrack && track.showWaveform);
}

bool Timeline::showsClipVolume(const model::Track& track) const {
    if (track.kind != model::TrackKind::Audio) return false;
    return display_.clipVolume || (display_.perTrack && track.showClipVolume);
}

std::optional<model::AutomationParam> Timeline::automationShown(const model::Track& track) const {
    if (track.kind != model::TrackKind::Audio) return std::nullopt;
    if (display_.perTrack && track.showVolume) return model::AutomationParam::Volume;
    if (display_.perTrack && track.showPan) return model::AutomationParam::Pan;
    return display_.automation;
}

void Timeline::setPerTrackSettings(bool on) {
    display_.perTrack = on;
    syncScrollBars();
    view_->update();
}

// Per track settings are kept in the record sequence without an undo step, as locks are.
void Timeline::changeTracks(const QStringList& tracks, const std::function<void(model::Track&)>& change) {
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    bool any = false;
    for (const QString& name : tracks) {
        if (model::Track* t = seq.track(name)) {
            change(*t);
            any = true;
        }
    }
    if (!any) return;
    storeLocks(seq);
    syncScrollBars();
    view_->update();
}

QStringList Timeline::audioTracksFor(const QString& track, bool all) const {
    if (!all) return {track};
    QStringList out;
    const model::Sequence seq = recordSequenceOf(composer_);
    for (const model::Track& t : seq.tracks()) {
        if (t.kind == model::TrackKind::Audio) out << t.name();
    }
    return out;
}

void Timeline::setTrackWaveform(const QString& track, bool on, bool all) {
    display_.perTrack = true;
    changeTracks(audioTracksFor(track, all), [on](model::Track& t) { t.showWaveform = on; });
    view_->update();
}

void Timeline::setTrackAudioData(const QString& track, bool clipVolume, std::optional<model::AutomationParam> automation,
                                 bool all) {
    display_.perTrack = true;
    changeTracks(audioTracksFor(track, all), [&](model::Track& t) {
        t.showClipVolume = clipVolume;
        t.showVolume = automation == model::AutomationParam::Volume;
        t.showPan = automation == model::AutomationParam::Pan;
    });
    view_->update();
}

// "Click the Clip Volume/Pan button ... and select Clip Volume or Volume"; "repeat the previous step
// and select an additional volume value" shows both (p.679-680). Volume and Pan are one or the
// other. Pan L and Pan R > [speaker layout] for surround sequences are not offered yet.
QMenu* Timeline::trackAudioDataMenu(const QString& track, bool all, QWidget* parent) {
    auto* menu = new QMenu(parent);
    const model::Sequence seq = recordSequenceOf(composer_);
    const model::Track* t = seq.track(track);
    const bool perTrack = display_.perTrack && t;
    const bool clip = perTrack && t->showClipVolume;
    std::optional<model::AutomationParam> automation;
    if (perTrack && t->showVolume) automation = model::AutomationParam::Volume;
    if (perTrack && t->showPan) automation = model::AutomationParam::Pan;
    auto item = [&](const QString& text, bool on, std::function<void()> apply) {
        QAction* a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(on);
        connect(a, &QAction::triggered, this, std::move(apply));
    };
    item(tr("Clip Volume"), clip, [=, this] { setTrackAudioData(track, !clip, automation, all); });
    item(tr("Volume"), automation == model::AutomationParam::Volume, [=, this] {
        const bool on = automation == model::AutomationParam::Volume;
        setTrackAudioData(track, clip, on ? std::nullopt : std::optional(model::AutomationParam::Volume), all);
    });
    item(tr("Pan"), automation == model::AutomationParam::Pan, [=, this] {
        const bool on = automation == model::AutomationParam::Pan;
        setTrackAudioData(track, clip, on ? std::nullopt : std::optional(model::AutomationParam::Pan), all);
    });
    return menu;
}

void Timeline::setTrackInactive(const QString& track, bool inactive) {
    changeTracks({track}, [inactive](model::Track& t) {
        if (t.kind == model::TrackKind::Audio) t.inactive = inactive;
    });
    // What plays changes with it.
    emit sequenceEdited();
}

void Timeline::setTrackControlPanel(bool on) {
    display_.trackControlPanel = on;
    syncScrollBars();
    view_->update();
}

void Timeline::setSoloMuteState(std::function<bool(const QString&, bool)> state) {
    soloMuteState_ = std::move(state);
    view_->update();
}

bool Timeline::isSoloMuted(const QString& track, bool soloed) const {
    return soloMuteState_ && soloMuteState_(track, soloed);
}

bool Timeline::resizeTracks(int steps) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    const model::Sequence seq = model::Sequence::fromItem(rec->item);
    const QStringList tracks = patch_.selectedRecordTracks(seq);
    if (tracks.isEmpty()) return fail(tr("Select the tracks to enlarge or reduce."));
    bool changed = false;
    for (const QString& name : tracks) {
        const model::Track* t = seq.track(name);
        const int to = std::clamp(t->heightStep + steps, -model::kTrackHeightSteps / 2, model::kTrackHeightSteps);
        changed = changed || to != t->heightStep;
    }
    if (!changed) return false;
    changeTracks(tracks, [steps](model::Track& t) {
        t.heightStep = std::clamp(t.heightStep + steps, -model::kTrackHeightSteps / 2, model::kTrackHeightSteps);
    });
    return true;
}

int Timeline::trackHeight(const QString& track) const {
    int h = rowHeight(track);
    const model::Track* t = hasShown() ? shown().track(track) : nullptr;
    if (!t) return h;
    // Audio rows are taller while they show a waveform, a line or a graph (ours), and tall enough
    // for both lines of the Track Control panel.
    if (t->kind == model::TrackKind::Audio) {
        if (showsWaveform(*t) || showsClipVolume(*t) || automationShown(*t)) h = kAudioDataRow;
        if (display_.trackControlPanel && display_.trackPanel) h = std::max(h, 2 * kControlLine + 6);
    }
    return std::max(h + t->heightStep * kTrackStep, kMinRow);
}

void Timeline::setTrackColor(const QStringList& tracks, const QString& hex) {
    changeTracks(tracks, [hex](model::Track& t) { t.color = hex; });
}

void Timeline::setSelectionColors(const QString& segment, const QString& markIO) {
    display_.segmentSelectionColor = segment;
    display_.markIOColor = markIO;
    view_->update();
}

void Timeline::setMarkersShown(const QString& color, bool shown) {
    if (shown) {
        display_.hiddenMarkers.remove(color);
    } else {
        display_.hiddenMarkers.insert(color);
    }
    view_->update();
}

void Timeline::setAllMarkersShown(bool shown) {
    display_.hiddenMarkers.clear();
    if (!shown) {
        for (const model::MarkerColor& c : model::markerColors()) display_.hiddenMarkers.insert(c.name);
    }
    view_->update();
}

// ---------------------------------------------------------------- display, views and View Type (U1b2)

QJsonObject Timeline::displayToJson(const Display& d) {
    QJsonObject o{
        {QStringLiteral("trackPanel"), d.trackPanel},
        {QStringLiteral("clipNames"), d.clipNames},
        {QStringLiteral("clipDurations"), d.clipDurations},
        {QStringLiteral("clipTracks"), d.clipTracks},
        {QStringLiteral("timecodeTrack"), d.timecodeTrack},
        {QStringLiteral("effectIcons"), d.effectIcons},
        {QStringLiteral("dissolveIcons"), d.dissolveIcons},
        {QStringLiteral("clipFrames"), d.clipFrames},
        {QStringLiteral("clipVolume"), d.clipVolume},
        {QStringLiteral("waveform"), d.waveform},
        {QStringLiteral("outputLevels"), d.outputLevels},
        {QStringLiteral("fullHeight"), d.fullHeight},
        {QStringLiteral("waveformZoom"), d.waveformZoom},
        {QStringLiteral("perTrack"), d.perTrack},
        {QStringLiteral("trackControlPanel"), d.trackControlPanel},
        {QStringLiteral("segmentSelectionColor"), d.segmentSelectionColor},
        {QStringLiteral("markIOColor"), d.markIOColor},
        {QStringLiteral("dupeVideo"), d.dupeVideo},
        {QStringLiteral("dupeAudio"), d.dupeAudio},
        {QStringLiteral("dupeData"), d.dupeData},
        {QStringLiteral("clipFullDurations"), d.clipFullDurations},
        {QStringLiteral("clipTransDuration"), d.clipTransDuration},
        {QStringLiteral("sourceNames"), d.sourceNames},
        {QStringLiteral("mediaFullNames"), d.mediaFullNames},
        {QStringLiteral("clipResolution"), d.clipResolution},
        {QStringLiteral("clipNotes"), d.clipNotes},
        {QStringLiteral("titleText"), d.titleText},
        {QStringLiteral("motionAdapters"), d.motionAdapters},
        {QStringLiteral("clipFontFamily"), d.clipFontFamily},
        {QStringLiteral("clipFontSize"), d.clipFontSize},
        {QStringLiteral("syncBreaks"), d.syncBreaks == SyncBreaks::Video   ? QStringLiteral("video")
                                       : d.syncBreaks == SyncBreaks::Audio ? QStringLiteral("audio")
                                       : d.syncBreaks == SyncBreaks::None  ? QStringLiteral("none")
                                                                                    : QStringLiteral("all")},
        {QStringLiteral("renderRanges"), d.renderRanges == RenderRanges::None  ? QStringLiteral("none")
                                         : d.renderRanges == RenderRanges::All ? QStringLiteral("all")
                                                                               : QStringLiteral("partial")},
    };
    if (d.automation) {
        o.insert(QStringLiteral("automation"),
                 *d.automation == model::AutomationParam::Volume ? QStringLiteral("volume") : QStringLiteral("pan"));
    }
    QJsonArray hidden;
    QStringList markers(d.hiddenMarkers.begin(), d.hiddenMarkers.end());
    markers.sort();
    for (const QString& m : markers) hidden.append(m);
    o.insert(QStringLiteral("hiddenMarkers"), hidden);
    o.insert(QStringLiteral("viewType"), d.viewType == ViewType::Heads        ? QStringLiteral("heads")
                                         : d.viewType == ViewType::HeadsTails ? QStringLiteral("headsTails")
                                                                              : QStringLiteral("standard"));
    return o;
}

Timeline::Display Timeline::displayFromJson(const QJsonObject& o) {
    Display d;
    auto flag = [&o](const char* key, bool& field) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isBool()) field = v.toBool();
    };
    flag("trackPanel", d.trackPanel);
    flag("clipNames", d.clipNames);
    flag("clipDurations", d.clipDurations);
    flag("clipTracks", d.clipTracks);
    flag("timecodeTrack", d.timecodeTrack);
    flag("effectIcons", d.effectIcons);
    flag("dissolveIcons", d.dissolveIcons);
    flag("clipFrames", d.clipFrames);
    flag("clipVolume", d.clipVolume);
    flag("waveform", d.waveform);
    flag("outputLevels", d.outputLevels);
    flag("fullHeight", d.fullHeight);
    flag("perTrack", d.perTrack);
    flag("trackControlPanel", d.trackControlPanel);
    flag("dupeVideo", d.dupeVideo);
    flag("dupeAudio", d.dupeAudio);
    flag("dupeData", d.dupeData);
    flag("clipFullDurations", d.clipFullDurations);
    flag("clipTransDuration", d.clipTransDuration);
    flag("sourceNames", d.sourceNames);
    flag("mediaFullNames", d.mediaFullNames);
    flag("clipResolution", d.clipResolution);
    flag("clipNotes", d.clipNotes);
    flag("titleText", d.titleText);
    flag("motionAdapters", d.motionAdapters);
    const QString sync = o.value(QStringLiteral("syncBreaks")).toString();
    d.syncBreaks = sync == QLatin1String("video")  ? SyncBreaks::Video
                   : sync == QLatin1String("audio") ? SyncBreaks::Audio
                   : sync == QLatin1String("none")  ? SyncBreaks::None
                                                    : SyncBreaks::All;
    const QString ranges = o.value(QStringLiteral("renderRanges")).toString();
    d.renderRanges = ranges == QLatin1String("none") ? RenderRanges::None
                     : ranges == QLatin1String("all") ? RenderRanges::All
                                                      : RenderRanges::PartialOnly;
    d.waveformZoom = std::clamp(o.value(QStringLiteral("waveformZoom")).toInt(), -3, 4);
    const QString automation = o.value(QStringLiteral("automation")).toString();
    if (automation == QLatin1String("volume")) d.automation = model::AutomationParam::Volume;
    if (automation == QLatin1String("pan")) d.automation = model::AutomationParam::Pan;
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    for (auto [key, field] : {std::pair{"segmentSelectionColor", &d.segmentSelectionColor}, std::pair{"markIOColor", &d.markIOColor}}) {
        const QString c = o.value(QLatin1String(key)).toString();
        if (hex.match(c).hasMatch()) *field = c;
    }
    for (const QJsonValue& v : o.value(QStringLiteral("hiddenMarkers")).toArray()) d.hiddenMarkers.insert(v.toString());
    const QString view = o.value(QStringLiteral("viewType")).toString();
    d.viewType = view == QLatin1String("heads")        ? ViewType::Heads
                 : view == QLatin1String("headsTails") ? ViewType::HeadsTails
                                                       : ViewType::Standard;
    d.clipFontFamily = o.value(QStringLiteral("clipFontFamily")).toString();
    d.clipFontSize = std::max(0, o.value(QStringLiteral("clipFontSize")).toInt());
    return d;
}

QFont Timeline::clipFont() const {
    QFont f = view_->font();
    if (!display_.clipFontFamily.isEmpty()) f.setFamily(display_.clipFontFamily);
    if (display_.clipFontSize > 0) f.setPointSize(display_.clipFontSize);
    return f;
}

void Timeline::setDisplay(const Display& d) {
    if (display_.automation != d.automation) keySelection_.clear();
    display_ = d;
    syncScrollBars();
    view_->update();
    emit changed();
}

void Timeline::setViewType(ViewType type) {
    display_.viewType = type;
    syncScrollBars();
    view_->update();
}

bool Timeline::showsHeads() const {
    return display_.viewType != ViewType::Standard && !showSource_ && hasShown();
}

// The selected record tracks, or every track when none is selected.
QStringList Timeline::headsTracks() const {
    const model::Sequence seq = recordSequenceOf(composer_);
    QStringList tracks = patch_.selectedRecordTracks(seq);
    if (tracks.isEmpty()) tracks = seq.trackNames();
    return tracks;
}

QList<std::pair<std::int64_t, std::int64_t>> Timeline::headsSpans() const {
    return recordSequenceOf(composer_).headsSpans(headsTracks());
}

// "If you rearrange a split edit ... the system cuts all tracks to the same edit point" (p.638):
// model::Sequence::moveSpan moves the clip's frames on every selected track.
// ---------------------------------------------------------------- Sync Breaks, Dupe Detection, Clip Text (U1c1)

void Timeline::setExpertSelection(const QList<model::RenderTarget>& targets, bool modifying) {
    expert_ = targets;
    modifyingExpert_ = modifying;
    view_->update();
}

void Timeline::cancelExpertSelection() {
    if (expert_.isEmpty() && !modifyingExpert_) return;
    expert_.clear();
    modifyingExpert_ = false;
    view_->update();
    emit expertSelectionCancelled();
}

bool Timeline::toggleExpertTarget(const QString& track, std::int64_t frame) {
    const auto target = model::renderTargetAt(shown(), track, frame);
    if (!target) return false;
    if (!expert_.removeOne(*target)) expert_ << *target;
    view_->update();
    return true;
}

void Timeline::setPlaybackIndicators(const QList<Composer::PlaybackRange>& ranges) {
    if (indicators_ == ranges) return;
    indicators_ = ranges;
    view_->update();
}

void Timeline::setSyncBreaks(SyncBreaks which) {
    display_.syncBreaks = which;
    view_->update();
}

void Timeline::setDupeDetection(model::TrackKind kind, bool on) {
    (kind == model::TrackKind::Video ? display_.dupeVideo : kind == model::TrackKind::Audio ? display_.dupeAudio : display_.dupeData) = on;
    view_->update();
}

bool Timeline::showsSyncBreaks(model::TrackKind kind) const {
    switch (display_.syncBreaks) {
    case SyncBreaks::All: return true;
    case SyncBreaks::Video: return kind == model::TrackKind::Video;
    case SyncBreaks::Audio: return kind == model::TrackKind::Audio;
    case SyncBreaks::None: return false;
    }
    return false;
}

// The Clip Text submenu's items (p.592-593) in its order. The source clip's facts come from its bin
// item: Clip Full Durations its whole duration, Source Names its Tape column or else its Source File,
// Media Full Names the name of the media file the segment's track plays from, Clip Resolution its
// Video column (ours: the guide gives no examples of these four).
QString Timeline::clipText(const model::Track& track, const model::Segment& s) const {
    const auto& d = display_;
    const model::MonitorState& state = composer_.state(shownMonitor());
    QStringList parts;
    // "Mixed rate clips also display with their original frame rate appended to the clip name"
    // (p.470): on video tracks (ours: the guide's picture shows a video track).
    if (d.clipNames) {
        const bool mixed = s.adapter && track.kind == model::TrackKind::Video && !(s.adapter->clipRate == s.adapter->sequenceRate);
        parts << (mixed ? model::mixedRateName(s.name, s.adapter->clipRate) : s.name);
    }
    if (d.clipTracks) parts << s.sourceTrack;
    if (d.clipDurations) parts << state.durationText(s.length);
    const bool needClip = d.clipFullDurations || d.sourceNames || d.mediaFullNames || d.clipResolution;
    const std::optional<model::BinItem> clip = needClip ? composer_.resolve(s.binId, s.clipId) : std::nullopt;
    if (d.clipFullDurations && clip) parts << state.durationText(clip->durationFrames);
    if (d.clipTransDuration && s.transition) parts << state.durationText(s.transition->duration);
    // "You must enable the Timeline Fast menu > Clip Text > Timeline Clip Notes to display the notes
    // in the Timeline" (p.483).
    if (d.clipNotes) parts << s.note;
    // "A label called "Title Text" appears in the Timeline on segments created with Avid Titler+ ... based on their
    // topmost Titler+ text content" (FX Guide p.305).
    if (d.titleText && s.effect && model::isTitlerEffect(*s.effect)) parts << model::titleOf(*s.effect).topText();
    if (d.sourceNames && clip) {
        const QString tape = model::cellText(*clip, QStringLiteral("Tape"));
        parts << (tape.isEmpty() ? model::cellText(*clip, QStringLiteral("Source File")) : tape);
    }
    if (d.mediaFullNames && clip) {
        if (const auto link = media::linkOf(*clip)) {
            QString file = link->file();
            if (track.kind == model::TrackKind::Audio) {
                const auto ref = model::parseTrackName(s.sourceTrack);
                if (ref && ref->kind == model::TrackKind::Audio) file = link->audioFile(ref->number - 1);
            }
            if (!file.isEmpty()) parts << QFileInfo(file).fileName();
        }
    }
    if (d.clipResolution && clip) parts << model::cellText(*clip, QStringLiteral("Video"));
    parts.removeAll(QString());
    return parts.join(QStringLiteral("  "));
}

void Timeline::setViews(const QStringList& names, const QString& current) {
    views_ = names;
    currentView_ = current;
    if (QToolButton* b = toolButton(QStringLiteral("viewMenu"))) {
        b->setText(current.isEmpty() ? tr("Untitled") : current);
    }
}

// "Click the View Menu button, and select Save As. The View Name dialog box opens. Type a name for the
// view"; Alt+click lists "saved view names, each appended with the Replace command" (p.604).
QMenu* Timeline::viewMenu(bool replace, QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("timelineViewMenu"));
    for (const QString& name : views_) {
        if (replace) {
            connect(menu->addAction(tr("%1 Replace").arg(name)), &QAction::triggered, this,
                    [this, name] { emit viewSaveRequested(name, true); });
            continue;
        }
        QAction* a = menu->addAction(name);
        a->setCheckable(true);
        a->setChecked(name == currentView_);
        connect(a, &QAction::triggered, this, [this, name] { emit viewRequested(name); });
    }
    if (replace) return menu;
    if (!views_.isEmpty()) menu->addSeparator();
    connect(menu->addAction(tr("Save As...")), &QAction::triggered, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("View Name"), tr("View name:"), QLineEdit::Normal,
                                                   currentView_, &ok).trimmed();
        if (ok && !name.isEmpty()) emit viewSaveRequested(name, false);
    });
    return menu;
}

bool Timeline::moveHeads(int from, int before) {
    message_.clear();
    const model::LoadedClip* rec = recordSequence();
    if (!rec) return false;
    model::Sequence seq = model::Sequence::fromItem(rec->item);
    const QStringList tracks = headsTracks();
    const auto spans = seq.headsSpans(tracks);
    if (from < 0 || from >= spans.size() || before < 0 || before > spans.size()) return false;
    const auto [start, end] = spans.at(from);
    const std::int64_t to = before < spans.size() ? spans.at(before).first : spans.constLast().second;
    if (!seq.moveSpan(tracks, start, end - start, to)) return fail(tr("The clip cannot be moved there, or a track is locked."));
    return commitSequence(tr("Move Clip"), seq);
}

QList<int> Timeline::waveformChannels(model::AudioFormat format) const {
    using media::Speaker;
    const QList<Speaker> speakers = media::trackSpeakers(format);
    // The monitor orders of p.689: 5.1 SMPTE L R C Lfe Ls Rs and Pro Tools L C R Ls Rs Lfe; 7.1 SMPTE DS
    // L R C Lfe Lss Rss Lsr Rsr and Pro Tools L C R Lss Rss Lsr Rsr Lfe.
    const QList<Speaker> order =
        options_.proToolsChannelOrder
            ? QList<Speaker>{Speaker::L, Speaker::C, Speaker::R, Speaker::Lss, Speaker::Rss, Speaker::Lsr, Speaker::Rsr, Speaker::Lfe}
            : QList<Speaker>{Speaker::L, Speaker::R, Speaker::C, Speaker::Lfe, Speaker::Lss, Speaker::Rss, Speaker::Lsr, Speaker::Rsr};
    QList<int> out;
    for (int k = 0; k < speakers.size(); ++k) out << k;
    if (format != model::AudioFormat::Surround51 && format != model::AudioFormat::Surround71) return out;
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return order.indexOf(speakers.at(a)) < order.indexOf(speakers.at(b)); });
    return out;
}

void Timeline::setMismatchedRates(bool show, int projectRate) {
    if (show == showMismatchedRates_ && projectRate == projectSampleRate_) return;
    showMismatchedRates_ = show;
    projectSampleRate_ = projectRate;
    view_->update();
}

void Timeline::setSourceRecordColors(bool on) {
    if (on == sourceRecordColors_) return;
    sourceRecordColors_ = on;
    view_->update();
}

} // namespace lumenis::ui
