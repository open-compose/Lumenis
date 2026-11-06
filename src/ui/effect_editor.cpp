#include "lumenis/ui/angle_dial.h"
#include "lumenis/ui/effect_editor.h"

#include "lumenis/model/subcap.h"

#include "lumenis/model/correction_shapes.h"

#include "lumenis/ui/timeline.h"
#include "lumenis/ui/titler_panel.h"
#include "lumenis/ui/titler_tool.h"
#include "lumenis/ui/intraframe_tool.h"
#include "lumenis/ui/brush_preview.h"
#include "lumenis/ui/tracking_window.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/model/correction_effect.h"
#include "lumenis/model/pan_scan.h"
#include "lumenis/model/promote3d.h"
#include "lumenis/model/title_clip.h"
#include "lumenis/model/intraframe.h"

#include "lumenis/media/ofx_host.h"
#include "lumenis/media/compositor.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFormLayout>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QFontComboBox>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// The Effect Editor (effect_editor.h). It builds its controls from resources/effect_parameters.json
// (the parameter categories) and resources/effects.json (which categories an effect has, through
// EffectCatalog in effect_palette.*). The effect itself is found and changed through the Timeline
// (timeline.*), which finds it in the Record monitor's sequence (model::effectAt and
// model::effectFor in src/model/sequence.*).

namespace lumenis::ui {

namespace {

// Parameter categories by name.
using Catalog = QHash<QString, QList<EffectParameter>>;

const Catalog& catalog() {
    static const Catalog all = [] {
        Catalog c;
        QFile f(QStringLiteral(":/lumenis/ui/effect_parameters.json"));
        if (!f.open(QIODevice::ReadOnly)) return c;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue& v : root.value(QStringLiteral("categories")).toArray()) {
            const QJsonObject o = v.toObject();
            QList<EffectParameter> list;
            for (const QJsonValue& pv : o.value(QStringLiteral("parameters")).toArray()) {
                const QJsonObject po = pv.toObject();
                EffectParameter p;
                p.text = po.value(QStringLiteral("name")).toString();
                p.subcategory = po.value(QStringLiteral("subcategory")).toString();
                p.name = p.text;
                const bool taken = std::any_of(list.cbegin(), list.cend(), [&](const EffectParameter& q) { return q.name == p.text; });
                if (taken) p.name = (p.subcategory.isEmpty() ? QStringLiteral("2") : p.subcategory) + QStringLiteral(": ") + p.text;
                // "key": the stored name given outright (Corner Pin's "Corner 1: X" ...).
                if (po.contains(QStringLiteral("key"))) p.name = po.value(QStringLiteral("key")).toString();
                p.kind = po.value(QStringLiteral("kind")).toString();
                // A range the reference gives, else ours ("ours"), else 0 to 100.
                const QJsonObject own = po.value(QStringLiteral("ours")).toObject();
                p.decimals = po.value(QStringLiteral("decimals")).toInt(0);
                // Whole-number sliders keep whole ranges (a range printed with decimals, as -3000.0, is whole).
                auto bound = [&](const char* which, double fallback) {
                    const double got = po.value(QLatin1String(which)).toDouble(own.value(QLatin1String(which)).toDouble(fallback));
                    return p.decimals > 0 ? got : std::trunc(got);
                };
                p.min = bound("min", 0.0);
                p.max = bound("max", 100.0);
                if (p.max < p.min) std::swap(p.min, p.max); // a range printed backwards
                p.defaultValue = po.value(QStringLiteral("default"));
                p.keyframeable = po.value(QStringLiteral("keyframeable")).toBool(true);
                for (const QJsonValue& opt : po.value(QStringLiteral("options")).toArray()) p.options << opt.toString();
                list << p;
            }
            c.insert(o.value(QStringLiteral("name")).toString(), list);
        }
        // OFX plug-ins' parameters (6.11b1; media/ofx_host.h), registered at start-up before the editor is first used.
        for (const media::OfxPluginInfo& plugin : media::ofxPlugins()) {
            if (!plugin.usable()) continue;
            QList<EffectParameter>& list = c[media::ofxParameterCategory(plugin)];
            list.clear();
            for (const media::OfxEditorParameter& o : media::ofxEditorParameters(plugin)) {
                EffectParameter p;
                p.name = o.key;
                p.text = o.text;
                p.kind = o.kind;
                p.subcategory = o.subcategory;
                p.min = o.min;
                p.max = o.max;
                p.decimals = o.decimals;
                p.defaultValue = o.defaultValue;
                p.options = o.options;
                p.keyframeable = o.keyframeable;
                list << p;
            }
        }
        // A colour correction's sliders in Color Correction mode (FX Guide p.685; lumenis/model/correction_effect.h).
        for (const model::CorrectionParameter& cp : model::correctionParameters()) {
            EffectParameter p;
            p.name = cp.name;
            p.text = cp.name;
            // Levels and Curves: a keyframe track only (p.685).
            p.kind = cp.trackOnly ? QStringLiteral("track") : QStringLiteral("slider");
            p.min = cp.min;
            p.max = cp.max;
            p.decimals = cp.decimals;
            p.defaultValue = cp.defaultValue;
            c[cp.category] << p;
        }
        return c;
    }();
    return all;
}

// The key of a parameter's control in controls_ and valueLabels_.
QString key(const QString& category, const QString& parameter) {
    return category + QLatin1Char('/') + parameter;
}

// A slider's starting value: the default if it is a number, within the range.
int defaultInt(const EffectParameter& p) {
    const double v = p.defaultValue.isDouble() ? p.defaultValue.toDouble() : 0.0;
    return p.sliderPosition(std::clamp(v, p.min, p.max));
}

// A colour parameter's starting colour: the default if the file gives one ("#rrggbb" or a colour
// name), black otherwise (the parameter file gives none for most).
QColor defaultColor(const EffectParameter& p) {
    const QColor c(p.defaultValue.toString());
    return c.isValid() ? c : QColor(Qt::black);
}

// A colour parameter's Color Preview window (FX Guide p.88-89): a click opens the system's colour dialog (ours: the
// Other Options button's dialog on the swatch itself); pressing it and dragging away makes the eyedropper, and
// releasing over a monitor's picture picks the colour there.
class ColorSwatch : public QToolButton {
public:
    ColorSwatch(QWidget* parent, std::function<void(const QPoint&)> pick, std::function<void()> started = {})
        : QToolButton(parent), pick_(std::move(pick)), started_(std::move(started)) {}

protected:
    void mousePressEvent(QMouseEvent* event) override {
        press_ = event->position().toPoint();
        picking_ = false;
        QToolButton::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!picking_ && (event->buttons() & Qt::LeftButton) &&
            (event->position().toPoint() - press_).manhattanLength() >= QApplication::startDragDistance()) {
            // "Position the pointer over the Color Preview window to activate the eyedropper" (p.89).
            picking_ = true;
            setDown(false);
            setCursor(Qt::CrossCursor);
            if (started_) started_();
        }
        if (!picking_) QToolButton::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (!picking_) return QToolButton::mouseReleaseEvent(event);
        picking_ = false;
        unsetCursor();
        setDown(false);
        if (pick_) pick_(event->globalPosition().toPoint());
    }

private:
    std::function<void(const QPoint&)> pick_;
    std::function<void()> started_;
    QPoint press_;
    bool picking_ = false;
};

// Shows a colour on a parameter's swatch button and keeps it for the colour dialog.
void setSwatch(QToolButton* swatch, const QColor& color) {
    swatch->setProperty("color", color);
    swatch->setStyleSheet(QStringLiteral("background: %1;").arg(color.name()));
}

// Alignment names (p.500).
struct AlignmentName {
    model::TransitionAlignment alignment;
    const char* text;
};
const AlignmentName kAlignments[] = {
    {model::TransitionAlignment::EndingAtCut, QT_TRANSLATE_NOOP("EffectEditor", "Ending at Cut")},
    {model::TransitionAlignment::CenteredOnCut, QT_TRANSLATE_NOOP("EffectEditor", "Centered on Cut")},
    {model::TransitionAlignment::StartingAtCut, QT_TRANSLATE_NOOP("EffectEditor", "Starting at Cut")},
    {model::TransitionAlignment::CustomStart, QT_TRANSLATE_NOOP("EffectEditor", "Custom Start")},
};

// Our own alignment icon: a cut and where the transition lies (p.500).
QIcon alignmentIcon(std::optional<model::TransitionAlignment> a, const QColor& color) {
    QPixmap pm(18, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QPen(color, 1.5));
    p.drawLine(QPointF(9, 1), QPointF(9, 13));
    if (a) {
        const double from = *a == model::TransitionAlignment::EndingAtCut     ? 2
                            : *a == model::TransitionAlignment::StartingAtCut ? 9
                                                                              : 5;
        p.fillRect(QRectF(from, 5, 7, 4), color);
    }
    return QIcon(pm);
}

// Our own keyframe button icons: a keyframe triangle, with an arrow to its left (Go to Previous Event,
// -1), its right (Go to Next Event, 1) or a plus (Add Keyframe, 0).
QIcon keyframeIcon(int direction, const QColor& color) {
    QPixmap pm(18, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const double x = direction < 0 ? 12 : direction > 0 ? 6 : 6;
    p.drawPolygon(QPolygonF{QPointF(x - 4, 2), QPointF(x + 4, 2), QPointF(x, 10)});
    p.setPen(QPen(color, 1.5));
    if (direction < 0) p.drawLine(QPointF(1, 7), QPointF(6, 7));
    if (direction > 0) p.drawLine(QPointF(12, 7), QPointF(17, 7));
    if (direction == 0) {
        p.drawLine(QPointF(13, 4), QPointF(13, 12));
        p.drawLine(QPointF(9, 8), QPointF(17, 8));
    }
    return QIcon(pm);
}

// Ours: a small curve for Show/Hide Keyframe Graphs.
QIcon graphIcon(const QColor& color) {
    QPixmap pm(18, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.5));
    QPainterPath path(QPointF(1, 12));
    path.cubicTo(QPointF(6, 12), QPointF(10, 2), QPointF(17, 2));
    p.drawPath(path);
    return QIcon(pm);
}

// A small disabled button for a feature that comes later, labelled with its name's first letters.
QToolButton* offButton(const QString& name, const QString& tip, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setText(tip.left(2));
    b->setToolTip(tip + QStringLiteral(" (") + QObject::tr("not available yet") + QLatin1Char(')'));
    b->setEnabled(false);
    b->setAutoRaise(true);
    b->setFixedSize(26, 22);
    return b;
}

} // namespace

const QList<EffectParameter>* effectParameters(const QString& category) {
    const auto it = catalog().constFind(category);
    return it == catalog().constEnd() ? nullptr : &*it;
}

// The title row, the scrolling parameter groups with the side buttons, and the bottom row.
EffectEditor::EffectEditor(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("effectEditor"));

    // The effect's name and icon at the top (p.84, 2).
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("effectTitle"));
    QFont bold = title_->font();
    bold.setBold(true);
    title_->setFont(bold);
    blank_ = new QLabel(this);
    blank_->setObjectName(QStringLiteral("effectBlank"));
    blank_->setWordWrap(true);
    blank_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    body_ = new QWidget;
    groups_ = new QVBoxLayout(body_);
    groups_->setContentsMargins(0, 0, 0, 0);
    groups_->setSpacing(2);
    groups_->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("effectParameters"));
    scroll->setWidgetResizable(true);
    scroll->setWidget(body_);
    scroll->setFrameShape(QFrame::NoFrame);

    // The buttons down the right side (p.84, 12; p.85-88): previewing and
    // the grid need playback and the Effect Preview monitor.
    auto* side = new QVBoxLayout;
    side->setSpacing(2);
    side->addStretch(1);
    side->addWidget(offButton(QStringLiteral("effectReduce"), tr("Reduce"), this));
    side->addWidget(offButton(QStringLiteral("effectEnlarge"), tr("Enlarge"), this));
    side->addWidget(offButton(QStringLiteral("effectDualSplit"), tr("Dual Split"), this));
    side->addWidget(offButton(QStringLiteral("effectPlayLoop"), tr("Play Loop"), this));
    side->addWidget(offButton(QStringLiteral("effectPlay"), tr("Play"), this));
    side->addWidget(offButton(QStringLiteral("effectHQ"), tr("HQ"), this));
    // The Tracking Tool button (p.253): the Tracking window.
    QToolButton* trackingTool = offButton(QStringLiteral("effectTracking"), tr("Tracking Tool"), this);
    trackingTool->setToolTip(tr("Tracking Tool"));
    trackingTool->setEnabled(true);
    connect(trackingTool, &QToolButton::clicked, this, [this] { showTrackingWindow(); });
    side->addWidget(trackingTool);
    side->addWidget(offButton(QStringLiteral("effectGrid"), tr("Grid"), this));
    // The 3D effect handle buttons (p.292): checkable, one at a time, shown for 3D effects. Ours: lettered.
    struct HandleButton {
        media::HandleTool tool;
        const char* name;
        const char* text;
        const char* tip;
    };
    static const HandleButton kHandleButtons[] = {
        {media::HandleTool::CornerPin, "effectCornerPin", "CP", QT_TR_NOOP("Corner Pin")},
        {media::HandleTool::Crop, "effectCrop", "Cr", QT_TR_NOOP("Crop")},
        {media::HandleTool::Scale, "effectScale", "Sc", QT_TR_NOOP("Scale")},
        {media::HandleTool::Axis, "effectAxis", "Ax", QT_TR_NOOP("Axis")},
        {media::HandleTool::ZRotation, "effectZRotation", "Rz", QT_TR_NOOP("Z Rotation")},
        {media::HandleTool::XRotation, "effectXRotation", "Rx", QT_TR_NOOP("X Rotation")},
        {media::HandleTool::YRotation, "effectYRotation", "Ry", QT_TR_NOOP("Y Rotation")},
        {media::HandleTool::XYPosition, "effectXYPosition", "XY", QT_TR_NOOP("XY Position")},
        {media::HandleTool::XZPosition, "effectXZPosition", "XZ", QT_TR_NOOP("XZ Position")},
    };
    for (const HandleButton& h : kHandleButtons) {
        auto* b = new QToolButton(this);
        b->setObjectName(QLatin1String(h.name));
        b->setText(QLatin1String(h.text));
        b->setToolTip(tr(h.tip));
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setFixedSize(26, 22);
        b->setVisible(false);
        const media::HandleTool tool = h.tool;
        connect(b, &QToolButton::clicked, this, [this, tool](bool on) { setHandleTool(on ? tool : media::HandleTool::None); });
        handleButtons_.insert(static_cast<int>(tool), b);
        side->addWidget(b);
    }
    // The Intraframe tools (p.394-396, p.439): Selection, Bring Forward, Send Backward (Alt+click: to the front or back),
    // and the Rectangle, Oval, Polygon, Curve and Brush tools. Ours: lettered, shown for Intraframe effects.
    struct DrawButton {
        int tool; // IntraframeTool::Tool, or -1/-2 for Bring Forward/Send Backward
        const char* name;
        const char* text;
        const char* tip;
    };
    static const DrawButton kDrawButtons[] = {
        {static_cast<int>(IntraframeTool::Tool::Selection), "intraframeSelection", "Sel", QT_TR_NOOP("Selection tool")},
        {static_cast<int>(IntraframeTool::Tool::ZRotation), "intraframeZRotation", "Rz", QT_TR_NOOP("Z-Rotation button")},
        {static_cast<int>(IntraframeTool::Tool::Reshape), "intraframeReshape", "Rs", QT_TR_NOOP("Reshape button")},
        {-1, "intraframeBringForward", "BF", QT_TR_NOOP("Bring Forward")},
        {-2, "intraframeSendBackward", "SB", QT_TR_NOOP("Send Backward")},
        {static_cast<int>(IntraframeTool::Tool::Rectangle), "intraframeRectangle", "Re", QT_TR_NOOP("Rectangle tool")},
        {static_cast<int>(IntraframeTool::Tool::Oval), "intraframeOval", "Ov", QT_TR_NOOP("Oval tool")},
        {static_cast<int>(IntraframeTool::Tool::Polygon), "intraframePolygon", "Po", QT_TR_NOOP("Polygon tool")},
        {static_cast<int>(IntraframeTool::Tool::Curve), "intraframeCurve", "Cu", QT_TR_NOOP("Curve tool")},
        {static_cast<int>(IntraframeTool::Tool::Brush), "intraframeBrush", "Br", QT_TR_NOOP("Brush tool")},
        {static_cast<int>(IntraframeTool::Tool::Marker), "intraframeMarker", "Pv", QT_TR_NOOP("Previsualization Marker tool")},
    };
    for (const DrawButton& d : kDrawButtons) {
        auto* b = new QToolButton(this);
        b->setObjectName(QLatin1String(d.name));
        b->setText(QLatin1String(d.text));
        b->setToolTip(tr(d.tip));
        b->setAutoRaise(true);
        b->setFixedSize(26, 22);
        b->setVisible(false);
        if (d.tool >= 0) {
            b->setCheckable(true);
            const auto tool = static_cast<IntraframeTool::Tool>(d.tool);
            connect(b, &QToolButton::clicked, this, [this, tool] {
                // "To drag the control points only, Alt+click ... the Reshape tool to hide the direction handles" (p.414).
                if (tool == IntraframeTool::Tool::Reshape) intraframe_->setHandlesHidden((QApplication::keyboardModifiers() & Qt::AltModifier) != 0);
                intraframe_->setTool(tool);
            });
            intraframeButtons_.insert(d.tool, b);
        } else {
            const bool forward = d.tool == -1;
            connect(b, &QToolButton::clicked, this, [this, forward] {
                const bool all = (QApplication::keyboardModifiers() & Qt::AltModifier) != 0;
                if (forward) intraframe_->bringForward(all);
                else intraframe_->sendBackward(all);
            });
            (forward ? bringForward_ : sendBackward_) = b;
        }
        side->addWidget(b);
    }
    // The Object menu (p.417-418: "select Object > Bring Forward ... Object > Group ... Object > Lock"). Ours: a menu
    // button with the drawing tools, as Media Composer's Object menu has no place among our main menus.
    objectMenu_ = new QToolButton(this);
    objectMenu_->setObjectName(QStringLiteral("intraframeObjectMenu"));
    objectMenu_->setText(tr("Obj"));
    objectMenu_->setToolTip(tr("Object"));
    objectMenu_->setAutoRaise(true);
    objectMenu_->setFixedSize(26, 22);
    objectMenu_->setPopupMode(QToolButton::InstantPopup);
    objectMenu_->setVisible(false);
    auto* objects = new QMenu(objectMenu_);
    objects->setObjectName(QStringLiteral("intraframeObject"));
    struct ObjectItem {
        const char* name;
        const char* text;
    };
    static const ObjectItem kObjectItems[] = {{"bringForward", QT_TR_NOOP("Bring Forward")}, {"sendBackward", QT_TR_NOOP("Send Backward")},
                                              {"bringToFront", QT_TR_NOOP("Bring to Front")}, {"sendToBack", QT_TR_NOOP("Send to Back")},
                                              {"group", QT_TR_NOOP("Group")}, {"ungroup", QT_TR_NOOP("UnGroup")},
                                              {"lock", QT_TR_NOOP("Lock")}, {"unlock", QT_TR_NOOP("Unlock")}};
    for (const ObjectItem& item : kObjectItems) {
        QAction* a = objects->addAction(tr(item.text));
        const QString name = QLatin1String(item.name);
        a->setObjectName(QStringLiteral("intraframeObject:") + name);
        connect(a, &QAction::triggered, this, [this, name] {
            if (name == QLatin1String("bringForward")) intraframe_->bringForward(false);
            else if (name == QLatin1String("sendBackward")) intraframe_->sendBackward(false);
            else if (name == QLatin1String("bringToFront")) intraframe_->bringForward(true);
            else if (name == QLatin1String("sendToBack")) intraframe_->sendBackward(true);
            else if (name == QLatin1String("group")) intraframe_->group();
            else if (name == QLatin1String("ungroup")) intraframe_->ungroup();
            else if (name == QLatin1String("lock")) intraframe_->lock(true);
            else if (name == QLatin1String("unlock")) intraframe_->lock(false);
        });
    }
    objectMenu_->setMenu(objects);
    side->addWidget(objectMenu_);
    side->addStretch(1);

    // The bottom row (p.84, 6-11): Render Effect, Outline/Path, the
    // Transition Effect Alignment button and Duration box, 3D Promote and
    // the keyframe graph.
    alignment_ = new QToolButton(this);
    alignment_->setObjectName(QStringLiteral("transitionAlignment"));
    alignment_->setToolTip(tr("Transition Effect Alignment"));
    alignment_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(alignment_);
    for (const AlignmentName& a : kAlignments) {
        menu->addAction(tr(a.text), this, [this, a] { setAlignment(a.alignment); });
    }
    alignment_->setMenu(menu);
    duration_ = new QLineEdit(this);
    duration_->setObjectName(QStringLiteral("transitionDuration"));
    duration_->setToolTip(tr("Transition Effect Duration"));
    duration_->setFixedWidth(64);
    connect(duration_, &QLineEdit::editingFinished, this, [this] {
        if (!syncing_ && duration_->isModified()) setDuration(duration_->text());
    });
    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(2);
    // Render Effect (p.84; FX Guide p.58): renders the effect at the position, as Render at Position
    // does; MainWindow carries it out (Shift renders again, Alt skips the dialog).
    QToolButton* render = offButton(QStringLiteral("effectRender"), tr("Render Effect"), this);
    render->setToolTip(tr("Render Effect"));
    render->setEnabled(true);
    connect(render, &QToolButton::clicked, this, [this] { emit renderRequested(QGuiApplication::keyboardModifiers()); });
    bottom->addWidget(render);
    // Outline/Path (p.84, p.92-93, p.293): the wire frame and the motion path in the Effect Preview.
    outline_ = offButton(QStringLiteral("effectOutline"), tr("Outline/Path"), this);
    outline_->setToolTip(tr("Outline/Path"));
    outline_->setCheckable(true);
    connect(outline_, &QToolButton::toggled, this, &EffectEditor::setOutlineShown);
    bottom->addWidget(outline_);
    bottom->addWidget(alignment_);
    bottom->addWidget(duration_);
    bottom->addStretch(1);
    // 3D Promote (p.86, p.289): the 2D effect becomes a 3D Warp (model::promoteTo3D).
    promote3D_ = offButton(QStringLiteral("effectPromote3D"), tr("3D Promote"), this);
    connect(promote3D_, &QToolButton::clicked, this, [this] { promoteTo3D(); });
    bottom->addWidget(promote3D_);
    // The advanced keyframe controls (p.128): Go to Previous Event, Go to Next Event, Add Keyframe; and
    // the Show/Hide Keyframe Graphs button "in the bottom right corner" (p.96).
    auto keyButton = [this](const QString& name, const QString& tip, const QIcon& icon) {
        auto* b = new QToolButton(this);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setIcon(icon);
        b->setAutoRaise(true);
        b->setFixedSize(26, 22);
        return b;
    };
    const QColor ink = palette().color(QPalette::WindowText);
    prevEvent_ = keyButton(QStringLiteral("effectPrevEvent"), tr("Go to Previous Event"), keyframeIcon(-1, ink));
    nextEvent_ = keyButton(QStringLiteral("effectNextEvent"), tr("Go to Next Event"), keyframeIcon(1, ink));
    addKeyframe_ = keyButton(QStringLiteral("effectAddKeyframe"), tr("Add Keyframe"), keyframeIcon(0, ink));
    graphsButton_ = keyButton(QStringLiteral("effectKeyframes"), tr("Show/Hide Keyframe Graphs"), graphIcon(ink));
    graphsButton_->setCheckable(true);
    connect(prevEvent_, &QToolButton::clicked, this, [this] { goToEvent(false); });
    connect(nextEvent_, &QToolButton::clicked, this, [this] { goToEvent(true); });
    connect(addKeyframe_, &QToolButton::clicked, this,
            [this] { addKeyframeButton(QApplication::keyboardModifiers() & Qt::AltModifier); });
    connect(graphsButton_, &QToolButton::toggled, this, &EffectEditor::setKeyframeGraphsShown);
    // The horizontal scale bar and the scroll thumbwheel (p.128): ours, a slider and a scroll bar.
    hScale_ = new QSlider(Qt::Horizontal, this);
    hScale_->setObjectName(QStringLiteral("effectGraphZoom"));
    hScale_->setToolTip(tr("Horizontal scale bar"));
    hScale_->setRange(0, 100);
    hScale_->setFixedWidth(60);
    connect(hScale_, &QSlider::valueChanged, this, [this](int v) {
        // "expands or contracts the view in the keyframe graphs centered around the position
        // indicator" (p.99): the position keeps its place; 100 shows a thirty-second of the effect.
        const double span = std::pow(2.0, -v / 20.0);
        const double at = positionTime();
        viewStart_ = v == 0 ? 0.0 : at - (at - viewStart_) * span / viewSpan_;
        viewSpan_ = span;
        syncScrollBar();
        updateTracks();
    });
    hScroll_ = new QScrollBar(Qt::Horizontal, this);
    hScroll_->setObjectName(QStringLiteral("effectGraphScroll"));
    hScroll_->setToolTip(tr("Scroll thumbwheel"));
    hScroll_->setFixedWidth(60);
    connect(hScroll_, &QScrollBar::valueChanged, this, [this](int v) {
        viewStart_ = v / 1000.0;
        updateTracks();
    });
    bottom->addWidget(prevEvent_);
    bottom->addWidget(nextEvent_);
    bottom->addWidget(addKeyframe_);
    bottom->addWidget(hScale_);
    bottom->addWidget(hScroll_);
    bottom->addWidget(graphsButton_);

    auto* middle = new QHBoxLayout;
    middle->addWidget(scroll, 1);
    middle->addLayout(side);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    auto* titleRow = new QHBoxLayout;
    titleRow->setSpacing(4);
    titleIcon_ = new QLabel(this);
    titleIcon_->setObjectName(QStringLiteral("effectIcon"));
    titleIcon_->setToolTip(tr("Drag to a bin to save an effect template (Alt: with its source media)"));
    titleIcon_->installEventFilter(this);
    titleRow->addWidget(titleIcon_);
    titleRow->addWidget(title_, 1);
    layout->addLayout(titleRow);
    layout->addWidget(blank_);
    layout->addLayout(middle, 1);
    layout->addLayout(bottom);

    connect(&timeline_, &Timeline::changed, this, &EffectEditor::refresh);
    connect(&timeline_, &Timeline::effectModeChanged, this, &EffectEditor::refresh);
    connect(&timeline_, &Timeline::correctionModeChanged, this, &EffectEditor::refresh);
    // The Effect Preview monitor's keyframes and buttons (FX Guide p.76-77, p.103-106): its Add Keyframe
    // button runs the default command without the menu; Alt+click deletes the selected keyframes.
    connect(&timeline_.composer(), &Composer::effectKeyframePressed, this, &EffectEditor::previewKeyframePressed);
    // The keyframe markers below the Current Color Correction monitor select here too (FX Guide p.686).
    connect(&timeline_, &Timeline::correctionKeyframesSelected, this, &EffectEditor::selectCorrectionKeyframes);
    connect(&timeline_.composer(), &Composer::effectCommandRequested, this, [this](const QString& id, bool alt) {
        if (id == QLatin1String("effect.add_keyframe")) {
            if (alt) deleteKeyframes(mode_);
            else addKeyframes(mode_);
        } else if (id == QLatin1String("effect.previous_event") || id == QLatin1String("effect.next_event")) {
            goToEvent(id == QLatin1String("effect.next_event"));
        }
    });
    connect(&timeline_.composer(), &Composer::changed, this, &EffectEditor::refresh);
    // Titler+'s tools in the Record monitor (FX Guide p.303-319).
    titler_ = new TitlerTool(*this);
    // The Intraframe drawing tools (FX Guide p.395-418).
    intraframe_ = new IntraframeTool(*this, this);
    connect(intraframe_, &IntraframeTool::changed, this, [this] {
        syncIntraframeButtons();
        syncPreview();
    });
    // Handles dragged in the Effect Preview monitor (p.92, p.292).
    connect(&timeline_.composer(), &Composer::effectHandleDragged, this,
            [this](const QString& id, const QPointF& from, const QPointF& to, bool finished, bool shift) {
                dragHandle(id, from, to, finished, shift);
            });
    // Data points lassoed or double-clicked there (p.271).
    connect(&timeline_.composer(), &Composer::effectLasso, this, &EffectEditor::lassoPoints);
    connect(&timeline_.composer(), &Composer::effectPointDoubleClicked, this, [this](const QString& id) {
        const QStringList parts = id.split(QLatin1Char(':'));
        if (parts.size() != 3) return;
        const int index = parts.at(1).toInt();
        const std::int64_t frame = parts.at(2).toLongLong();
        selectPoints(index, {frame});
        moveToFrame(frame);
    });
    // The Delete key with data points selected (p.272), before the Timeline's own uses of it.
    timeline_.setDeleteHook([this] { return deleteSelectedPoints(); });
    refresh();
}

QList<model::TrimRoller> EffectEditor::transitionCuts() const {
    if (shown_ && shown_->target == model::EffectTarget::Transition) {
        return {model::TrimRoller{shown_->track, shown_->frame, model::TrimSide::Both}};
    }
    return shown_ ? QList<model::TrimRoller>() : trimCuts();
}

// Clicking a transition in Trim mode shows its Transition parameters (p.63).
QList<model::TrimRoller> EffectEditor::trimCuts() const {
    QList<model::TrimRoller> cuts;
    if (!timeline_.isTrimMode()) return cuts;
    for (const model::TrimRoller& r : timeline_.rollers()) {
        if (r.track.startsWith(QLatin1Char('V'))) cuts << r;
    }
    return cuts;
}

// The shown effect can outlive the sequence it was in until the next refresh() (the Record monitor
// emptied without a change signal), so nothing assumes a sequence is loaded.
model::Sequence EffectEditor::recordSequence() const {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    return rec ? model::Sequence::fromItem(rec->item) : model::Sequence();
}

QString EffectEditor::durationText(std::int64_t frames) const {
    // Seconds and frames: 1:00 is one second (p.500).
    const std::int64_t fps = std::max(timeline_.format().nominalFps, 1);
    return QStringLiteral("%1:%2").arg(frames / fps).arg(frames % fps, 2, 10, QLatin1Char('0'));
}

// Finds the effect at the Record monitor's position. A different effect (or a switch to or from
// Transition parameters only) rebuilds the controls; the same one only updates their values.
void EffectEditor::refresh() {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    std::optional<model::EffectRef> ref = timeline_.currentEffect();
    const model::Effect* effect = nullptr;
    model::Sequence seq;
    // In Color Correction mode the editor shows the current correction (FX Guide p.683-685; 6.10d3b).
    correctionShown_ = false;
    if (timeline_.isCorrectionMode()) {
        ref.reset();
        if (const auto stored = timeline_.storedCorrection(); stored && rec) {
            seq = model::Sequence::fromItem(rec->item);
            const QString track = timeline_.correctionTrack();
            std::int64_t start = 0;
            const model::Track* t = seq.track(track);
            const int i = t ? t->segmentAt(timeline_.composer().state(MonitorId::Record).position(), &start) : -1;
            if (i >= 0) {
                correctionEffect_ = model::correctionEffect(*stored);
                effect = &correctionEffect_;
                ref = model::EffectRef{track, start, model::EffectTarget::Segment};
                correctionShown_ = true;
                current_ = *effect;
                chain_.clear();
                level_ = 0;
                span_ = model::EffectSpan(std::max<std::int64_t>(t->segments.at(i).length, 1));
                effectStart_ = start;
                // The other side is another correction (p.596).
                const QString name = effect->category + (timeline_.correctionSide() == model::CorrectionSide::Source ? QStringLiteral("/source")
                                                                                                                     : QStringLiteral("/program"));
                if (ref != shown_ || name != shownName_) {
                    shown_ = ref;
                    shownName_ = name;
                    transitionOnly_ = false;
                    preview_.reset();
                    rebuild(&*current_);
                } else {
                    selection_.removeIf([&](const KeyframeId& id) {
                        return id.index >= model::keyframesOf(*current_, id.parameter.section(QLatin1Char('/'), 0, 0),
                                                              id.parameter.section(QLatin1Char('/'), 1))
                                                .keys.size();
                    });
                    syncValues(*current_);
                }
                updateTracks();
                return;
            }
        }
    } else if (ref && rec) {
        seq = model::Sequence::fromItem(rec->item);
        effect = model::effectFor(seq, *ref);
    }
    if (!effect) ref.reset(); // also with no sequence loaded

    // Where the effect lies and how its frames map to its time (model::EffectSpan).
    current_.reset();
    // A handle drag in the Effect Preview keeps its preview while the Composer redraws (its refresh says
    // changed() too); anything else previewed is dropped.
    if (!handleStart_ && !pluginDragging_ && !(titler_ && titler_->busy())) preview_.reset();
    chain_.clear();
    // Another effect opens at its outermost level.
    if (ref != shown_) level_ = 0;
    if (effect) {
        if (ref->target == model::EffectTarget::Transition) {
            if (const model::Transition* t = model::transitionFor(seq, *ref)) {
                span_ = model::EffectSpan(t->duration, true);
                effectStart_ = ref->frame - t->before();
            }
        } else if (const model::Track* track = seq.track(ref->track)) {
            std::int64_t start = 0;
            const int i = track->segmentAt(ref->frame, &start);
            if (i >= 0) {
                span_ = model::EffectSpan(track->segments.at(i).length);
                effectStart_ = start;
                // The nested effects at the position (p.221); the open one is shown.
                nestOffset_ = timeline_.composer().state(MonitorId::Record).position() - start;
                chain_ = model::nestedEffects(track->segments.at(i), nestOffset_);
                if (level_ >= chain_.size()) level_ = 0;
                if (level_ > 0) {
                    const model::NestedEffect& n = chain_.at(level_);
                    effect = &n.effect;
                    span_ = model::EffectSpan(std::max<std::int64_t>(n.length, 1));
                    effectStart_ = start + n.start;
                }
            }
        }
        current_ = *effect;
    }
    if (chain_.isEmpty()) level_ = 0;
    // An Intraframe effect's objects (6.9b): the whole effect for the drawing tools; with one object selected, the
    // controls show that object's parameters. Another effect starts with nothing selected.
    if (ref != shown_) {
        intraframeSelection_.clear();
        intraframePreview_.reset();
        if (intraframe_) intraframe_->reset();
    }
    wholeIntraframe_.reset();
    if (current_ && level_ == 0 && drawsObjects(*current_)) {
        wholeIntraframe_ = current_;
        const QList<model::IntraframeObject> objects = model::intraframeObjects(*current_);
        intraframeSelection_.removeIf([&](const QString& id) {
            return std::none_of(objects.cbegin(), objects.cend(), [&](const model::IntraframeObject& o) { return o.id == id; });
        });
        if (intraframe_) intraframe_->keepSelection(intraframeSelection_);
        if (intraframeSelection_.size() == 1) {
            for (const model::IntraframeObject& o : objects) {
                if (o.id != intraframeSelection_.constFirst()) continue;
                // A Color Correction effect's shape shows only its own Shapes and Shape Tracking with the effect's
                // other categories (p.699); a Paint object has all its parameters.
                current_ = model::isColorCorrectionEffect(*current_) ? model::correctionShapeView(*current_, o) : model::objectEffect(*current_, o);
            }
        }
    }

    // From the new ref, not shown_, which still holds the effect shown before. The nested effects (their
    // order, the one open) are part of what is shown.
    const bool transitionOnly = !ref && !trimCuts().isEmpty();
    QString name = effect ? effect->category + QLatin1Char('/') + effect->name : QString();
    if (chain_.size() > 1) {
        name += QStringLiteral("|%1").arg(level_);
        for (const model::NestedEffect& n : chain_) name += QLatin1Char('|') + n.effect.category + QLatin1Char('/') + n.effect.name;
    }
    if (ref != shown_ || name != shownName_ || transitionOnly != transitionOnly_) {
        shown_ = ref;
        shownName_ = name;
        transitionOnly_ = transitionOnly;
        handleStart_.reset();
        preview_.reset();
        // A title's selection and typing belong to the effect shown (ours: typing not finished goes).
        if (titler_) titler_->reset();
        // Selected data points and the Display Markers belong to the effect shown.
        pointSelection_.clear();
        lastPoint_.reset();
        displayRange_.reset();
        rebuild(current_ ? &*current_ : effect);
        // An Avid Pan & Zoom whose image has moved asks for it (p.239).
        if (effect && media::isPanAndZoom(*effect)) {
            const QString path = media::panZoomImagePath(*effect);
            if (!path.isEmpty() && !QFileInfo::exists(path)) {
                auto* locate = new QFileDialog(this, tr("Locate %1").arg(QFileInfo(path).fileName()), QFileInfo(path).absolutePath());
                locate->setObjectName(QStringLiteral("panZoomLocate"));
                // Ours: Qt's own dialog, which also opens where no window server runs native panels (CI's Mac).
                locate->setOption(QFileDialog::DontUseNativeDialog);
                locate->setAttribute(Qt::WA_DeleteOnClose);
                connect(locate, &QFileDialog::fileSelected, this, [this](const QString& found) { importImage(found); });
                locate->open();
            }
        }
    } else if (effect) {
        // Keyframes an undo took away are no longer selected.
        selection_.removeIf([&](const KeyframeId& id) {
            return id.index >= model::keyframesOf(*effect, id.parameter.section(QLatin1Char('/'), 0, 0),
                                                  id.parameter.section(QLatin1Char('/'), 1))
                                    .keys.size();
        });
        syncValues(preview_ ? *preview_ : (current_ ? *current_ : *effect));
    }
    syncTransition();
    promote3D_->setEnabled(current_ && model::canPromoteTo3D(*current_));
    syncHandleButtons();
    syncIntraframeButtons();
    if (trackingWindow_) trackingWindow_->refresh();
    // The position in the tracks moves while playing only with Update Position While Playing (p.132).
    if (updateWhilePlaying_ || !timeline_.composer().isPlaying()) updateTracks();
    else syncPreview();
}

bool EffectEditor::isBlank() const {
    return !shown_ && !transitionOnly_;
}

std::optional<model::SafeLimiting> EffectEditor::safeColorAnalysis() const {
    const model::Effect* e = shownEffect();
    if (!e || !model::isSafeColorLimiter(*e)) return std::nullopt;
    const model::SafeLimiting l = model::safeColorLimiting(*e, positionTime(), span_);
    if (!l.analysis) return std::nullopt;
    return l;
}

QString EffectEditor::effectName() const {
    return shown_ ? title_->text() : QString();
}

// A group per parameter category the effect lists (EffectInfo::parameters) and the parameter file
// knows. Each control stores its value as it changes; a slider only when released, if dragged.
void EffectEditor::rebuild(const model::Effect* effect) {
    // Clear the old controls, keeping the stretch at the end.
    while (groups_->count() > 1) {
        QLayoutItem* item = groups_->takeAt(0);
        delete item->widget();
        delete item;
    }
    groupBodies_.clear();
    titlerPanel_ = nullptr;
    openers_.clear();
    controls_.clear();
    trackingControls_.clear();
    burnInControls_.clear();
    valueLabels_.clear();
    categoryOrder_.clear();
    graphRanges_.clear();
    graphZoomBase_.clear();
    viewStart_ = 0.0;
    viewSpan_ = 1.0;
    if (hScale_) {
        const QSignalBlocker block(hScale_);
        hScale_->setValue(0);
    }
    syncScrollBar();
    tracks_.clear();
    graphOpeners_.clear();
    correctionEnables_.clear();
    openGraphs_.clear();
    selection_.clear();
    activeParameter_.clear();
    title_->setText(effect ? effect->name : QString());
    title_->setVisible(effect != nullptr);
    titleIcon_->setVisible(effect != nullptr);
    if (effect) {
        const EffectInfo* shownInfo = EffectCatalog::builtin().find(effect->category, effect->name);
        const bool transition = shown_ && shown_->target == model::EffectTarget::Transition;
        titleIcon_->setPixmap(effectIcon(transition, shownInfo ? shownInfo->realTime : true, shownInfo && !shownInfo->ofxPlugin.isEmpty()).pixmap(26, 18));
    }
    // If the position is not on an effect, or its track is not selected,
    // the Effect Editor is blank (p.84).
    blank_->setVisible(isBlank());
    blank_->setText(isBlank() ? tr("No effect at the position on the selected tracks.") : QString());
    if (!effect) return;
    // Titler+: its controls for the selected objects, above its catalogue categories ("The Effect Editor and the
    // Dashboard update to dynamically reflect the current selection with only the relevant parameters", FX Guide p.320).
    if (model::isTitlerEffect(*effect) && level_ == 0) {
        titlerPanel_ = new TitlerPanel(*titler_, false, this);
        groups_->insertWidget(0, titlerPanel_);
    }
    // A classic Title effect: "Click the Edit Title button" (FX Guide p.372) opens it in the Title Tool.
    if (model::isClassicTitleEffect(*effect) && level_ == 0) {
        auto* edit = new QPushButton(tr("Edit Title"), body_);
        edit->setObjectName(QStringLiteral("effectEditor:editTitle"));
        connect(edit, &QPushButton::clicked, this, [this] {
            if (shown_) emit editTitleRequested(model::SegmentRef{shown_->track, shown_->frame});
        });
        groups_->insertWidget(0, edit);
    }

    const EffectInfo* info = EffectCatalog::builtin().find(effect->category, effect->name);
    const QStringList categories = info                                  ? info->parameters
                                   : model::isCorrectionModeEffect(*effect) ? model::correctionParameterCategories()
                                                                          : QStringList();
    int at = 0;
    // A row of controls with its keyframe track at the right, behind a graph opener's place.
    auto withTrack = [this](QHBoxLayout* row, QWidget* rowParent, KeyframeTrack::Kind kind, const QString& which,
                            QToolButton* graphOpener) {
        row->addStretch(0);
        if (graphOpener) {
            row->addWidget(graphOpener);
        } else {
            auto* gap = new QWidget(rowParent);
            gap->setFixedWidth(16);
            row->addWidget(gap);
        }
        auto* track = new KeyframeTrack(*this, kind, which, rowParent);
        row->addWidget(track);
        tracks_.insert(which.isEmpty() ? QStringLiteral("static:%1").arg(tracks_.size()) : which, track);
    };
    // Nested effects (p.221): a title row for each, indented level by level, the open one's groups under
    // its row. A title row is clicked to open its effect and dragged onto another to change the order
    // (p.222).
    auto nestedRow = [this](int level) {
        const model::NestedEffect& n = chain_.at(level);
        auto* row = new QWidget(body_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(12 * level, 0, 0, 0);
        auto* b = new QToolButton(row);
        b->setObjectName(QStringLiteral("nested:%1").arg(level));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setAutoRaise(true);
        b->setCheckable(true);
        b->setChecked(level == level_);
        const EffectInfo* nestedInfo = EffectCatalog::builtin().find(n.effect.category, n.effect.name);
        b->setIcon(effectIcon(false, nestedInfo ? nestedInfo->realTime : true, nestedInfo && !nestedInfo->ofxPlugin.isEmpty()));
        b->setIconSize(QSize(26, 18));
        b->setText((level == level_ ? QStringLiteral("\u25BE ") : QStringLiteral("\u25B8 ")) + n.effect.name);
        b->setToolTip(tr("Click to show its parameters; drag onto another nested effect to change their order"));
        b->setProperty("nestedLevel", level);
        b->setAcceptDrops(true);
        b->installEventFilter(this);
        // Queued: opening another effect rebuilds the rows, this button with them.
        connect(b, &QToolButton::clicked, this, [this, level] { setNestedLevel(level); }, Qt::QueuedConnection);
        h->addWidget(b);
        h->addStretch(1);
        return row;
    };
    const bool nested = chain_.size() > 1;
    if (nested) {
        for (int k = 0; k <= level_; ++k) groups_->insertWidget(at++, nestedRow(k));
    }
    // The effect track beside the top of the parameters (p.95).
    {
        auto* row = new QWidget(body_);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addStretch(1);
        withTrack(h, row, KeyframeTrack::Kind::Effect, QStringLiteral("effect"), nullptr);
        groups_->insertWidget(at++, row);
    }
    for (const QString& category : categories) {
        const QList<EffectParameter>* params = effectParameters(category);
        if (!params) continue;
        // A category with its triangular opener (p.84, 1 and 3).
        auto* opener = new QToolButton(body_);
        opener->setObjectName(QStringLiteral("opener:") + category);
        opener->setArrowType(Qt::DownArrow);
        opener->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        opener->setText(category);
        opener->setAutoRaise(true);
        opener->setCheckable(true);
        opener->setChecked(true);
        // An effect template dropped on the opener sets this group alone (p.54).
        opener->setAcceptDrops(true);
        opener->setProperty("templateGroup", category);
        opener->installEventFilter(this);
        auto* box = new QWidget(body_);
        auto* grid = new QVBoxLayout(box);
        grid->setContentsMargins(18, 0, 0, 4);
        grid->setSpacing(1);
        // Timecode Burn-In's displays have controls of their own (burnInPanel()).
        if (category == QLatin1String("Timecode Burn-In Effect Parameters")) grid->addWidget(burnInPanel(box));
        else for (const EffectParameter& p : *params) {
            // Scratch Removal's Frame Processing button "does not appear when you are working with single-field
            // material" (FX p.547).
            if (p.name == QLatin1String("Frame Processing") && !interlacedProject_) continue;
            auto* row = new QHBoxLayout;
            const QString k = key(category, p.name);
            const bool keyframed = p.keyframeable && (p.kind == QLatin1String("slider") || p.kind == QLatin1String("color") || p.kind == QLatin1String("track") ||
                                                      p.kind == QLatin1String("angle"));
            QLabel* nameLabel = nullptr;
            if (p.kind == QLatin1String("checkbox")) {
                auto* c = new QCheckBox(p.text, box);
                c->setObjectName(QStringLiteral("param:") + k);
                c->setChecked(p.defaultValue.toBool());
                connect(c, &QCheckBox::toggled, this, [this, category, name = p.name](bool on) {
                    setValue(category, name, on);
                });
                row->addWidget(c);
                controls_.insert(k, c);
            } else {
                auto* label = new QLabel(p.text, box);
                label->setObjectName(QStringLiteral("paramLabel:") + k);
                label->setMinimumWidth(64);
                row->addWidget(label);
                nameLabel = label;
                // Motion tracking's controls (p.257-260, p.275, p.498-499, p.571, p.577-578).
                static const struct {
                    const char* category;
                    const char* name;
                    model::TrackedParameter parameter;
                } kTracked[] = {{"Position", "Tracking", model::TrackedParameter::Position},
                                {"Scaling", "Tracking", model::TrackedParameter::Scaling},
                                {"Tracking", "Tracker selectors", model::TrackedParameter::Tracking},
                                {"Corner Tracking", "Tracker selectors", model::TrackedParameter::Tracking},
                                {"Position (3D)", "Tracker Selector", model::TrackedParameter::Position},
                                {"Rotation", "Tracker Selector", model::TrackedParameter::Rotation},
                                {"Scaling (3D)", "Tracker Selector", model::TrackedParameter::Scaling}};
                const auto tracked = std::find_if(std::begin(kTracked), std::end(kTracked), [&](const auto& t) {
                    return category == QLatin1String(t.category) && p.name == QLatin1String(t.name);
                });
                const bool stabilize = p.name == QLatin1String("Stabilize Tracking");
                const bool axis3D = category == QLatin1String("Position (3D)") && p.name == QLatin1String("Axis");
                if (tracked != std::end(kTracked)) {
                    row->addWidget(trackingControls(tracked->parameter, box), 1);
                } else if (axis3D || stabilize) {
                    // The 3D Axis menu and Stabilize Tracking belong to their category's tracking.
                    const model::TrackedParameter tp = category == QLatin1String("Rotation") ? model::TrackedParameter::Rotation
                                                       : category.startsWith(QLatin1String("Scaling")) ? model::TrackedParameter::Scaling
                                                                                                        : model::TrackedParameter::Position;
                    if (axis3D) {
                        auto* m = new QComboBox(box);
                        m->setObjectName(QStringLiteral("tracking:Position:Axis"));
                        m->addItems({QStringLiteral("X and Y"), QStringLiteral("X Tracking Only"), QStringLiteral("Y Tracking Only")});
                        connect(m, &QComboBox::textActivated, this, [this](const QString& t) { setTrackingAxis(t); });
                        trackingControls_.insert(m->objectName(), m);
                        row->addWidget(m, 1);
                    } else {
                        auto* c = new QCheckBox(box);
                        c->setObjectName(QStringLiteral("tracking:%1:Stabilize").arg(model::trackedParameterKey(tp)));
                        connect(c, &QCheckBox::clicked, this, [this, tp](bool on) { setStabilizeTracking(tp, on); });
                        trackingControls_.insert(c->objectName(), c);
                        row->addWidget(c);
                        row->addStretch(1);
                    }
                } else if (category == model::kObjectTrackingCategory && p.name == QLatin1String("Tracker")) {
                    // "Select the tracker from the list of created trackers" (p.699): the selected shape's tracker.
                    row->addWidget(shapeTrackerControl(box), 1);
                } else if (p.kind == QLatin1String("track")) {
                    // A Levels or Curves control in Color Correction mode (p.685): its Enable button where it has one
                    // of its own, or on the first of the points that share a Levels tab's; no value to adjust here.
                    const auto cp = model::correctionParameterOf(category, p.name);
                    if (cp && !cp->enablePath.isEmpty() && !correctionEnables_.contains(cp->enablePath)) {
                        auto* enable = new QCheckBox(tr("Enable"), box);
                        enable->setObjectName(QStringLiteral("correctionEnable:") + cp->enablePath);
                        connect(enable, &QCheckBox::toggled, this, [this, path = cp->enablePath](bool on) { setCorrectionEnabled(path, on); });
                        correctionEnables_.insert(cp->enablePath, enable);
                        row->addWidget(enable);
                    }
                    row->addStretch(1);
                } else if (p.kind == QLatin1String("angle")) {
                    // An OFX plug-in's Angle control (p.162; angle_dial.h): kept when a drag ends or a key changes it.
                    auto* dial = new AngleDial(box);
                    dial->setObjectName(QStringLiteral("param:") + k);
                    dial->setValue(p.defaultValue.toDouble());
                    auto* value = new QLabel(p.shown(p.defaultValue.toDouble()), box);
                    value->setMinimumWidth(48);
                    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    connect(dial, &AngleDial::valueChanged, this, [value, p](double v) { value->setText(p.shown(v)); });
                    connect(dial, &AngleDial::editingFinished, this, [this, category, p](double v) { setValue(category, p.name, p.rounded(v)); });
                    row->addWidget(dial);
                    row->addWidget(value);
                    row->addStretch(1);
                    controls_.insert(k, dial);
                    valueLabels_.insert(k, value);
                } else if (p.kind == QLatin1String("button") && category.endsWith(QLatin1String(" Plug-In Parameters"))) {
                    // An OFX plug-in's push button (p.161): the plug-in is told, and keeps what it sets (ofxPushButton).
                    auto* press = new QToolButton(box);
                    press->setObjectName(QStringLiteral("param:") + k);
                    press->setText(p.text);
                    connect(press, &QToolButton::clicked, this, [this, name = p.name] { pressPluginButton(name); });
                    row->addWidget(press);
                    row->addStretch(1);
                } else if (p.kind == QLatin1String("slider")) {
                    auto* s = new QSlider(Qt::Horizontal, box);
                    s->setObjectName(QStringLiteral("param:") + k);
                    s->setRange(p.sliderPosition(p.min), p.sliderPosition(p.max));
                    s->setValue(defaultInt(p));
                    s->setPageStep(10);
                    s->installEventFilter(this);
                    auto* value = new QLabel(p.shown(p.fromSlider(s->value()).toDouble()), box);
                    value->setMinimumWidth(36);
                    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    connect(s, &QSlider::valueChanged, this, [this, s, value, category, p](int v) {
                        value->setText(p.shown(p.fromSlider(v).toDouble()));
                        if (!s->isSliderDown()) setValue(category, p.name, p.fromSlider(v));
                    });
                    connect(s, &QSlider::sliderReleased, this,
                            [this, s, category, p] { setValue(category, p.name, p.fromSlider(s->value())); });
                    row->addWidget(s, 1);
                    row->addWidget(value);
                    controls_.insert(k, s);
                    valueLabels_.insert(k, value);
                } else if (p.kind == QLatin1String("color")) {
                    auto* swatch = new ColorSwatch(
                        box, [this, category, name = p.name](const QPoint& global) { pickColorAt(category, name, global); },
                        // "Open the color info tool when picking color" (Effect Editor setting, Editing Guide p.1172).
                        [this] { emit colorPickingStarted(); });
                    swatch->setObjectName(QStringLiteral("param:") + k);
                    swatch->setFixedSize(18, 14);
                    setSwatch(swatch, defaultColor(p));
                    connect(swatch, &QToolButton::clicked, this, [this, swatch, category, name = p.name] {
                        const QColor c = QColorDialog::getColor(swatch->property("color").value<QColor>(), this);
                        if (c.isValid()) setValue(category, name, c.name());
                    });
                    row->addWidget(swatch);
                    row->addStretch(1);
                    controls_.insert(k, swatch);
                } else if (p.kind == QLatin1String("menu") && !p.options.isEmpty()) {
                    auto* m = new QComboBox(box);
                    m->setObjectName(QStringLiteral("param:") + k);
                    m->addItems(p.options);
                    connect(m, &QComboBox::currentTextChanged, this,
                            [this, category, name = p.name](const QString& t) { setValue(category, name, t); });
                    if (category == QLatin1String(model::kPanScanCategory) && p.name == QLatin1String("Target")) {
                        // Choosing the Target already chosen also resets the wire frame (p.235).
                        connect(m, &QComboBox::textActivated, this, [this, m](const QString& t) {
                            if (t == m->currentText()) chooseTarget(t);
                        });
                    }
                    row->addWidget(m, 1);
                    controls_.insert(k, m);
                } else if (category == QLatin1String("Stamp") && (p.name == QLatin1String("Stamp Image") || p.name == QLatin1String("Clear"))) {
                    // "Stamp Image: Lets you add the stamp on one or more keyframes"; "Clear: Lets you clear the
                    // stamp buffer" (p.585). Ours: each press is kept at the position's time in the effect.
                    auto* press = new QToolButton(box);
                    press->setObjectName(QStringLiteral("param:") + k);
                    press->setText(p.text);
                    const bool stamp = p.name == QLatin1String("Stamp Image");
                    connect(press, &QToolButton::clicked, this, [this, stamp] { stamp ? stampImage() : clearStamps(); });
                    row->addWidget(press);
                    row->addStretch(1);
                } else if (p.name == QLatin1String("Import Image") && effect && media::isPanAndZoom(*effect)) {
                    // "click the Other Options button. A file selection dialog box opens" (p.240); the file's name
                    // beside it.
                    auto* import = new QToolButton(box);
                    import->setObjectName(QStringLiteral("param:") + k);
                    import->setText(tr("Other Options..."));
                    connect(import, &QToolButton::clicked, this, [this] {
                        const QString file = QFileDialog::getOpenFileName(this, tr("Import Image"));
                        if (!file.isEmpty()) importImage(file);
                    });
                    auto* file = new QLabel(QFileInfo(media::panZoomImagePath(*effect)).fileName(), box);
                    file->setObjectName(QStringLiteral("panZoomFile"));
                    row->addWidget(import);
                    row->addWidget(file, 1);
                } else if (category == QLatin1String(model::kPanScanCategory) && p.kind == QLatin1String("button")) {
                    // Establish Origin, Subdivide Effect and Reset to Origin (p.564).
                    auto* press = new QToolButton(box);
                    press->setObjectName(QStringLiteral("param:") + k);
                    press->setText(p.text);
                    const QString action = p.name;
                    connect(press, &QToolButton::clicked, this, [this, action] {
                        if (action == QLatin1String("Establish Origin")) establishOrigin();
                        else if (action == QLatin1String("Reset to Origin")) resetToOrigin();
                        else if (action == QLatin1String("Subdivide Effect")) subdivideEffect();
                    });
                    row->addWidget(press);
                    row->addStretch(1);
                } else if (category == QLatin1String("Custom Grid") && p.name == QLatin1String("More Options")) {
                    // "You define the grid by clicking the More Options button to open a dialog box"
                    // (p.494).
                    auto* more = new QToolButton(box);
                    more->setObjectName(QStringLiteral("param:") + k);
                    more->setText(tr("More Options..."));
                    connect(more, &QToolButton::clicked, this, [this] {
                        const QJsonObject custom = current_ ? current_->parameters.value(QStringLiteral("Custom Grid")).toObject() : QJsonObject{};
                        QDialog dialog(this);
                        dialog.setWindowTitle(tr("Custom Grid"));
                        auto* form = new QFormLayout(&dialog);
                        auto* columns = new QSpinBox(&dialog);
                        columns->setRange(2, 64);
                        columns->setValue(custom.value(QStringLiteral("Columns")).toInt(8));
                        auto* rows = new QSpinBox(&dialog);
                        rows->setRange(2, 64);
                        rows->setValue(custom.value(QStringLiteral("Rows")).toInt(6));
                        form->addRow(tr("Columns"), columns);
                        form->addRow(tr("Rows"), rows);
                        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
                        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
                        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                        form->addRow(buttons);
                        if (dialog.exec() == QDialog::Accepted) setCustomGrid(columns->value(), rows->value());
                    });
                    row->addWidget(more);
                    row->addStretch(1);
                } else if (p.name == QLatin1String("Frame Processing") && p.kind == QLatin1String("button")) {
                    // "click the Frame Processing button. If the Frame Processing button is selected, Media Composer uses
                    // frame processing" (FX p.421).
                    auto* press = new QToolButton(box);
                    press->setObjectName(QStringLiteral("param:") + k);
                    press->setText(tr("Frame Processing"));
                    press->setCheckable(true);
                    press->setChecked(true);
                    connect(press, &QToolButton::toggled, this, [this, category, name = p.name](bool on) { setValue(category, name, on); });
                    row->addWidget(press);
                    row->addStretch(1);
                    controls_.insert(k, press);
                } else if (category == QLatin1String(model::kSubCapCategory) &&
                           (p.kind == QLatin1String("other") || p.kind == QLatin1String("button") || p.name == QLatin1String("Font Face"))) {
                    // SubCap's controls (FX Guide p.501-504; docs/spec/subcap.md): the Font Face list, the thumbwheels
                    // as number boxes, the Caption text box, and the buttons, which MainWindow carries out.
                    if (p.kind == QLatin1String("button")) {
                        auto* press = new QToolButton(box);
                        press->setObjectName(QStringLiteral("param:") + k);
                        press->setText(p.text);
                        const QString action = p.name == QLatin1String("Import Caption Data")      ? QStringLiteral("import")
                                               : p.name == QLatin1String("Export Caption Data")    ? QStringLiteral("export")
                                               : p.name == QLatin1String("Edit Master Caption List") ? QStringLiteral("masterList")
                                                                                                     : QStringLiteral("globalProperties");
                        connect(press, &QToolButton::clicked, this, [this, action] { emit subcapActionRequested(action); });
                        row->addWidget(press);
                        row->addStretch(1);
                    } else if (p.name == QLatin1String("Font Face")) {
                        auto* fonts = new QFontComboBox(box);
                        fonts->setObjectName(QStringLiteral("param:") + k);
                        connect(fonts, &QFontComboBox::currentTextChanged, this,
                                [this, category, name = p.name](const QString& t) { setValue(category, name, t); });
                        row->addWidget(fonts, 1);
                        controls_.insert(k, fonts);
                    } else if (p.name == QLatin1String(model::kCaptionTextKey)) {
                        // "You cannot enter line breaks (carriage returns) in the Effect Editor text box" (p.381): one
                        // line; the Master Caption List takes rows.
                        auto* edit = new QLineEdit(box);
                        edit->setObjectName(QStringLiteral("param:") + k);
                        connect(edit, &QLineEdit::editingFinished, this, [this, category, name = p.name, edit] {
                            if (!syncing_) setValue(category, name, edit->text());
                        });
                        row->addWidget(edit, 1);
                        controls_.insert(k, edit);
                    } else {
                        auto* number = new QSpinBox(box);
                        number->setObjectName(QStringLiteral("param:") + k);
                        number->setRange(static_cast<int>(p.min), static_cast<int>(p.max));
                        connect(number, &QSpinBox::valueChanged, this, [this, category, name = p.name](int v) {
                            if (!syncing_) setValue(category, name, v);
                        });
                        row->addWidget(number);
                        row->addStretch(1);
                        controls_.insert(k, number);
                    }
                } else if (category == model::kBrushCategory && p.name == QLatin1String("Shape buttons")) {
                    // The Brush Shape buttons, the Brush Template icon and the Brush Preview window (p.397-399).
                    row->addWidget(brushPanel(box), 1);
                } else {
                    // Buttons and other controls need the Effect Preview
                    // monitor or tools that come later.
                    auto* later = new QLabel(tr("(later)"), box);
                    later->setEnabled(false);
                    row->addWidget(later, 1);
                }
            }
            if (QWidget* control = controls_.value(k)) {
                control->setProperty("parameterKey", k);
                control->setProperty("nameLabel", QVariant::fromValue(nameLabel));
                control->installEventFilter(this);
            }
            // The parameter's keyframe track; a slider's has an opener for its graph (p.96-97).
            QToolButton* graphOpener = nullptr;
            if (keyframed && p.kind == QLatin1String("slider")) {
                graphOpener = new QToolButton(box);
                graphOpener->setObjectName(QStringLiteral("graphOpener:") + k);
                graphOpener->setArrowType(Qt::RightArrow);
                graphOpener->setAutoRaise(true);
                graphOpener->setCheckable(true);
                graphOpener->setFixedSize(16, 16);
                connect(graphOpener, &QToolButton::toggled, this, [this, k](bool open) { setGraphOpen(k, open); });
                graphOpeners_.insert(k, graphOpener);
            }
            withTrack(row, box, KeyframeTrack::Kind::Parameter, keyframed ? k : QString(), graphOpener);
            grid->addLayout(row);
            if (graphOpener) {
                auto* graphRow = new QHBoxLayout;
                graphRow->addStretch(1);
                // Left of the graph: Zoom to Curve Height and the vertical scale bar (p.99-100).
                auto* gap = new QWidget(box);
                gap->setFixedWidth(16);
                auto* side = new QVBoxLayout(gap);
                side->setContentsMargins(0, 0, 0, 0);
                side->setSpacing(1);
                auto* fit = new QToolButton(gap);
                fit->setObjectName(QStringLiteral("graphFit:") + k);
                fit->setToolTip(tr("Zoom to Curve Height"));
                fit->setAutoRaise(true);
                fit->setFixedSize(16, 14);
                fit->setIcon(graphIcon(palette().color(QPalette::WindowText)));
                connect(fit, &QToolButton::clicked, this, [this, k] { zoomToCurveHeight(k); });
                auto* scale = new QSlider(Qt::Vertical, gap);
                scale->setObjectName(QStringLiteral("graphScale:") + k);
                scale->setToolTip(tr("Vertical scale bar"));
                scale->setRange(0, 100);
                scale->setValue(50);
                // "Drag the vertical scale bar down (to zoom in) or up (to zoom out)" (p.99), around
                // the middle of the values shown.
                connect(scale, &QSlider::valueChanged, this, [this, k](int v) {
                    if (!graphZoomBase_.contains(k)) graphZoomBase_.insert(k, graphRange(k));
                    const auto [baseLo, baseHi] = graphZoomBase_.value(k);
                    const auto [lo, hi] = graphRange(k);
                    const double middle = (lo + hi) / 2.0;
                    const double half = (baseHi - baseLo) / 2.0 * std::pow(2.0, (v - 50) / 25.0);
                    setGraphRange(k, middle - half, middle + half);
                });
                side->addWidget(fit);
                side->addWidget(scale, 1);
                graphRow->addWidget(gap);
                auto* graph = new KeyframeTrack(*this, KeyframeTrack::Kind::Graph, QStringLiteral("graph:") + k, box);
                graphRow->addWidget(graph);
                tracks_.insert(QStringLiteral("graph:") + k, graph);
                // The side controls come and go with their graph.
                graph->setProperty("side", QVariant::fromValue(gap));
                grid->addLayout(graphRow);
            }
        }
        connect(opener, &QToolButton::toggled, this, [opener, box](bool open) {
            opener->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
            box->setVisible(open);
        });
        // The category's opener and its group track (p.95).
        auto* header = new QWidget(body_);
        auto* h = new QHBoxLayout(header);
        h->setContentsMargins(0, 0, 0, 0);
        opener->setParent(header);
        h->addWidget(opener);
        h->addStretch(1);
        withTrack(h, header, KeyframeTrack::Kind::Group, QStringLiteral("group:") + category, nullptr);
        groups_->insertWidget(at++, header);
        groups_->insertWidget(at++, box);
        categoryOrder_ << category;
        groupBodies_.insert(category, box);
        openers_.insert(category, opener);
    }
    // The nested effects inside the open one, after its groups.
    if (nested) {
        for (int k = level_ + 1; k < chain_.size(); ++k) groups_->insertWidget(at++, nestedRow(k));
    }
    applyLook();
    syncValues(*effect);
}

QWidget* EffectEditor::burnInPanel(QWidget* parent) {
    // "Display 1, Display 2, Display 3: Let you add up to three timecode displays to the video"; the
    // Notes Track's text annotation (p.505-508). Ours: a tab for each.
    auto* tabs = new QTabWidget(parent);
    tabs->setObjectName(QStringLiteral("burnInTabs"));
    const QStringList readers = {QStringLiteral("Timecode"),       QStringLiteral("Source Timecode"),  QStringLiteral("Edgecode"),
                                 QStringLiteral("Source Edgecode"), QStringLiteral("Frames"),          QStringLiteral("Source Clip Name"),
                                 QStringLiteral("Sequence Name"),   QStringLiteral("Source Bin Column"), QStringLiteral("Sequence Bin Column"),
                                 QStringLiteral("Other Counters"),  QStringLiteral("Timeline Clip Text")};
    const QStringList types = {QStringLiteral("Current"), QStringLiteral("End"),  QStringLiteral("Duration"),  QStringLiteral("Mark IN"),
                               QStringLiteral("Mark OUT"), QStringLiteral("I/O"), QStringLiteral("Remaining"), QStringLiteral("Absolute")};
    QStringList tracks = {QStringLiteral("Current Track")};
    tracks << recordSequence().trackNames();
    for (int display = 1; display <= 4; ++display) {
        const int which = display % 4; // Display 1-3, then the Notes (0)
        const QString tab = which == 0 ? QStringLiteral("Notes") : QStringLiteral("Display %1").arg(which);
        auto* page = new QWidget(tabs);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(4, 4, 4, 4);
        auto key = [tab](const QString& name) { return tab + QLatin1Char('/') + name; };
        auto check = [&](const QString& name, const QString& text) {
            auto* c = new QCheckBox(text, page);
            c->setObjectName(QStringLiteral("burnIn:") + key(name));
            connect(c, &QCheckBox::toggled, this, [this, which, name](bool on) { setBurnIn(which, name, on); });
            form->addRow(c);
            burnInControls_.insert(key(name), c);
        };
        auto menu = [&](const QString& name, const QStringList& options) {
            auto* m = new QComboBox(page);
            m->setObjectName(QStringLiteral("burnIn:") + key(name));
            m->addItems(options);
            connect(m, &QComboBox::currentTextChanged, this, [this, which, name](const QString& t) { setBurnIn(which, name, t); });
            form->addRow(name, m);
            burnInControls_.insert(key(name), m);
        };
        auto number = [&](const QString& name, int lo, int hi, double scale) {
            // Opacities are 0 to 1 "in 0.1 increments" (p.507-508): a slider of tenths.
            auto* n = new QSpinBox(page);
            n->setObjectName(QStringLiteral("burnIn:") + key(name));
            n->setRange(lo, hi);
            n->setProperty("scale", scale);
            connect(n, &QSpinBox::valueChanged, this, [this, which, name, scale](int v) {
                setBurnIn(which, name, scale == 1.0 ? QJsonValue(v) : QJsonValue(v * scale));
            });
            form->addRow(name, n);
            burnInControls_.insert(key(name), n);
        };
        auto line = [&](const QString& name, const QString& label) {
            auto* l = new QLineEdit(page);
            l->setObjectName(QStringLiteral("burnIn:") + key(name));
            connect(l, &QLineEdit::editingFinished, this, [this, which, name, l] { setBurnIn(which, name, l->text()); });
            form->addRow(label, l);
            burnInControls_.insert(key(name), l);
        };
        auto colour = [&](const QString& name, const QString& label) {
            auto* swatch = new QToolButton(page);
            swatch->setObjectName(QStringLiteral("burnIn:") + key(name));
            swatch->setFixedSize(18, 14);
            connect(swatch, &QToolButton::clicked, this, [this, which, name, swatch] {
                const QColor c = QColorDialog::getColor(swatch->property("color").value<QColor>(), this);
                if (c.isValid()) setBurnIn(which, name, c.name());
            });
            form->addRow(label, swatch);
            burnInControls_.insert(key(name), swatch);
        };
        if (which == 0) {
            line(QStringLiteral("Text"), tr("Text"));
        } else {
            check(QStringLiteral("Enable"), tr("Enable"));
            menu(QStringLiteral("Reader"), readers);
            menu(QStringLiteral("Type"), types);
            check(QStringLiteral("Display Label"), tr("Display Label"));
            check(QStringLiteral("Display Fields"), tr("Display Fields"));
            check(QStringLiteral("Local Frame Count"), tr("Local Frame Count"));
            number(QStringLiteral("Start Counting From"), 0, 9999999, 1.0);
            menu(QStringLiteral("Source Track"), tracks);
            line(QStringLiteral("Column Name"), tr("Column Name"));
        }
        // Appearance (p.507-508).
        number(QStringLiteral("X"), 0, 100, 1.0);
        number(QStringLiteral("Y"), 0, 100, 1.0);
        number(QStringLiteral("Font Size"), 4, 400, 1.0);
        colour(which == 0 ? QStringLiteral("Text Color") : QStringLiteral("Text"), tr("Text"));
        number(QStringLiteral("Text Opacity"), 0, 10, 0.1);
        check(QStringLiteral("Use Text Outline"), tr("Use Text Outline"));
        number(QStringLiteral("Line Weight"), 0, 10, 1.0);
        colour(QStringLiteral("Background"), tr("Background"));
        number(QStringLiteral("Border Opacity"), 0, 10, 0.1);
        tabs->addTab(page, tab);
    }
    return tabs;
}

void EffectEditor::syncBurnIn(const model::Effect& effect) {
    for (auto it = burnInControls_.cbegin(); it != burnInControls_.cend(); ++it) {
        const QString tab = it.key().section(QLatin1Char('/'), 0, 0);
        const QString name = it.key().section(QLatin1Char('/'), 1);
        const int display = tab == QLatin1String("Notes") ? 0 : tab.right(1).toInt();
        const QJsonValue v = media::burnInSetting(effect, display, name);
        QWidget* w = it.value();
        const QSignalBlocker block(w);
        if (auto* c = qobject_cast<QCheckBox*>(w)) {
            c->setChecked(v.toBool());
        } else if (auto* m = qobject_cast<QComboBox*>(w)) {
            m->setCurrentText(v.toString());
        } else if (auto* n = qobject_cast<QSpinBox*>(w)) {
            n->setValue(static_cast<int>(std::lround(v.toDouble() / n->property("scale").toDouble())));
        } else if (auto* l = qobject_cast<QLineEdit*>(w)) {
            l->setText(v.toString());
        } else if (auto* b = qobject_cast<QToolButton*>(w)) {
            const QColor color(v.toString());
            if (color.isValid()) setSwatch(b, color);
        }
    }
}

void EffectEditor::setBurnIn(int display, const QString& name, const QJsonValue& value) {
    if (syncing_ || !shown_) return;
    const QString category = QStringLiteral("Timecode Burn-In Effect Parameters");
    const QString tab = display == 0 ? QStringLiteral("Notes") : QStringLiteral("Display %1").arg(display);
    changeShown(tr("Modify Effect"), [&](model::Effect& e) {
        QJsonObject all = e.parameters.value(category).toObject();
        QJsonObject d = all.value(tab).toObject();
        d.insert(name, value);
        all.insert(tab, d);
        e.parameters.insert(category, all);
    });
}

// Parameters without a stored value show their default (defaultValue()), or keep what the control shows
// when there is none.
void EffectEditor::syncValues(const model::Effect& effect) {
    syncing_ = true;
    if (auto* file = findChild<QLabel*>(QStringLiteral("panZoomFile"))) file->setText(QFileInfo(media::panZoomImagePath(effect)).fileName());
    syncTracking(effect);
    for (auto it = controls_.cbegin(); it != controls_.cend(); ++it) {
        const QString category = it.key().section(QLatin1Char('/'), 0, 0);
        const QString name = it.key().section(QLatin1Char('/'), 1);
        // Keyframed parameters show their value at the position (p.95).
        QJsonValue v = effect.parameters.value(category).toObject().value(name);
        if (!model::keyframesOf(effect, category, name).keys.isEmpty()) v = valueAt(effect, it.key(), positionTime());
        if (v.isUndefined()) v = defaultValue(effect, it.key());
        if (v.isUndefined()) continue;
        QWidget* w = it.value();
        const QSignalBlocker block(w);
        if (auto* s = qobject_cast<QSlider*>(w)) {
            const EffectParameter* info = parameterInfo(it.key());
            s->setValue(info ? info->sliderPosition(v.toDouble()) : static_cast<int>(std::lround(v.toDouble())));
            if (QLabel* l = valueLabels_.value(it.key())) l->setText(info ? info->shown(v.toDouble()) : QString::number(s->value()));
        } else if (auto* dial = qobject_cast<AngleDial*>(w)) {
            dial->setValue(v.toDouble());
            const EffectParameter* info = parameterInfo(it.key());
            if (QLabel* l = valueLabels_.value(it.key())) l->setText(info ? info->shown(v.toDouble()) : QString::number(v.toDouble()));
        } else if (auto* c = qobject_cast<QCheckBox*>(w)) {
            c->setChecked(v.toBool());
        } else if (auto* m = qobject_cast<QComboBox*>(w)) {
            m->setCurrentText(v.toString());
        } else if (auto* number = qobject_cast<QSpinBox*>(w)) {
            number->setValue(v.toInt());
        } else if (auto* edit = qobject_cast<QLineEdit*>(w)) {
            edit->setText(v.toString());
        } else if (auto* b = qobject_cast<QToolButton*>(w); b && b->isCheckable()) {
            b->setChecked(v.toBool());
        } else if (auto* swatch = qobject_cast<QToolButton*>(w)) {
            const QColor color(v.toString());
            if (color.isValid()) setSwatch(swatch, color);
        }
        // The Frame/Field slider "is labeled Frame" with frame processing, "Field" without (FX p.421).
        if (name == QLatin1String("Frame Processing")) {
            if (auto* l = findChild<QLabel*>(QStringLiteral("paramLabel:") + category + QStringLiteral("/Frame/Field"))) l->setText(v.toBool() ? tr("Frame") : tr("Field"));
        }
    }
    if (brushPreview_) brushPreview_->setBrush(effect.parameters.value(model::kBrushCategory).toObject());
    syncBurnIn(effect);
    // The Levels and Curves Enable buttons follow the correction's, as the tool's (p.685: "ganged together").
    if (correctionShown_ && !correctionEnables_.isEmpty()) {
        const auto stored = timeline_.storedCorrection();
        for (auto it = correctionEnables_.cbegin(); it != correctionEnables_.cend(); ++it) {
            const QSignalBlocker block(it.value());
            it.value()->setChecked(stored && stored->enabled(it.key()));
        }
    }
    syncing_ = false;
}

bool EffectEditor::setCorrectionEnabled(const QString& path, bool on) {
    if (!correctionShown_ || syncing_) return false;
    auto stored = timeline_.storedCorrection();
    if (!stored) return false;
    stored->setEnabled(path, on);
    return timeline_.setStoredCorrection(*stored, tr("Enable"));
}

void EffectEditor::syncTransition() {
    // The Transition parameters are active for transition effects (p.500).
    const QList<model::TrimRoller> cuts = transitionCuts();
    alignment_->setEnabled(!cuts.isEmpty());
    duration_->setEnabled(!cuts.isEmpty());
    std::optional<model::TransitionAlignment> shownAlignment;
    syncing_ = true;
    duration_->clear();
    if (shown_ && shown_->target == model::EffectTarget::Transition) {
        const model::Sequence seq = recordSequence();
        if (const model::Transition* t = model::transitionFor(seq, *shown_)) {
            duration_->setText(durationText(t->duration));
            shownAlignment = t->alignment;
            shownAlignment_ = t->alignment;
        }
    } else if (!cuts.isEmpty()) {
        duration_->setText(durationText(0));
    }
    syncing_ = false;
    // Without a transition the icon is a vertical bar (p.500).
    alignment_->setIcon(alignmentIcon(shownAlignment, palette().color(QPalette::WindowText)));
}

// A keyframed parameter's change goes to its selected keyframes, else to the keyframe at the position,
// else to a new keyframe there ("You can also add keyframes automatically by adjusting a parameter when
// you place the position indicator in a part of the keyframe graph that has no keyframe", p.103).
QJsonValue EffectEditor::defaultValue(const model::Effect& effect, const QString& parameter) const {
    // The effect's own default, as the compositor uses it, else the parameter's from
    // effect_parameters.json.
    const QJsonValue own = media::parameterDefault(effect, parameter.section(QLatin1Char('/'), 0, 0), parameter.section(QLatin1Char('/'), 1));
    // A null default (none of the effect's own) falls back too.
    if (!own.isUndefined() && !own.isNull()) return own;
    const EffectParameter* info = parameterInfo(parameter);
    return info ? info->defaultValue : QJsonValue();
}

void EffectEditor::setCustomGrid(int columns, int rows) {
    // "The minimum number of rows and columns is 2 x 2" (p.494).
    setValue(QStringLiteral("Custom Grid"), QStringLiteral("Columns"), std::max(columns, 2));
    setValue(QStringLiteral("Custom Grid"), QStringLiteral("Rows"), std::max(rows, 2));
}

void EffectEditor::setValue(const QString& category, const QString& parameter, const QJsonValue& value) {
    if (syncing_ || !shown_) return;
    if (current_ && model::isPanAndScanEffect(*current_) && category == QLatin1String(model::kPanScanCategory) &&
        parameter == QLatin1String("Target")) {
        chooseTarget(value.toString());
        return;
    }
    if (current_ && media::isPanAndZoom(*current_) && (parameter == QLatin1String("Velocity In") || parameter == QLatin1String("Velocity Out"))) {
        // "Click the keyframe indicator for the keyframe you want to adjust. Click the In Fast Menu button, and then
        // select a Velocity In option" (p.243): the selected keyframes' times take it, kept as keyframes of the
        // menu; with none selected the effect does. "When you select Constant for Velocity In, Media Composer
        // automatically selects Constant for Velocity Out" (p.530).
        QList<double> times;
        for (const KeyframeId& id : selection_) {
            const QString cat = id.parameter.section(QLatin1Char('/'), 0, 0);
            const model::ParameterKeyframes keys = model::keyframesOf(*current_, cat, id.parameter.section(QLatin1Char('/'), 1));
            if (id.index >= keys.keys.size()) continue;
            const double t = model::keyframeTime(keys, keys.keys.at(id.index), span_);
            if (std::none_of(times.begin(), times.end(), [&](double u) { return std::abs(u - t) <= span_.halfFrame(); })) times << t;
        }
        QStringList names{parameter};
        if (parameter == QLatin1String("Velocity In") && value.toString() == QLatin1String("Constant")) names << QStringLiteral("Velocity Out");
        changeShown(tr("Modify Effect"), [&](model::Effect& e) {
            for (const QString& name : names) {
                if (times.isEmpty()) {
                    QJsonObject group = e.parameters.value(category).toObject();
                    group.insert(name, value);
                    e.parameters.insert(category, group);
                } else {
                    for (double t : times) model::setKeyframe(e, category, name, t, span_, value);
                }
            }
        });
        return;
    }
    if (current_ && media::isPanAndZoom(*current_) && parameter == QLatin1String("Cache")) {
        // The Cache menu's entries are actions (p.247): Free Current Cache, Free All Caches.
        if (value.toString() == QLatin1String("Free All Caches")) media::freePanZoomCache();
        else if (value.toString() == QLatin1String("Free Current Cache")) media::freePanZoomCache(media::panZoomImagePath(*current_));
        return;
    }
    const QString k = key(category, parameter);
    setActiveParameter(k);
    // Fixed Aspect "Gangs the X and Y sliders together so that they move in unison when you drag either
    // one" (Scaling p.499; Feathering's Hor and Vert p.495, Blur's p.533).
    QString ganged;
    // Pan and Scan's wire frame keeps the Target's shape (p.236): its Scaling X and Y move together too.
    const bool panScanScaling = current_ && model::isPanAndScanEffect(*current_) && category == QLatin1String("Scaling");
    if (current_ && (panScanScaling || current_->parameters.value(category).toObject().value(QStringLiteral("Fixed Aspect")).toBool())) {
        static const QHash<QString, QString> pairs = {{QStringLiteral("X"), QStringLiteral("Y")}, {QStringLiteral("Y"), QStringLiteral("X")},
                                                      {QStringLiteral("Hor"), QStringLiteral("Vert")}, {QStringLiteral("Vert"), QStringLiteral("Hor")}};
        ganged = pairs.value(parameter);
    }
    if (current_ && !model::keyframesOf(*current_, category, parameter).keys.isEmpty()) {
        const double t = positionTime();
        changeKeyframes(tr("Modify Effect"), [&](model::Effect& e) {
            model::ParameterKeyframes keys = model::keyframesOf(e, category, parameter);
            bool any = false;
            for (const KeyframeId& id : selection_) {
                if (id.parameter != k || id.index >= keys.keys.size()) continue;
                keys.keys[id.index].value = value;
                any = true;
            }
            if (any) {
                model::setKeyframes(e, category, parameter, keys);
            } else {
                model::setKeyframe(e, category, parameter, t, span_, value);
            }
            if (!ganged.isEmpty()) model::setKeyframe(e, category, ganged, t, span_, value);
            return true;
        });
        return;
    }
    changeShown(tr("Modify Effect"), [&](model::Effect& e) {
        QJsonObject group = e.parameters.value(category).toObject();
        group.insert(parameter, value);
        if (!ganged.isEmpty()) group.insert(ganged, value);
        e.parameters.insert(category, group);
    });
}

// Keeps the shown transition's duration and start; elsewhere the default transition length.
void EffectEditor::setAlignment(model::TransitionAlignment alignment) {
    const QList<model::TrimRoller> cuts = transitionCuts();
    if (cuts.isEmpty()) return;
    std::int64_t duration = timeline_.defaultTransitionLength();
    std::int64_t start = 0;
    if (shown_) {
        const model::Sequence seq = recordSequence();
        if (const model::Transition* t = model::transitionFor(seq, *shown_)) {
            duration = t->duration;
            start = t->before();
        }
    }
    shownAlignment_ = alignment;
    timeline_.setTransitions(cuts, duration, alignment, start);
}

bool EffectEditor::setDuration(const QString& text) {
    // Seconds:frames, or frames (p.500).
    const std::int64_t fps = std::max(timeline_.format().nominalFps, 1);
    std::int64_t frames = 0;
    bool ok = false;
    if (text.contains(QLatin1Char(':'))) {
        bool okS = false;
        bool okF = false;
        const std::int64_t s = text.section(QLatin1Char(':'), 0, 0).toLongLong(&okS);
        const std::int64_t f = text.section(QLatin1Char(':'), 1).toLongLong(&okF);
        ok = okS && okF;
        frames = s * fps + f;
    } else {
        frames = text.toLongLong(&ok);
    }
    if (!ok || frames <= 0) {
        syncTransition();
        return false;
    }
    // The start (frames before the cut) stays the shown transition's, within the new duration; for a
    // new transition it is half the duration.
    const QList<model::TrimRoller> cuts = transitionCuts();
    if (cuts.isEmpty()) return false;
    std::int64_t start = frames / 2;
    if (shown_) {
        const model::Sequence seq = recordSequence();
        if (const model::Transition* t = model::transitionFor(seq, *shown_)) start = std::min(t->before(), frames);
    }
    const bool done = timeline_.setTransitions(cuts, frames, shownAlignment_, start);
    if (!done) syncTransition();
    return done;
}

QStringList EffectEditor::categories() const {
    return categoryOrder_;
}

bool EffectEditor::isOpen(const QString& category) const {
    const QToolButton* b = openers_.value(category);
    return b && b->isChecked();
}

void EffectEditor::setOpen(const QString& category, bool open) {
    if (QToolButton* b = openers_.value(category)) b->setChecked(open);
}

QSlider* EffectEditor::slider(const QString& category, const QString& parameter) const {
    return qobject_cast<QSlider*>(controls_.value(key(category, parameter)));
}

QCheckBox* EffectEditor::checkBox(const QString& category, const QString& parameter) const {
    return qobject_cast<QCheckBox*>(controls_.value(key(category, parameter)));
}

bool EffectEditor::eventFilter(QObject* watched, QEvent* event) {
    // Dragging the effect icon to a bin (p.53).
    if (watched == titleIcon_) {
        if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            iconPress_ = static_cast<QMouseEvent*>(event)->position().toPoint();
            return true;
        }
        if (event->type() == QEvent::MouseMove && !iconPress_.isNull()) {
            auto* m = static_cast<QMouseEvent*>(event);
            if ((m->position().toPoint() - iconPress_).manhattanLength() >= QApplication::startDragDistance()) {
                iconPress_ = {};
                if (QMimeData* mime = templateMime(m->modifiers().testFlag(Qt::AltModifier))) {
                    auto* drag = new QDrag(this);
                    drag->setMimeData(mime);
                    if (!titleIcon_->pixmap().isNull()) drag->setPixmap(titleIcon_->pixmap());
                    drag->exec(Qt::CopyAction);
                }
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) iconPress_ = {};
    }
    // A nested effect's title row dragged onto another (p.222).
    if (const QVariant lv = watched->property("nestedLevel"); lv.isValid()) {
        const int level = lv.toInt();
        const QString mimeType = QStringLiteral("application/x-cutthulhu-nested-effect");
        if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            nestedPress_ = static_cast<QMouseEvent*>(event)->position().toPoint();
            nestedPressLevel_ = level;
        } else if (event->type() == QEvent::MouseMove && nestedPressLevel_ == level &&
                   (static_cast<QMouseEvent*>(event)->position().toPoint() - nestedPress_).manhattanLength() >= QApplication::startDragDistance()) {
            nestedPressLevel_ = -1;
            auto* mime = new QMimeData;
            mime->setData(mimeType, QByteArray::number(level));
            auto* drag = new QDrag(this);
            drag->setMimeData(mime);
            drag->exec(Qt::MoveAction);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            nestedPressLevel_ = -1;
        } else if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* d = static_cast<QDropEvent*>(event);
            if (d->mimeData()->hasFormat(mimeType)) {
                d->acceptProposedAction();
            } else {
                d->ignore();
            }
            return true;
        } else if (event->type() == QEvent::Drop) {
            auto* d = static_cast<QDropEvent*>(event);
            if (d->mimeData()->hasFormat(mimeType)) {
                const int from = d->mimeData()->data(mimeType).toInt();
                d->acceptProposedAction();
                // Later: the rows are rebuilt by the move.
                QMetaObject::invokeMethod(this, [this, from, level] { moveNestedEffect(from, level); }, Qt::QueuedConnection);
            }
            return true;
        }
    }
    // An effect template on a parameter group's opener (p.54).
    if (const QString group = watched->property("templateGroup").toString(); !group.isEmpty()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* d = static_cast<QDropEvent*>(event);
            if (timeline_.droppedTemplate(d->mimeData())) {
                d->acceptProposedAction();
            } else {
                d->ignore();
            }
            return true;
        }
        if (event->type() == QEvent::Drop) {
            auto* d = static_cast<QDropEvent*>(event);
            if (const auto t = timeline_.droppedTemplate(d->mimeData()); t && applyTemplateGroup(group, *t)) d->acceptProposedAction();
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn) {
        const QString k = watched->property("parameterKey").toString();
        if (!k.isEmpty()) setActiveParameter(k);
    }
    // Arrow keys move a slider by one, with Shift by ten (FX guide p.87).
    if (event->type() == QEvent::KeyPress) {
        auto* s = qobject_cast<QSlider*>(watched);
        auto* k = static_cast<QKeyEvent*>(event);
        if (s && (k->modifiers() & Qt::ShiftModifier) && (k->key() == Qt::Key_Left || k->key() == Qt::Key_Right)) {
            s->setValue(s->value() + (k->key() == Qt::Key_Right ? 10 : -10));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------- 3D

namespace {

// Adds `time` to the Stamp category's list `key` ("Stamps" or "Clears"), once.
void addStampTime(model::Effect& e, const char* key, double time) {
    QJsonObject stamp = e.parameters.value(QStringLiteral("Stamp")).toObject();
    QJsonArray times = stamp.value(QLatin1String(key)).toArray();
    for (const QJsonValue& v : times) {
        if (std::abs(v.toDouble() - time) < 1e-9) return;
    }
    times.append(time);
    stamp.insert(QLatin1String(key), times);
    e.parameters.insert(QStringLiteral("Stamp"), stamp);
}

} // namespace

bool EffectEditor::stampImage() {
    if (!current_ || !media::is3DEffect(*current_)) return false;
    const double t = positionTime();
    return changeShown(tr("Stamp Image"), [&](model::Effect& e) { addStampTime(e, "Stamps", t); });
}

bool EffectEditor::clearStamps() {
    if (!current_ || !media::is3DEffect(*current_)) return false;
    const double t = positionTime();
    return changeShown(tr("Clear Stamps"), [&](model::Effect& e) { addStampTime(e, "Clears", t); });
}

bool EffectEditor::promoteTo3D() {
    if (!current_) return false;
    const auto promoted = model::promoteTo3D(*current_);
    if (!promoted) return false;
    const bool ok = changeShown(tr("3D Promote"), [&](model::Effect& e) { e = *promoted; });
    if (ok) refresh();
    return ok;
}

// ---------------------------------------------------------------- nested effects

bool EffectEditor::changeShown(const QString& description, const std::function<void(model::Effect&)>& change) {
    if (!shown_) return false;
    // A correction shown in Color Correction mode goes back to the segment's correction (6.10d3b).
    if (correctionShown_ && current_) {
        const auto stored = timeline_.storedCorrection();
        if (!stored) return false;
        model::Effect e = *current_;
        change(e);
        return timeline_.setStoredCorrection(model::correctionFromEffect(e, *stored), description);
    }
    // Every change of the shown effect keeps a Stabilize effect's Auto-Zoom current (p.279).
    auto changed = [&](model::Effect& e) {
        const model::Effect before = e;
        // With one Intraframe object selected, the change is that object's (6.9b).
        if (level_ == 0 && model::isColorCorrectionEffect(e) && intraframeSelection_.size() == 1) {
            // The Color Correction effect with one shape selected: its view of the effect, split back (6.10e3b3).
            const QList<model::IntraframeObject> objects = model::intraframeObjects(e);
            for (const model::IntraframeObject& o : objects) {
                if (o.id != intraframeSelection_.constFirst()) continue;
                model::Effect view = model::correctionShapeView(e, o);
                change(view);
                model::keepCorrectionShapeChange(e, o.id, view);
                return;
            }
        }
        if (level_ == 0 && model::isIntraframeEffect(e) && intraframeSelection_.size() == 1) {
            QList<model::IntraframeObject> objects = model::intraframeObjects(e);
            for (model::IntraframeObject& o : objects) {
                if (o.id != intraframeSelection_.constFirst()) continue;
                model::Effect oe = model::objectEffect(e, o);
                change(oe);
                o.parameters = oe.parameters;
                model::setIntraframeObjects(e, objects);
                return;
            }
        }
        change(e);
        keepAutoZoom(before, e);
    };
    autoZoomOff_ = false;
    const bool done = level_ == 0 ? timeline_.changeEffect(*shown_, description, [&](model::Effect& e, model::Transition*) { changed(e); })
                                  : timeline_.changeNestedEffect(*shown_, nestOffset_, level_, description, changed);
    if (done && autoZoomOff_) {
        // "A message box appears, informing you that Auto-Zoom will be turned off. If you want to use the Auto-Zoom
        // option again later, you must click Auto-Zoom" (p.279).
        auto* box = new QMessageBox(QMessageBox::Information, tr("Auto-Zoom"),
                                    tr("Auto-Zoom is turned off. Click Auto-Zoom to use it again."), QMessageBox::Ok, this);
        box->setObjectName(QStringLiteral("autoZoomOff"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    }
    autoZoomOff_ = false;
    return done;
}

QStringList EffectEditor::nestedEffects() const {
    QStringList out;
    for (const model::NestedEffect& n : chain_) out << n.effect.name;
    return out;
}

void EffectEditor::setNestedLevel(int level) {
    if (level < 0 || level >= std::max<qsizetype>(chain_.size(), 1)) return;
    level_ = level;
    refresh();
}

bool EffectEditor::moveNestedEffect(int from, int to) {
    if (!shown_ || from == to) return false;
    if (!timeline_.moveNestedEffect(*shown_, nestOffset_, from, to)) {
        if (!timeline_.lastMessage().isEmpty()) {
            auto* box = new QMessageBox(QMessageBox::Information, tr("Change Order of Nested Effects"), timeline_.lastMessage(),
                                        QMessageBox::Ok, this);
            box->setObjectName(QStringLiteral("nestedOrderRefused"));
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
        }
        return false;
    }
    // The moved effect stays open.
    if (level_ == from) level_ = to;
    refresh();
    return true;
}

// ---------------------------------------------------------------- effect templates

std::optional<model::EffectTemplate> EffectEditor::shownTemplate(bool withSource) const {
    if (!shown_) return std::nullopt;
    const model::Sequence seq = recordSequence();
    const Rational rate = timeline_.composer().projectRate();
    // A nested effect open: its own template, without source media.
    if (level_ > 0 && current_) {
        model::Segment s;
        s.length = span_.length;
        s.effect = current_;
        return model::segmentTemplate(s, rate, false);
    }
    if (shown_->target == model::EffectTarget::Transition) {
        const model::Transition* t = model::transitionFor(seq, *shown_);
        if (!t) return std::nullopt;
        return model::transitionTemplate(*t, rate);
    }
    const model::Track* track = seq.track(shown_->track);
    if (!track) return std::nullopt;
    const int i = track->segmentAt(shown_->frame);
    if (i < 0) return std::nullopt;
    return model::segmentTemplate(track->segments.at(i), rate, withSource);
}

QMimeData* EffectEditor::templateMime(bool withSource) const {
    const auto t = shownTemplate(withSource);
    if (!t) return nullptr;
    auto* mime = new QMimeData;
    const QJsonObject o{{QStringLiteral("name"), model::effectTemplateName(*t)}, {QStringLiteral("template"), t->toJson()}};
    mime->setData(effectTemplateMime(), QJsonDocument(o).toJson(QJsonDocument::Compact));
    mime->setText(model::effectTemplateName(*t));
    return mime;
}

bool EffectEditor::applyTemplateGroup(const QString& category, const model::EffectTemplate& t) {
    if (!shown_) return false;
    // Only a group the template's effect has: its values there, or its defaults.
    const EffectInfo* info = EffectCatalog::builtin().find(t.effect.category, t.effect.name);
    const bool has = info ? info->parameters.contains(category) : t.effect.parameters.contains(category);
    if (!has) return false;
    if (!current_) return false;
    model::Effect changed = *current_;
    const Rational rate = timeline_.composer().projectRate();
    if (!model::applyTemplateGroup(changed, t, category, rate)) return false;
    return changeShown(tr("Apply Effect Template"), [&](model::Effect& e) { e = changed; });
}

} // namespace lumenis::ui

namespace lumenis::ui {

// ---------------------------------------------------------------- Intraframe objects (6.9b)

void EffectEditor::previewIntraframe(const std::optional<model::Effect>& effect) {
    intraframePreview_ = effect;
    syncPreview();
    timeline_.composer().refreshDisplay();
}

bool EffectEditor::changeIntraframe(const QString& description, const model::Effect& effect) {
    if (!shown_ || level_ != 0) return false;
    intraframePreview_.reset();
    model::Effect kept = effect;
    // New Color Correction shapes take none of the effect's corrections (6.10e3b3).
    if (model::isColorCorrectionEffect(kept)) {
        QList<model::IntraframeObject> shapes = model::intraframeObjects(kept);
        model::trimShapeParameters(shapes);
        model::setIntraframeObjects(kept, shapes);
    }
    const bool done = timeline_.changeEffect(*shown_, description, [&](model::Effect& e, model::Transition*) { e = kept; });
    syncPreview();
    return done;
}

void EffectEditor::setIntraframeSelection(const QStringList& ids) {
    if (ids == intraframeSelection_) return;
    intraframeSelection_ = ids;
    refresh();
}

double EffectEditor::previewAspect() const {
    const Rational a = timeline_.composer().aspectRatio();
    return a.num() > 0 && a.den() > 0 ? static_cast<double>(a.num()) / static_cast<double>(a.den()) : 16.0 / 9.0;
}

bool EffectEditor::drawsObjects(const model::Effect& effect) const {
    // The Color Correction effect's shapes (p.699) outside Color Correction mode, where the editor shows the mode's
    // correction instead.
    return model::isIntraframeEffect(effect) || (!correctionShown_ && model::isColorCorrectionEffect(effect));
}

void EffectEditor::syncIntraframeButtons() {
    const bool on = wholeIntraframe_.has_value();
    // The Blur and Mosaic effects have the Selection, layer, Rectangle and Oval tools (p.439).
    const bool shapesOnly = on && (wholeIntraframe_->name == QLatin1String("Blur Effect") || wholeIntraframe_->name == QLatin1String("Mosaic Effect"));
    for (auto it = intraframeButtons_.cbegin(); it != intraframeButtons_.cend(); ++it) {
        const auto tool = static_cast<IntraframeTool::Tool>(it.key());
        const bool freehand = tool == IntraframeTool::Tool::Polygon || tool == IntraframeTool::Tool::Curve || tool == IntraframeTool::Tool::Brush;
        // The Previsualization Marker tool: the Paint Effect in 24-fps film projects only (p.412).
        const bool marker = tool == IntraframeTool::Tool::Marker;
        const bool markerShown = on && filmProject_ && wholeIntraframe_->name == QLatin1String("Paint Effect");
        it.value()->setVisible(marker ? markerShown : on && !(shapesOnly && freehand));
        it.value()->setChecked(on && intraframe_ && intraframe_->tool() == tool);
    }
    for (QToolButton* b : {bringForward_, sendBackward_}) {
        if (!b) continue;
        b->setVisible(on);
        b->setEnabled(on && intraframe_ && !intraframe_->selection().isEmpty());
    }
    if (objectMenu_) objectMenu_->setVisible(on);
}

} // namespace lumenis::ui

namespace lumenis::ui {

bool EffectEditor::pressPluginButton(const QString& name) {
    if (!current_ || !shown_) return false;
    const auto* plugin = media::ofxPluginOf(*current_);
    if (!plugin) return false;
    const QString category = media::ofxParameterCategory(*plugin);
    const QJsonObject set = media::ofxPushButton(*current_, name, positionTime(), span_);
    // What the plug-in set goes into the effect, as edits of the Effect Editor's own controls would.
    for (auto it = set.constBegin(); it != set.constEnd(); ++it) setValue(category, it.key(), it.value());
    return true;
}

} // namespace lumenis::ui
