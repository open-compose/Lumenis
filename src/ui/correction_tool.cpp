// The Color Correction tool (correction_tool.h; FX Guide p.595-605, p.631-644): its tabs and Enable buttons, and the
// HSL group's panes. Every change goes to the Timeline as an undo step on the current segment's correction.

#include "lumenis/ui/correction_tool.h"

#include "lumenis/media/color_correction.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonDocument>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

namespace lumenis::ui {

namespace {

// A tool button that starts a drag with what `mime` makes (nothing when it makes none): the template button and the
// buckets (p.613-614). A click without a drag is a click.
class DragButton : public QToolButton {
public:
    DragButton(QWidget* parent, std::function<QMimeData*()> mime) : QToolButton(parent), mime_(std::move(mime)) {}

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) pressed_ = event->position().toPoint();
        QToolButton::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (pressed_ && (event->buttons() & Qt::LeftButton) &&
            (event->position().toPoint() - *pressed_).manhattanLength() >= QApplication::startDragDistance()) {
            pressed_.reset();
            if (QMimeData* made = mime_()) {
                setDown(false);
                auto* drag = new QDrag(this);
                drag->setMimeData(made);
                drag->setPixmap(icon().pixmap(20, 20));
                drag->exec(Qt::CopyAction);
                return;
            }
        }
        QToolButton::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        pressed_.reset();
        QToolButton::mouseReleaseEvent(event);
    }

private:
    std::function<QMimeData*()> mime_;
    std::optional<QPoint> pressed_;
};

// A template as correctionTemplateMime() describes it.
QMimeData* correctionTemplateMimeData(const model::CorrectionTemplate& t, const QString& name) {
    auto* mime = new QMimeData;
    QJsonObject o{{QStringLiteral("name"), name}, {QStringLiteral("template"), t.toJson()}};
    mime->setData(correctionTemplateMime(), QJsonDocument(o).toJson(QJsonDocument::Compact));
    return mime;
}


const char* const kGroups[] = {"hsl", "channels", "levels", "curves", "secondary"};
const char* const kGroupNames[] = {QT_TRANSLATE_NOOP("CorrectionTool", "HSL"), QT_TRANSLATE_NOOP("CorrectionTool", "Channels"),
                                   QT_TRANSLATE_NOOP("CorrectionTool", "Levels"), QT_TRANSLATE_NOOP("CorrectionTool", "Curves"),
                                   QT_TRANSLATE_NOOP("CorrectionTool", "Sec")};
// The Controls tab's second level, "Master, Highlights, Midtones, Shadows" (p.632); the Luma Ranges tab's, "Highlights,
// Midtones, Shadows" (p.642).
const char* const kRanges[] = {"master", "highlights", "midtones", "shadows"};
const char* const kRangeNames[] = {QT_TRANSLATE_NOOP("CorrectionTool", "Master"), QT_TRANSLATE_NOOP("CorrectionTool", "Highlights"),
                                   QT_TRANSLATE_NOOP("CorrectionTool", "Midtones"), QT_TRANSLATE_NOOP("CorrectionTool", "Shadows")};
// "The wheels appear in the following order from left to right: Shadows, Midtones, Highlights, and Master" (p.635).
const char* const kWheels[] = {"shadows", "midtones", "highlights", "master"};

// The angle of red in the CbCr plane (Rec. 709) on screen, Cb to the right and Cr up: the vectorscope's, "with red
// near the top" (p.636); the hues go counterclockwise from it.
double redScreenAngle() {
    constexpr double kR = 0.2126, kB = 0.0722;
    return std::atan2(0.5, -kR / (2.0 * (1.0 - kB)));
}

QString label(const QString& path) {
    const QString name = path.section(QLatin1Char('/'), -1);
    static const std::pair<const char*, const char*> names[] = {
        {"hue", QT_TRANSLATE_NOOP("CorrectionTool", "Hue")},
        {"saturation", QT_TRANSLATE_NOOP("CorrectionTool", "Saturation")},
        {"brightness", QT_TRANSLATE_NOOP("CorrectionTool", "Brightness")},
        {"contrast", QT_TRANSLATE_NOOP("CorrectionTool", "Contrast")},
        {"gain", QT_TRANSLATE_NOOP("CorrectionTool", "Gain")},
        {"gamma", QT_TRANSLATE_NOOP("CorrectionTool", "Gamma")},
        {"setup", QT_TRANSLATE_NOOP("CorrectionTool", "Setup")},
        {"clipLow", QT_TRANSLATE_NOOP("CorrectionTool", "Clip Low")},
        {"clipHigh", QT_TRANSLATE_NOOP("CorrectionTool", "Clip High")},
        {"invertChroma", QT_TRANSLATE_NOOP("CorrectionTool", "Invert Chroma")},
        {"invertLuma", QT_TRANSLATE_NOOP("CorrectionTool", "Invert Luma")},
        {"masterSaturation", QT_TRANSLATE_NOOP("CorrectionTool", "Master Saturation")},
        {"masterGain", QT_TRANSLATE_NOOP("CorrectionTool", "Master Gain")},
        {"masterGamma", QT_TRANSLATE_NOOP("CorrectionTool", "Master Gamma")},
        {"masterSetup", QT_TRANSLATE_NOOP("CorrectionTool", "Master Setup")},
    };
    for (const auto& [key, text] : names) {
        if (name == QLatin1String(key)) return CorrectionTool::tr(text);
    }
    return name;
}

// The tool buttons' icons (ours): a mode square with a star, a template sheet, a sliders panel, a gamut triangle and a
// speech bubble, drawn in `ink` (the Comments bubble yellow when the segment has a comment, p.609).
QIcon toolIcon(const QString& key, const QColor& ink) {
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ink, 1.3));
    if (key == QLatin1String("effectMode")) {
        p.drawRect(QRectF(2.5, 4.5, 13, 10));
        p.drawLine(QPointF(9, 6.5), QPointF(9, 12.5));
        p.drawLine(QPointF(6, 9.5), QPointF(12, 9.5));
    } else if (key == QLatin1String("template")) {
        p.drawRect(QRectF(4.5, 2.5, 9, 13));
        for (double y : {6.0, 9.0, 12.0}) p.drawLine(QPointF(6.5, y), QPointF(11.5, y));
    } else if (key == QLatin1String("settings")) {
        for (double y : {5.0, 9.0, 13.0}) p.drawLine(QPointF(3, y), QPointF(15, y));
        p.setBrush(ink);
        p.drawEllipse(QPointF(6, 5), 1.6, 1.6);
        p.drawEllipse(QPointF(12, 9), 1.6, 1.6);
        p.drawEllipse(QPointF(8, 13), 1.6, 1.6);
    } else if (key == QLatin1String("safeColors")) {
        p.drawPolygon(QPolygonF{QPointF(9, 2.5), QPointF(15.5, 14.5), QPointF(2.5, 14.5)});
    } else {
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(2.5, 3.5, 13, 9), 3, 3);
        bubble.moveTo(6, 12.5);
        bubble.lineTo(5, 16);
        bubble.lineTo(9, 12.5);
        p.drawPath(bubble);
    }
    return QIcon(pm);
}

// The small Enable button of a tab or control (p.603): lit when on.
QToolButton* enableButtonFor(QWidget* parent, const QString& path) {
    auto* b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("enable:") + path);
    b->setCheckable(true);
    b->setFixedSize(14, 14);
    b->setToolTip(CorrectionTool::tr("Enable (Alt+click resets)"));
    return b;
}

} // namespace

QToolButton* correctionActionButton(QWidget* parent, const QString& name, const QString& text, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

QMenu* CorrectionTool::shortcutMenu(QWidget* parent) {
    auto* menu = new QMenu(parent);
    const std::pair<const char*, const char*> items[] = {{"cc.update_correction", QT_TR_NOOP("Update Correction")},
                                                         {"cc.merge_correction", QT_TR_NOOP("Merge Correction")},
                                                         {"cc.flatten_correction", QT_TR_NOOP("Flatten Correction")},
                                                         {"cc.remove_correction", QT_TR_NOOP("Remove Correction")}};
    for (const auto& [id, text] : items) {
        QAction* a = menu->addAction(tr(text));
        const QString command = QString::fromLatin1(id);
        a->setObjectName(command);
        if (command == QLatin1String("cc.merge_correction")) a->setEnabled(timeline_.canMergeCorrections());
        // Queued: the command's dialog opens after the menu has closed.
        connect(a, &QAction::triggered, this, [this, command] { timeline_.perform(command); }, Qt::QueuedConnection);
    }
    return menu;
}

void CorrectionTool::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = shortcutMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

CorrectionTool::CorrectionTool(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("correctionTool"));
    setFocusPolicy(Qt::StrongFocus);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // Top: the Correction Type button and the group tabs (p.596, p.599).
    auto* top = new QHBoxLayout;
    top->setSpacing(4);
    type_ = new QToolButton(this);
    type_->setObjectName(QStringLiteral("correctionType"));
    type_->setPopupMode(QToolButton::InstantPopup);
    auto* types = new QMenu(type_);
    // "Options for Relationship Color Corrections" (p.597) and the Color Correction effect (p.596; with 6.10e).
    using model::CorrectionRelationship;
    for (const CorrectionRelationship r : {CorrectionRelationship::SourceTape, CorrectionRelationship::SourceClipName,
                                           CorrectionRelationship::MasterClip, CorrectionRelationship::SubClip,
                                           CorrectionRelationship::SourceSegment, CorrectionRelationship::ProgramTrack,
                                           CorrectionRelationship::ProgramSegment}) {
        QAction* a = types->addAction(tr(qPrintable(model::relationshipText(r))));
        a->setCheckable(true);
        a->setData(static_cast<int>(r));
        a->setObjectName(QStringLiteral("type:") + model::relationshipKey(r));
        connect(a, &QAction::triggered, this, [this, r] {
            timeline_.setCorrectionRelationship(r);
            reload();
        });
    }
    types->addAction(tr("Color Correction Effect"))->setEnabled(false);
    type_->setMenu(types);
    top->addWidget(type_);
    for (int i = 0; i < 5; ++i) top->addWidget(tab(this, QLatin1String(kGroups[i]), tr(kGroupNames[i])));
    top->addStretch(1);
    // The tool buttons on the right (p.605), each with an icon of our own.
    const std::pair<const char*, const char*> buttons[] = {
        {"effectMode", QT_TR_NOOP("Effect Mode")},
        {"template", QT_TR_NOOP("Color Correction Effect Template")},
        {"settings", QT_TR_NOOP("Correction Mode Settings")},
        {"safeColors", QT_TR_NOOP("Safe Color Settings")},
        {"comments", QT_TR_NOOP("Comments")}};
    for (const auto& [key, text] : buttons) {
        // The template button is dragged: "Click the Color Correction Effect Template button, press and hold the mouse
        // button, and then drag the effect icon to a bin" (p.613).
        auto* b = QLatin1String(key) == QLatin1String("template") ? new DragButton(this, [this] { return templateMime(); })
                                                                   : new QToolButton(this);
        b->setObjectName(QStringLiteral("ccButton:") + QLatin1String(key));
        b->setToolTip(tr(text));
        b->setAutoRaise(true);
        b->setIcon(toolIcon(QLatin1String(key), palette().color(QPalette::WindowText)));
        top->addWidget(b);
    }
    button(QStringLiteral("safeColors"))->setEnabled(false); // 6.10e
    connect(button(QStringLiteral("effectMode")), &QToolButton::clicked, this, &CorrectionTool::effectModeRequested);
    connect(button(QStringLiteral("settings")), &QToolButton::clicked, this, &CorrectionTool::settingsRequested);
    connect(button(QStringLiteral("comments")), &QToolButton::clicked, this, &CorrectionTool::editComment);
    outer->addLayout(top);

    groups_ = new QStackedWidget(this);
    // The HSL group: the subdividing tabs on the left (p.599-601) and the panes.
    auto* hsl = new QWidget(groups_);
    auto* hslRow = new QHBoxLayout(hsl);
    hslRow->setContentsMargins(0, 0, 0, 0);
    auto* first = new QWidget(hsl);
    auto* firstColumn = new QVBoxLayout(first);
    firstColumn->setContentsMargins(0, 0, 0, 0);
    firstColumn->setSpacing(1);
    firstColumn->addWidget(tab(first, QStringLiteral("hsl/controls"), tr("Controls")));
    firstColumn->addWidget(tab(first, QStringLiteral("hsl/hueOffsets"), tr("Hue Offsets")));
    firstColumn->addWidget(tab(first, QStringLiteral("hsl/lumaRanges"), tr("Luma Ranges")));
    firstColumn->addStretch(1);
    hslRow->addWidget(first);
    rangeColumn_ = new QWidget(hsl);
    auto* second = new QVBoxLayout(rangeColumn_);
    second->setContentsMargins(0, 0, 0, 0);
    second->setSpacing(1);
    for (int i = 0; i < 4; ++i) second->addWidget(tab(rangeColumn_, QStringLiteral("hsl/controls/") + QLatin1String(kRanges[i]), tr(kRangeNames[i])));
    second->addStretch(1);
    hslRow->addWidget(rangeColumn_);
    lumaColumn_ = new QWidget(hsl);
    auto* luma = new QVBoxLayout(lumaColumn_);
    luma->setContentsMargins(0, 0, 0, 0);
    luma->setSpacing(1);
    for (int i = 1; i < 4; ++i) luma->addWidget(tab(lumaColumn_, QStringLiteral("hsl/lumaRanges/") + QLatin1String(kRanges[i]), tr(kRangeNames[i])));
    luma->addStretch(1);
    hslRow->addWidget(lumaColumn_);
    hslPages_ = new QStackedWidget(hsl);
    hslPages_->addWidget(controlsPane());
    hslPages_->addWidget(hueOffsetsPane());
    hslPages_->addWidget(lumaRangesPane());
    hslRow->addWidget(hslPages_, 1);
    groups_->addWidget(hsl);
    // Each group's Color Match control under its pane (p.624).
    auto withMatch = [this](QWidget* pane, media::MatchGroup group) {
        auto* box = new QWidget;
        auto* v = new QVBoxLayout(box);
        v->setContentsMargins(0, 0, 0, 0);
        v->addWidget(pane, 1);
        v->addWidget(colorMatchFor(group, box));
        return box;
    };
    groups_->addWidget(withMatch(channelsPane(), media::MatchGroup::Channels));
    groups_->addWidget(withMatch(levelsPane(), media::MatchGroup::Levels));
    groups_->addWidget(withMatch(curvesPane(), media::MatchGroup::Curves));
    groups_->addWidget(withMatch(secondaryPane(), media::MatchGroup::Secondary));
    outer->addWidget(groups_, 1);
    // The Color Correction buckets C1-C8 (p.613), at the foot on the right, under the Color Match controls.
    auto* bucketRow = new QHBoxLayout;
    bucketRow->setSpacing(2);
    bucketRow->addStretch(1);
    for (int n = 0; n < kBuckets; ++n) {
        auto* b = new DragButton(this, [this, n]() -> QMimeData* {
            const auto& t = buckets_.at(static_cast<std::size_t>(n));
            return t ? correctionTemplateMimeData(*t, QStringLiteral("C%1").arg(n + 1)) : nullptr;
        });
        b->setObjectName(QStringLiteral("bucket:%1").arg(n + 1));
        b->setText(QStringLiteral("C%1").arg(n + 1));
        b->setToolTip(tr("Click to apply; Alt+click to save the current correction, or to clear on an uncorrected segment"));
        b->setFixedSize(30, 22);
        connect(b, &QToolButton::clicked, this, [this, n] { clickBucket(n, QApplication::keyboardModifiers().testFlag(Qt::AltModifier)); });
        bucketRow->addWidget(b);
    }
    outer->addLayout(bucketRow);
    setAcceptDrops(true);
    reload();
}

QToolButton* CorrectionTool::bucketButton(int n) const {
    return findChild<QToolButton*>(QStringLiteral("bucket:%1").arg(n + 1));
}

bool CorrectionTool::clickBucket(int n, bool alt) {
    if (n < 0 || n >= kBuckets) return false;
    std::optional<model::CorrectionTemplate>& b = buckets_.at(static_cast<std::size_t>(n));
    bool done = false;
    if (alt) {
        // "Alt+click ... the bucket in which you want to save the template ... you overwrite the previous template";
        // "Move the position indicator to any uncorrected segment ... and then Alt+click ... the bucket" clears it (p.614).
        const auto t = timeline_.currentCorrectionTemplate();
        if (t && !t->isEmpty()) {
            b = *t;
            done = true;
        } else if (t && b) {
            b.reset();
            done = true;
        }
    } else if (b) {
        done = timeline_.applyCorrectionTemplate(*b, false);
        reload();
    }
    // "a Color Correction icon replaces the "C" label on the bucket" (p.614); ours: a template sheet in the tool's colour.
    if (QToolButton* button = bucketButton(n)) {
        button->setIcon(b ? toolIcon(QStringLiteral("template"), QColor(0x3c, 0xa0, 0x50)) : QIcon());
        button->setText(b ? QString() : QStringLiteral("C%1").arg(n + 1));
        button->setToolButtonStyle(b ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly);
    }
    return done;
}

QMimeData* CorrectionTool::templateMime() const {
    const auto t = timeline_.currentCorrectionTemplate();
    if (!t || t->isEmpty()) return nullptr;
    return correctionTemplateMimeData(*t, timeline_.currentCorrectionName());
}

bool CorrectionTool::saveCorrection() {
    const auto t = timeline_.currentCorrectionTemplate();
    if (!t || t->isEmpty()) return false;
    emit saveCorrectionRequested(*t, timeline_.currentCorrectionName());
    return true;
}

void CorrectionTool::dragEnterEvent(QDragEnterEvent* event) {
    if (timeline_.droppedCorrectionTemplate(event->mimeData())) event->acceptProposedAction();
}

void CorrectionTool::dropEvent(QDropEvent* event) {
    const auto t = timeline_.droppedCorrectionTemplate(event->mimeData());
    if (!t) return;
    // "drag it to the tab, and drop it anywhere in the tab. The controls in that tab update" (p.615).
    if (timeline_.applyCorrectionTemplateGroup(*t, shownPath())) event->acceptProposedAction();
    reload();
}

QWidget* CorrectionTool::tab(QWidget* parent, const QString& path, const QString& text) {
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);
    QToolButton* enable = enableButtonFor(w, path);
    connect(enable, &QToolButton::clicked, this, [this, path] {
        clickEnable(path, QApplication::keyboardModifiers().testFlag(Qt::AltModifier));
    });
    h->addWidget(enable);
    auto* name = new QToolButton(w);
    name->setObjectName(QStringLiteral("tab:") + path);
    name->setText(text);
    name->setCheckable(true);
    name->setAutoRaise(true);
    // "Click the tab in the area containing the group name ... Do not click the Enable button when you want to display a
    // color correction group tab" (p.600).
    connect(name, &QToolButton::clicked, this, [this, path] {
        const QStringList parts = path.split(QLatin1Char('/'));
        showGroup(parts.at(0));
        if (parts.at(0) == QLatin1String("levels")) {
            if (parts.size() > 1) showLevelsTab(parts.at(1));
        } else {
            if (parts.size() > 1) showHslTab(parts.at(1));
            if (parts.size() > 2) showRangeTab(parts.at(2));
        }
    });
    h->addWidget(name, 1);
    return w;
}

QWidget* CorrectionTool::sliderRow(QWidget* parent, const QString& path, const QString& text) {
    // "Components of a control in the Controls tab. Left to right: Enable button, text box, direction button, slider,
    // direction button" (p.633).
    auto* row = new QWidget(parent);
    row->setObjectName(path);
    row->setProperty("ccPath", path);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(2);
    QToolButton* enable = enableButtonFor(row, path);
    connect(enable, &QToolButton::clicked, this, [this, path] { clickEnable(path, QApplication::keyboardModifiers().testFlag(Qt::AltModifier)); });
    h->addWidget(enable);
    auto* name = new QLabel(text, row);
    name->setMinimumWidth(70);
    h->addWidget(name);
    const QString leaf = path.section(QLatin1Char('/'), -1);
    if (leaf == QLatin1String("invertChroma") || leaf == QLatin1String("invertLuma")) {
        auto* box = new QToolButton(row);
        box->setObjectName(QStringLiteral("toggle"));
        box->setCheckable(true);
        box->setText(text);
        connect(box, &QToolButton::clicked, this, [this, path](bool on) { setControl(path, on ? 1.0 : 0.0); });
        h->addWidget(box);
        h->addStretch(1);
        return row;
    }
    const auto range = model::colorControlRange(path).value_or(std::pair{0.0, 100.0});
    const bool fine = leaf == QLatin1String("gamma");
    auto* value = new UnitSpinBox(row);
    value->setProperty("unitPath", path);
    value->setObjectName(QStringLiteral("value"));
    value->setRange(range.first, range.second);
    value->setDecimals(fine ? 3 : 2);
    value->setSingleStep(fine ? 0.01 : 1.0);
    value->setKeyboardTracking(false);
    value->setFixedWidth(72);
    h->addWidget(value);
    auto* down = new QToolButton(row);
    down->setObjectName(QStringLiteral("down"));
    down->setArrowType(Qt::LeftArrow);
    down->setAutoRepeat(true);
    h->addWidget(down);
    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(QStringLiteral("slider"));
    slider->setRange(static_cast<int>(std::lround(range.first * 100)), static_cast<int>(std::lround(range.second * 100)));
    h->addWidget(slider, 1);
    auto* up = new QToolButton(row);
    up->setObjectName(QStringLiteral("up"));
    up->setArrowType(Qt::RightArrow);
    up->setAutoRepeat(true);
    h->addWidget(up);
    connect(value, &QDoubleSpinBox::valueChanged, this, [this, path](double v) { setControl(path, v); });
    connect(down, &QToolButton::clicked, value, [value] { value->stepBy(-1); });
    connect(up, &QToolButton::clicked, value, [value] { value->stepBy(1); });
    // Dragging shows the value; letting go sets it.
    connect(slider, &QSlider::sliderMoved, value, [value](int v) {
        const QSignalBlocker block(value);
        value->setValue(v / 100.0);
    });
    connect(slider, &QSlider::sliderReleased, this, [this, path, slider] { setControl(path, slider->value() / 100.0); });
    connect(slider, &QSlider::actionTriggered, this, [this, path, slider](int action) {
        if (action != QAbstractSlider::SliderMove) {
            // Page and single steps: a value of their own.
            QMetaObject::invokeMethod(this, [this, path, slider] { setControl(path, slider->value() / 100.0); }, Qt::QueuedConnection);
        }
    });
    return row;
}

QWidget* CorrectionTool::controlsPane() {
    // "Controls Tab Reference" (p.633-634): Invert Chroma, Invert Luma, Clip Low and Clip High on the Master tab only.
    auto* pane = new QWidget;
    pane->setObjectName(QStringLiteral("pane:controls"));
    auto* v = new QVBoxLayout(pane);
    v->setContentsMargins(0, 0, 0, 0);
    controlPages_ = new QStackedWidget(pane);
    for (const char* r : kRanges) {
        auto* page = new QWidget(controlPages_);
        auto* rows = new QVBoxLayout(page);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(1);
        const QString base = QStringLiteral("hsl/controls/") + QLatin1String(r) + QLatin1Char('/');
        const bool master = QLatin1String(r) == QLatin1String("master");
        QStringList names;
        if (master) names << QStringLiteral("invertChroma");
        names << QStringLiteral("hue") << QStringLiteral("saturation") << QStringLiteral("brightness") << QStringLiteral("contrast");
        if (master) names << QStringLiteral("invertLuma");
        names << QStringLiteral("gain") << QStringLiteral("gamma") << QStringLiteral("setup");
        if (master) names << QStringLiteral("clipLow") << QStringLiteral("clipHigh");
        for (const QString& n : names) rows->addWidget(sliderRow(page, base + n, label(base + n)));
        if (master) {
            // "Left to right: Auto Black button, Auto Contrast button, and Auto White button in the HSL > Controls > Master
            // tab" (p.620).
            auto* autos = new QHBoxLayout;
            for (const auto& [kind, key, text, tip] : {std::tuple{media::AutoCorrection::HslBlack, "black", QT_TR_NOOP("Black"), QT_TR_NOOP("Auto Black")},
                                                       std::tuple{media::AutoCorrection::HslContrast, "contrast", QT_TR_NOOP("Contrast"), QT_TR_NOOP("Auto Contrast")},
                                                       std::tuple{media::AutoCorrection::HslWhite, "white", QT_TR_NOOP("White"), QT_TR_NOOP("Auto White")}}) {
                QToolButton* b = correctionActionButton(page, QStringLiteral("auto:controls:") + QLatin1String(key), tr(text), tr(tip));
                connect(b, &QToolButton::clicked, this, [this, k = kind] { autoCorrect(k, false); });
                autos->addWidget(b);
            }
            autos->addStretch(1);
            rows->addLayout(autos);
        }
        rows->addStretch(1);
        controlPages_->addWidget(page);
    }
    v->addWidget(controlPages_);
    v->addWidget(colorMatchFor(media::MatchGroup::HslControls, pane));
    return pane;
}

QWidget* CorrectionTool::hueOffsetsPane() {
    // ChromaWheels (p.635-638) and the Gain, Gamma and Setup sliders (p.638-639).
    auto* pane = new QWidget;
    pane->setObjectName(QStringLiteral("pane:hueOffsets"));
    auto* v = new QVBoxLayout(pane);
    v->setContentsMargins(0, 0, 0, 0);
    auto* wheels = new QGridLayout;
    for (int i = 0; i < 4; ++i) {
        const QString base = QStringLiteral("hsl/hueOffsets/") + QLatin1String(kWheels[i]);
        auto* head = new QHBoxLayout;
        QToolButton* enable = enableButtonFor(pane, base);
        connect(enable, &QToolButton::clicked, this, [this, base] { clickEnable(base, QApplication::keyboardModifiers().testFlag(Qt::AltModifier)); });
        head->addWidget(enable);
        const int nameIndex = i == 0 ? 3 : i == 1 ? 2 : i == 2 ? 1 : 0;
        head->addWidget(new QLabel(tr(kRangeNames[nameIndex]), pane), 1);
        wheels->addLayout(head, 0, i);
        auto* wheel = new ChromaWheel(i, pane);
        wheel->setObjectName(base);
        wheels->addWidget(wheel, 1, i);
        auto* boxes = new QHBoxLayout;
        auto* hue = new QDoubleSpinBox(pane);
        hue->setObjectName(base + QStringLiteral("/hue"));
        hue->setRange(-180, 180);
        hue->setDecimals(2);
        hue->setKeyboardTracking(false);
        auto* amount = new QDoubleSpinBox(pane);
        amount->setObjectName(base + QStringLiteral("/amount"));
        amount->setRange(0, 100);
        amount->setDecimals(2);
        amount->setKeyboardTracking(false);
        boxes->addWidget(hue);
        boxes->addWidget(amount);
        wheels->addLayout(boxes, 2, i);
        if (i < 3) {
            // "Remove Color Cast button below a ChromaWheel color wheel in the HSL > Hue Offsets tab" (p.621), for the
            // Shadows, Midtones and Highlights wheels (p.622).
            const auto where = i == 0 ? media::CastRemoval::Shadows : i == 1 ? media::CastRemoval::Midtones : media::CastRemoval::Highlights;
            QToolButton* cast = correctionActionButton(pane, QStringLiteral("removeCast:%1").arg(static_cast<int>(where)), tr("Remove Cast"),
                                                       tr("Remove Color Cast"));
            cast->setCheckable(true);
            connect(cast, &QToolButton::clicked, this, [this, where] { startRemoveColorCast(where); });
            wheels->addWidget(cast, 3, i, Qt::AlignHCenter);
        }
        connect(wheel, &ChromaWheel::moved, this, [hue, amount](double h, double a) {
            const QSignalBlocker b1(hue), b2(amount);
            hue->setValue(h);
            amount->setValue(a);
        });
        connect(wheel, &ChromaWheel::released, this, [this, base](double h, double a) {
            correction_.set(base + QStringLiteral("/hue"), h);
            correction_.set(base + QStringLiteral("/amount"), a);
            commit(tr("Hue Offset"));
        });
        connect(hue, &QDoubleSpinBox::valueChanged, this, [this, base](double x) { setControl(base + QStringLiteral("/hue"), x); });
        connect(amount, &QDoubleSpinBox::valueChanged, this, [this, base](double x) { setControl(base + QStringLiteral("/amount"), x); });
    }
    v->addLayout(wheels);
    // "Left to right: Auto Balance button, Auto Black button, Auto Contrast button, and Auto White button in the HSL > Hue
    // Offsets tab" (p.620).
    auto* autos = new QHBoxLayout;
    for (const auto& [kind, key, text, tip] : {std::tuple{media::AutoCorrection::HslBalance, "balance", QT_TR_NOOP("Balance"), QT_TR_NOOP("Auto Balance")},
                                               std::tuple{media::AutoCorrection::HslBlack, "black", QT_TR_NOOP("Black"), QT_TR_NOOP("Auto Black")},
                                               std::tuple{media::AutoCorrection::HslContrast, "contrast", QT_TR_NOOP("Contrast"), QT_TR_NOOP("Auto Contrast")},
                                               std::tuple{media::AutoCorrection::HslWhite, "white", QT_TR_NOOP("White"), QT_TR_NOOP("Auto White")}}) {
        QToolButton* b = correctionActionButton(pane, QStringLiteral("auto:hueOffsets:") + QLatin1String(key), tr(text), tr(tip));
        connect(b, &QToolButton::clicked, this, [this, k = kind] { autoCorrect(k); });
        autos->addWidget(b);
    }
    autos->addStretch(1);
    v->addLayout(autos);
    for (const char* n : {"gain", "gamma", "setup"}) {
        const QString path = QStringLiteral("hsl/hueOffsets/") + QLatin1String(n);
        v->addWidget(sliderRow(pane, path, label(path)));
    }
    v->addWidget(colorMatchFor(media::MatchGroup::HslHueOffsets, pane));
    v->addStretch(1);
    return pane;
}

QWidget* CorrectionTool::lumaRangesPane() {
    // "Preview buttons (top left), text boxes (bottom left), and the Luma Ranges graph (right)" (p.641).
    auto* pane = new QWidget;
    pane->setObjectName(QStringLiteral("pane:lumaRanges"));
    auto* h = new QHBoxLayout(pane);
    h->setContentsMargins(0, 0, 0, 0);
    auto* left = new QVBoxLayout;
    auto* previews = new QButtonGroup(pane);
    const std::pair<const char*, LumaPreview> options[] = {{QT_TR_NOOP("Input"), LumaPreview::Input},
                                                           {QT_TR_NOOP("Three-tone"), LumaPreview::ThreeTone},
                                                           {QT_TR_NOOP("Output"), LumaPreview::Output}};
    for (const auto& [text, preview] : options) {
        auto* b = new QRadioButton(tr(text), pane);
        b->setObjectName(QStringLiteral("preview:") + QString::fromLatin1(text));
        b->setChecked(preview == LumaPreview::Output);
        previews->addButton(b);
        connect(b, &QRadioButton::toggled, this, [this, p = preview](bool on) {
            if (on) setLumaPreview(p);
        });
        left->addWidget(b);
    }
    left->addStretch(1);
    lumaIn_ = new UnitSpinBox(pane);
    lumaIn_->setProperty("unitPath", QStringLiteral("luma"));
    lumaIn_->setObjectName(QStringLiteral("lumaIn"));
    lumaIn_->setRange(0, 255);
    lumaIn_->setKeyboardTracking(false);
    lumaOut_ = new QDoubleSpinBox(pane);
    lumaOut_->setObjectName(QStringLiteral("lumaOut"));
    lumaOut_->setRange(0, 100);
    lumaOut_->setKeyboardTracking(false);
    left->addWidget(new QLabel(tr("In"), pane));
    left->addWidget(lumaIn_);
    left->addWidget(new QLabel(tr("Out"), pane));
    left->addWidget(lumaOut_);
    h->addLayout(left);
    lumaGraph_ = new LumaRangesGraph(pane);
    lumaGraph_->setObjectName(QStringLiteral("lumaRangesGraph"));
    h->addWidget(lumaGraph_, 1);
    auto store = [this](const QList<QPointF>& curve) {
        QJsonArray a;
        for (const QPointF& p : curve) a.append(QJsonArray{p.x(), p.y()});
        const QString range = range_ == QLatin1String("master") ? QStringLiteral("highlights") : range_;
        correction_.set(QStringLiteral("hsl/lumaRanges/") + range, a);
        commit(tr("Luma Ranges"));
    };
    connect(lumaGraph_, &LumaRangesGraph::curveChanged, this, store);
    connect(lumaGraph_, &LumaRangesGraph::selectionChanged, this, [this](int i) {
        const QSignalBlocker b1(lumaIn_), b2(lumaOut_);
        const QList<QPointF>& c = lumaGraph_->activeCurve();
        if (i >= 0 && i < c.size()) {
            lumaIn_->setValue(c.at(i).x());
            lumaOut_->setValue(c.at(i).y());
        }
    });
    // "Type values in the In and Out text boxes to define the position of the point on the graph" (p.643).
    auto typed = [this, store] {
        const int i = lumaGraph_->selectedPoint();
        QList<QPointF> c = lumaGraph_->activeCurve();
        if (i < 0 || i >= c.size()) return;
        c[i] = QPointF(lumaIn_->value(), lumaOut_->value());
        std::sort(c.begin(), c.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
        store(c);
    };
    connect(lumaIn_, &QDoubleSpinBox::valueChanged, this, typed);
    connect(lumaOut_, &QDoubleSpinBox::valueChanged, this, typed);
    return pane;
}

void CorrectionTool::reload() {
    // The button follows the type of a corrected segment the position moves to (p.596), not every reload: a type just
    // chosen for the other side stays.
    const auto shot = timeline_.correctionShot(0);
    const QString track = timeline_.correctionTrack();
    if (shot != shownShot_ || track != shownTrack_) {
        shownShot_ = shot;
        shownTrack_ = track;
        timeline_.followCorrectionType();
    }
    const std::optional<model::ColorCorrection> c = timeline_.currentCorrection();
    hasSegment_ = c.has_value();
    correction_ = c.value_or(model::ColorCorrection{});
    // The Comments button's icon is yellow on a segment with a comment (p.609).
    const bool comment = !timeline_.currentCorrectionComment().value_or(QString()).isEmpty();
    if (comment != hasComment_ || button(QStringLiteral("comments"))->icon().isNull()) {
        hasComment_ = comment;
        button(QStringLiteral("comments"))->setIcon(toolIcon(QStringLiteral("comments"), comment ? QColor(240, 200, 40) : palette().color(QPalette::WindowText)));
    }
    button(QStringLiteral("comments"))->setEnabled(hasSegment_);
    const auto t = timeline_.currentCorrectionTemplate();
    button(QStringLiteral("template"))->setEnabled(t && !t->isEmpty());
    sync();
}

QToolButton* CorrectionTool::button(const QString& name) const {
    return findChild<QToolButton*>(QStringLiteral("ccButton:") + name);
}

void CorrectionTool::editComment() {
    const std::optional<QString> current = timeline_.currentCorrectionComment();
    if (!current) return;
    std::optional<QString> answer;
    if (askComment_) {
        answer = askComment_(*current);
    } else {
        // "The Comments dialog box opens": a text window, OK and Remove (p.609-610).
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("correctionComments"));
        dialog.setWindowTitle(tr("Comments"));
        auto* text = new QPlainTextEdit(*current, &dialog);
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
        v->addWidget(text);
        v->addWidget(buttons);
        if (dialog.exec() == QDialog::Accepted) answer = removed ? QString() : text->toPlainText();
    }
    if (!answer) return;
    timeline_.setCorrectionComment(answer->trimmed());
    reload();
}

bool CorrectionTool::showPath(const QString& path) {
    const QStringList parts = path.split(QLatin1Char('/'));
    if (parts.isEmpty() || !groupShown(parts.at(0))) return false;
    if (parts.at(0) == QLatin1String("hsl")) {
        if (parts.size() > 1) {
            const QStringList tabs{QStringLiteral("controls"), QStringLiteral("hueOffsets"), QStringLiteral("lumaRanges")};
            if (!tabs.contains(parts.at(1))) return false;
        }
        showGroup(QStringLiteral("hsl"));
        if (parts.size() > 1) showHslTab(parts.at(1));
        if (parts.size() > 2) showRangeTab(parts.at(2));
    } else if (parts.at(0) == QLatin1String("levels")) {
        if (parts.size() > 1 && !model::levelsTabs().contains(parts.at(1))) return false;
        showGroup(QStringLiteral("levels"));
        if (parts.size() > 1) showLevelsTab(parts.at(1));
    } else {
        showGroup(parts.at(0));
    }
    return group_ == parts.at(0);
}

void CorrectionTool::commit(const QString& step) {
    if (!hasSegment_) {
        reload();
        return;
    }
    timeline_.setCurrentCorrection(correction_, step);
    reload();
}

void CorrectionTool::setControl(const QString& path, double value) {
    // The same value again, or the default typed into an unset control, changes nothing.
    if (correction_.number(path) == value) return;
    // An input vector's hue or saturation with Lock Vectors on moves the output too (p.676).
    if (lockVectors_ && path.startsWith(QLatin1String("secondary/vectors/")) && path.contains(QLatin1String("/input/")) &&
        (path.endsWith(QLatin1String("/hue")) || path.endsWith(QLatin1String("/saturation")))) {
        model::setVectorInput(correction_, path.section(QLatin1Char('/'), 2, 2), path.section(QLatin1Char('/'), -1), value, true);
        commit(label(path));
        return;
    }
    correction_.set(path, value);
    commit(label(path));
}

void CorrectionTool::clickEnable(const QString& path, bool alt) {
    if (alt) {
        // "You cannot reset controls that are not currently displayed. If you Alt+click ... the Enable button for a tab
        // whose controls are not currently displayed, you display the controls but do not reset them" (p.603).
        // A control's button is on screen with its control; a tab's controls are displayed when it is the tab shown or
        // one above it.
        const QString shown = shownPath();
        const bool displayed = !tabButton(path) || shown == path || shown.startsWith(path + QLatin1Char('/'));
        if (!displayed) {
            const QStringList parts = path.split(QLatin1Char('/'));
            showGroup(parts.at(0));
            if (parts.at(0) == QLatin1String("levels")) {
                if (parts.size() > 1) showLevelsTab(parts.at(1));
            } else {
                if (parts.size() > 1) showHslTab(parts.at(1));
                if (parts.size() > 2) showRangeTab(parts.at(2));
            }
            sync();
            return;
        }
        correction_.reset(path);
        commit(tr("Reset"));
        return;
    }
    // Off when lit; on again when turned off (ours: an unset control's button stays unlit, its defaults change nothing).
    correction_.setEnabled(path, !correction_.enabled(path) ? true : !enableLit(path));
    commit(tr("Enable"));
}

bool CorrectionTool::enableLit(const QString& path) const {
    return correction_.enabled(path) && correction_.hasValues(path) && correction_.adjusted(path);
}

void CorrectionTool::showGroup(const QString& group) {
    if (!groupShown(group)) return;
    for (int i = 0; i < 5; ++i) {
        if (group == QLatin1String(kGroups[i])) {
            group_ = group;
            groups_->setCurrentIndex(i);
        }
    }
    sync();
}

void CorrectionTool::showHslTab(const QString& tab) {
    const QStringList tabs{QStringLiteral("controls"), QStringLiteral("hueOffsets"), QStringLiteral("lumaRanges")};
    const auto i = tabs.indexOf(tab);
    if (i < 0) return;
    hslTab_ = tab;
    hslPages_->setCurrentIndex(static_cast<int>(i));
    if (tab == QLatin1String("lumaRanges") && range_ == QLatin1String("master")) range_ = QStringLiteral("highlights");
    sync();
}

void CorrectionTool::showRangeTab(const QString& range) {
    for (const char* r : kRanges) {
        if (range == QLatin1String(r)) range_ = range;
    }
    sync();
}

QString CorrectionTool::shownPath() const {
    if (group_ == QLatin1String("levels")) return QStringLiteral("levels/") + levelsTab_;
    if (group_ != QLatin1String("hsl")) return group_;
    if (hslTab_ == QLatin1String("hueOffsets")) return QStringLiteral("hsl/hueOffsets");
    return QStringLiteral("hsl/") + hslTab_ + QLatin1Char('/') + range_;
}

void CorrectionTool::setLumaPreview(LumaPreview preview) {
    if (preview == lumaPreview_) return;
    lumaPreview_ = preview;
    emit lumaPreviewChanged(preview);
}

QWidget* CorrectionTool::control(const QString& path) const {
    return findChild<QWidget*>(path);
}

QToolButton* CorrectionTool::enableButton(const QString& path) const {
    return findChild<QToolButton*>(QStringLiteral("enable:") + path);
}

QToolButton* CorrectionTool::tabButton(const QString& path) const {
    return findChild<QToolButton*>(QStringLiteral("tab:") + path);
}

ChromaWheel* CorrectionTool::wheel(const QString& range) const {
    return findChild<ChromaWheel*>(QStringLiteral("hsl/hueOffsets/") + range);
}

void CorrectionTool::keyPressEvent(QKeyEvent* event) {
    // "To switch between group tabs using the keyboard: Press the Page Up key or the Page Down key" (p.600).
    if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
        int i = 0;
        for (int k = 0; k < 5; ++k) {
            if (group_ == QLatin1String(kGroups[k])) i = k;
        }
        // The tabs the Correction setting hides are passed over.
        for (int step = 0; step < 5; ++step) {
            i = (i + (event->key() == Qt::Key_PageDown ? 1 : 4)) % 5;
            if (groupShown(QLatin1String(kGroups[i]))) break;
        }
        showGroup(QLatin1String(kGroups[i]));
        return;
    }
    QWidget::keyPressEvent(event);
}

void CorrectionTool::setOptions(const Options& options) {
    if (options == options_) return;
    options_ = options;
    if (!groupShown(group_)) {
        for (const char* g : kGroups) {
            if (groupShown(QLatin1String(g))) {
                showGroup(QLatin1String(g));
                break;
            }
        }
    }
    for (const char* w : {"master", "highlights", "midtones", "shadows"}) {
        if (ChromaWheel* wheel = this->wheel(QLatin1String(w))) wheel->setBackground(options_.backgrounds);
    }
    for (ColorMatchControl* m : findChildren<ColorMatchControl*>()) {
        m->setShowInfo(options_.eyedropperInfo);
        m->setLabels(options_.savedColorLabels);
        m->setPickAnywhere(options_.eyedropperAnywhere);
    }
    sync();
    syncPanes();
}

bool CorrectionTool::groupShown(const QString& group) const {
    bool any = false;
    for (int i = 0; i < 5; ++i) any = any || options_.groups.at(static_cast<std::size_t>(i));
    for (int i = 0; i < 5; ++i) {
        if (group == QLatin1String(kGroups[i])) return options_.groups.at(static_cast<std::size_t>(i)) || (!any && i == 0);
    }
    return false;
}

void CorrectionTool::applyUnits() {
    for (UnitSpinBox* box : findChildren<UnitSpinBox*>()) {
        QString path = box->property("unitPath").toString();
        if (path.startsWith(QLatin1String("levels:"))) path = QStringLiteral("levels/") + levelsTab_ + QLatin1Char('/') + path.mid(7);
        if (path == QLatin1String("luma")) {
            box->setUnit(options_.units.luma);
        } else if (const auto kind = model::controlUnitKind(path)) {
            box->setUnit(options_.units.of(kind->first), kind->second);
        } else {
            box->setUnit(model::CorrectionUnit::EightBit);
        }
    }
}

void CorrectionTool::sync() {
    for (int i = 0; i < 5; ++i) {
        if (QToolButton* t = tabButton(QLatin1String(kGroups[i]))) t->parentWidget()->setVisible(groupShown(QLatin1String(kGroups[i])));
    }
    // The Correction Type button and the active tabs: "Green indicates Source corrections. Blue indicates Program
    // corrections" (p.597).
    const bool program = timeline_.correctionSide() == model::CorrectionSide::Program;
    const model::CorrectionRelationship type = timeline_.correctionRelationship();
    type_->setText(tr(qPrintable(model::relationshipText(type))));
    for (QAction* a : type_->menu()->actions()) a->setChecked(a->isCheckable() && a->data().toInt() == static_cast<int>(type));
    const QColor tint = program ? QColor(0x3a, 0x6e, 0xc8) : QColor(0x3c, 0xa0, 0x50);
    QPalette pal = type_->palette();
    pal.setColor(QPalette::Button, tint);
    type_->setPalette(pal);
    groups_->setEnabled(hasSegment_);
    rangeColumn_->setVisible(hslTab_ == QLatin1String("controls"));
    lumaColumn_->setVisible(hslTab_ == QLatin1String("lumaRanges"));
    for (int i = 0; i < 4; ++i) {
        if (range_ == QLatin1String(kRanges[i])) controlPages_->setCurrentIndex(i);
    }
    const QString shown = shownPath();
    for (QToolButton* b : findChildren<QToolButton*>()) {
        const QString name = b->objectName();
        if (name.startsWith(QLatin1String("tab:"))) {
            const QString path = name.mid(4);
            const bool on = shown == path || shown.startsWith(path + QLatin1Char('/'));
            b->setChecked(on);
            QPalette p = b->palette();
            p.setColor(QPalette::Button, on ? tint : palette().color(QPalette::Button));
            b->setPalette(p);
        } else if (name.startsWith(QLatin1String("enable:"))) {
            b->setChecked(enableLit(name.mid(7)));
        }
    }
    syncControls();
}

void CorrectionTool::syncControls() {
    for (QWidget* row : findChildren<QWidget*>()) {
        const QString path = row->property("ccPath").toString();
        if (path.isEmpty()) continue;
        if (auto* value = row->findChild<QDoubleSpinBox*>(QStringLiteral("value"))) {
            const QSignalBlocker block(value);
            value->setValue(correction_.number(path));
        }
        if (auto* slider = row->findChild<QSlider*>(QStringLiteral("slider"))) {
            const QSignalBlocker block(slider);
            slider->setValue(static_cast<int>(std::lround(correction_.number(path) * 100)));
        }
        if (auto* toggle = row->findChild<QToolButton*>(QStringLiteral("toggle"))) toggle->setChecked(correction_.number(path) > 0.5);
    }
    for (const char* w : kWheels) {
        const QString base = QStringLiteral("hsl/hueOffsets/") + QLatin1String(w);
        const double h = correction_.number(base + QStringLiteral("/hue")), a = correction_.number(base + QStringLiteral("/amount"));
        if (ChromaWheel* wheel = this->wheel(QLatin1String(w))) wheel->setOffset(h, a);
        if (auto* box = findChild<QDoubleSpinBox*>(base + QStringLiteral("/hue"))) {
            const QSignalBlocker block(box);
            box->setValue(h);
        }
        if (auto* box = findChild<QDoubleSpinBox*>(base + QStringLiteral("/amount"))) {
            const QSignalBlocker block(box);
            box->setValue(a);
        }
    }
    std::array<QList<QPointF>, 3> curves;
    for (int r = 0; r < 3; ++r) curves[static_cast<std::size_t>(r)] = model::lumaRangeCurve(correction_, static_cast<model::LumaRange>(r));
    const int active = range_ == QLatin1String("shadows") ? 0 : range_ == QLatin1String("midtones") ? 1 : 2;
    lumaGraph_->setCurves(curves, active);
    syncPanes();
}

// ---------------------------------------------------------------- ChromaWheel

ChromaWheel::ChromaWheel(int pointer, QWidget* parent) : QWidget(parent), pointer_(pointer) {
    setMinimumSize(90, 90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ChromaWheel::setOffset(double hue, double amount) {
    hue_ = hue;
    amount_ = amount;
    update();
}

QPointF ChromaWheel::pointFor(double hue, double amount) const {
    const double r = std::min(width(), height()) / 2.0 - 6.0;
    const double a = redScreenAngle() + hue * std::numbers::pi / 180.0;
    const QPointF c(width() / 2.0, height() / 2.0);
    return c + QPointF(std::cos(a), -std::sin(a)) * (r * std::clamp(amount, 0.0, 100.0) / 100.0);
}

std::pair<double, double> ChromaWheel::offsetAt(const QPointF& point) const {
    const double r = std::min(width(), height()) / 2.0 - 6.0;
    const QPointF d = point - QPointF(width() / 2.0, height() / 2.0);
    const double amount = std::clamp(std::hypot(d.x(), d.y()) / std::max(r, 1.0) * 100.0, 0.0, 100.0);
    double hue = (std::atan2(-d.y(), d.x()) - redScreenAngle()) * 180.0 / std::numbers::pi;
    hue = std::remainder(hue, 360.0);
    return {amount > 0.0 ? hue : hue_, amount};
}

void ChromaWheel::setBackground(bool on) {
    if (on == background_) return;
    background_ = on;
    backgroundImage_ = {};
    update();
}

void ChromaWheel::paintEvent(QPaintEvent*) {
    // "graphical outlines that resemble the design of a vectorscope monitor ... Axes indicate the offset in degrees from
    // the 0° point, which corresponds with red" (p.636).
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double r = std::min(width(), height()) / 2.0 - 6.0;
    const QPointF c(width() / 2.0, height() / 2.0);
    const QColor ink = palette().color(QPalette::WindowText);
    p.setPen(QPen(ink, 1));
    p.setBrush(palette().color(QPalette::Base));
    p.drawEllipse(c, r, r);
    if (background_) {
        // Each point in the colour the wheel's hue and amount name there (ours: media::wheelColor at mid luma).
        if (backgroundImage_.size() != size()) {
            backgroundImage_ = QImage(size(), QImage::Format_ARGB32_Premultiplied);
            backgroundImage_.fill(Qt::transparent);
            for (int y = 0; y < height(); ++y) {
                for (int x = 0; x < width(); ++x) {
                    const QPointF at(x + 0.5, y + 0.5);
                    if (QLineF(at, c).length() > r) continue;
                    const auto [hue, amount] = offsetAt(at);
                    const auto rgb = media::wheelColor(hue, amount, 0.5);
                    backgroundImage_.setPixelColor(x, y, QColor::fromRgbF(static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2])));
                }
            }
        }
        p.drawImage(0, 0, backgroundImage_);
    }
    p.setPen(QPen(ink.darker(160), 1));
    for (int deg = 0; deg < 360; deg += 60) p.drawLine(c, pointFor(deg, 100));
    p.drawEllipse(c, r / 2, r / 2);
    // The pointer, a shape of its own for each wheel (ours).
    const QPointF at = pointFor(hue_, amount_);
    p.setPen(QPen(palette().color(QPalette::BrightText), 1.5));
    p.setBrush(Qt::NoBrush);
    switch (pointer_) {
    case 0: p.drawLine(at - QPointF(6, 0), at + QPointF(6, 0)); p.drawLine(at - QPointF(0, 6), at + QPointF(0, 6)); break;
    case 1: p.drawEllipse(at, 5, 5); p.drawLine(at - QPointF(7, 0), at + QPointF(7, 0)); break;
    case 2: p.drawRect(QRectF(at - QPointF(4, 4), QSizeF(8, 8))); break;
    default: p.drawPolygon(QPolygonF{at + QPointF(0, -6), at + QPointF(6, 0), at + QPointF(0, 6), at + QPointF(-6, 0)}); break;
    }
}

void ChromaWheel::mousePressEvent(QMouseEvent* event) {
    // "Click anywhere in the wheel, and press and hold the mouse button ... the crosshair pointer is dynamically linked
    // to the mouse" (p.638).
    if (event->button() != Qt::LeftButton) return;
    dragging_ = true;
    last_ = event->position();
    if (!event->modifiers().testFlag(Qt::ShiftModifier)) {
        const auto [h, a] = offsetAt(event->position());
        setOffset(h, a);
        emit moved(hue_, amount_);
    }
}

void ChromaWheel::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) return;
    QPointF to = event->position();
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        // Shift: the pointer moves a quarter as far as the mouse.
        to = pointFor(hue_, amount_) + (event->position() - last_) / 4.0;
    }
    last_ = event->position();
    const auto [h, a] = offsetAt(to);
    setOffset(h, a);
    emit moved(hue_, amount_);
}

void ChromaWheel::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) return;
    dragging_ = false;
    emit released(hue_, amount_);
}

// ---------------------------------------------------------------- LumaRangesGraph

LumaRangesGraph::LumaRangesGraph(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::ClickFocus);
    setMinimumSize(160, 90);
}

void LumaRangesGraph::setCurves(const std::array<QList<QPointF>, 3>& curves, int active) {
    if (!dragging_) {
        if (active != active_) selected_ = -1;
        curves_ = curves;
        active_ = active;
        if (selected_ >= activeCurve().size()) selected_ = -1;
    }
    update();
}

void LumaRangesGraph::selectPoint(int index) {
    selected_ = index;
    update();
    emit selectionChanged(index);
}

QPointF LumaRangesGraph::toWidget(const QPointF& value) const {
    const QRectF r = QRectF(rect()).adjusted(6, 6, -6, -6);
    return {r.left() + value.x() / 255.0 * r.width(), r.bottom() - value.y() / 100.0 * r.height()};
}

QPointF LumaRangesGraph::toValue(const QPointF& point) const {
    const QRectF r = QRectF(rect()).adjusted(6, 6, -6, -6);
    return {std::clamp((point.x() - r.left()) / r.width() * 255.0, 0.0, 255.0), std::clamp((r.bottom() - point.y()) / r.height() * 100.0, 0.0, 100.0)};
}

void LumaRangesGraph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().color(QPalette::Base));
    const QColor ink = palette().color(QPalette::WindowText);
    p.setPen(QPen(ink.darker(170), 1));
    for (int q = 0; q <= 4; ++q) {
        p.drawLine(toWidget(QPointF(255.0 * q / 4, 0)), toWidget(QPointF(255.0 * q / 4, 100)));
        p.drawLine(toWidget(QPointF(0, 25.0 * q)), toWidget(QPointF(255, 25.0 * q)));
    }
    // "You can always see all three curves, but you can only adjust the curve that is active" (p.641).
    const QColor colours[] = {QColor(0x60, 0x60, 0x60), QColor(0xa0, 0xa0, 0xa0), QColor(0xf0, 0xf0, 0xf0)};
    for (int r = 0; r < 3; ++r) {
        QPainterPath path;
        const QList<QPointF>& c = curves_.at(static_cast<std::size_t>(r));
        for (qsizetype i = 0; i < c.size(); ++i) {
            if (i == 0) path.moveTo(toWidget(c.at(i)));
            else path.lineTo(toWidget(c.at(i)));
        }
        p.setPen(QPen(colours[r], r == active_ ? 2.0 : 1.0));
        p.drawPath(path);
    }
    p.setPen(QPen(palette().color(QPalette::BrightText), 1));
    const QList<QPointF>& c = activeCurve();
    for (qsizetype i = 0; i < c.size(); ++i) {
        const QPointF at = toWidget(c.at(i));
        p.setBrush(palette().color(QPalette::BrightText));
        p.drawRect(QRectF(at - QPointF(2.5, 2.5), QSizeF(5, 5)));
        // "A new control point appears with a circle around it to indicate that it is the active point" (p.643).
        if (i == selected_) {
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(at, 6, 6);
        }
    }
}

void LumaRangesGraph::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    QList<QPointF>& c = curves_[static_cast<std::size_t>(active_)];
    for (qsizetype i = 0; i < c.size(); ++i) {
        if (QLineF(toWidget(c.at(i)), event->position()).length() <= 6) {
            selectPoint(static_cast<int>(i));
            dragging_ = true;
            return;
        }
    }
    // "Click the curve at the point where you want to add the point" (p.643): near the active curve.
    const QPointF v = toValue(event->position());
    for (qsizetype i = 1; i < c.size(); ++i) {
        if (v.x() < c.at(i - 1).x() || v.x() > c.at(i).x()) continue;
        const double t = c.at(i).x() > c.at(i - 1).x() ? (v.x() - c.at(i - 1).x()) / (c.at(i).x() - c.at(i - 1).x()) : 0.0;
        const QPointF on(v.x(), c.at(i - 1).y() + (c.at(i).y() - c.at(i - 1).y()) * t);
        if (QLineF(toWidget(on), event->position()).length() > 8) return;
        c.insert(i, on);
        selectPoint(static_cast<int>(i));
        dragging_ = true;
        update();
        return;
    }
}

void LumaRangesGraph::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_ || selected_ < 0) return;
    QList<QPointF>& c = curves_[static_cast<std::size_t>(active_)];
    QPointF v = toValue(event->position());
    // A point stays between its neighbours.
    const double lo = selected_ > 0 ? c.at(selected_ - 1).x() : 0.0;
    const double hi = selected_ + 1 < c.size() ? c.at(selected_ + 1).x() : 255.0;
    v.setX(std::clamp(v.x(), lo, hi));
    c[selected_] = v;
    emit selectionChanged(selected_);
    update();
}

void LumaRangesGraph::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) return;
    dragging_ = false;
    emit curveChanged(activeCurve());
}

void LumaRangesGraph::keyPressEvent(QKeyEvent* event) {
    // "To delete a control point: Click the point to select it, and then press the Delete key" (p.643); two stay.
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && selected_ >= 0 && activeCurve().size() > 2) {
        curves_[static_cast<std::size_t>(active_)].removeAt(selected_);
        selected_ = -1;
        emit curveChanged(activeCurve());
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ---------------------------------------------------------------- UnitSpinBox

void UnitSpinBox::setUnit(model::CorrectionUnit unit, bool difference) {
    if (unit == unit_ && difference == difference_) return;
    unit_ = unit;
    difference_ = difference;
    const QSignalBlocker block(this);
    lineEdit()->setText(textFromValue(value()));
}

QString UnitSpinBox::textFromValue(double value) const {
    const double shown = model::toCorrectionUnit(value, unit_, difference_);
    // Whole numbers on the 8-bit and 10-bit scales when the box has no decimals; a decimal more on the others.
    const int places = unit_ == model::CorrectionUnit::EightBit || unit_ == model::CorrectionUnit::TenBit ? decimals() : std::max(decimals(), 1);
    return locale().toString(shown, 'f', places);
}

double UnitSpinBox::valueFromText(const QString& text) const {
    bool ok = false;
    const double shown = locale().toDouble(text.trimmed(), &ok);
    return ok ? model::fromCorrectionUnit(shown, unit_, difference_) : value();
}

QValidator::State UnitSpinBox::validate(QString& text, int&) const {
    const QString t = text.trimmed();
    if (t.isEmpty() || t == QLatin1String("-") || t == QLatin1String("+") || t == locale().decimalPoint()) return QValidator::Intermediate;
    bool ok = false;
    locale().toDouble(t, &ok);
    return ok ? QValidator::Acceptable : QValidator::Invalid;
}

} // namespace lumenis::ui
