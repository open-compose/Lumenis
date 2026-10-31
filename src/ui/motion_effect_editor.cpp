#include "lumenis/ui/motion_effect_editor.h"

#include "lumenis/core/project_format.h"
#include "lumenis/core/timecode.h"
#include "lumenis/media/fields.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/motion_effect.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/timewarp.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/timeline.h"

#include <QCheckBox>
#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// The Motion Effect Editor and its control graphs (motion_effect_editor.h). The Timewarp's model is
// lumenis/model/timewarp.h; its keyframes are those of lumenis/model/effect_keyframes.h.
namespace lumenis::ui {

namespace {

// The graphs' colours: "Green line ... Indicates output speed" (p.188), "Position indicator (blue)",
// "Active keyframes are pink" (p.187); ours for the rest.
const QColor kLine(0xff3cc85a);
const QColor kIndicator(0xff3c78dc);
const QColor kActiveKey(0xffff7fbf);
const QColor kKey(0xffd0d0d0);

QString categoryFor(MotionGraph::Kind kind) {
    return QLatin1String(kind == MotionGraph::Kind::Speed ? model::kSpeedCategory : model::kPositionCategory);
}
QString parameterFor(MotionGraph::Kind kind) {
    return QLatin1String(kind == MotionGraph::Kind::Speed ? model::kSpeedParameter : model::kPositionParameter);
}

} // namespace

// ---------------------------------------------------------------- MotionGraph

MotionGraph::MotionGraph(Kind kind, QWidget* parent) : QWidget(parent), kind_(kind) {
    setObjectName(kind == Kind::Speed ? QStringLiteral("speedGraph") : QStringLiteral("positionGraph"));
    setMinimumHeight(90);
}

void MotionGraph::setData(const std::optional<model::Effect>& effect, std::int64_t length, std::int64_t position, bool active,
                          int selected) {
    effect_ = effect;
    length_ = std::max<std::int64_t>(length, 1);
    position_ = position;
    active_ = active;
    selected_ = selected;
    values_.clear();
    keys_.clear();
    if (effect_) {
        // The Position graph is the mapping itself; the Speed graph its keyframed speed when active, else
        // the speed the Position graph makes (the step from each frame to the next).
        const bool speedActive = !model::timewarpOf(*effect_).positionGraph;
        const model::EffectSpan span(length_);
        for (std::int64_t o = 0; o < length_; ++o) {
            if (kind_ == Kind::Position) {
                values_ << model::timewarpPosition(*effect_, o, length_);
            } else if (speedActive) {
                const auto v = model::numberAt(*effect_, categoryFor(kind_), parameterFor(kind_), span.timeOf(static_cast<double>(o)), span);
                values_ << (v ? *v : 100.0);
            } else {
                values_ << 100.0 * (model::timewarpPosition(*effect_, o + 1, length_) - model::timewarpPosition(*effect_, o, length_));
            }
        }
        if (active_) {
            const model::ParameterKeyframes keys = model::keyframesOf(*effect_, categoryFor(kind_), parameterFor(kind_));
            for (const model::EffectKeyframe& k : keys.keys) {
                keys_ << QPointF(span.frameOf(model::keyframeTime(keys, k, span)), k.value.toDouble());
            }
        }
    }
    // The scale: the values and the keyframes with a margin, and at least 0-100% or the segment's frames.
    low_ = 0.0;
    high_ = kind_ == Kind::Speed ? 100.0 : static_cast<double>(length_ - 1);
    for (double v : values_) {
        low_ = std::min(low_, v);
        high_ = std::max(high_, v);
    }
    for (const QPointF& k : keys_) {
        low_ = std::min(low_, k.y());
        high_ = std::max(high_, k.y());
    }
    const double margin = std::max((high_ - low_) * 0.1, 1.0);
    low_ -= margin;
    high_ += margin;
    update();
}

QRectF MotionGraph::plot() const {
    return QRectF(rect()).adjusted(6, 6, -6, -6);
}

QPointF MotionGraph::pointFor(double frame, double value) const {
    const QRectF r = plot();
    const double x = r.left() + (length_ > 1 ? frame / static_cast<double>(length_ - 1) : 0.0) * r.width();
    const double y = r.bottom() - (value - low_) / (high_ - low_) * r.height();
    return {x, y};
}

double MotionGraph::valueAt(double y) const {
    const QRectF r = plot();
    return low_ + (r.bottom() - y) / r.height() * (high_ - low_);
}

double MotionGraph::frameAt(double x) const {
    const QRectF r = plot();
    return std::clamp((x - r.left()) / r.width(), 0.0, 1.0) * static_cast<double>(length_ - 1);
}

int MotionGraph::keyframeNear(const QPointF& at) const {
    for (int i = 0; i < keys_.size(); ++i) {
        if (QLineF(pointFor(keys_.at(i).x(), keys_.at(i).y()), at).length() <= 6.0) return i;
    }
    return -1;
}

void MotionGraph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = plot();
    // "The darker gray area of a graph represents the valid working area" (p.180).
    p.fillRect(rect(), palette().color(QPalette::Base).lighter(110));
    p.fillRect(r, palette().color(QPalette::Base));
    // The zero line, and 100% on the Speed graph.
    p.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DotLine));
    p.drawLine(pointFor(0, 0), pointFor(static_cast<double>(length_ - 1), 0));
    if (kind_ == Kind::Speed) p.drawLine(pointFor(0, 100), pointFor(static_cast<double>(length_ - 1), 100));
    if (!values_.isEmpty()) {
        QPainterPath path;
        path.moveTo(pointFor(0, values_.first()));
        for (int i = 1; i < values_.size(); ++i) path.lineTo(pointFor(i, values_.at(i)));
        // The inactive graph's line is fainter.
        QColor line = kLine;
        if (!active_) line.setAlpha(120);
        p.setPen(QPen(line, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    if (position_ >= 0 && position_ < length_) {
        p.setPen(QPen(kIndicator, 1));
        p.drawLine(QPointF(pointFor(static_cast<double>(position_), 0).x(), r.top()),
                   QPointF(pointFor(static_cast<double>(position_), 0).x(), r.bottom()));
    }
    for (int i = 0; i < keys_.size(); ++i) {
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(i == selected_ ? kActiveKey : kKey);
        p.drawEllipse(pointFor(keys_.at(i).x(), keys_.at(i).y()), 4, 4);
    }
}

void MotionGraph::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int key = active_ ? keyframeNear(event->position()) : -1;
    if (key >= 0) {
        selected_ = key;
        dragging_ = key;
        dragTime_ = event->modifiers().testFlag(Qt::AltModifier);
        emit keyframeSelected(key);
        update();
        return;
    }
    emit positionPicked(std::llround(frameAt(event->position().x())));
}

void MotionGraph::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ < 0) return;
    const double value = valueAt(event->position().y());
    std::optional<double> time;
    if (dragTime_) time = model::EffectSpan(length_).timeOf(frameAt(event->position().x()));
    // The point follows the mouse until the release keeps it.
    keys_[dragging_] = QPointF(dragTime_ ? frameAt(event->position().x()) : keys_.at(dragging_).x(), value);
    update();
    emit keyframeDragged(dragging_, time, value, false);
}

void MotionGraph::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging_ < 0) return;
    const int index = dragging_;
    dragging_ = -1;
    std::optional<double> time;
    if (dragTime_) time = model::EffectSpan(length_).timeOf(frameAt(event->position().x()));
    emit keyframeDragged(index, time, valueAt(event->position().y()), true);
}

// ---------------------------------------------------------------- MotionEffectEditor

MotionEffectEditor::MotionEffectEditor(Timeline& timeline, QWidget* parent) : QWidget(parent), timeline_(timeline) {
    setObjectName(QStringLiteral("motionEffectEditor"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // The toolbar that is always there (p.184).
    auto* toolbar = new QHBoxLayout;
    auto button = [this, toolbar](const QString& text, const QString& name, auto slot) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setObjectName(name);
        b->setToolTip(text);
        connect(b, &QToolButton::clicked, this, slot);
        toolbar->addWidget(b);
        return b;
    };
    button(tr("Render Effect"), QStringLiteral("renderEffect"), [this] { emit renderRequested(); });
    button(tr("Remove Effect"), QStringLiteral("removeEffect"), [this] { emit removeRequested(); });
    button(tr("Match Frame"), QStringLiteral("matchFrame"), [this] { emit matchFrameRequested(); });
    button(tr("Play Loop"), QStringLiteral("playLoop"), [this] {
        if (target_) emit playLoopRequested(target_->start, target_->start + length_ - 1);
    });
    button(tr("Play"), QStringLiteral("play"), [this] { emit playRequested(); });
    button(tr("Trim Mode"), QStringLiteral("trimMode"), [this] { emit trimRequested(); });
    toolbar->addStretch();
    layout->addLayout(toolbar);

    blank_ = new QLabel(tr("No motion effect at the position."), this);
    blank_->setObjectName(QStringLiteral("blank"));
    layout->addWidget(blank_);

    body_ = new QWidget(this);
    auto* body = new QVBoxLayout(body_);
    body->setContentsMargins(0, 0, 0, 0);
    auto* top = new QHBoxLayout;
    // "Speed Graph display button ... Position Graph display button" (p.182-183): open or close them.
    speedButton_ = new QToolButton(body_);
    speedButton_->setObjectName(QStringLiteral("speedGraphDisplay"));
    speedButton_->setText(tr("Speed Graph"));
    speedButton_->setCheckable(true);
    speedButton_->setChecked(true);
    positionButton_ = new QToolButton(body_);
    positionButton_->setObjectName(QStringLiteral("positionGraphDisplay"));
    positionButton_->setText(tr("Position Graph"));
    positionButton_->setCheckable(true);
    promote_ = new QPushButton(tr("Promote"), body_);
    promote_->setObjectName(QStringLiteral("promote"));
    connect(promote_, &QPushButton::clicked, this, &MotionEffectEditor::promote);
    // The Timewarp Effect icon (p.182): ours, the effect icon, dragged to a bin.
    timewarpIcon_ = new QLabel(body_);
    timewarpIcon_->setObjectName(QStringLiteral("timewarpIcon"));
    timewarpIcon_->setPixmap(effectIcon(false, false).pixmap(26, 18));
    timewarpIcon_->setToolTip(tr("Drag to a bin to save a Timewarp Effect template"));
    timewarpIcon_->installEventFilter(this);
    top->addWidget(timewarpIcon_);
    top->addWidget(speedButton_);
    top->addWidget(positionButton_);
    top->addWidget(promote_);
    top->addStretch();
    body->addLayout(top);

    auto* form = new QFormLayout;
    editGraph_ = new QComboBox(body_);
    editGraph_->setObjectName(QStringLiteral("editGraph"));
    editGraph_->addItems({tr("Speed"), tr("Position")});
    form->addRow(tr("Edit Graph:"), editGraph_);
    type_ = new QComboBox(body_);
    type_->setObjectName(QStringLiteral("type"));
    const model::TimewarpType types[] = {model::TimewarpType::DuplicatedField,   model::TimewarpType::BothFields,
                                         model::TimewarpType::InterpolatedField, model::TimewarpType::VtrStyle,
                                         model::TimewarpType::BlendedInterpolated, model::TimewarpType::BlendedVtr,
                                         model::TimewarpType::FluidMotionDraft,  model::TimewarpType::FluidMotion};
    const QStringList typeNames{tr("Duplicated Field"), tr("Both Fields"), tr("Interpolated Field"), tr("VTR-Style"),
                                tr("Blended Interpolated"), tr("Blended VTR"), tr("FluidMotion Draft"), tr("FluidMotion")};
    for (int i = 0; i < typeNames.size(); ++i) type_->addItem(typeNames.at(i), model::timewarpTypeName(types[i]));
    form->addRow(tr("Type:"), type_);
    ignore_ = new QCheckBox(tr("Ignore Render Setting"), body_);
    ignore_->setObjectName(QStringLiteral("ignoreRenderSetting"));
    form->addRow(ignore_);
    // "Source menu ... Output menu" (p.183-184) and the format conversions (p.205): filled by syncControls()
    // from the project's format.
    source_ = new QComboBox(body_);
    source_->setObjectName(QStringLiteral("sourceFormat"));
    outputFormat_ = new QComboBox(body_);
    outputFormat_->setObjectName(QStringLiteral("outputFormat"));
    form->addRow(tr("Source:"), source_);
    // "If you select Film with Pulldown as the source format, additional options display that lets you either
    // manually or automatically set the pulldown pattern ... Progressive Repeated ... the repeated frame
    // count" (p.205): Auto finds it in the clip; else the phase menu (the 2:3 cycle frame the clip starts on)
    // or the count.
    auto* pattern = new QHBoxLayout;
    autoPattern_ = new QCheckBox(tr("Auto"), body_);
    autoPattern_->setObjectName(QStringLiteral("autoPattern"));
    phase_ = new QComboBox(body_);
    phase_->setObjectName(QStringLiteral("pulldownPhase"));
    phase_->addItems({QStringLiteral("AA"), QStringLiteral("BB"), QStringLiteral("BC"), QStringLiteral("CD"), QStringLiteral("DD")});
    repeat_ = new QSpinBox(body_);
    repeat_->setObjectName(QStringLiteral("repeatCount"));
    repeat_->setRange(1, 30);
    pattern->addWidget(autoPattern_);
    pattern->addWidget(phase_);
    pattern->addWidget(repeat_);
    form->addRow(tr("Pattern:"), pattern);
    form->addRow(tr("Output:"), outputFormat_);
    adaptive_ = new QCheckBox(tr("Adaptive Deinterlace Source"), body_);
    adaptive_->setObjectName(QStringLiteral("adaptiveDeinterlace"));
    form->addRow(adaptive_);
    auto* anchorRow = new QHBoxLayout;
    anchor_ = new QDoubleSpinBox(body_);
    anchor_->setObjectName(QStringLiteral("anchor"));
    anchor_->setDecimals(0);
    anchor_->setRange(-1e7, 1e7);
    anchorButton_ = new QToolButton(body_);
    anchorButton_->setObjectName(QStringLiteral("sourceAnchorFrame"));
    anchorButton_->setText(tr("Anchor"));
    anchorButton_->setToolTip(tr("Source Anchor Frame"));
    connect(anchorButton_, &QToolButton::clicked, this, &MotionEffectEditor::anchorAtActiveKeyframe);
    anchorRow->addWidget(anchor_);
    anchorRow->addWidget(anchorButton_);
    form->addRow(tr("Anchor:"), anchorRow);
    strobe_ = new QSpinBox(body_);
    strobe_->setObjectName(QStringLiteral("strobe"));
    strobe_->setRange(1, 1000);
    form->addRow(tr("Strobe (frames):"), strobe_);
    speedText_ = new QLabel(body_);
    speedText_->setObjectName(QStringLiteral("speed"));
    form->addRow(tr("Speed:"), speedText_);
    duration_ = new QLabel(body_);
    duration_->setObjectName(QStringLiteral("duration"));
    form->addRow(tr("Duration:"), duration_);
    body->addLayout(form);

    speedGraph_ = new MotionGraph(MotionGraph::Kind::Speed, body_);
    positionGraph_ = new MotionGraph(MotionGraph::Kind::Position, body_);
    positionGraph_->setVisible(false);
    body->addWidget(speedGraph_, 1);
    body->addWidget(positionGraph_, 1);
    connect(speedButton_, &QToolButton::toggled, speedGraph_, &QWidget::setVisible);
    connect(positionButton_, &QToolButton::toggled, positionGraph_, &QWidget::setVisible);

    // Under the active graph (p.184-185): the Fast menu, Go to Previous/Next Event, Add Keyframe, and
    // the active keyframe's value and output position (p.188-189).
    graphTools_ = new QWidget(body_);
    auto* tools = new QHBoxLayout(graphTools_);
    tools->setContentsMargins(0, 0, 0, 0);
    auto* fast = new QToolButton(graphTools_);
    fast->setObjectName(QStringLiteral("graphFastMenu"));
    fast->setText(tr("Fast"));
    fast->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(fast);
    menu->addAction(tr("Shelf"), this, [this] { setInterpolation(model::Interpolation::Shelf); });
    menu->addAction(tr("Linear"), this, [this] { setInterpolation(model::Interpolation::Linear); });
    menu->addAction(tr("Spline"), this, [this] { setInterpolation(model::Interpolation::Spline); });
    menu->addAction(tr("Bézier"), this, [this] { setInterpolation(model::Interpolation::Bezier); });
    fast->setMenu(menu);
    auto* previous = new QToolButton(graphTools_);
    previous->setObjectName(QStringLiteral("previousEvent"));
    previous->setText(QStringLiteral("|<"));
    previous->setToolTip(tr("Go to Previous Event"));
    connect(previous, &QToolButton::clicked, this, [this] { goToEvent(false); });
    auto* next = new QToolButton(graphTools_);
    next->setObjectName(QStringLiteral("nextEvent"));
    next->setText(QStringLiteral(">|"));
    next->setToolTip(tr("Go to Next Event"));
    connect(next, &QToolButton::clicked, this, [this] { goToEvent(true); });
    auto* add = new QToolButton(graphTools_);
    add->setObjectName(QStringLiteral("addKeyframe"));
    add->setText(tr("Add Keyframe"));
    connect(add, &QToolButton::clicked, this,
            [this] { addKeyframe(QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier)); });
    value_ = new QDoubleSpinBox(graphTools_);
    value_->setObjectName(QStringLiteral("keyframeValue"));
    value_->setRange(-1e7, 1e7);
    value_->setDecimals(1);
    output_ = new QSpinBox(graphTools_);
    output_->setObjectName(QStringLiteral("outputPosition"));
    output_->setRange(-1000000, 1000000);
    tools->addWidget(fast);
    tools->addWidget(previous);
    tools->addWidget(next);
    tools->addWidget(add);
    tools->addWidget(value_);
    tools->addWidget(output_);
    tools->addStretch();
    body->addWidget(graphTools_);
    layout->addWidget(body_, 1);

    // Changes, each an undoable edit of the sequence.
    connect(editGraph_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (syncing_) return;
        editTimewarp(tr("Edit Graph"), [index](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.positionGraph = index == 1;
            model::setTimewarp(e, t);
        });
    });
    connect(type_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncing_) return;
        const QString name = type_->currentData().toString();
        if (shownKind_ == Shown::Adapter) {
            // "To change the appearance of a mixed rate clip, you can select a different rendering option
            // from the Type list" (Editing Guide p.471).
            if (!target_) return;
            const Target target = *target_;
            timeline_.changeRecordSequence(tr("Type"), [&](model::Sequence& seq) {
                model::Track* t = seq.track(target.track);
                if (!t || target.index >= t->segments.size() || !t->segments[target.index].adapter) return false;
                std::optional<model::MotionAdapter>& adapter = t->segments[target.index].adapter;
                if (adapter->type == name) return false;
                adapter->type = name;
                return true;
            });
            return;
        }
        editTimewarp(tr("Type"), [name](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.type = model::timewarpTypeFromName(name);
            model::setTimewarp(e, t);
        });
    });
    connect(source_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!syncing_) setSource(source_->currentData().toString());
    });
    connect(outputFormat_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncing_) return;
        const auto f = model::timewarpFormatFromName(outputFormat_->currentData().toString());
        editTimewarp(tr("Output"), [f](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.output = f;
            model::setTimewarp(e, t);
        });
    });
    connect(adaptive_, &QCheckBox::toggled, this, [this](bool on) {
        if (syncing_) return;
        if (shownKind_ != Shown::Timewarp) return;
        editTimewarp(tr("Adaptive Deinterlace Source"), [on](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.adaptiveDeinterlace = on;
            model::setTimewarp(e, t);
        });
    });
    connect(autoPattern_, &QCheckBox::toggled, this, [this](bool on) {
        if (syncing_) return;
        editTimewarp(tr("Pattern"), [on](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.autoPattern = on;
            model::setTimewarp(e, t);
        });
        // Auto finds the pattern again.
        if (on && effect_) setSource(model::timewarpFormatName(model::sourceFormat(model::timewarpOf(*effect_), interlaced_)));
    });
    connect(phase_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (syncing_) return;
        editTimewarp(tr("Pattern"), [index](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.pulldownPhase = index;
            t.autoPattern = false;
            model::setTimewarp(e, t);
        });
    });
    connect(repeat_, &QSpinBox::editingFinished, this, [this] {
        if (syncing_) return;
        const int n = repeat_->value();
        editTimewarp(tr("Pattern"), [n](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.repeatCount = n;
            t.autoPattern = false;
            model::setTimewarp(e, t);
        });
    });
    connect(ignore_, &QCheckBox::toggled, this, [this](bool on) {
        if (syncing_) return;
        editTimewarp(tr("Ignore Render Setting"), [on](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.ignoreRenderSetting = on;
            model::setTimewarp(e, t);
        });
    });
    connect(anchor_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (syncing_ || !effect_) return;
        // "Enter the source timecode of the frame ... in the Anchor text box" (p.181): ours, the source
        // clip's frame number; kept from the segment's source IN.
        const model::Segment* s = targetSegment(timeline_.shown());
        if (!s) return;
        const double frame = anchor_->value() - static_cast<double>(s->sourceIn);
        editTimewarp(tr("Anchor"), [frame](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.anchor = frame;
            model::setTimewarp(e, t);
        });
    });
    connect(strobe_, &QSpinBox::editingFinished, this, [this] {
        if (syncing_) return;
        const int n = strobe_->value();
        editTimewarp(tr("Strobe"), [n](model::Effect& e, std::int64_t) {
            model::Timewarp t = model::timewarpOf(e);
            t.strobe = n;
            model::setTimewarp(e, t);
        });
    });
    auto picked = [this](std::int64_t offset) {
        if (target_) timeline_.composer().moveTo(MonitorId::Record, target_->start + offset);
    };
    connect(speedGraph_, &MotionGraph::positionPicked, this, picked);
    connect(positionGraph_, &MotionGraph::positionPicked, this, picked);
    auto selectedFrom = [this](int index) { selectKeyframe(index); };
    connect(speedGraph_, &MotionGraph::keyframeSelected, this, selectedFrom);
    connect(positionGraph_, &MotionGraph::keyframeSelected, this, selectedFrom);
    auto dragged = [this](int index, std::optional<double> time, double value, bool finished) {
        if (!finished) return;
        const QString category = activeCategory();
        const QString parameter = activeParameter();
        editTimewarp(tr("Move Keyframe"), [&](model::Effect& e, std::int64_t length) {
            model::ParameterKeyframes keys = model::keyframesOf(e, category, parameter);
            if (model::moveKeyframe(keys, index, time, value, model::EffectSpan(length))) model::setKeyframes(e, category, parameter, keys);
        });
    };
    connect(speedGraph_, &MotionGraph::keyframeDragged, this, dragged);
    connect(positionGraph_, &MotionGraph::keyframeDragged, this, dragged);
    // "You change the percent speed ... by typing a number in the Percent Speed text box" and the output
    // position "by typing a timecode in the Output Position text box" (p.188-189; ours, frames).
    connect(value_, &QDoubleSpinBox::editingFinished, this, [this] {
        if (syncing_ || selected_ < 0) return;
        const int index = selected_;
        const double v = value_->value();
        const QString category = activeCategory();
        const QString parameter = activeParameter();
        editTimewarp(tr("Keyframe Value"), [&](model::Effect& e, std::int64_t length) {
            model::ParameterKeyframes keys = model::keyframesOf(e, category, parameter);
            if (model::moveKeyframe(keys, index, std::nullopt, v, model::EffectSpan(length))) model::setKeyframes(e, category, parameter, keys);
        });
    });
    connect(output_, &QSpinBox::editingFinished, this, [this] {
        if (syncing_ || selected_ < 0) return;
        const int index = selected_;
        const double frame = output_->value();
        const QString category = activeCategory();
        const QString parameter = activeParameter();
        editTimewarp(tr("Output Position"), [&](model::Effect& e, std::int64_t length) {
            model::ParameterKeyframes keys = model::keyframesOf(e, category, parameter);
            const model::EffectSpan span(length);
            if (model::moveKeyframe(keys, index, span.timeOf(frame), std::nullopt, span)) model::setKeyframes(e, category, parameter, keys);
        });
    });
    connect(&timeline_, &Timeline::changed, this, &MotionEffectEditor::refresh);
    connect(&timeline_.composer(), &Composer::changed, this, &MotionEffectEditor::refresh);
    refresh();
}

std::optional<MotionEffectEditor::Target> MotionEffectEditor::findTarget(const model::Sequence& seq, std::int64_t position) const {
    // The selected video tracks, top down; all of them when none is selected.
    QList<const model::Track*> video;
    const QStringList selected = timeline_.patch().selectedRecordTracks(seq);
    for (const model::Track& t : seq.tracks()) {
        if (t.kind == model::TrackKind::Video && (selected.isEmpty() || selected.contains(t.name()))) video << &t;
    }
    if (video.isEmpty()) {
        for (const model::Track& t : seq.tracks()) {
            if (t.kind == model::TrackKind::Video) video << &t;
        }
    }
    std::sort(video.begin(), video.end(), [](const model::Track* a, const model::Track* b) { return a->number > b->number; });
    for (const model::Track* t : video) {
        std::int64_t start = 0;
        const int i = t->segmentAt(position, &start);
        if (i < 0 || t->segments.at(i).isFiller()) continue;
        return Target{t->name(), i, start};
    }
    return std::nullopt;
}

const model::Segment* MotionEffectEditor::targetSegment(const model::Sequence& seq) const {
    if (!target_) return nullptr;
    const model::Track* t = seq.track(target_->track);
    if (!t || target_->index < 0 || target_->index >= t->segments.size()) return nullptr;
    return &t->segments.at(target_->index);
}

void MotionEffectEditor::refresh() {
    const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
    const std::int64_t position = timeline_.composer().state(MonitorId::Record).position();
    const model::Sequence& seq = timeline_.shown();
    target_ = rec && rec->item.kind == model::BinItemKind::Sequence ? findTarget(seq, position) : std::nullopt;
    effect_.reset();
    shownKind_ = Shown::None;
    const model::Segment* s = targetSegment(seq);
    QString speedText;
    if (s) {
        length_ = std::max<std::int64_t>(s->length, 1);
        position_ = position - target_->start;
        if (s->effect && model::isTimewarp(*s->effect)) {
            effect_ = *s->effect;
            shownKind_ = Shown::Timewarp;
        } else if (s->effect && model::motionSpeed(*s->effect)) {
            shownKind_ = Shown::Traditional;
            speedText = QStringLiteral("%1%").arg(*model::motionSpeed(*s->effect) * 100.0, 0, 'g', 4);
        } else if (const auto clip = timeline_.composer().resolve(s->binId, s->clipId); clip && model::motionEffectOf(*clip)) {
            const auto motion = model::motionEffectOf(*clip);
            shownKind_ = Shown::Traditional;
            speedText = QStringLiteral("%1%").arg(motion->speed * 100.0, 0, 'g', 4);
            if (motion->strobe > 1) speedText += tr(", strobe %1").arg(motion->strobe);
        } else if (s->adapter && model::adapterShown(*s->adapter)) {
            // "You can check the values Media Composer has calculated, but you cannot change them" (p.471):
            // the speed, and the clip's rate and Field Motion (the Source).
            shownKind_ = Shown::Adapter;
            speedText = tr("%1%, %2 fps %3 source")
                            .arg(model::adapterSpeed(*s->adapter), 0, 'g', 4)
                            .arg(QString::fromStdString(frameRateText(s->adapter->clipRate)), s->adapter->fieldMotion);
        }
    }
    if (selected_ >= 0 && effect_ &&
        selected_ >= model::keyframesOf(*effect_, activeCategory(), activeParameter()).keys.size()) {
        selected_ = -1;
    }
    if (!effect_) selected_ = -1;
    speedText_->setText(speedText);
    timewarpIcon_->setEnabled(shownKind_ == Shown::Timewarp);
    syncControls();
}

QMimeData* MotionEffectEditor::timewarpTemplateMime() const {
    if (shownKind_ != Shown::Timewarp || !effect_) return nullptr;
    model::Segment s;
    s.length = length_;
    s.effect = effect_;
    const auto t = model::segmentTemplate(s, timeline_.composer().projectRate(), false);
    if (!t) return nullptr;
    auto* mime = new QMimeData;
    const QJsonObject o{{QStringLiteral("name"), model::effectTemplateName(*t)}, {QStringLiteral("template"), t->toJson()}};
    mime->setData(effectTemplateMime(), QJsonDocument(o).toJson(QJsonDocument::Compact));
    mime->setText(model::effectTemplateName(*t));
    return mime;
}

bool MotionEffectEditor::eventFilter(QObject* watched, QEvent* event) {
    if (watched == timewarpIcon_) {
        if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            iconPress_ = static_cast<QMouseEvent*>(event)->position().toPoint();
            return true;
        }
        if (event->type() == QEvent::MouseMove && !iconPress_.isNull()) {
            auto* m = static_cast<QMouseEvent*>(event);
            if ((m->position().toPoint() - iconPress_).manhattanLength() >= QApplication::startDragDistance()) {
                iconPress_ = {};
                if (QMimeData* mime = timewarpTemplateMime()) {
                    auto* drag = new QDrag(this);
                    drag->setMimeData(mime);
                    drag->setPixmap(timewarpIcon_->pixmap());
                    drag->exec(Qt::CopyAction);
                }
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) iconPress_ = {};
    }
    return QWidget::eventFilter(watched, event);
}

void MotionEffectEditor::setRenderUsing(const QString& type) {
    renderUsing_ = type.isEmpty() ? QStringLiteral("original") : type;
    syncControls();
}

QString MotionEffectEditor::activeCategory() const {
    return QLatin1String(effect_ && model::timewarpOf(*effect_).positionGraph ? model::kPositionCategory : model::kSpeedCategory);
}

QString MotionEffectEditor::activeParameter() const {
    return QLatin1String(effect_ && model::timewarpOf(*effect_).positionGraph ? model::kPositionParameter : model::kSpeedParameter);
}

void MotionEffectEditor::setProjectFormat(int nominalFps, bool interlaced, bool film) {
    nominalFps_ = nominalFps;
    interlaced_ = interlaced;
    film_ = film;
    syncControls();
}

void MotionEffectEditor::setSource(const QString& format) {
    const auto f = model::timewarpFormatFromName(format);
    if (!f || !target_ || shownKind_ != Shown::Timewarp) return;
    // The pattern found in the clip's first frames, from the segment's source IN.
    std::optional<int> phase;
    std::optional<int> repeat;
    const model::Segment* s = targetSegment(timeline_.shown());
    const bool autoPattern = effect_ && model::timewarpOf(*effect_).autoPattern;
    if (s && autoPattern && (*f == model::TimewarpFormat::Film || *f == model::TimewarpFormat::Repeated)) {
        const auto clip = timeline_.composer().resolveForSegment(*s);
        MediaFrames* frames = MediaFrames::instance();
        if (clip && frames) {
            const std::int64_t in = s->sourceIn;
            const media::FrameGetter frameAt = [&](std::int64_t n) { return frames->frame(*clip, in + n, QSize(160, 90)); };
            const bool topFirst = clip->link.value(QStringLiteral("fieldOrder")).toString() != QLatin1String("bottom");
            if (*f == model::TimewarpFormat::Film) phase = media::detectPulldownPhase(frameAt, s->length, topFirst);
            if (*f == model::TimewarpFormat::Repeated) repeat = media::detectRepeatCount(frameAt, s->length);
        }
    }
    const QList<model::TimewarpFormat> outputs = model::outputFormats(*f, nominalFps_, interlaced_);
    editTimewarp(tr("Source"), [&](model::Effect& e, std::int64_t) {
        model::Timewarp t = model::timewarpOf(e);
        t.source = *f;
        if (!outputs.contains(model::outputFormat(t, interlaced_))) t.output = outputs.value(0, model::TimewarpFormat::Progressive);
        if (phase) t.pulldownPhase = *phase;
        if (repeat) t.repeatCount = *repeat;
        model::setTimewarp(e, t);
    });
}

void MotionEffectEditor::hideFormats() {
    source_->setEnabled(false);
    outputFormat_->setEnabled(false);
    autoPattern_->setVisible(false);
    phase_->setVisible(false);
    repeat_->setVisible(false);
    adaptive_->setVisible(false);
}

void MotionEffectEditor::syncControls() {
    syncing_ = true;
    blank_->setVisible(shownKind_ == Shown::None);
    body_->setVisible(shownKind_ != Shown::None);
    const bool timewarp = shownKind_ == Shown::Timewarp;
    // "The Promote button appears in the Motion Effect Editor only when you have the position indicator
    // on a traditional motion effect" (p.177).
    promote_->setVisible(shownKind_ == Shown::Traditional || shownKind_ == Shown::Adapter);
    QWidget* const timewarpOnly[] = {editGraph_, anchor_, anchorButton_, graphTools_, speedButton_, positionButton_};
    for (QWidget* w : timewarpOnly) w->setEnabled(timewarp);
    speedGraph_->setVisible(timewarp && speedButton_->isChecked());
    positionGraph_->setVisible(timewarp && positionButton_->isChecked());
    duration_->setText(QString::fromStdString(
        Timecode(length_, TimecodeFormat::forRate(timeline_.composer().state(MonitorId::Record).current()
                                                      ? timeline_.composer().state(MonitorId::Record).current()->item.editRate
                                                      : Rational(25),
                                                  false))
            .toString()));
    if (timewarp) {
        const model::Timewarp t = model::timewarpOf(*effect_);
        editGraph_->setCurrentIndex(t.positionGraph ? 1 : 0);
        // "The Ignore Render Setting check box appears below the Type menu when you have made a rendering
        // selection other than Original Preference ... In this situation, the Type menu is dimmed" (p.183).
        const bool fixed = renderUsing_ != QLatin1String("original");
        ignore_->setVisible(fixed);
        ignore_->setChecked(t.ignoreRenderSetting);
        type_->setEnabled(!fixed || t.ignoreRenderSetting);
        const QString shownType = fixed && !t.ignoreRenderSetting ? renderUsing_ : model::timewarpTypeName(t.type);
        type_->setCurrentIndex(std::max(0, type_->findData(shownType)));
        const model::Segment* s = targetSegment(timeline_.shown());
        anchor_->setValue(t.anchor + (s ? static_cast<double>(s->sourceIn) : 0.0));
        // The formats (p.183, p.205), "not available in Film projects".
        const QList<model::TimewarpFormat> sources = model::sourceFormats(nominalFps_, interlaced_, film_);
        const model::TimewarpFormat src = model::sourceFormat(t, interlaced_);
        const model::TimewarpFormat out = model::outputFormat(t, interlaced_);
        source_->clear();
        for (const model::TimewarpFormat f : sources) source_->addItem(model::timewarpFormatText(f), model::timewarpFormatName(f));
        source_->setEnabled(!sources.isEmpty());
        source_->setCurrentIndex(std::max(0, source_->findData(model::timewarpFormatName(src))));
        outputFormat_->clear();
        for (const model::TimewarpFormat f : model::outputFormats(src, nominalFps_, interlaced_)) {
            outputFormat_->addItem(model::timewarpFormatText(f), model::timewarpFormatName(f));
        }
        outputFormat_->setEnabled(!sources.isEmpty() && outputFormat_->count() > 1);
        outputFormat_->setCurrentIndex(std::max(0, outputFormat_->findData(model::timewarpFormatName(out))));
        const bool film = src == model::TimewarpFormat::Film;
        const bool repeated = src == model::TimewarpFormat::Repeated;
        autoPattern_->setVisible(film || repeated);
        autoPattern_->setChecked(t.autoPattern);
        phase_->setVisible(film);
        phase_->setCurrentIndex(t.pulldownPhase);
        phase_->setEnabled(!t.autoPattern);
        repeat_->setVisible(repeated);
        repeat_->setValue(t.repeatCount);
        repeat_->setEnabled(!t.autoPattern);
        const bool adaptive = model::adaptiveAvailable(src, out, t.type);
        adaptive_->setVisible(adaptive);
        adaptive_->setChecked(t.adaptiveDeinterlace);
        strobe_->setValue(t.strobe);
        strobe_->setEnabled(true);
        speedGraph_->setData(effect_, length_, position_, !t.positionGraph, t.positionGraph ? -1 : selected_);
        positionGraph_->setData(effect_, length_, position_, t.positionGraph, t.positionGraph ? selected_ : -1);
        const model::ParameterKeyframes keys = model::keyframesOf(*effect_, activeCategory(), activeParameter());
        const bool key = selected_ >= 0 && selected_ < keys.keys.size();
        value_->setEnabled(key);
        output_->setEnabled(key);
        if (key) {
            const model::EffectSpan span(length_);
            value_->setValue(keys.keys.at(selected_).value.toDouble());
            output_->setValue(static_cast<int>(std::llround(span.frameOf(model::keyframeTime(keys, keys.keys.at(selected_), span)))));
        }
    } else if (shownKind_ == Shown::Adapter) {
        ignore_->setVisible(false);
        hideFormats();
        const model::Segment* s = targetSegment(timeline_.shown());
        type_->setEnabled(s != nullptr);
        if (s && s->adapter) type_->setCurrentIndex(std::max(0, type_->findData(s->adapter->type)));
        strobe_->setEnabled(false);
    } else {
        ignore_->setVisible(false);
        type_->setEnabled(false);
        strobe_->setEnabled(false);
        hideFormats();
    }
    syncing_ = false;
}

void MotionEffectEditor::editTimewarp(const QString& description, const std::function<void(model::Effect&, std::int64_t)>& change) {
    if (!target_ || shownKind_ != Shown::Timewarp) return;
    const Target target = *target_;
    timeline_.changeRecordSequence(description, [&](model::Sequence& seq) {
        model::Track* t = seq.track(target.track);
        if (!t || target.index >= t->segments.size()) return false;
        model::Segment& s = t->segments[target.index];
        if (!s.effect || !model::isTimewarp(*s.effect)) return false;
        const model::Effect before = *s.effect;
        change(*s.effect, std::max<std::int64_t>(s.length, 1));
        return !(*s.effect == before);
    });
}

void MotionEffectEditor::addKeyframe(bool remove) {
    if (!effect_) return;
    const QString category = activeCategory();
    const QString parameter = activeParameter();
    const std::int64_t at = std::clamp<std::int64_t>(position_, 0, length_ - 1);
    if (remove) {
        editTimewarp(tr("Remove Keyframe"), [&](model::Effect& e, std::int64_t length) {
            const model::EffectSpan span(length);
            model::removeKeyframe(e, category, parameter, span.timeOf(static_cast<double>(at)), span);
        });
        selected_ = -1;
        return;
    }
    // A new keyframe keeps what the graph shows there: the speed, or the source position.
    const model::EffectSpan here(length_);
    double value = 100.0;
    if (model::timewarpOf(*effect_).positionGraph) {
        value = model::timewarpPosition(*effect_, at, length_);
    } else if (const auto v = model::numberAt(*effect_, category, parameter, here.timeOf(static_cast<double>(at)), here)) {
        value = *v;
    }
    editTimewarp(tr("Add Keyframe"), [&](model::Effect& e, std::int64_t length) {
        const model::EffectSpan span(length);
        model::setKeyframe(e, category, parameter, span.timeOf(static_cast<double>(at)), span, value);
    });
    if (effect_) {
        const model::EffectSpan span(length_);
        selected_ = model::keyframeAt(model::keyframesOf(*effect_, category, parameter), span.timeOf(static_cast<double>(at)), span);
        syncControls();
    }
}

void MotionEffectEditor::goToEvent(bool next) {
    if (!effect_ || !target_) return;
    const model::ParameterKeyframes keys = model::keyframesOf(*effect_, activeCategory(), activeParameter());
    const model::EffectSpan span(length_);
    int best = -1;
    double bestFrame = 0.0;
    for (int i = 0; i < keys.keys.size(); ++i) {
        const double frame = std::round(span.frameOf(model::keyframeTime(keys, keys.keys.at(i), span)));
        const bool after = next ? frame > static_cast<double>(position_) : frame < static_cast<double>(position_);
        if (!after) continue;
        if (best < 0 || (next ? frame < bestFrame : frame > bestFrame)) {
            best = i;
            bestFrame = frame;
        }
    }
    // "If there is no keyframe to the left, nothing happens" (p.185).
    if (best < 0) return;
    selected_ = best;
    timeline_.composer().moveTo(MonitorId::Record, target_->start + static_cast<std::int64_t>(bestFrame));
}

void MotionEffectEditor::setInterpolation(model::Interpolation interpolation) {
    const QString category = activeCategory();
    const QString parameter = activeParameter();
    editTimewarp(tr("Interpolation"), [&](model::Effect& e, std::int64_t) {
        model::ParameterKeyframes keys = model::keyframesOf(e, category, parameter);
        keys.interpolation = interpolation;
        model::setKeyframes(e, category, parameter, keys);
    });
}

void MotionEffectEditor::selectKeyframe(int index) {
    selected_ = index;
    syncControls();
}

void MotionEffectEditor::anchorAtActiveKeyframe() {
    if (!effect_ || selected_ < 0) return;
    const model::ParameterKeyframes keys = model::keyframesOf(*effect_, activeCategory(), activeParameter());
    if (selected_ >= keys.keys.size()) return;
    const model::EffectSpan span(length_);
    const double time = model::keyframeTime(keys, keys.keys.at(selected_), span);
    const std::int64_t frame = std::llround(span.frameOf(time));
    // Anchored where the effect shows now, so the picture does not jump.
    const double source = model::timewarpPosition(*effect_, frame, length_);
    editTimewarp(tr("Source Anchor Frame"), [&](model::Effect& e, std::int64_t length) {
        model::Timewarp t = model::timewarpOf(e);
        t.anchorAt = model::EffectSpan(length).timeOf(static_cast<double>(frame));
        t.anchor = source;
        model::setTimewarp(e, t);
    });
}

void MotionEffectEditor::promote() {
    if (!target_ || (shownKind_ != Shown::Traditional && shownKind_ != Shown::Adapter)) return;
    const Target target = *target_;
    Composer& composer = timeline_.composer();
    const bool adapter = shownKind_ == Shown::Adapter;
    timeline_.changeRecordSequence(tr("Promote to Timewarp"), [&](model::Sequence& seq) {
        model::Track* t = seq.track(target.track);
        if (!t || target.index >= t->segments.size()) return false;
        if (adapter) return model::promoteAdapter(t->segments[target.index]);
        return model::promoteToTimewarp(t->segments[target.index], [&composer](const QString& bin, const QString& clip) {
            return composer.resolve(bin, clip);
        });
    });
}

} // namespace lumenis::ui
