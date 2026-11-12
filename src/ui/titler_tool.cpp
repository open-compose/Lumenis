// The Titler+ tools in the Record monitor (titler_tool.h; FX Guide p.303-319; docs/spec/titler.md).
#include "lumenis/ui/titler_tool.h"

#include "lumenis/media/titler.h"
#include "lumenis/ui/effect_editor.h"

#include <QKeyEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lumenis::ui {

namespace {

using model::Title;
using model::TitleObject;
using model::TitleObjectKind;

// How near a corner handle a press must be (title pixels, ours), and how far outside it the rotation zone reaches.
constexpr double kHandleReach = 16.0;
constexpr double kRotateReach = 40.0;

bool isText(const Title& t, const QString& id) {
    const TitleObject* o = t.object(id);
    return o && o->kind == TitleObjectKind::Text;
}

} // namespace

TitlerTool::TitlerTool(EffectEditor& editor) : QObject(&editor), editor_(editor) {}

model::Title TitlerTool::title() const {
    if (working_) return *working_;
    return editor_.storedTitle().value_or(Title{});
}

void TitlerTool::preview(const Title& t) {
    working_ = t;
    editor_.previewTitle(t);
    emit changed();
}

bool TitlerTool::commit(const QString& description, const Title& t) {
    // A copy first: `t` may be the working title itself.
    const Title stored = t;
    working_.reset();
    const bool done = editor_.commitTitle(description, stored);
    emit changed();
    return done;
}

QPointF TitlerTool::toTitle(const QPointF& fraction) const {
    const QSizeF size = editor_.previewSize();
    const double scale = size.height() / 1080.0;
    return {(fraction.x() * size.width() - size.width() / 2.0) / scale, (fraction.y() * size.height() - size.height() / 2.0) / scale};
}

QPointF TitlerTool::toFraction(const QPointF& point) const {
    const QSizeF size = editor_.previewSize();
    const double scale = size.height() / 1080.0;
    return {(point.x() * scale + size.width() / 2.0) / size.width(), (point.y() * scale + size.height() / 2.0) / size.height()};
}

void TitlerTool::setTool(Tool tool) {
    finishEditing();
    tool_ = tool;
    emit changed();
}

void TitlerTool::select(const QStringList& ids) {
    finishEditing();
    selected_ = ids;
    emit changed();
    editor_.refreshTitleOverlay();
}

void TitlerTool::reset() {
    working_.reset();
    selected_.clear();
    editing_.clear();
    caret_ = anchor_ = 0;
    drag_ = Drag::None;
}

void TitlerTool::startEditing(const Title& t, const QString& id, std::optional<qsizetype> at) {
    const TitleObject* o = t.object(id);
    if (!o) return;
    editing_ = id;
    selected_ = {id};
    caret_ = anchor_ = at.value_or(o->text.size());
    working_ = t;
    emit changed();
    editor_.refreshTitleOverlay();
}

void TitlerTool::finishEditing() {
    if (editing_.isEmpty()) return;
    const QString id = editing_;
    editing_.clear();
    if (!working_) return;
    Title t = *working_;
    // "If you click in an empty area but do not type anything, that empty layer gets deleted after existing the text
    // mode" (p.303).
    if (const TitleObject* o = t.object(id); o && o->text.isEmpty()) {
        model::removeObject(t, id);
        selected_.removeAll(id);
    }
    const Title stored = editor_.storedTitle().value_or(Title{});
    if (t == stored) {
        working_.reset();
        editor_.cancelTitlePreview();
        emit changed();
        return;
    }
    commit(tr("Title Text"), t);
}

QString TitlerTool::objectAt(const Title& t, const QPointF& point, bool deep) const {
    // From the front. "Hold down the Option (macOS) or Alt (Windows) key and select a child object within the group"
    // (p.318).
    std::function<QString(const QList<TitleObject>&)> find = [&](const QList<TitleObject>& list) -> QString {
        for (auto it = list.crbegin(); it != list.crend(); ++it) {
            const auto outline = media::titleObjectOutline(t, it->id);
            if (!outline || !outline->containsPoint(point, Qt::OddEvenFill)) continue;
            if (deep && it->kind == TitleObjectKind::Group) {
                const QString inner = find(it->children);
                if (!inner.isEmpty()) return inner;
            }
            return it->id;
        }
        return {};
    };
    return find(t.objects);
}

std::optional<int> TitlerTool::cornerAt(const Title& t, const QPointF& point, bool* rotate) const {
    if (selected_.size() != 1) return std::nullopt;
    const auto outline = media::titleObjectOutline(t, selected_.first());
    if (!outline || outline->size() < 4) return std::nullopt;
    const QPointF centre = outline->boundingRect().center();
    for (int k = 0; k < 4; ++k) {
        const QPointF c = outline->at(k);
        const double d = std::hypot(point.x() - c.x(), point.y() - c.y());
        if (d <= kHandleReach) {
            *rotate = false;
            return k;
        }
        // "hover next to one of the bounding box corners to activate the Rotation icon" (p.312): just outside it.
        const bool outside = std::hypot(point.x() - centre.x(), point.y() - centre.y()) > std::hypot(c.x() - centre.x(), c.y() - centre.y());
        if (d <= kRotateReach && outside) {
            *rotate = true;
            return k;
        }
    }
    return std::nullopt;
}

bool TitlerTool::overlayPress(const QPointF& at, Qt::KeyboardModifiers modifiers, bool doubleClick) {
    const QPointF p = toTitle(at);
    Title t = title();
    // With the Tracking window open the Effect Preview's tracking boxes take the mouse (p.326 step 3); a Roll or
    // Crawl out of Edit Mode is not edited (ours).
    if (editor_.trackingWindowShown() || (t.rollCrawl.on && !t.rollCrawl.editMode)) return false;
    const bool deep = modifiers & Qt::AltModifier;
    if (!doubleClick && tool_ == Tool::Selection) {
        bool rotate = false;
        if (const auto corner = cornerAt(t, p, &rotate)) {
            drag_ = rotate ? Drag::Rotate : Drag::Scale;
            corner_ = *corner;
            dragFrom_ = p;
            dragStart_ = t;
            return true;
        }
    }
    const QString hit = objectAt(t, p, deep);
    if (doubleClick) {
        // "Double-clicking the title text allows you to enter text mode"; "you can active the Text Tool by simply
        // double-clicking in an empty area of the viewer" (p.303).
        finishEditing();
        if (!hit.isEmpty() && isText(t, hit)) {
            tool_ = Tool::Text;
            startEditing(title(), hit);
        } else if (hit.isEmpty()) {
            setTool(Tool::Text);
        }
        return true;
    }
    switch (tool_) {
    case Tool::Text: {
        // "With the Text Tool activated, click and drag inside a text box to highlight any text you want to modify"
        // (p.304): the press puts the cursor where it is, a drag highlights to where it goes (6.8c2).
        auto indexAt = [&](const Title& in, const QString& id) -> qsizetype {
            const TitleObject* o = in.object(id);
            const auto placed = media::titleObjectPlacement(in, id);
            return o && placed ? media::titleTextIndexAt(*o, placed->inverted().map(p)) : 0;
        };
        if (!editing_.isEmpty() && hit == editing_) {
            caret_ = indexAt(t, hit);
            if (!(modifiers & Qt::ShiftModifier)) anchor_ = caret_;
            drag_ = Drag::Highlight;
            emit changed();
            editor_.refreshTitleOverlay();
            return true;
        }
        finishEditing();
        t = title();
        // "You can also return to an existing text layer to edit the text by clicking inside its bounding box while in
        // text mode" (p.303); a shape is selected.
        if (!hit.isEmpty() && isText(t, hit)) {
            startEditing(t, hit, indexAt(t, hit));
            drag_ = Drag::Highlight;
            return true;
        }
        if (!hit.isEmpty()) {
            selected_ = {hit};
            emit changed();
            editor_.refreshTitleOverlay();
            return true;
        }
        // "you can type anywhere and create a text layer at that location" (p.303).
        const TitleObject o = model::textObject(t, QString(), p);
        t.objects << o;
        startEditing(t, o.id, 0);
        preview(t);
        return true;
    }
    case Tool::Rectangle:
    case Tool::Ellipse: {
        // "Click and drag in the viewer to create the desired object" (p.306).
        finishEditing();
        t = title();
        dragStart_ = t;
        const TitleObject o = model::shapeObject(t, tool_ == Tool::Ellipse ? TitleObjectKind::Ellipse : TitleObjectKind::Rectangle, p, QSizeF(0, 0));
        t.objects << o;
        selected_ = {o.id};
        dragFrom_ = p;
        drag_ = Drag::Draw;
        working_ = t;
        emit changed();
        return true;
    }
    case Tool::Selection:
        if (hit.isEmpty()) {
            // "click on an empty space in the viewer to return to the standard mode" (p.318).
            selected_.clear();
            emit changed();
            editor_.refreshTitleOverlay();
            return true;
        }
        if (modifiers & Qt::ShiftModifier) {
            // "Click on the objects you want to group together, one at a time, while you hold down the Shift key"
            // (p.318).
            if (selected_.contains(hit)) selected_.removeAll(hit);
            else selected_ << hit;
        } else if (!selected_.contains(hit)) {
            selected_ = {hit};
        }
        drag_ = Drag::Move;
        dragFrom_ = p;
        dragStart_ = t;
        emit changed();
        editor_.refreshTitleOverlay();
        return true;
    }
    return false;
}

void TitlerTool::overlayMove(const QPointF& at, Qt::KeyboardModifiers modifiers) {
    if (drag_ == Drag::None) return;
    const QPointF p = toTitle(at);
    if (drag_ == Drag::Highlight) {
        const Title t = title();
        const TitleObject* o = t.object(editing_);
        const auto placed = media::titleObjectPlacement(t, editing_);
        if (!o || !placed) return;
        const qsizetype index = media::titleTextIndexAt(*o, placed->inverted().map(p));
        if (index != caret_) {
            caret_ = index;
            emit changed();
            editor_.refreshTitleOverlay();
        }
        return;
    }
    Title t = dragStart_;
    const bool shift = modifiers & Qt::ShiftModifier;
    if (drag_ == Drag::Move) {
        QPointF by = p - dragFrom_;
        // "Shift+Drag Up or Down: Moves the object along the Y axis; Shift+Drag Left or Right: Moves the object along
        // the X axis" (p.311).
        if (shift) {
            if (std::abs(by.x()) >= std::abs(by.y())) by.setY(0.0);
            else by.setX(0.0);
        }
        for (const QString& id : selected_) {
            if (TitleObject* o = t.object(id)) o->transform.position += by;
        }
    } else if (drag_ == Drag::Draw) {
        // "Dragging while holding the Shift key constrains the aspect ratio ... dragging while holding Command+Shift
        // (macOS) or Ctrl+Shift (Windows) constrains the aspect ratio and will originate from the Anchor Point"
        // (p.306): the press is the centre then.
        TitleObject* o = selected_.isEmpty() ? nullptr : (working_ ? &*working_ : &t)->object(selected_.first());
        if (!o) return;
        const bool fromCentre = shift && (modifiers & Qt::ControlModifier);
        QPointF d = p - dragFrom_;
        if (shift) {
            const double side = std::max(std::abs(d.x()), std::abs(d.y()));
            d = QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
        }
        o->size = fromCentre ? QSizeF(2.0 * std::abs(d.x()), 2.0 * std::abs(d.y())) : QSizeF(std::abs(d.x()), std::abs(d.y()));
        o->transform.position = fromCentre ? dragFrom_ : dragFrom_ + d / 2.0;
        preview(*working_);
        return;
    } else if (!selected_.isEmpty()) {
        const QString id = selected_.first();
        TitleObject* o = t.object(id);
        const auto place = media::titleObjectPlacement(dragStart_, id);
        if (!o || !place) return;
        const QPointF anchorAt = place->map(o->transform.anchor);
        if (drag_ == Drag::Rotate) {
            // "You can also hold down the Shift key while dragging to rotate the shape in increments of 15 degrees"
            // (p.312).
            const double turn = (std::atan2(p.y() - anchorAt.y(), p.x() - anchorAt.x()) - std::atan2(dragFrom_.y() - anchorAt.y(), dragFrom_.x() - anchorAt.x())) *
                                180.0 / std::numbers::pi;
            double rotation = o->transform.rotation + turn;
            if (shift) rotation = std::round(rotation / 15.0) * 15.0;
            o->transform.rotation = rotation;
        } else {
            // Scaling from the anchor point, by how far the handle went along each of the object's own axes (ours:
            // always from the anchor point). "Command (macOS) or Alt (Windows): Freeform scale; Shift: Constrained
            // scale (maintain aspect ratio)"; without them a text box becomes "a defined paragraph text box" (p.304).
            const QTransform back = place->inverted();
            const QPointF a = o->transform.anchor;
            const QPointF from = back.map(dragFrom_) - a, to = back.map(p) - a;
            const double fx = std::abs(from.x()) > 1e-6 ? to.x() / from.x() : 1.0;
            const double fy = std::abs(from.y()) > 1e-6 ? to.y() / from.y() : 1.0;
            const double uniform = (std::abs(fx) + std::abs(fy)) / 2.0;
            const bool freeform = modifiers & Qt::AltModifier;
            if (o->kind == TitleObjectKind::Text && !freeform && !shift) {
                const auto outline = media::titleObjectOutline(dragStart_, id);
                const double width = o->size.width() > 0 ? o->size.width() : (outline ? outline->boundingRect().width() / (o->transform.scaleX / 100.0) : 100.0);
                o->size = QSizeF(std::max(10.0, width * std::abs(fx)), 0.0);
            } else if (o->kind == TitleObjectKind::Text || o->kind == TitleObjectKind::Group) {
                o->transform.scaleX *= shift ? uniform : std::abs(fx);
                o->transform.scaleY *= shift ? uniform : std::abs(fy);
            } else {
                o->size = QSizeF(o->size.width() * (shift ? uniform : std::abs(fx)), o->size.height() * (shift ? uniform : std::abs(fy)));
            }
        }
    }
    preview(t);
}

void TitlerTool::overlayRelease(const QPointF& at, Qt::KeyboardModifiers modifiers) {
    if (drag_ == Drag::None) return;
    overlayMove(at, modifiers);
    const Drag drag = drag_;
    drag_ = Drag::None;
    if (drag == Drag::Highlight) return; // highlighting is no change to the title
    if (!working_) return;
    if (drag == Drag::Draw) {
        const TitleObject* o = selected_.isEmpty() ? nullptr : working_->object(selected_.first());
        if (!o || o->size.width() < 4.0 || o->size.height() < 4.0) {
            // Too small to be meant (ours).
            working_.reset();
            selected_.clear();
            editor_.cancelTitlePreview();
            emit changed();
            return;
        }
        commit(o->kind == TitleObjectKind::Ellipse ? tr("Ellipse") : tr("Rectangle"), *working_);
        return;
    }
    if (*working_ == dragStart_) {
        working_.reset();
        editor_.cancelTitlePreview();
        return;
    }
    commit(drag == Drag::Move ? tr("Move Title Object") : drag == Drag::Rotate ? tr("Rotate Title Object") : tr("Scale Title Object"), *working_);
}

bool TitlerTool::overlayWantsKey(const QKeyEvent* event) const {
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if (!editing_.isEmpty()) {
        // Typing takes the keys without Ctrl (Command) but Ctrl+A.
        return !(mods & Qt::ControlModifier) || event->key() == Qt::Key_A;
    }
    switch (event->key()) {
    case Qt::Key_G: return mods & Qt::ControlModifier;
    case Qt::Key_T:
    case Qt::Key_M: return mods == Qt::NoModifier;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
    case Qt::Key_Minus:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
    case Qt::Key_Escape: return !selected_.isEmpty() || event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown;
    default: return false;
    }
}

bool TitlerTool::overlayKey(QKeyEvent* event) {
    if (!overlayWantsKey(event)) return false;
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if (!editing_.isEmpty()) {
        Title t = title();
        TitleObject* o = t.object(editing_);
        if (!o) {
            editing_.clear();
            return false;
        }
        const QString text = o->text;
        const qsizetype from = std::min(anchor_, caret_), to = std::max(anchor_, caret_);
        // Text changes keep the formatted characters' runs with their characters (6.8c2).
        auto replace = [&](const QString& with) {
            model::replaceText(*o, from, to, with);
            caret_ = anchor_ = from + with.size();
        };
        const bool extend = mods & Qt::ShiftModifier;
        switch (event->key()) {
        case Qt::Key_Escape: finishEditing(); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter: replace(QStringLiteral("\n")); break;
        case Qt::Key_Backspace:
            if (from == to && from > 0) {
                model::replaceText(*o, from - 1, from, QString());
                caret_ = anchor_ = from - 1;
            } else {
                replace(QString());
            }
            break;
        case Qt::Key_Delete:
            if (from == to && from < text.size()) model::replaceText(*o, from, from + 1, QString());
            else replace(QString());
            break;
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Home:
        case Qt::Key_End:
            // Shift extends the selection (ours: Home and End go to the text's start and end).
            if (event->key() == Qt::Key_Left) caret_ = std::max<qsizetype>(0, caret_ - 1);
            if (event->key() == Qt::Key_Right) caret_ = std::min(text.size(), caret_ + 1);
            if (event->key() == Qt::Key_Home) caret_ = 0;
            if (event->key() == Qt::Key_End) caret_ = text.size();
            if (!extend) anchor_ = caret_;
            break;
        case Qt::Key_A:
            if (mods & Qt::ControlModifier) {
                anchor_ = 0;
                caret_ = text.size();
                break;
            }
            [[fallthrough]];
        default:
            if (event->text().isEmpty() || !event->text().at(0).isPrint()) return true;
            replace(event->text());
            break;
        }
        preview(t);
        editor_.refreshTitleOverlay();
        return true;
    }
    switch (event->key()) {
    // "pressing T on your keyboard"; "pressing M on your keyboard" (p.303-304).
    case Qt::Key_T: setTool(Tool::Text); return true;
    case Qt::Key_M: setTool(Tool::Selection); return true;
    // "use the "+" keyboard shortcut ... use the "-" keyboard shortcut" (p.318).
    case Qt::Key_Plus:
    case Qt::Key_Equal: bringForward(); return true;
    case Qt::Key_Minus: sendBackward(); return true;
    // "Command+G (macOS) or Ctrl+G (Windows) ... Command+Shift+G (macOS) or Ctrl+Shift+G (Windows)" (p.318).
    case Qt::Key_G:
        if (mods & Qt::ShiftModifier) ungroup();
        else group();
        return true;
    case Qt::Key_PageUp: selectAdjacent(true); return true;
    case Qt::Key_PageDown: selectAdjacent(false); return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: removeSelected(); return true;
    case Qt::Key_Escape: select({}); return true;
    default: return false;
    }
}

bool TitlerTool::applyPreset(const model::EffectTemplate& preset, model::TitlePresetPart part) {
    if (!model::isTitlerEffect(preset.effect) || selected_.isEmpty()) return false;
    finishEditing();
    const Title source = model::titleOf(preset.effect);
    Title t = title();
    bool any = false;
    for (const QString& id : selected_) {
        if (TitleObject* o = t.object(id)) any = model::applyTitlePreset(*o, source, part) || any;
    }
    return any && commit(tr("Title Designer Template"), t);
}

std::optional<model::EffectTemplate> TitlerTool::droppedTemplate(const QMimeData* mime) const {
    return editor_.droppedTemplate(mime);
}

bool TitlerTool::setRollCrawl(bool on) {
    finishEditing();
    const bool done = editor_.setTitleRollCrawl(on);
    emit changed();
    return done;
}

QStringList TitlerTool::trackerNames() const {
    return editor_.trackerNames();
}

bool TitlerTool::setKeyframes(bool add) {
    const bool done = editor_.setTitleKeyframes(add, selected_.isEmpty());
    emit changed();
    return done;
}

bool TitlerTool::bringForward() {
    finishEditing();
    Title t = title();
    bool any = false;
    for (const QString& id : selected_) any = model::bringForward(t, id) || any;
    return any && commit(tr("Bring Forward"), t);
}

bool TitlerTool::sendBackward() {
    finishEditing();
    Title t = title();
    bool any = false;
    for (const QString& id : selected_) any = model::sendBackward(t, id) || any;
    return any && commit(tr("Send Backward"), t);
}

bool TitlerTool::group() {
    finishEditing();
    Title t = title();
    const auto id = model::groupObjects(t, selected_);
    if (!id) return false;
    selected_ = {*id};
    return commit(tr("Group"), t);
}

bool TitlerTool::ungroup() {
    finishEditing();
    Title t = title();
    if (selected_.size() != 1) return false;
    const TitleObject* g = t.object(selected_.first());
    if (!g || g->kind != TitleObjectKind::Group) return false;
    QStringList children;
    for (const TitleObject& c : g->children) children << c.id;
    if (!model::ungroupObject(t, selected_.first())) return false;
    selected_ = children;
    return commit(tr("Ungroup"), t);
}

bool TitlerTool::removeSelected() {
    finishEditing();
    Title t = title();
    bool any = false;
    for (const QString& id : selected_) any = model::removeObject(t, id) || any;
    selected_.clear();
    return any && commit(tr("Delete Title Object"), t);
}

bool TitlerTool::selectAdjacent(bool next) {
    finishEditing();
    const Title t = title();
    if (t.objects.isEmpty()) return false;
    // Among the top-level objects, back to front; wrapping round (ours).
    qsizetype at = -1;
    for (qsizetype i = 0; i < t.objects.size(); ++i) {
        if (!selected_.isEmpty() && t.objects.at(i).id == selected_.last()) at = i;
    }
    const auto n = t.objects.size();
    const qsizetype pick = at < 0 ? (next ? 0 : n - 1) : ((at + (next ? 1 : -1)) % n + n) % n;
    select({t.objects.at(pick).id});
    return true;
}

bool TitlerTool::modifySelected(const QString& description, const std::function<void(TitleObject&)>& change) {
    Title t = title();
    bool any = false;
    // With characters highlighted in the text being typed, their properties change and not the whole text's
    // ("specific properties may be applied to the highlighted portions of text", p.304; 6.8c2).
    if (const auto range = highlighted()) {
        if (TitleObject* o = t.object(editing_)) any = model::changeCharacters(*o, range->first, range->second, change);
    } else {
        for (const QString& id : selected_) {
            if (TitleObject* o = t.object(id)) {
                change(*o);
                any = true;
            }
        }
    }
    if (!any || t == editor_.storedTitle().value_or(Title{})) return false;
    // During typing the change joins the text being typed (its undo step comes when typing ends).
    if (!editing_.isEmpty()) {
        preview(t);
        return true;
    }
    return commit(description, t);
}

std::optional<std::pair<qsizetype, qsizetype>> TitlerTool::highlighted() const {
    if (editing_.isEmpty() || anchor_ == caret_) return std::nullopt;
    return std::pair{std::min(anchor_, caret_), std::max(anchor_, caret_)};
}

std::optional<TitleObject> TitlerTool::shownObject() const {
    if (selected_.isEmpty()) return std::nullopt;
    const Title t = title();
    const QString id = highlighted() ? editing_ : selected_.first();
    const TitleObject* o = t.object(id);
    if (!o) return std::nullopt;
    if (const auto range = highlighted()) return model::characterView(*o, range->first);
    return *o;
}

bool TitlerTool::modifyTitle(const QString& description, const std::function<void(Title&)>& change) {
    Title t = title();
    change(t);
    if (t == editor_.storedTitle().value_or(Title{})) return false;
    if (!editing_.isEmpty()) {
        preview(t);
        return true;
    }
    return commit(description, t);
}

bool TitlerTool::align(Align how) {
    finishEditing();
    Title t = title();
    QRectF frame;
    {
        // The frame in title pixels; with the Grid on, its Safe Title area first: "If the Grid overlay is enabled, the
        // first time you click on an alignment button, it will align to the grid. The second time ... to the frame
        // edges" (p.312).
        const QSizeF size = editor_.previewSize();
        const double width = size.height() > 0 ? 1080.0 * size.width() / size.height() : 1920.0;
        frame = QRectF(-width / 2.0, -540.0, width, 1080.0);
    }
    QRectF reference = frame;
    QList<std::pair<QString, QRectF>> bounds;
    QRectF all;
    for (const QString& id : selected_) {
        if (const auto outline = media::titleObjectOutline(t, id)) {
            bounds << std::pair{id, outline->boundingRect()};
            all = all.united(outline->boundingRect());
        }
    }
    if (bounds.isEmpty()) return false;
    // "If multiple objects are selected, alignment is relative to the selected objects, not to the frame" (p.312);
    // the two Center options centre on the frame always (ours).
    const bool onFrame = how == Align::HorizontalCenter || how == Align::VerticalCenter;
    if (bounds.size() > 1 && !onFrame) reference = all;
    if (grid_ && bounds.size() == 1 && !onFrame) {
        const QRectF safe(frame.center() - QPointF(frame.width() * 0.4, frame.height() * 0.4), frame.size() * 0.8);
        const QRectF b = bounds.first().second;
        const bool atSafe = (how == Align::Left && std::abs(b.left() - safe.left()) < 0.5) || (how == Align::Right && std::abs(b.right() - safe.right()) < 0.5) ||
                            (how == Align::Top && std::abs(b.top() - safe.top()) < 0.5) || (how == Align::Bottom && std::abs(b.bottom() - safe.bottom()) < 0.5);
        if (!atSafe) reference = safe;
    }
    for (const auto& [id, b] : bounds) {
        QPointF by;
        switch (how) {
        case Align::Left: by.setX(reference.left() - b.left()); break;
        case Align::Right: by.setX(reference.right() - b.right()); break;
        case Align::Center:
        case Align::HorizontalCenter: by.setX(reference.center().x() - b.center().x()); break;
        case Align::Top: by.setY(reference.top() - b.top()); break;
        case Align::Bottom: by.setY(reference.bottom() - b.bottom()); break;
        case Align::Middle:
        case Align::VerticalCenter: by.setY(reference.center().y() - b.center().y()); break;
        }
        if (TitleObject* o = t.object(id)) o->transform.position += by;
    }
    if (t == editor_.storedTitle().value_or(Title{})) return false;
    return commit(tr("Align"), t);
}

namespace {

QPointF anchorPoint(const QRectF& b, TitlerTool::Anchor where) {
    switch (where) {
    case TitlerTool::Anchor::Center: return b.center();
    case TitlerTool::Anchor::Right: return {b.right(), b.center().y()};
    case TitlerTool::Anchor::Left: return {b.left(), b.center().y()};
    case TitlerTool::Anchor::TopCenter: return {b.center().x(), b.top()};
    case TitlerTool::Anchor::TopRight: return b.topRight();
    case TitlerTool::Anchor::TopLeft: return b.topLeft();
    case TitlerTool::Anchor::BottomCenter: return {b.center().x(), b.bottom()};
    case TitlerTool::Anchor::BottomRight: return b.bottomRight();
    case TitlerTool::Anchor::BottomLeft: return b.bottomLeft();
    }
    return b.center();
}

} // namespace

bool TitlerTool::setAnchor(Anchor where) {
    // "a presets submenu for Anchor Point Position is available for quick and easy placement" (p.313): the point on
    // the object's own bounds; the object shifts with its anchor ("Changing the Anchor Point Position will shift the
    // object relative to the anchor point", p.311).
    return modifySelected(tr("Anchor Point"), [&](TitleObject& o) { o.transform.anchor = anchorPoint(media::titleObjectBounds(o), where); });
}

std::optional<TitlerTool::Anchor> TitlerTool::anchorOf(const TitleObject& object) const {
    const QRectF b = media::titleObjectBounds(object);
    for (int k = 0; k <= static_cast<int>(Anchor::BottomLeft); ++k) {
        const QPointF p = anchorPoint(b, static_cast<Anchor>(k));
        if (std::abs(p.x() - object.transform.anchor.x()) < 0.01 && std::abs(p.y() - object.transform.anchor.y()) < 0.01) return static_cast<Anchor>(k);
    }
    return std::nullopt;
}

void TitlerTool::setGridShown(bool shown) {
    grid_ = shown;
    emit changed();
    editor_.refreshTitleOverlay();
}

void TitlerTool::fillOverlay(Composer::EffectPreview& preview) const {
    const Title t = title();
    if (grid_) {
        // "the Safe Action/Title grid" (p.319): ours, the areas 90% and 80% of the frame.
        for (const double share : {0.9, 0.8}) {
            const double m = (1.0 - share) / 2.0;
            preview.guides << QList<QPointF>{QPointF(m, m), QPointF(1.0 - m, m), QPointF(1.0 - m, 1.0 - m), QPointF(m, 1.0 - m)};
        }
    }
    auto fractions = [&](const QPolygonF& polygon) {
        QList<QPointF> out;
        for (const QPointF& p : polygon) out << toFraction(p);
        return out;
    };
    for (const QString& id : selected_) {
        const auto outline = media::titleObjectOutline(t, id);
        const auto place = media::titleObjectPlacement(t, id);
        const TitleObject* o = t.object(id);
        if (!outline || !place || !o) continue;
        preview.boxes << fractions(*outline);
        // Anchor points "appear in the viewer as crosshairs on selected objects" (p.313).
        preview.anchors << toFraction(place->map(o->transform.anchor));
        if (selected_.size() == 1 && editing_.isEmpty() && tool_ == Tool::Selection) {
            for (int k = 0; k < outline->size() && k < 4; ++k) {
                preview.handles << Composer::EffectPreview::Handle{QStringLiteral("titler:corner:%1").arg(k), toFraction(outline->at(k)), std::nullopt};
            }
        }
        if (id == editing_) {
            // "Highlighted text is indicated by an overlay" (p.304), and the text cursor.
            for (const QPolygonF& r : media::titleTextRange(*o, anchor_, caret_)) preview.highlights << fractions(place->map(r));
            const QLineF caret = place->map(media::titleTextCaret(*o, caret_));
            preview.caret = std::pair{toFraction(caret.p1()), toFraction(caret.p2())};
        }
    }
}

} // namespace lumenis::ui
