// Accessible names from what a control shows (10.3a), and what the widgets we paint are and show (10.3b): the
// Timeline as tracks of segments, the position bars as sliders, the monitors' pictures and the graphs as pictures
// (accessibility.h; ROADMAP 10.3, docs/spec/accessibility.md).

#include "lumenis/ui/accessibility.h"

#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/timeline.h"

#include <QAbstractButton>
#include <QAccessibleValueInterface>
#include <QAccessibleWidget>
#include <QComboBox>
#include <QHash>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QApplication>
#include <QEvent>
#include <QLineEdit>
#include <QPointer>
#include <QTextDocumentFragment>

namespace lumenis::ui {

namespace {

// Names we gave are marked, so they can follow the control's tooltip; a name set by the code itself is never touched.
constexpr char kDerived[] = "lumenisDerivedAccessibleName";

QString plain(const QString& text) {
    // Tooltips may be rich text ("<b>Overwrite</b> (B)"); a reader wants the words.
    QString out = Qt::mightBeRichText(text) ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
    out.remove(QLatin1Char('&')); // mnemonics
    return out.simplified();
}

class AccessibleNames : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::Polish:
        case QEvent::Show:
        case QEvent::ToolTipChange:
            if (watched->isWidgetType()) name(static_cast<QWidget*>(watched));
            break;
        default:
            break;
        }
        return false;
    }

private:
    static void name(QWidget* w) {
        const bool ours = w->property(kDerived).toBool();
        if (!w->accessibleName().isEmpty() && !ours) return; // set by the code: left alone
        const QString derived = derivedAccessibleName(w);
        if (derived.isEmpty() || derived == w->accessibleName()) return;
        w->setAccessibleName(derived);
        w->setProperty(kDerived, true);
    }
};

} // namespace

QString derivedAccessibleName(const QWidget* w) {
    if (const auto* b = qobject_cast<const QAbstractButton*>(w)) {
        const QString text = plain(b->text());
        if (!text.isEmpty()) return {}; // Qt names it by its text already
    }
    const QString tip = plain(w->toolTip());
    if (!tip.isEmpty()) return tip;
    // The clear button Qt puts in a line edit (setClearButtonEnabled) has neither text nor tooltip.
    if (qstrcmp(w->metaObject()->className(), "QLineEditIconButton") == 0) return QApplication::translate("Accessibility", "Clear");
    if (const auto* e = qobject_cast<const QLineEdit*>(w)) {
        // An editable combo box's own text field is named as the combo box is.
        if (const auto* combo = qobject_cast<const QComboBox*>(w->parentWidget())) {
            if (!combo->accessibleName().isEmpty()) return combo->accessibleName();
            return derivedAccessibleName(combo);
        }
        // A spin box's own text field is named as the spin box is.
        if (const auto* spin = qobject_cast<const QAbstractSpinBox*>(w->parentWidget())) {
            if (!spin->accessibleName().isEmpty()) return spin->accessibleName();
            const QString derived = derivedAccessibleName(spin);
            if (!derived.isEmpty()) return derived;
            // Named by a form's label (its buddy), which only Qt's own interface for the spin box knows.
            if (QAccessibleInterface* a = QAccessible::queryAccessibleInterface(const_cast<QAbstractSpinBox*>(spin)))
                return plain(a->text(QAccessible::Name));
            return {};
        }
        return plain(e->placeholderText());
    }
    return {};
}


// ---------------------------------------------------------------- the widgets we paint (10.3b)

namespace {

QString tr(const char* text) {
    return QApplication::translate("Accessibility", text);
}

QString timecodeText(std::int64_t frames, const TimecodeFormat& format) {
    return QString::fromStdString(Timecode(frames, format).toString());
}

// A Timeline track's segments as items, one interface per (track, segment): created when a reader asks, kept for
// the application's life (registered with QAccessible, which deletes them when it shuts down) and reused, and reading
// the sequence as it is now each time, so they never point at a segment that has gone (isValid() is then false).
class SegmentAccessible;
class TrackAccessible;

const model::Track* trackOf(const TimelineView* view, const QString& track) {
    if (!view) return nullptr;
    return view->timeline().shown().track(track);
}

class SegmentAccessible : public QAccessibleInterface {
public:
    SegmentAccessible(TimelineView* view, QString track, int index) : view_(view), track_(std::move(track)), index_(index) {}
    bool isValid() const override {
        const model::Track* t = trackOf(view_, track_);
        return t && index_ < t->segments.size();
    }
    QObject* object() const override { return nullptr; }
    QWindow* window() const override {
        TimelineView* v = view_.data();
        QWidget* top = v ? v->window() : nullptr;
        return top ? top->windowHandle() : nullptr;
    }
    QAccessibleInterface* parent() const override;
    QAccessibleInterface* child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    QAccessibleInterface* focusChild() const override { return nullptr; }
    QString text(QAccessible::Text kind) const override {
        TimelineView* v = view_.data();
        const model::Track* t = trackOf(v, track_);
        if (kind != QAccessible::Name || !v || !t || index_ >= t->segments.size()) return {};
        std::int64_t start = 0;
        for (int i = 0; i < index_; ++i) start += t->segments.at(i).length;
        const model::Segment& s = t->segments.at(index_);
        const TimecodeFormat& f = v->timeline().format();
        const QString what = s.isFiller() ? tr("Filler") : s.name;
        return QStringLiteral("%1, %2 %3, %4 %5").arg(what, tr("starts"), timecodeText(start, f), tr("length"), timecodeText(s.length, f));
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override {
        TimelineView* v = view_.data();
        const model::Track* t = trackOf(v, track_);
        if (!v || !t || index_ >= t->segments.size()) return {};
        std::int64_t start = 0;
        for (int i = 0; i < index_; ++i) start += t->segments.at(i).length;
        for (const TimelineView::Row& r : v->rows()) {
            if (r.track != track_) continue;
            const int x0 = int(v->xFor(start)), x1 = int(v->xFor(start + t->segments.at(index_).length));
            return QRect(v->mapToGlobal(QPoint(x0, r.top)), QSize(std::max(1, x1 - x0), r.height));
        }
        return {};
    }
    QAccessible::Role role() const override { return QAccessible::ListItem; }
    QAccessible::State state() const override {
        QAccessible::State s;
        s.selectable = true;
        if (!isValid()) s.invisible = true;
        return s;
    }
    int index() const { return index_; }

private:
    QPointer<TimelineView> view_;
    QString track_;
    int index_;
};

class TrackAccessible : public QAccessibleInterface {
public:
    TrackAccessible(TimelineView* view, QString track) : view_(view), track_(std::move(track)) {}
    bool isValid() const override { return trackOf(view_, track_) != nullptr; }
    QObject* object() const override { return nullptr; }
    QWindow* window() const override {
        TimelineView* v = view_.data();
        QWidget* top = v ? v->window() : nullptr;
        return top ? top->windowHandle() : nullptr;
    }
    QAccessibleInterface* parent() const override { return QAccessible::queryAccessibleInterface(view_.data()); }
    int childCount() const override {
        const model::Track* t = trackOf(view_, track_);
        return t ? int(t->segments.size()) : 0;
    }
    QAccessibleInterface* child(int index) const override {
        if (index < 0 || index >= childCount()) return nullptr;
        auto it = children_.find(index);
        if (it == children_.end()) {
            QAccessibleInterface* made = new SegmentAccessible(view_.data(), track_, index);
            it = children_.insert(index, QAccessible::registerAccessibleInterface(made));
        }
        return QAccessible::accessibleInterface(*it);
    }
    int indexOfChild(const QAccessibleInterface* c) const override {
        const auto* s = dynamic_cast<const SegmentAccessible*>(c);
        return s ? s->index() : -1;
    }
    QAccessibleInterface* childAt(int x, int y) const override {
        for (int i = 0; i < childCount(); ++i) {
            if (child(i)->rect().contains(x, y)) return child(i);
        }
        return nullptr;
    }
    QAccessibleInterface* focusChild() const override { return nullptr; }
    QString text(QAccessible::Text kind) const override {
        const model::Track* t = trackOf(view_.data(), track_);
        if (kind != QAccessible::Name || !t) return {};
        const QString name = t->customName.isEmpty() ? track_ : QStringLiteral("%1 %2").arg(track_, t->customName);
        return tr("Track %1, %2 segments").arg(name).arg(t->segments.size());
    }
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override {
        TimelineView* v = view_.data();
        if (!v) return {};
        for (const TimelineView::Row& r : v->rows()) {
            if (r.track == track_) return QRect(v->mapToGlobal(QPoint(0, r.top)), QSize(v->width(), r.height));
        }
        return {};
    }
    QAccessible::Role role() const override { return QAccessible::List; }
    QAccessible::State state() const override { return {}; }
    const QString& track() const { return track_; }

private:
    QPointer<TimelineView> view_;
    QString track_;
    mutable QHash<int, QAccessible::Id> children_;
};

QAccessibleInterface* SegmentAccessible::parent() const {
    if (!view_) return nullptr;
    QAccessibleInterface* timeline = QAccessible::queryAccessibleInterface(view_.data());
    for (int i = 0; timeline && i < timeline->childCount(); ++i) {
        auto* t = dynamic_cast<TrackAccessible*>(timeline->child(i));
        if (t && t->track() == track_) return t;
    }
    return nullptr;
}

// The Timeline: its tracks top to bottom, as the rows are shown.
class TimelineAccessible : public QAccessibleWidget {
public:
    explicit TimelineAccessible(TimelineView* view) : QAccessibleWidget(view, QAccessible::Pane) {}
    TimelineView* view() const { return static_cast<TimelineView*>(widget()); }
    QStringList tracks() const {
        QStringList out;
        for (const TimelineView::Row& r : view()->rows()) {
            if (r.exists && trackOf(view(), r.track)) out << r.track;
        }
        return out;
    }
    int childCount() const override { return int(tracks().size()); }
    QAccessibleInterface* child(int index) const override {
        const QStringList names = tracks();
        if (index < 0 || index >= names.size()) return nullptr;
        auto it = children_.find(names.at(index));
        if (it == children_.end()) {
            it = children_.insert(names.at(index), QAccessible::registerAccessibleInterface(new TrackAccessible(view(), names.at(index))));
        }
        return QAccessible::accessibleInterface(*it);
    }
    int indexOfChild(const QAccessibleInterface* c) const override {
        const auto* t = dynamic_cast<const TrackAccessible*>(c);
        return t ? int(tracks().indexOf(t->track())) : -1;
    }
    QString text(QAccessible::Text kind) const override {
        if (kind == QAccessible::Description) {
            const model::Sequence& s = view()->timeline().shown();
            return tr("%1 tracks, %2 long").arg(childCount()).arg(timecodeText(s.duration(), view()->timeline().format()));
        }
        return QAccessibleWidget::text(kind);
    }

private:
    mutable QHash<QString, QAccessible::Id> children_;
};

// A monitor's position bar: a slider over the frames it spans, at the monitor's position.
class PositionBarAccessible : public QAccessibleWidget, public QAccessibleValueInterface {
public:
    explicit PositionBarAccessible(PositionBar* bar) : QAccessibleWidget(bar, QAccessible::Slider) {}
    PositionBar* bar() const { return static_cast<PositionBar*>(widget()); }
    void* interface_cast(QAccessible::InterfaceType type) override {
        return type == QAccessible::ValueInterface ? static_cast<QAccessibleValueInterface*>(this) : QAccessibleWidget::interface_cast(type);
    }
    QString text(QAccessible::Text kind) const override {
        if (kind == QAccessible::Name && widget()->accessibleName().isEmpty())
            return bar()->monitor() == MonitorId::Source ? tr("Source monitor position") : tr("Record monitor position");
        if (kind == QAccessible::Value) return QString::number(bar()->composer().state(bar()->monitor()).position());
        return QAccessibleWidget::text(kind);
    }
    QVariant currentValue() const override { return QVariant::fromValue<qlonglong>(bar()->composer().state(bar()->monitor()).position()); }
    void setCurrentValue(const QVariant& value) override { bar()->composer().moveTo(bar()->monitor(), value.toLongLong()); }
    QVariant maximumValue() const override { return QVariant::fromValue<qlonglong>(std::max<std::int64_t>(bar()->span().second, 0)); }
    QVariant minimumValue() const override { return QVariant::fromValue<qlonglong>(bar()->span().first); }
    QVariant minimumStepSize() const override { return 1; }
};

// A monitor's picture: what is loaded and where.
class MonitorPictureAccessible : public QAccessibleWidget {
public:
    explicit MonitorPictureAccessible(MonitorPicture* picture) : QAccessibleWidget(picture, QAccessible::Graphic) {}
    MonitorPicture* picture() const { return static_cast<MonitorPicture*>(widget()); }
    QString text(QAccessible::Text kind) const override {
        const bool source = picture()->monitor() == MonitorId::Source;
        if (kind == QAccessible::Name && widget()->accessibleName().isEmpty()) return source ? tr("Source monitor") : tr("Record monitor");
        if (kind == QAccessible::Description) {
            const model::MonitorState& s = picture()->composer().state(picture()->monitor());
            if (!s.current()) return tr("Empty");
            return tr("%1, frame %2 of %3").arg(s.current()->item.name).arg(s.position() + 1).arg(s.duration());
        }
        return QAccessibleWidget::text(kind);
    }
};

// The other widgets we paint (graphs, wheels, meters, previews): pictures, named by their class when the code gave
// them no name.
struct Painted {
    const char* className;
    const char* name;
};
constexpr Painted kPainted[] = {
    {"lumenis::ui::AngleDial", "Angle"},
    {"lumenis::ui::AudioMeters", "Audio meters"},
    {"lumenis::ui::BrushPreview", "Brush preview"},
    {"lumenis::ui::ChromaCurveGraph", "Chroma curve"},
    {"lumenis::ui::ChromaWheel", "Color wheel"},
    {"lumenis::ui::EqGraph", "EQ graph"},
    {"lumenis::ui::FramingViewer", "Framing"},
    {"lumenis::ui::Histogram", "Histogram"},
    {"lumenis::ui::KeyframeTrack", "Keyframes"},
    {"lumenis::ui::LumaRangesGraph", "Luma ranges"},
    {"lumenis::ui::MotionGraph", "Motion graph"},
    {"lumenis::ui::PanGrid", "Pan"},
    {"lumenis::ui::SafeColorGraph", "Safe colors"},
    {"lumenis::ui::SequenceMap", "Sequence map"},
    {"lumenis::ui::TitleCanvas", "Title"},
    {"lumenis::ui::TrackerTimeline", "Tracking data"},
    {"lumenis::ui::VectorWheel", "Vector wheel"},
};

class PaintedAccessible : public QAccessibleWidget {
public:
    PaintedAccessible(QWidget* w, const char* name) : QAccessibleWidget(w, QAccessible::Graphic), name_(name) {}
    QString text(QAccessible::Text kind) const override {
        if (kind == QAccessible::Name && widget()->accessibleName().isEmpty()) return tr(name_);
        return QAccessibleWidget::text(kind);
    }

private:
    const char* name_;
};

QAccessibleInterface* paintedWidgets(const QString& className, QObject* object) {
    if (!object || !object->isWidgetType()) return nullptr;
    if (className == QLatin1String("lumenis::ui::TimelineView")) return new TimelineAccessible(static_cast<TimelineView*>(object));
    if (className == QLatin1String("lumenis::ui::PositionBar")) return new PositionBarAccessible(static_cast<PositionBar*>(object));
    if (className == QLatin1String("lumenis::ui::MonitorPicture")) return new MonitorPictureAccessible(static_cast<MonitorPicture*>(object));
    for (const Painted& p : kPainted) {
        if (className == QLatin1String(p.className)) return new PaintedAccessible(static_cast<QWidget*>(object), p.name);
    }
    return nullptr;
}

} // namespace

void installAccessibleNames(QApplication& app) {
    // Once per application. The filter is the application's child and goes with it; the pointer is cleared then, so a
    // later application gets its own.
    static AccessibleNames* filter = nullptr;
    if (filter) return;
    filter = new AccessibleNames(&app);
    QObject::connect(filter, &QObject::destroyed, [] { filter = nullptr; });
    app.installEventFilter(filter);
    // What the widgets we paint are and show (10.3b).
    QAccessible::installFactory(paintedWidgets);
}

} // namespace lumenis::ui
