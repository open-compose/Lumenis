#include "lumenis/ui/effect_editor.h"

#include "lumenis/ui/timeline.h"

#include "lumenis/media/compositor.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFile>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

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
                p.name = po.value(QStringLiteral("name")).toString();
                p.kind = po.value(QStringLiteral("kind")).toString();
                p.subcategory = po.value(QStringLiteral("subcategory")).toString();
                p.min = static_cast<int>(po.value(QStringLiteral("min")).toDouble(0));
                p.max = static_cast<int>(po.value(QStringLiteral("max")).toDouble(100));
                if (p.max < p.min) std::swap(p.min, p.max); // a range printed backwards
                p.defaultValue = po.value(QStringLiteral("default"));
                p.keyframeable = po.value(QStringLiteral("keyframeable")).toBool(true);
                for (const QJsonValue& opt : po.value(QStringLiteral("options")).toArray()) p.options << opt.toString();
                list << p;
            }
            c.insert(o.value(QStringLiteral("name")).toString(), list);
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
    const int v = p.defaultValue.isDouble() ? static_cast<int>(p.defaultValue.toDouble()) : 0;
    return std::clamp(v, p.min, p.max);
}

// A colour parameter's starting colour: the default if the file gives one ("#rrggbb" or a colour
// name), black otherwise (the parameter file gives none for most).
QColor defaultColor(const EffectParameter& p) {
    const QColor c(p.defaultValue.toString());
    return c.isValid() ? c : QColor(Qt::black);
}

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
    side->addWidget(offButton(QStringLiteral("effectTracking"), tr("Tracking"), this));
    side->addWidget(offButton(QStringLiteral("effectGrid"), tr("Grid"), this));
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
    bottom->addWidget(offButton(QStringLiteral("effectRender"), tr("Render Effect"), this));
    bottom->addWidget(offButton(QStringLiteral("effectOutline"), tr("Outline/Path"), this));
    bottom->addWidget(alignment_);
    bottom->addWidget(duration_);
    bottom->addStretch(1);
    bottom->addWidget(offButton(QStringLiteral("effectPromote3D"), tr("3D Promote"), this));
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
    titleRow->addWidget(titleIcon_);
    titleRow->addWidget(title_, 1);
    layout->addLayout(titleRow);
    layout->addWidget(blank_);
    layout->addLayout(middle, 1);
    layout->addLayout(bottom);

    connect(&timeline_, &Timeline::changed, this, &EffectEditor::refresh);
    connect(&timeline_, &Timeline::effectModeChanged, this, &EffectEditor::refresh);
    // The Effect Preview monitor's keyframes and buttons (FX Guide p.76-77, p.103-106): its Add Keyframe
    // button runs the default command without the menu; Alt+click deletes the selected keyframes.
    connect(&timeline_.composer(), &Composer::effectKeyframePressed, this, &EffectEditor::previewKeyframePressed);
    connect(&timeline_.composer(), &Composer::effectCommandRequested, this, [this](const QString& id, bool alt) {
        if (id == QLatin1String("effect.add_keyframe")) {
            if (alt) deleteKeyframes(mode_);
            else addKeyframes(mode_);
        } else if (id == QLatin1String("effect.previous_event") || id == QLatin1String("effect.next_event")) {
            goToEvent(id == QLatin1String("effect.next_event"));
        }
    });
    connect(&timeline_.composer(), &Composer::changed, this, &EffectEditor::refresh);
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
    if (ref && rec) {
        seq = model::Sequence::fromItem(rec->item);
        effect = model::effectFor(seq, *ref);
    }
    if (!effect) ref.reset(); // also with no sequence loaded

    // Where the effect lies and how its frames map to its time (model::EffectSpan).
    current_.reset();
    preview_.reset();
    if (effect) {
        current_ = *effect;
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
            }
        }
    }

    // From the new ref, not shown_, which still holds the effect shown before.
    const bool transitionOnly = !ref && !trimCuts().isEmpty();
    const QString name = effect ? effect->category + QLatin1Char('/') + effect->name : QString();
    if (ref != shown_ || name != shownName_ || transitionOnly != transitionOnly_) {
        shown_ = ref;
        shownName_ = name;
        transitionOnly_ = transitionOnly;
        rebuild(effect);
    } else if (effect) {
        // Keyframes an undo took away are no longer selected.
        selection_.removeIf([&](const KeyframeId& id) {
            return id.index >= model::keyframesOf(*effect, id.parameter.section(QLatin1Char('/'), 0, 0),
                                                  id.parameter.section(QLatin1Char('/'), 1))
                                    .keys.size();
        });
        syncValues(*effect);
    }
    syncTransition();
    // The position in the tracks moves while playing only with Update Position While Playing (p.132).
    if (updateWhilePlaying_ || !timeline_.composer().isPlaying()) updateTracks();
    else syncPreview();
}

bool EffectEditor::isBlank() const {
    return !shown_ && !transitionOnly_;
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
    openers_.clear();
    controls_.clear();
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
    openGraphs_.clear();
    selection_.clear();
    activeParameter_.clear();
    title_->setText(effect ? effect->name : QString());
    title_->setVisible(effect != nullptr);
    titleIcon_->setVisible(effect != nullptr);
    if (effect) {
        const EffectInfo* shownInfo = EffectCatalog::builtin().find(effect->category, effect->name);
        const bool transition = shown_ && shown_->target == model::EffectTarget::Transition;
        titleIcon_->setPixmap(effectIcon(transition, shownInfo ? shownInfo->realTime : true).pixmap(26, 18));
    }
    // If the position is not on an effect, or its track is not selected,
    // the Effect Editor is blank (p.84).
    blank_->setVisible(isBlank());
    blank_->setText(isBlank() ? tr("No effect at the position on the selected tracks.") : QString());
    if (!effect) return;

    const EffectInfo* info = EffectCatalog::builtin().find(effect->category, effect->name);
    const QStringList categories = info ? info->parameters : QStringList();
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
        auto* box = new QWidget(body_);
        auto* grid = new QVBoxLayout(box);
        grid->setContentsMargins(18, 0, 0, 4);
        grid->setSpacing(1);
        for (const EffectParameter& p : *params) {
            auto* row = new QHBoxLayout;
            const QString k = key(category, p.name);
            const bool keyframed = p.keyframeable && (p.kind == QLatin1String("slider") || p.kind == QLatin1String("color"));
            QLabel* nameLabel = nullptr;
            if (p.kind == QLatin1String("checkbox")) {
                auto* c = new QCheckBox(p.name, box);
                c->setObjectName(QStringLiteral("param:") + k);
                c->setChecked(p.defaultValue.toBool());
                connect(c, &QCheckBox::toggled, this, [this, category, name = p.name](bool on) {
                    setValue(category, name, on);
                });
                row->addWidget(c);
                controls_.insert(k, c);
            } else {
                auto* label = new QLabel(p.name, box);
                label->setMinimumWidth(64);
                row->addWidget(label);
                nameLabel = label;
                if (p.kind == QLatin1String("slider")) {
                    auto* s = new QSlider(Qt::Horizontal, box);
                    s->setObjectName(QStringLiteral("param:") + k);
                    s->setRange(p.min, p.max);
                    s->setValue(defaultInt(p));
                    s->setPageStep(10);
                    s->installEventFilter(this);
                    auto* value = new QLabel(QString::number(s->value()), box);
                    value->setMinimumWidth(36);
                    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    connect(s, &QSlider::valueChanged, this, [this, s, value, category, name = p.name](int v) {
                        value->setNum(v);
                        if (!s->isSliderDown()) setValue(category, name, v);
                    });
                    connect(s, &QSlider::sliderReleased, this,
                            [this, s, category, name = p.name] { setValue(category, name, s->value()); });
                    row->addWidget(s, 1);
                    row->addWidget(value);
                    controls_.insert(k, s);
                    valueLabels_.insert(k, value);
                } else if (p.kind == QLatin1String("color")) {
                    auto* swatch = new QToolButton(box);
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
                    row->addWidget(m, 1);
                    controls_.insert(k, m);
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
    applyLook();
    syncValues(*effect);
}

// Parameters without a stored value show their default (defaultValue()), or keep what the control shows
// when there is none.
void EffectEditor::syncValues(const model::Effect& effect) {
    syncing_ = true;
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
            s->setValue(static_cast<int>(std::lround(v.toDouble())));
            if (QLabel* l = valueLabels_.value(it.key())) l->setNum(s->value());
        } else if (auto* c = qobject_cast<QCheckBox*>(w)) {
            c->setChecked(v.toBool());
        } else if (auto* m = qobject_cast<QComboBox*>(w)) {
            m->setCurrentText(v.toString());
        } else if (auto* b = qobject_cast<QToolButton*>(w)) {
            const QColor color(v.toString());
            if (color.isValid()) setSwatch(b, color);
        }
    }
    syncing_ = false;
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
    if (!own.isUndefined()) return own;
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
    const QString k = key(category, parameter);
    setActiveParameter(k);
    // Fixed Aspect "Gangs the X and Y sliders together so that they move in unison when you drag either
    // one" (Scaling p.499; Feathering's Hor and Vert p.495, Blur's p.533).
    QString ganged;
    if (current_ && current_->parameters.value(category).toObject().value(QStringLiteral("Fixed Aspect")).toBool()) {
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
    const model::EffectRef ref = *shown_;
    timeline_.changeEffect(ref, tr("Modify Effect"), [&](model::Effect& e, model::Transition*) {
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

} // namespace lumenis::ui
