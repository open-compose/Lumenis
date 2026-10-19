#include "lumenis/ui/effect_editor.h"

#include "lumenis/ui/timeline.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

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
    bottom->addWidget(offButton(QStringLiteral("effectKeyframes"), tr("Show/Hide Keyframe Graph"), this));

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
    connect(&timeline_.composer(), &Composer::changed, this, &EffectEditor::refresh);
    refresh();
}

QList<model::TrimRoller> EffectEditor::transitionCuts() const {
    if (shown_ && shown_->target == model::EffectTarget::Transition) {
        return {model::TrimRoller{shown_->track, shown_->frame, model::TrimSide::Both}};
    }
    // Clicking a transition in Trim mode shows its Transition parameters
    // (p.63).
    if (!shown_ && timeline_.isTrimMode()) {
        QList<model::TrimRoller> cuts;
        for (const model::TrimRoller& r : timeline_.rollers()) {
            if (r.track.startsWith(QLatin1Char('V'))) cuts << r;
        }
        return cuts;
    }
    return {};
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
        if (!effect) ref.reset();
    }
    const bool transitionOnly = !ref && !transitionCuts().isEmpty();
    const QString name = effect ? effect->category + QLatin1Char('/') + effect->name : QString();
    if (ref != shown_ || name != shownName_ || transitionOnly != transitionOnly_) {
        shown_ = ref;
        shownName_ = name;
        transitionOnly_ = transitionOnly;
        rebuild(effect);
    } else if (effect) {
        syncValues(*effect);
    }
    syncTransition();
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
                } else {
                    // Buttons and other controls need the Effect Preview
                    // monitor or tools that come later.
                    auto* later = new QLabel(tr("(later)"), box);
                    later->setEnabled(false);
                    row->addWidget(later, 1);
                }
            }
            grid->addLayout(row);
        }
        connect(opener, &QToolButton::toggled, this, [opener, box](bool open) {
            opener->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
            box->setVisible(open);
        });
        groups_->insertWidget(at++, opener);
        groups_->insertWidget(at++, box);
        groupBodies_.insert(category, box);
        openers_.insert(category, opener);
    }
    syncValues(*effect);
}

// Parameters without a stored value keep what the control shows (its default after a rebuild).
void EffectEditor::syncValues(const model::Effect& effect) {
    syncing_ = true;
    for (auto it = controls_.cbegin(); it != controls_.cend(); ++it) {
        const QString category = it.key().section(QLatin1Char('/'), 0, 0);
        const QString name = it.key().section(QLatin1Char('/'), 1);
        const QJsonValue v = effect.parameters.value(category).toObject().value(name);
        if (v.isUndefined()) continue;
        QWidget* w = it.value();
        const QSignalBlocker block(w);
        if (auto* s = qobject_cast<QSlider*>(w)) {
            s->setValue(static_cast<int>(v.toDouble()));
            if (QLabel* l = valueLabels_.value(it.key())) l->setNum(s->value());
        } else if (auto* c = qobject_cast<QCheckBox*>(w)) {
            c->setChecked(v.toBool());
        } else if (auto* m = qobject_cast<QComboBox*>(w)) {
            m->setCurrentText(v.toString());
        } else if (auto* b = qobject_cast<QToolButton*>(w)) {
            const QColor color(v.toString());
            b->setProperty("color", color);
            b->setStyleSheet(QStringLiteral("background: %1;").arg(color.name()));
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
        const model::LoadedClip* rec = timeline_.composer().state(MonitorId::Record).current();
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
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

void EffectEditor::setValue(const QString& category, const QString& parameter, const QJsonValue& value) {
    if (syncing_ || !shown_) return;
    const model::EffectRef ref = *shown_;
    timeline_.changeEffect(ref, tr("Modify Effect"), [&](model::Effect& e, model::Transition*) {
        QJsonObject group = e.parameters.value(category).toObject();
        group.insert(parameter, value);
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
        const model::Sequence seq = model::Sequence::fromItem(timeline_.composer().state(MonitorId::Record).current()->item);
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
        const model::Sequence seq = model::Sequence::fromItem(timeline_.composer().state(MonitorId::Record).current()->item);
        if (const model::Transition* t = model::transitionFor(seq, *shown_)) start = std::min(t->before(), frames);
    }
    const bool done = timeline_.setTransitions(cuts, frames, shownAlignment_, start);
    if (!done) syncTransition();
    return done;
}

// The openers are the group layout's tool buttons; their text is the category.
QStringList EffectEditor::categories() const {
    QStringList out;
    for (int i = 0; i < groups_->count(); ++i) {
        if (auto* b = qobject_cast<QToolButton*>(groups_->itemAt(i)->widget())) out << b->text();
    }
    return out;
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
