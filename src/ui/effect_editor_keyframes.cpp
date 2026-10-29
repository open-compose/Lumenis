#include "lumenis/ui/effect_editor.h"

#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <memory>

// The Effect Editor's advanced keyframes (effect_editor.h; FX Guide ch.4, p.94-131;
// docs/spec/effect-keyframes.md): the keyframe tracks and graphs (KeyframeTrack), the selection, the
// tracks' shortcut menus, the Add Keyframe button with its mode menus and Go to Previous/Next Event.
// The keyframes themselves and every change to them are model::ParameterKeyframes operations
// (src/model/effect_keyframes.*), stored through Timeline::changeEffect as undo steps. The rest of the
// editor, which builds these tracks beside its controls, is in effect_editor.cpp.

namespace lumenis::ui {

namespace {

// The track colours of p.95 ("effect track (gray), parameter group tracks (darker blue), parameter
// tracks (lighter blue)") and the graphs' "very dark grey" (p.97), darker when active.
const QColor kEffectTrack(0x5c, 0x5c, 0x5c);
const QColor kGroupTrack(0x2c, 0x46, 0x6c);
const QColor kParameterTrack(0x4a, 0x70, 0xa0);
const QColor kStaticTrack(0x3a, 0x3a, 0x3a);
const QColor kGraph(0x24, 0x24, 0x24);
const QColor kActiveGraph(0x16, 0x16, 0x16);
// Selected keyframes are pink (p.111); unselected gray, darker in group and effect tracks (p.102).
const QColor kSelected(0xff, 0x8c, 0xc8);
const QColor kKeyframe(0xc4, 0xc4, 0xc4);
const QColor kParentKeyframe(0x84, 0x84, 0x84);
const QColor kPosition(0x6c, 0xb4, 0xff);
const QColor kCurve(0xe8, 0xd0, 0x60);

// The margin either side of a track's time, so that the first and last keyframes can be clicked.
constexpr double kMargin = 6.0;

QString categoryOf(const QString& parameter) {
    return parameter.section(QLatin1Char('/'), 0, 0);
}
QString nameOf(const QString& parameter) {
    return parameter.section(QLatin1Char('/'), 1);
}

const char* addModeText(EffectEditor::KeyframeMode m) {
    switch (m) {
    case EffectEditor::KeyframeMode::ActiveParameter: return QT_TRANSLATE_NOOP("EffectEditor", "Add to Active Parameter");
    case EffectEditor::KeyframeMode::ActiveGroup: return QT_TRANSLATE_NOOP("EffectEditor", "Add to Active Group");
    case EffectEditor::KeyframeMode::OpenGroups: return QT_TRANSLATE_NOOP("EffectEditor", "Add to Open Groups");
    case EffectEditor::KeyframeMode::EnabledGroups: return QT_TRANSLATE_NOOP("EffectEditor", "Add to Enabled Groups");
    case EffectEditor::KeyframeMode::OpenGraphs: return QT_TRANSLATE_NOOP("EffectEditor", "Add to Open Graphs");
    case EffectEditor::KeyframeMode::AllParameters: return QT_TRANSLATE_NOOP("EffectEditor", "Add to All Parameters");
    }
    return "";
}

const char* deleteModeText(EffectEditor::KeyframeMode m) {
    switch (m) {
    case EffectEditor::KeyframeMode::ActiveParameter: return QT_TRANSLATE_NOOP("EffectEditor", "Delete From Active Parameter");
    case EffectEditor::KeyframeMode::ActiveGroup: return QT_TRANSLATE_NOOP("EffectEditor", "Delete From Active Group");
    case EffectEditor::KeyframeMode::OpenGroups: return QT_TRANSLATE_NOOP("EffectEditor", "Delete From Open Groups");
    case EffectEditor::KeyframeMode::EnabledGroups: return QT_TRANSLATE_NOOP("EffectEditor", "Delete From Enabled Groups");
    case EffectEditor::KeyframeMode::OpenGraphs: return QT_TRANSLATE_NOOP("EffectEditor", "Delete From Open Graphs");
    case EffectEditor::KeyframeMode::AllParameters: return QT_TRANSLATE_NOOP("EffectEditor", "Delete From All Parameters");
    }
    return "";
}

// The Effect Editor shortcut menu's names for the same commands (p.107-111, p.132).
const char* shortcutModeText(EffectEditor::KeyframeMode m) {
    switch (m) {
    case EffectEditor::KeyframeMode::ActiveParameter: return QT_TRANSLATE_NOOP("EffectEditor", "Add Keyframes to Active Parameter");
    case EffectEditor::KeyframeMode::ActiveGroup: return QT_TRANSLATE_NOOP("EffectEditor", "Add Keyframes to Active Group");
    case EffectEditor::KeyframeMode::OpenGroups: return QT_TRANSLATE_NOOP("EffectEditor", "Add Keyframes to Open Groups");
    case EffectEditor::KeyframeMode::EnabledGroups: return QT_TRANSLATE_NOOP("EffectEditor", "Add Keyframes to Enabled Groups");
    case EffectEditor::KeyframeMode::OpenGraphs: return QT_TRANSLATE_NOOP("EffectEditor", "Add Keyframes to Open Graphs");
    case EffectEditor::KeyframeMode::AllParameters: return QT_TRANSLATE_NOOP("EffectEditor", "Add Keyframes to All Parameters");
    }
    return "";
}

constexpr EffectEditor::KeyframeMode kModes[] = {
    EffectEditor::KeyframeMode::ActiveParameter, EffectEditor::KeyframeMode::ActiveGroup,
    EffectEditor::KeyframeMode::OpenGroups,      EffectEditor::KeyframeMode::EnabledGroups,
    EffectEditor::KeyframeMode::OpenGraphs,      EffectEditor::KeyframeMode::AllParameters,
};

} // namespace

// ================================================================ KeyframeTrack

KeyframeTrack::KeyframeTrack(EffectEditor& editor, Kind kind, const QString& which, QWidget* parent)
    : QWidget(parent), editor_(editor), kind_(kind), which_(which) {
    setObjectName(which.isEmpty() ? QStringLiteral("track:static") : QStringLiteral("track:") + which);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(kind == Kind::Graph ? 72 : 16);
    setMouseTracking(false);
}

double KeyframeTrack::xFor(double t) const {
    return kMargin + t * (width() - 2.0 * kMargin);
}

double KeyframeTrack::timeAt(double x) const {
    const double w = std::max(width() - 2.0 * kMargin, 1.0);
    return (x - kMargin) / w;
}

std::pair<double, double> KeyframeTrack::range() const {
    const QString p = which_.startsWith(QLatin1String("graph:")) ? which_.mid(6) : which_;
    const EffectParameter* info = editor_.parameterInfo(p);
    if (!info) return {0.0, 100.0};
    return {static_cast<double>(info->min), static_cast<double>(std::max(info->max, info->min + 1))};
}

double KeyframeTrack::yFor(double value) const {
    const auto [lo, hi] = range();
    const double h = std::max(height() - 8.0, 1.0);
    return 4.0 + (hi - value) / (hi - lo) * h;
}

double KeyframeTrack::valueAt(double y) const {
    const auto [lo, hi] = range();
    const double h = std::max(height() - 8.0, 1.0);
    return hi - (y - 4.0) / h * (hi - lo);
}

QList<KeyframeTrack::Column> KeyframeTrack::columns() const {
    QList<Column> out;
    const model::Effect* e = editor_.shownEffect();
    if (!e || which_.isEmpty()) return out;
    const model::EffectSpan& span = editor_.span_;
    const double half = span.halfFrame();
    for (const QString& p : editor_.parametersOf(which_)) {
        const model::ParameterKeyframes keys = model::keyframesOf(*e, categoryOf(p), nameOf(p));
        for (int i = 0; i < keys.keys.size(); ++i) {
            const double t = model::keyframeTime(keys, keys.keys.at(i), span);
            const EffectEditor::KeyframeId id{p, i};
            // In a group or the effect track, keyframes at one time share an indicator (p.102).
            auto same = std::find_if(out.begin(), out.end(), [&](const Column& c) { return std::abs(c.t - t) <= half; });
            if (kind_ != Kind::Parameter && kind_ != Kind::Graph && same != out.end()) {
                same->keys << id;
                continue;
            }
            Column c;
            c.t = t;
            c.value = keys.keys.at(i).value.toDouble();
            c.fixed = keys.fixed;
            c.keys << id;
            out << c;
        }
    }
    std::sort(out.begin(), out.end(), [](const Column& a, const Column& b) { return a.t < b.t; });
    return out;
}

int KeyframeTrack::columnAt(const QPointF& point, const QList<Column>& columns) const {
    int best = -1;
    double bestDistance = 6.0;
    for (int i = 0; i < columns.size(); ++i) {
        double d = std::abs(xFor(columns.at(i).t) - point.x());
        if (kind_ == Kind::Graph) d = std::hypot(d, yFor(columns.at(i).value) - point.y());
        if (d <= bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

void KeyframeTrack::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QString parameter = which_.startsWith(QLatin1String("graph:")) ? which_.mid(6) : which_;
    QColor background = kStaticTrack;
    switch (kind_) {
    case Kind::Effect: background = kEffectTrack; break;
    // A group with nothing keyframeable looks like the tracks of its parameters (p.96).
    case Kind::Group: background = editor_.parametersOf(which_).isEmpty() ? kStaticTrack : kGroupTrack; break;
    case Kind::Parameter: background = which_.isEmpty() ? kStaticTrack : kParameterTrack; break;
    case Kind::Graph: background = parameter == editor_.activeParameter() ? kActiveGraph : kGraph; break;
    }
    p.fillRect(rect(), background);
    const QList<Column> cols = columns();
    const QList<EffectEditor::KeyframeId>& selection = editor_.selectedKeyframes();
    auto selected = [&](const Column& c) {
        // An indicator is highlighted only when all its keyframes are (p.103).
        return std::all_of(c.keys.begin(), c.keys.end(), [&](const EffectEditor::KeyframeId& id) { return selection.contains(id); });
    };

    if (kind_ == Kind::Graph) {
        // The curve across the effect (p.97), sampled at every pixel.
        const model::Effect* e = editor_.shownEffect();
        if (e) {
            QPainterPath path;
            for (int x = 0; x <= width(); ++x) {
                const double v = editor_.valueAt(*e, parameter, timeAt(x)).toDouble();
                const QPointF pt(x, yFor(v));
                if (x == 0) path.moveTo(pt);
                else path.lineTo(pt);
            }
            p.setPen(QPen(kCurve, 1.5));
            p.drawPath(path);
        }
        for (const Column& c : cols) {
            const QPointF at(xFor(c.t), yFor(c.value));
            p.setPen(Qt::NoPen);
            p.setBrush(selected(c) ? kSelected : kKeyframe);
            p.drawEllipse(at, 4.0, 4.0);
        }
    } else {
        // Triangles hanging from the top (an elastic keyframe is "a simple triangle", p.124); a fixed
        // one also has a pin below it (ours: the guide's picture is not described).
        const QColor normal = kind_ == Kind::Parameter ? kKeyframe : kParentKeyframe;
        for (const Column& c : cols) {
            const double x = xFor(c.t);
            QPolygonF tri;
            tri << QPointF(x - 5, 2) << QPointF(x + 5, 2) << QPointF(x, height() - 4.0);
            p.setPen(Qt::NoPen);
            p.setBrush(selected(c) ? kSelected : normal);
            p.drawPolygon(tri);
            if (c.fixed) p.fillRect(QRectF(x - 0.75, 2, 1.5, height() - 2.0), QColor(Qt::black));
        }
    }
    // The position indicator.
    p.setPen(QPen(kPosition, 1.5));
    const double x = xFor(editor_.positionTime());
    p.drawLine(QPointF(x, 0), QPointF(x, height()));
    // "Apply to Group" or "Apply to All" while the track's shortcut menu is open (p.104).
    if (editor_.applyTo_ == which_ && (kind_ == Kind::Group || kind_ == Kind::Effect)) {
        p.setPen(palette().color(QPalette::BrightText));
        p.drawText(rect(), Qt::AlignCenter, kind_ == Kind::Group ? EffectEditor::tr("Apply to Group") : EffectEditor::tr("Apply to All"));
    }
}

// A press on a keyframe selects it (Ctrl: adds or takes it away, p.111) and, with Set Position To
// Keyframe, moves the position to it (p.132); anywhere else it moves the position (p.96).
void KeyframeTrack::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return QWidget::mousePressEvent(event);
    const QString parameter = which_.startsWith(QLatin1String("graph:")) ? which_.mid(6) : which_;
    if (kind_ == Kind::Parameter || kind_ == Kind::Graph) {
        if (!parameter.isEmpty()) editor_.setActiveParameter(parameter);
    }
    const QList<Column> cols = columns();
    const int hit = columnAt(event->position(), cols);
    pressPoint_ = event->position();
    dragging_ = false;
    pressed_.reset();
    movingPosition_ = false;
    if (hit < 0) {
        movingPosition_ = true;
        editor_.moveToTime(std::clamp(timeAt(event->position().x()), 0.0, 1.0));
        return;
    }
    const Column& c = cols.at(hit);
    QList<EffectEditor::KeyframeId>& selection = editor_.selection_;
    if (event->modifiers() & Qt::ControlModifier) {
        const bool all = std::all_of(c.keys.begin(), c.keys.end(), [&](const EffectEditor::KeyframeId& id) { return selection.contains(id); });
        for (const EffectEditor::KeyframeId& id : c.keys) {
            selection.removeAll(id);
            if (!all) selection << id;
        }
    } else if (!std::all_of(c.keys.begin(), c.keys.end(), [&](const EffectEditor::KeyframeId& id) { return selection.contains(id); })) {
        // Clicking a keyframe already selected keeps the selection, so that several can be dragged.
        selection = c.keys;
    } else {
        // The one clicked becomes the reference (the last selected, p.115).
        for (const EffectEditor::KeyframeId& id : c.keys) {
            selection.removeAll(id);
            selection << id;
        }
    }
    pressed_ = c.keys.last();
    if (editor_.setsPositionToKeyframe()) editor_.moveToTime(c.t);
    editor_.updateTracks();
}

void KeyframeTrack::mouseMoveEvent(QMouseEvent* event) {
    if (movingPosition_) {
        editor_.moveToTime(std::clamp(timeAt(event->position().x()), 0.0, 1.0));
        return;
    }
    if (!pressed_) return;
    const QPointF d = event->position() - pressPoint_;
    if (!dragging_ && std::abs(d.x()) < 3 && std::abs(d.y()) < 3) return;
    dragging_ = true;
    // Tracks move keyframes in time only (p.113); a graph drag changes the value, Alt+Shift+drag the
    // time and Alt+drag both (p.112).
    const Qt::KeyboardModifiers m = event->modifiers();
    bool inTime = true;
    bool inValue = false;
    if (kind_ == Kind::Graph) {
        inValue = !((m & Qt::AltModifier) && (m & Qt::ShiftModifier));
        inTime = (m & Qt::AltModifier) != 0;
    }
    const model::EffectSpan& span = editor_.span_;
    const double frames = inTime ? std::round(span.frameOf(timeAt(event->position().x())) - span.frameOf(timeAt(pressPoint_.x()))) : 0.0;
    const double value = inValue ? valueAt(event->position().y()) - valueAt(pressPoint_.y()) : 0.0;
    editor_.dragKeyframes(*pressed_, frames, value, true);
}

void KeyframeTrack::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging_) editor_.finishDrag();
    dragging_ = false;
    movingPosition_ = false;
    pressed_.reset();
    QWidget::mouseReleaseEvent(event);
}

void KeyframeTrack::contextMenuEvent(QContextMenuEvent* event) {
    if (which_.isEmpty()) return;
    std::unique_ptr<QMenu> menu(editor_.keyframeMenu(which_, this));
    menu->exec(event->globalPos());
}

// ================================================================ EffectEditor: keyframes

const model::Effect* EffectEditor::shownEffect() const {
    if (preview_) return &*preview_;
    return current_ ? &*current_ : nullptr;
}

const EffectParameter* EffectEditor::parameterInfo(const QString& parameter) const {
    const QList<EffectParameter>* list = effectParameters(categoryOf(parameter));
    if (!list) return nullptr;
    const QString name = nameOf(parameter);
    for (const EffectParameter& p : *list) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

QJsonValue EffectEditor::valueAt(const model::Effect& effect, const QString& parameter, double t) const {
    const EffectParameter* info = parameterInfo(parameter);
    const QString category = categoryOf(parameter);
    const QString name = nameOf(parameter);
    if (info && info->kind == QLatin1String("color")) {
        if (const auto rgb = model::colorAt(effect, category, name, t, span_)) {
            return QColor::fromRgbF(static_cast<float>(rgb->red), static_cast<float>(rgb->green), static_cast<float>(rgb->blue)).name();
        }
        const QColor d(info->defaultValue.toString());
        return d.isValid() ? d.name() : QStringLiteral("#000000");
    }
    if (const auto v = model::numberAt(effect, category, name, t, span_)) return std::round(*v * 1000.0) / 1000.0;
    const QJsonValue stored = effect.parameters.value(category).toObject().value(name);
    if (!stored.isUndefined()) return stored;
    if (info && info->kind == QLatin1String("slider")) {
        return std::clamp(info->defaultValue.isDouble() ? info->defaultValue.toDouble() : 0.0, double(info->min), double(info->max));
    }
    return info ? info->defaultValue : QJsonValue();
}

QStringList EffectEditor::keyframeableParameters() const {
    QStringList out;
    if (!current_) return out;
    const EffectInfo* info = EffectCatalog::builtin().find(current_->category, current_->name);
    if (!info) return out;
    for (const QString& category : info->parameters) {
        const QList<EffectParameter>* params = effectParameters(category);
        if (!params) continue;
        for (const EffectParameter& p : *params) {
            if (!p.keyframeable) continue;
            if (p.kind != QLatin1String("slider") && p.kind != QLatin1String("color")) continue;
            out << category + QLatin1Char('/') + p.name;
        }
    }
    return out;
}

QStringList EffectEditor::parametersOf(const QString& which) const {
    if (which == QLatin1String("effect")) return keyframeableParameters();
    if (which.startsWith(QLatin1String("group:"))) {
        const QString category = which.mid(6);
        QStringList out;
        for (const QString& p : keyframeableParameters()) {
            if (categoryOf(p) == category) out << p;
        }
        return out;
    }
    const QString p = which.startsWith(QLatin1String("graph:")) ? which.mid(6) : which;
    return keyframeableParameters().contains(p) ? QStringList{p} : QStringList();
}

QStringList EffectEditor::parametersFor(KeyframeMode mode) const {
    const QStringList all = keyframeableParameters();
    QStringList out;
    switch (mode) {
    case KeyframeMode::ActiveParameter:
        if (all.contains(activeParameter_)) out << activeParameter_;
        break;
    case KeyframeMode::ActiveGroup: {
        // A parameter in a subcategory takes only its subcategory (p.108).
        const EffectParameter* active = parameterInfo(activeParameter_);
        if (!active || !all.contains(activeParameter_)) break;
        for (const QString& p : all) {
            const EffectParameter* info = parameterInfo(p);
            if (categoryOf(p) == categoryOf(activeParameter_) && info && info->subcategory == active->subcategory) out << p;
        }
        break;
    }
    case KeyframeMode::OpenGroups:
        for (const QString& p : all) {
            if (isOpen(categoryOf(p))) out << p;
        }
        break;
    case KeyframeMode::EnabledGroups:
        // Ours: groups have no enable switches yet, so every group is enabled.
        out = all;
        break;
    case KeyframeMode::OpenGraphs:
        for (const QString& p : all) {
            if (openGraphs_.contains(p)) out << p;
        }
        break;
    case KeyframeMode::AllParameters: out = all; break;
    }
    return out;
}

double EffectEditor::positionTime() const {
    const model::MonitorState& rec = timeline_.composer().state(MonitorId::Record);
    return span_.timeOf(static_cast<double>(rec.position() - effectStart_));
}

void EffectEditor::moveToTime(double t) {
    if (!shown_) return;
    const auto frame = static_cast<std::int64_t>(std::llround(span_.frameOf(t)));
    const std::int64_t last = std::max<std::int64_t>(span_.length - 1, 0);
    timeline_.composer().moveTo(MonitorId::Record, effectStart_ + std::clamp<std::int64_t>(frame, 0, last));
}

void EffectEditor::setKeyframeGraphsShown(bool shown) {
    graphsShown_ = shown;
    if (graphsButton_) {
        const QSignalBlocker block(graphsButton_);
        graphsButton_->setChecked(shown);
    }
    updateTracks();
}

QWidget* EffectEditor::keyframeTrack(const QString& which) const {
    QWidget* w = tracks_.value(which);
    return w && w->isVisible() ? w : nullptr;
}

QPoint EffectEditor::keyframePoint(const QString& which, double t, double value) const {
    const auto* track = dynamic_cast<const KeyframeTrack*>(tracks_.value(which));
    if (!track) return {};
    const double y = track->kind() == KeyframeTrack::Kind::Graph ? track->yFor(value) : track->height() / 2.0;
    return QPoint(static_cast<int>(std::lround(track->xFor(t))), static_cast<int>(std::lround(y)));
}

void EffectEditor::setGraphOpen(const QString& parameter, bool open) {
    if (open) openGraphs_.insert(parameter);
    else openGraphs_.remove(parameter);
    if (QToolButton* b = graphOpeners_.value(parameter)) {
        const QSignalBlocker block(b);
        b->setChecked(open);
        b->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    }
    updateTracks();
}

void EffectEditor::setActiveParameter(const QString& parameter) {
    if (activeParameter_ == parameter) return;
    activeParameter_ = parameter;
    // The active parameter's name is shown highlighted (p.107: "a slider that you clicked is
    // highlighted"; ours, in bold).
    for (auto it = controls_.cbegin(); it != controls_.cend(); ++it) {
        if (auto* label = it.value()->property("nameLabel").value<QLabel*>()) {
            QFont f = label->font();
            f.setBold(it.key() == parameter);
            label->setFont(f);
        }
    }
    updateTracks();
}

void EffectEditor::selectKeyframe(const KeyframeId& id, bool extend) {
    if (extend) {
        if (selection_.contains(id)) selection_.removeAll(id);
        else selection_ << id;
    } else {
        selection_ = {id};
    }
    updateTracks();
}

void EffectEditor::deselectAllKeyframes(const QStringList& parameters) {
    if (parameters.isEmpty()) selection_.clear();
    else selection_.removeIf([&](const KeyframeId& id) { return parameters.contains(id.parameter); });
    updateTracks();
}

void EffectEditor::updateTracks() {
    // The tracks take a share of the editor's width (ours: 45%), the same for every row so that their
    // times line up.
    const int w = std::max(120, static_cast<int>(width() * 0.45));
    for (auto it = tracks_.cbegin(); it != tracks_.cend(); ++it) {
        QWidget* t = it.value();
        t->setFixedWidth(w);
        bool visible = graphsShown_;
        if (it.key().startsWith(QLatin1String("graph:"))) visible = visible && openGraphs_.contains(it.key().mid(6));
        t->setVisible(visible);
        t->update();
    }
    for (QToolButton* b : std::as_const(graphOpeners_)) b->setVisible(graphsShown_);
    const bool keyed = shown_ && !keyframeableParameters().isEmpty();
    for (QToolButton* b : {addKeyframe_, prevEvent_, nextEvent_}) {
        if (b) b->setEnabled(keyed);
    }
}

void EffectEditor::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateTracks();
}

bool EffectEditor::changeKeyframes(const QString& description, const std::function<bool(model::Effect&)>& change) {
    if (!shown_ || !current_) return false;
    model::Effect edited = *current_;
    if (!change(edited) || edited.parameters == current_->parameters) return false;
    const QJsonObject parameters = edited.parameters;
    preview_.reset();
    return timeline_.changeEffect(*shown_, description, [&](model::Effect& e, model::Transition*) { e.parameters = parameters; });
}

bool EffectEditor::addKeyframes(KeyframeMode mode) {
    const QStringList params = parametersFor(mode);
    if (params.isEmpty()) return false;
    const double t = positionTime();
    const bool done = changeKeyframes(tr("Add Keyframe"), [&](model::Effect& e) {
        bool changed = false;
        for (const QString& p : params) {
            const model::ParameterKeyframes keys = model::keyframesOf(e, categoryOf(p), nameOf(p));
            if (model::keyframeAt(keys, t, span_) >= 0) continue;
            model::setKeyframe(e, categoryOf(p), nameOf(p), t, span_, valueAt(e, p, t));
            changed = true;
        }
        return changed;
    });
    // "Active keyframe indicators appear at the position indicator in all appropriate tracks" (p.104).
    if (done && current_) {
        selection_.clear();
        for (const QString& p : params) {
            const int i = model::keyframeAt(model::keyframesOf(*current_, categoryOf(p), nameOf(p)), t, span_);
            if (i >= 0) selection_ << KeyframeId{p, i};
        }
        updateTracks();
    }
    return done;
}

bool EffectEditor::deleteKeyframes(KeyframeMode mode) {
    const QStringList params = parametersFor(mode);
    QList<KeyframeId> gone;
    for (const KeyframeId& id : selection_) {
        if (params.contains(id.parameter)) gone << id;
    }
    if (gone.isEmpty()) return false;
    const bool done = changeKeyframes(tr("Delete Keyframe"), [&](model::Effect& e) {
        for (const QString& p : params) {
            model::ParameterKeyframes keys = model::keyframesOf(e, categoryOf(p), nameOf(p));
            QList<int> indices;
            for (const KeyframeId& id : gone) {
                if (id.parameter == p) indices << id.index;
            }
            if (indices.isEmpty()) continue;
            std::sort(indices.begin(), indices.end(), std::greater<>());
            for (int i : indices) {
                if (i < keys.keys.size()) keys.keys.removeAt(i);
            }
            model::setKeyframes(e, categoryOf(p), nameOf(p), keys);
        }
        return true;
    });
    if (done) {
        selection_.removeIf([&](const KeyframeId& id) { return params.contains(id.parameter); });
        updateTracks();
    }
    return done;
}

bool EffectEditor::goToEvent(bool next) {
    const model::Effect* e = shownEffect();
    if (!e) return false;
    const double now = positionTime();
    const double half = span_.halfFrame();
    std::optional<double> target;
    for (const QString& p : keyframeableParameters()) {
        const model::ParameterKeyframes keys = model::keyframesOf(*e, categoryOf(p), nameOf(p));
        for (const model::EffectKeyframe& k : keys.keys) {
            const double t = model::keyframeTime(keys, k, span_);
            if (next ? t > now + half : t < now - half) {
                if (!target || (next ? t < *target : t > *target)) target = t;
            }
        }
    }
    if (!target) return false;
    // "Moves the position indicator one keyframe to the left [right] and activates the keyframe" (p.128).
    selection_.clear();
    for (const QString& p : keyframeableParameters()) {
        const int i = model::keyframeAt(model::keyframesOf(*e, categoryOf(p), nameOf(p)), *target, span_);
        if (i >= 0) selection_ << KeyframeId{p, i};
    }
    moveToTime(*target);
    updateTracks();
    return true;
}

void EffectEditor::dragKeyframes(const KeyframeId& id, double frames, double value, bool previewOnly) {
    if (!current_) return;
    if (!selection_.contains(id)) selection_ << id;
    model::Effect e = *current_;
    if (frames != 0.0) {
        const double by = span_.timeOf(frames) - span_.timeOf(0.0);
        QStringList params;
        for (const KeyframeId& s : selection_) {
            if (!params.contains(s.parameter)) params << s.parameter;
        }
        for (const QString& p : params) {
            model::ParameterKeyframes keys = model::keyframesOf(e, categoryOf(p), nameOf(p));
            QList<int> indices;
            for (const KeyframeId& s : selection_) {
                if (s.parameter == p && s.index < keys.keys.size()) indices << s.index;
            }
            if (indices.isEmpty()) continue;
            const int reference = p == id.parameter ? id.index : indices.first();
            if (reference >= keys.keys.size()) continue;
            const double target = model::keyframeTime(keys, keys.keys.at(reference), span_) + by;
            model::alignKeyframes(keys, indices, reference, target, span_, false);
            model::setKeyframes(e, categoryOf(p), nameOf(p), keys);
        }
    }
    if (value != 0.0) {
        // The selected keyframes of the dragged keyframe's parameter, by the same amount, within its
        // range (p.113).
        model::ParameterKeyframes keys = model::keyframesOf(e, categoryOf(id.parameter), nameOf(id.parameter));
        const EffectParameter* info = parameterInfo(id.parameter);
        const double lo = info ? info->min : -1e9;
        const double hi = info ? info->max : 1e9;
        for (const KeyframeId& s : selection_) {
            if (s.parameter != id.parameter || s.index >= keys.keys.size()) continue;
            const double v = std::clamp(std::round(keys.keys.at(s.index).value.toDouble() + value), lo, hi);
            model::moveKeyframe(keys, s.index, std::nullopt, v, span_);
        }
        model::setKeyframes(e, categoryOf(id.parameter), nameOf(id.parameter), keys);
    }
    if (previewOnly) {
        preview_ = e;
        syncValues(e);
        updateTracks();
        return;
    }
    const QJsonObject parameters = e.parameters;
    changeKeyframes(tr("Move Keyframe"), [&](model::Effect& edited) {
        edited.parameters = parameters;
        return true;
    });
}

void EffectEditor::finishDrag() {
    if (!preview_) return;
    const QJsonObject parameters = preview_->parameters;
    preview_.reset();
    if (!changeKeyframes(tr("Move Keyframe"), [&](model::Effect& e) {
            e.parameters = parameters;
            return true;
        })) {
        refresh();
    }
}

// The shortcut menu for the keyframe track (p.130). Zoom to Curve Height, Reset All Graph Views and
// Copy All Values at Position come with 6.2b2.
QMenu* EffectEditor::keyframeMenu(const QString& which, QWidget* parent) {
    auto* menu = new QMenu(parent ? parent : this);
    menu->setObjectName(QStringLiteral("keyframeMenu"));
    const QStringList params = parametersOf(which);
    const double t = positionTime();
    auto selectedIn = [this, params](const QString& p) {
        QList<int> out;
        for (const KeyframeId& id : selection_) {
            if (id.parameter == p && params.contains(p)) out << id.index;
        }
        return out;
    };

    menu->addAction(tr("Add Keyframe"), this, [this, params, t] {
        changeKeyframes(tr("Add Keyframe"), [&](model::Effect& e) {
            for (const QString& p : params) {
                if (model::keyframeAt(model::keyframesOf(e, categoryOf(p), nameOf(p)), t, span_) < 0) {
                    model::setKeyframe(e, categoryOf(p), nameOf(p), t, span_, valueAt(e, p, t));
                }
            }
            return true;
        });
    });
    menu->addAction(tr("Delete Keyframe"), this, [this, params] {
        const QList<KeyframeId> keep = selection_;
        changeKeyframes(tr("Delete Keyframe"), [&](model::Effect& e) {
            for (const QString& p : params) {
                model::ParameterKeyframes keys = model::keyframesOf(e, categoryOf(p), nameOf(p));
                QList<int> indices;
                for (const KeyframeId& id : keep) {
                    if (id.parameter == p) indices << id.index;
                }
                std::sort(indices.begin(), indices.end(), std::greater<>());
                for (int i : indices) {
                    if (i < keys.keys.size()) keys.keys.removeAt(i);
                }
                model::setKeyframes(e, categoryOf(p), nameOf(p), keys);
            }
            return true;
        });
        selection_.removeIf([&](const KeyframeId& id) { return params.contains(id.parameter); });
        updateTracks();
    });
    menu->addAction(tr("Add Start and End Keyframes"), this, [this, params] {
        // "Adds keyframes at the first and last frames of your effect" (p.130).
        const double first = span_.timeOf(0.0);
        const double last = span_.timeOf(static_cast<double>(std::max<std::int64_t>(span_.length - 1, 0)));
        changeKeyframes(tr("Add Start and End Keyframes"), [&](model::Effect& e) {
            for (const QString& p : params) {
                for (double at : {first, last}) {
                    if (model::keyframeAt(model::keyframesOf(e, categoryOf(p), nameOf(p)), at, span_) < 0) {
                        model::setKeyframe(e, categoryOf(p), nameOf(p), at, span_, valueAt(e, p, at));
                    }
                }
            }
            return true;
        });
    });
    menu->addAction(tr("Remove Redundant Keyframes"), this, [this, params] {
        changeKeyframes(tr("Remove Redundant Keyframes"), [&](model::Effect& e) {
            for (const QString& p : params) {
                model::ParameterKeyframes keys = model::keyframesOf(e, categoryOf(p), nameOf(p));
                if (model::removeRedundantKeyframes(keys) > 0) model::setKeyframes(e, categoryOf(p), nameOf(p), keys);
            }
            return true;
        });
        deselectAllKeyframes(params);
    });
    menu->addSeparator();
    menu->addAction(tr("Select All Keyframes"), this, [this, params] {
        const model::Effect* e = shownEffect();
        if (!e) return;
        selection_.removeIf([&](const KeyframeId& id) { return params.contains(id.parameter); });
        for (const QString& p : params) {
            const qsizetype n = model::keyframesOf(*e, categoryOf(p), nameOf(p)).keys.size();
            for (int i = 0; i < n; ++i) selection_ << KeyframeId{p, i};
        }
        updateTracks();
    });
    menu->addAction(tr("Deselect All Keyframes"), this, [this, params] { deselectAllKeyframes(params); });
    // Align and Slip (p.114-116): the reference is the keyframe selected last; in each parameter the
    // selected keyframe nearest it in time leads.
    for (const bool slip : {false, true}) {
        menu->addAction(slip ? tr("Slip Keyframes") : tr("Align Keyframes"), this, [this, params, t, slip, selectedIn] {
            if (selection_.isEmpty()) return;
            const KeyframeId reference = selection_.last();
            const model::Effect* shown = shownEffect();
            if (!shown) return;
            const model::ParameterKeyframes refKeys = model::keyframesOf(*shown, categoryOf(reference.parameter), nameOf(reference.parameter));
            if (reference.index >= refKeys.keys.size()) return;
            const double refTime = model::keyframeTime(refKeys, refKeys.keys.at(reference.index), span_);
            changeKeyframes(slip ? tr("Slip Keyframes") : tr("Align Keyframes"), [&](model::Effect& e) {
                for (const QString& p : params) {
                    const QList<int> indices = selectedIn(p);
                    if (indices.isEmpty()) continue;
                    model::ParameterKeyframes keys = model::keyframesOf(e, categoryOf(p), nameOf(p));
                    int lead = indices.first();
                    for (int i : indices) {
                        if (i >= keys.keys.size()) continue;
                        if (std::abs(model::keyframeTime(keys, keys.keys.at(i), span_) - refTime) <
                            std::abs(model::keyframeTime(keys, keys.keys.at(lead), span_) - refTime)) {
                            lead = i;
                        }
                    }
                    if (lead >= keys.keys.size()) continue;
                    model::alignKeyframes(keys, indices, lead, t, span_, slip);
                    model::setKeyframes(e, categoryOf(p), nameOf(p), keys);
                }
                return true;
            });
        });
    }
    menu->addAction(tr("Reverse Keyframes"), this, [this, which, params] {
        // The effect track reverses the whole effect, which also swaps a transition's sources (p.124).
        changeKeyframes(tr("Reverse Keyframes"), [&](model::Effect& e) {
            model::reverseKeyframes(e, span_, which == QLatin1String("effect") ? QStringList() : params);
            return true;
        });
        deselectAllKeyframes(params);
    });
    menu->addSeparator();

    // The options, checked when every keyframed parameter of the track has them. They apply to the
    // parameters that have keyframes (ours: an option is kept with the keyframes).
    const model::Effect* e = shownEffect();
    auto all = [&](const std::function<bool(const model::ParameterKeyframes&)>& test) {
        bool any = false;
        for (const QString& p : params) {
            if (!e) break;
            const model::ParameterKeyframes keys = model::keyframesOf(*e, categoryOf(p), nameOf(p));
            if (keys.keys.isEmpty()) continue;
            any = true;
            if (!test(keys)) return false;
        }
        return any;
    };
    auto option = [&](const QString& text, const std::function<bool(const model::ParameterKeyframes&)>& isSet,
                      const std::function<void(model::ParameterKeyframes&)>& set) {
        QAction* a = menu->addAction(text, this, [this, params, text, set] {
            changeKeyframes(text, [&](model::Effect& edited) {
                for (const QString& p : params) {
                    model::ParameterKeyframes keys = model::keyframesOf(edited, categoryOf(p), nameOf(p));
                    if (keys.keys.isEmpty()) continue;
                    set(keys);
                    model::setKeyframes(edited, categoryOf(p), nameOf(p), keys);
                }
                return true;
            });
        });
        a->setCheckable(true);
        a->setChecked(all(isSet));
    };
    const std::pair<model::Interpolation, QString> interpolations[] = {
        {model::Interpolation::Shelf, tr("Shelf")},
        {model::Interpolation::Linear, tr("Linear")},
        {model::Interpolation::Spline, tr("Spline")},
        {model::Interpolation::Bezier, tr("Bézier")},
    };
    for (const auto& entry : interpolations) {
        const model::Interpolation kind = entry.first;
        option(entry.second, [kind](const model::ParameterKeyframes& k) { return k.interpolation == kind; },
               [kind](model::ParameterKeyframes& k) { k.interpolation = kind; });
    }
    menu->addSeparator();
    const model::EffectSpan span = span_;
    option(tr("Fixed"), [](const model::ParameterKeyframes& k) { return k.fixed; },
           [span](model::ParameterKeyframes& k) { model::setFixed(k, true, span); });
    option(tr("Elastic"), [](const model::ParameterKeyframes& k) { return !k.fixed; },
           [span](model::ParameterKeyframes& k) { model::setFixed(k, false, span); });
    menu->addSeparator();
    option(tr("Extrapolate"), [](const model::ParameterKeyframes& k) { return k.extrapolate; },
           [](model::ParameterKeyframes& k) { k.extrapolate = true; });
    option(tr("Hold"), [](const model::ParameterKeyframes& k) { return !k.extrapolate; },
           [](model::ParameterKeyframes& k) { k.extrapolate = false; });

    // "Apply to Group" or "Apply to All" while the menu is open (p.104).
    applyTo_ = which;
    updateTracks();
    connect(menu, &QMenu::aboutToHide, this, [this] {
        applyTo_.clear();
        updateTracks();
    });
    return menu;
}

// The Add Keyframe button (p.104-106, p.128): its mode menu with the default command checked and
// active, so that a second click runs it; Alt+click deletes the selected keyframes through the Delete
// Keyframe Mode menu. Without Show Add Keyframe Mode Menu it runs the default command at once.
void EffectEditor::addKeyframeButton(bool remove) {
    if (!showModeMenu_) {
        if (remove) deleteKeyframes(mode_);
        else addKeyframes(mode_);
        return;
    }
    QMenu menu(this);
    menu.setObjectName(remove ? QStringLiteral("deleteKeyframeMode") : QStringLiteral("addKeyframeMode"));
    QAction* current = nullptr;
    for (KeyframeMode m : kModes) {
        QAction* a = menu.addAction(tr(remove ? deleteModeText(m) : addModeText(m)));
        a->setCheckable(true);
        a->setChecked(m == mode_);
        a->setData(static_cast<int>(m));
        if (m == mode_) current = a;
    }
    menu.setActiveAction(current);
    QAction* chosen = menu.exec(addKeyframe_->mapToGlobal(QPoint(0, addKeyframe_->height())));
    if (!chosen) return;
    // The command chosen becomes the default for both menus (p.105, p.107).
    mode_ = static_cast<KeyframeMode>(chosen->data().toInt());
    if (remove) deleteKeyframes(mode_);
    else addKeyframes(mode_);
}

void EffectEditor::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && !selection_.isEmpty()) {
        deleteKeyframes(KeyframeMode::AllParameters);
        return;
    }
    QWidget::keyPressEvent(event);
}

void EffectEditor::contextMenuEvent(QContextMenuEvent* event) {
    // "The Effect Editor shortcut menu is available only for effects with advanced keyframes" (p.131).
    if (!shown_ || keyframeableParameters().isEmpty()) return;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("effectEditorMenu"));
    QAction* setPosition = menu.addAction(tr("Set Position To Keyframe"));
    setPosition->setCheckable(true);
    setPosition->setChecked(positionToKeyframe_);
    connect(setPosition, &QAction::toggled, this, [this](bool on) { positionToKeyframe_ = on; });
    QAction* showMenu = menu.addAction(tr("Show Add Keyframe Mode Menu"));
    showMenu->setCheckable(true);
    showMenu->setChecked(showModeMenu_);
    connect(showMenu, &QAction::toggled, this, [this](bool on) { showModeMenu_ = on; });
    menu.addSeparator();
    for (KeyframeMode m : kModes) {
        QAction* a = menu.addAction(tr(shortcutModeText(m)));
        a->setCheckable(true);
        a->setChecked(m == mode_);
        connect(a, &QAction::triggered, this, [this, m] { mode_ = m; });
    }
    menu.exec(event->globalPos());
}

} // namespace lumenis::ui
