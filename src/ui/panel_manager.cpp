#include "lumenis/ui/panels.h"

#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QCursor>
#include <QJsonArray>
#include <QKeyEvent>
#include <QScreen>
#include <QSplitter>

#include <algorithm>

namespace lumenis::ui {

namespace {

constexpr int kLayoutVersion = 1;
const QString kBlankPrefix = QStringLiteral("Blank");

Panel* panelAncestor(QObject* object) {
    for (auto* w = qobject_cast<QWidget*>(object); w; w = w->parentWidget()) {
        if (auto* p = qobject_cast<Panel*>(w)) return p;
    }
    return nullptr;
}

QJsonArray rectToJson(const QRect& r) {
    return QJsonArray{r.x(), r.y(), r.width(), r.height()};
}

QRect rectFromJson(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    if (a.size() != 4) return {};
    return QRect(a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt());
}

} // namespace

PanelManager::PanelManager(QWidget* owner) : QObject(owner), owner_(owner) {
    mainHost_ = new DockHost(*this);
    mainHost_->setObjectName(QStringLiteral("mainHost"));
    qApp->installEventFilter(this);
}

PanelManager::~PanelManager() {
    qApp->removeEventFilter(this);
    for (Panel* p : QList<Panel*>(panels_)) {
        if (!p->parentWidget()) delete p;
    }
}

QList<FloatingWindow*> PanelManager::floatingWindows() const {
    return floating_;
}

void PanelManager::addPanel(Panel* panel) {
    if (!panel || panels_.contains(panel)) return;
    panels_ << panel;
    connect(panel, &QObject::destroyed, this, [this, panel] {
        panels_.removeAll(panel);
        if (active_ == panel) active_ = nullptr;
    });
    if (!panel->group()) {
        panel->setParent(nullptr);
        panel->hide();
    }
}

Panel* PanelManager::panel(const QString& id) const {
    for (Panel* p : panels_) {
        if (p->id() == id) return p;
    }
    return nullptr;
}

QList<Panel*> PanelManager::panels() const {
    return panels_;
}

bool PanelManager::isOpen(const Panel* panel) const {
    return panel && panel->group();
}

bool PanelManager::isFloating(const Panel* panel) const {
    const PanelGroup* g = panel ? panel->group() : nullptr;
    return g && g->host() != mainHost_;
}

void PanelManager::activate(Panel* panel) {
    if (!panel || !panel->group()) return;
    PanelGroup* group = panel->group();
    group->setCurrent(panel);
    for (PanelGroup* g : allGroups()) g->setActive(g == group);
    if (active_ == panel) return;
    active_ = panel;
    emit activePanelChanged(panel);
}

// ---------------------------------------------------------------- moving

PanelGroup* PanelManager::newGroup() {
    return new PanelGroup(*this);
}

Panel* PanelManager::newBlankPanel(const QString& requestedId) {
    QString id = requestedId;
    if (id.isEmpty()) {
        do {
            id = kBlankPrefix + QString::number(nextBlank_++);
        } while (panel(id));
    } else {
        bool ok = false;
        const int n = id.mid(kBlankPrefix.size()).toInt(&ok);
        if (ok) nextBlank_ = std::max(nextBlank_, n + 1);
    }
    auto* blank = new Panel(id, tr("Blank Panel"));
    blank->blank_ = true;
    addPanel(blank);
    return blank;
}

void PanelManager::forget(Panel* panel) {
    panels_.removeAll(panel);
    for (auto it = blankHomes_.begin(); it != blankHomes_.end();) {
        it = it.value() == panel->id() ? blankHomes_.erase(it) : std::next(it);
    }
    if (active_ == panel) active_ = nullptr;
    panel->hide();
    panel->deleteLater();
}

void PanelManager::detach(Panel* panel) {
    PanelGroup* group = panel ? panel->group() : nullptr;
    if (!group) return;
    group->takePanel(panel);
    discardGroupIfEmpty(group);
}

void PanelManager::detachGroup(PanelGroup* group) {
    if (DockHost* host = group->host()) host->remove(group);
}

void PanelManager::discardGroupIfEmpty(PanelGroup* group) {
    if (!group || group->count() > 0) return;
    detachGroup(group);
    group->hide();
    group->deleteLater();
}

void PanelManager::removeEmptyFloatingWindows() {
    for (FloatingWindow* w : QList<FloatingWindow*>(floating_)) {
        if (w->host()->isEmpty()) {
            floating_.removeAll(w);
            w->hide();
            w->deleteLater();
        }
    }
}

PanelGroup* PanelManager::takeForMove(PanelGroup* group, Panel* panel) {
    if (panel && panel->group() && panel->group()->count() > 1) {
        detach(panel);
        PanelGroup* moving = newGroup();
        moving->addPanel(panel);
        return moving;
    }
    if (!group && panel) group = panel->group();
    if (group) {
        detachGroup(group);
        return group;
    }
    // A closed panel: a group of its own.
    PanelGroup* moving = newGroup();
    moving->addPanel(panel);
    return moving;
}

void PanelManager::finishMove(PanelGroup* moved) {
    removeEmptyFloatingWindows();
    activate(moved->current());
    changed();
}

void PanelManager::dock(Panel* panel, PanelGroup* target, DockSide side, double fraction) {
    if (!panel || !target || !target->host()) return;
    if (panel->group() == target && target->count() == 1) return;
    DockHost* host = target->host();
    PanelGroup* moving = takeForMove(nullptr, panel);
    host->insert(moving, target, side, fraction);
    finishMove(moving);
}

void PanelManager::dockGroup(PanelGroup* group, PanelGroup* target, DockSide side, double fraction) {
    if (!group || !target || group == target || !target->host()) return;
    DockHost* host = target->host();
    PanelGroup* moving = takeForMove(group, nullptr);
    host->insert(moving, target, side, fraction);
    finishMove(moving);
}

void PanelManager::dockAtEdge(Panel* panel, DockHost* host, DockSide side, double fraction) {
    if (!panel || !host) return;
    PanelGroup* moving = takeForMove(nullptr, panel);
    host->insertAtEdge(moving, side, fraction);
    finishMove(moving);
}

void PanelManager::tab(Panel* panel, PanelGroup* target) {
    if (!panel || !target || panel->group() == target) return;
    detach(panel);
    target->addPanel(panel);
    removeEmptyFloatingWindows();
    activate(panel);
    changed();
}

FloatingWindow* PanelManager::newFloatingWindow(const QRect& requested) {
    QRect geometry = requested;
    // Keep it reachable: on a screen that exists now.
    const QScreen* screen = QGuiApplication::screenAt(geometry.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
        if (screen) geometry.moveCenter(screen->availableGeometry().center());
    }
    auto* window = new FloatingWindow(*this, owner_);
    window->setGeometry(geometry);
    floating_ << window;
    connect(window, &QObject::destroyed, this, [this, window] { floating_.removeAll(window); });
    if (owner_->isVisible()) window->show();
    emit floatingWindowCreated(window);
    return window;
}

QRect PanelManager::defaultFloatingGeometry(const QSize& size) const {
    QRect r(QPoint(), size);
    if (owner_->isVisible()) {
        r.moveCenter(owner_->geometry().center());
    } else if (const QScreen* screen = QGuiApplication::primaryScreen()) {
        r.moveCenter(screen->availableGeometry().center());
    }
    return r;
}

FloatingWindow* PanelManager::floatPanel(Panel* panel, const QRect& geometry) {
    if (!panel) return nullptr;
    PanelGroup* group = panel->group();
    if (group && group->count() == 1) return floatGroup(group, geometry);
    QRect g = geometry;
    if (!g.isValid()) {
        g = group && panel->isVisible()
                ? QRect(panel->mapToGlobal(QPoint(-kTitleBarWidth, 0)), panel->size() + QSize(kTitleBarWidth, 0))
                : defaultFloatingGeometry(QSize(720, 420));
    }
    PanelGroup* moving = takeForMove(nullptr, panel);
    FloatingWindow* window = newFloatingWindow(g);
    window->host()->fill(moving);
    finishMove(moving);
    return window;
}

FloatingWindow* PanelManager::floatGroup(PanelGroup* group, const QRect& geometry) {
    if (!group) return nullptr;
    QRect g = geometry;
    if (!g.isValid()) {
        g = group->isVisible() ? QRect(group->mapToGlobal(QPoint()), group->size())
                               : defaultFloatingGeometry(QSize(720, 420));
    }
    // Already alone in a floating window: move that window.
    for (FloatingWindow* w : floatingWindows()) {
        if (w->host()->root() == group) {
            if (geometry.isValid()) w->setGeometry(geometry);
            activate(group->current());
            return w;
        }
    }
    PanelGroup* moving = takeForMove(group, nullptr);
    FloatingWindow* window = newFloatingWindow(g);
    window->host()->fill(moving);
    finishMove(moving);
    return window;
}

void PanelManager::floatAll() {
    // Measure first: each removal changes the size of the rest.
    QList<std::pair<PanelGroup*, QRect>> groups;
    for (PanelGroup* g : mainHost_->groups()) {
        groups.append({g, g->isVisible() ? QRect(g->mapToGlobal(QPoint()), g->size()) : QRect()});
    }
    QPointer<Panel> active = active_;
    for (const auto& [g, r] : groups) floatGroup(g, r);
    if (active) activate(active);
}

void PanelManager::floatActive() {
    if (isOpen(active_)) floatPanel(active_);
}

Panel* PanelManager::addBlankPanel() {
    Panel* blank = newBlankPanel();
    PanelGroup* group = newGroup();
    group->addPanel(blank);
    PanelGroup* target = isOpen(active_) ? active_->group() : nullptr;
    if (target && target->host()) {
        target->host()->insert(group, target, DockSide::Right, 0.5);
    } else {
        mainHost_->insertAtEdge(group, DockSide::Right, 0.33);
    }
    activate(blank);
    changed();
    return blank;
}

void PanelManager::closePanel(Panel* panel) {
    PanelGroup* group = panel ? panel->group() : nullptr;
    if (!group) return;
    const bool docked = group->host() == mainHost_;
    if (blankOnClose_ && docked && !panel->isBlank() && group->count() == 1) {
        Panel* blank = newBlankPanel();
        group->addPanel(blank);
        blankHomes_.insert(panel->id(), blank->id());
    }
    group->takePanel(panel);
    if (panel->isBlank()) forget(panel);
    const bool groupSurvives = group->count() > 0;
    discardGroupIfEmpty(group);
    removeEmptyFloatingWindows();
    if (!isOpen(active_)) {
        active_ = nullptr;
        if (groupSurvives) {
            activate(group->current());
        } else if (const QList<PanelGroup*> groups = allGroups(); !groups.isEmpty()) {
            activate(groups.first()->current());
        }
    }
    changed();
}

void PanelManager::showPanel(Panel* panel) {
    if (!panel) return;
    if (isOpen(panel)) {
        activate(panel);
        panel->window()->raise();
        panel->window()->activateWindow();
        return;
    }
    Panel* blank = this->panel(blankHomes_.take(panel->id()));
    if (blank && blank->group()) {
        PanelGroup* group = blank->group();
        group->insertPanel(static_cast<int>(group->panels().indexOf(blank)), panel);
        group->takePanel(blank);
        forget(blank);
        activate(panel);
        changed();
        return;
    }
    floatPanel(panel, defaultFloatingGeometry(QSize(720, 420)));
}

void PanelManager::floatingWindowClosed(FloatingWindow* window) {
    for (PanelGroup* g : window->host()->groups()) {
        for (Panel* p : QList<Panel*>(g->panels())) {
            g->takePanel(p);
            if (p->isBlank()) forget(p);
        }
    }
    floating_.removeAll(window);
    window->deleteLater();
    if (!isOpen(active_)) {
        active_ = nullptr;
        if (const QList<PanelGroup*> groups = allGroups(); !groups.isEmpty()) activate(groups.first()->current());
    }
    changed();
}

void PanelManager::cycleWindows(int direction) {
    const QList<PanelGroup*> groups = allGroups();
    if (groups.isEmpty()) return;
    const auto n = groups.size();
    const auto current = isOpen(active_) ? groups.indexOf(active_->group()) : -1;
    const auto next = current < 0 ? 0 : ((current + direction) % n + n) % n;
    PanelGroup* g = groups.at(next);
    activate(g->current());
    g->window()->raise();
    g->window()->activateWindow();
}

void PanelManager::cycleTabs() {
    if (!isOpen(active_)) return;
    PanelGroup* g = active_->group();
    if (g->count() < 2) return;
    const auto index = g->panels().indexOf(active_);
    activate(g->panels().at((index + 1) % g->count()));
}

QList<DockHost*> PanelManager::hosts() const {
    QList<DockHost*> out{mainHost_};
    for (FloatingWindow* w : floatingWindows()) out << w->host();
    return out;
}

QList<PanelGroup*> PanelManager::allGroups() const {
    QList<PanelGroup*> out;
    for (DockHost* h : hosts()) out << h->groups();
    return out;
}

void PanelManager::changed() {
    for (FloatingWindow* w : floatingWindows()) w->updateTitle();
    emit layoutChanged();
}

// ---------------------------------------------------------------- layouts

QJsonObject PanelManager::nodeToJson(QWidget* node) const {
    if (auto* g = qobject_cast<PanelGroup*>(node)) {
        QJsonArray tabs;
        for (const Panel* p : g->panels()) tabs.append(p->id());
        return {{QStringLiteral("tabs"), tabs},
                {QStringLiteral("current"), static_cast<int>(g->panels().indexOf(g->current()))}};
    }
    if (auto* s = qobject_cast<QSplitter*>(node)) {
        QJsonArray sizes;
        for (int size : s->sizes()) sizes.append(size);
        QJsonArray children;
        for (int i = 0; i < s->count(); ++i) children.append(nodeToJson(s->widget(i)));
        return {{QStringLiteral("split"), s->orientation() == Qt::Horizontal ? QStringLiteral("h") : QStringLiteral("v")},
                {QStringLiteral("sizes"), sizes},
                {QStringLiteral("children"), children}};
    }
    return {};
}

QWidget* PanelManager::nodeFromJson(const QJsonObject& node, QList<Panel*>& placed) {
    if (node.contains(QStringLiteral("tabs"))) {
        const QJsonArray tabs = node.value(QStringLiteral("tabs")).toArray();
        PanelGroup* group = nullptr;
        Panel* current = nullptr;
        const int currentIndex = node.value(QStringLiteral("current")).toInt(-1);
        for (int i = 0; i < tabs.size(); ++i) {
            const QString id = tabs.at(i).toString();
            Panel* p = panel(id);
            if (!p && id.startsWith(kBlankPrefix)) p = newBlankPanel(id);
            if (!p || placed.contains(p)) continue;
            if (!group) group = newGroup();
            group->addPanel(p);
            placed << p;
            if (i == currentIndex) current = p;
        }
        if (group && current) group->setCurrent(current);
        return group;
    }
    if (node.contains(QStringLiteral("split"))) {
        const Qt::Orientation o =
            node.value(QStringLiteral("split")).toString() == QLatin1String("h") ? Qt::Horizontal : Qt::Vertical;
        const QJsonArray children = node.value(QStringLiteral("children")).toArray();
        const QJsonArray sizes = node.value(QStringLiteral("sizes")).toArray();
        QList<QWidget*> built;
        QList<int> keptSizes;
        for (int i = 0; i < children.size(); ++i) {
            if (QWidget* child = nodeFromJson(children.at(i).toObject(), placed)) {
                built << child;
                keptSizes << std::max(1, sizes.at(i).toInt(1));
            }
        }
        if (built.isEmpty()) return nullptr;
        if (built.size() == 1) return built.first();
        QSplitter* split = DockHost::createSplitter(o);
        for (QWidget* w : built) split->addWidget(w);
        DockHost::setProportions(split, keptSizes);
        return split;
    }
    return nullptr;
}

QJsonObject PanelManager::saveLayout() const {
    QJsonObject layout{{QStringLiteral("version"), kLayoutVersion}};
    layout.insert(QStringLiteral("main"), mainHost_->root() ? nodeToJson(mainHost_->root()) : QJsonObject());
    QJsonArray floating;
    for (FloatingWindow* w : floatingWindows()) {
        floating.append(QJsonObject{{QStringLiteral("geometry"), rectToJson(w->geometry())},
                                    {QStringLiteral("root"), nodeToJson(w->host()->root())}});
    }
    layout.insert(QStringLiteral("floating"), floating);
    layout.insert(QStringLiteral("active"), active_ ? active_->id() : QString());
    QJsonObject homes;
    for (auto it = blankHomes_.cbegin(); it != blankHomes_.cend(); ++it) homes.insert(it.key(), it.value());
    layout.insert(QStringLiteral("blankHomes"), homes);
    return layout;
}

void PanelManager::restoreLayout(const QJsonObject& layout) {
    if (layout.value(QStringLiteral("version")).toInt() != kLayoutVersion) return;
    cancelDrag();
    for (PanelGroup* g : allGroups()) {
        for (Panel* p : QList<Panel*>(g->panels())) g->takePanel(p);
    }
    if (QWidget* old = mainHost_->takeRoot()) old->deleteLater();
    for (FloatingWindow* w : floatingWindows()) {
        w->hide();
        w->deleteLater();
    }
    floating_.clear();
    blankHomes_.clear();
    active_ = nullptr;

    QList<Panel*> placed;
    if (QWidget* root = nodeFromJson(layout.value(QStringLiteral("main")).toObject(), placed)) {
        mainHost_->setRoot(root);
    }
    for (const QJsonValue& v : layout.value(QStringLiteral("floating")).toArray()) {
        const QJsonObject o = v.toObject();
        QWidget* root = nodeFromJson(o.value(QStringLiteral("root")).toObject(), placed);
        if (!root) continue;
        QRect g = rectFromJson(o.value(QStringLiteral("geometry")));
        if (!g.isValid()) g = defaultFloatingGeometry(QSize(720, 420));
        newFloatingWindow(g)->host()->setRoot(root);
    }
    const QJsonObject homes = layout.value(QStringLiteral("blankHomes")).toObject();
    for (auto it = homes.begin(); it != homes.end(); ++it) {
        if (Panel* blank = panel(it.value().toString()); blank && placed.contains(blank)) {
            blankHomes_.insert(it.key(), blank->id());
        }
    }
    for (Panel* p : panels()) {
        if (p->isBlank() && !placed.contains(p)) forget(p);
    }
    Panel* active = panel(layout.value(QStringLiteral("active")).toString());
    if (!active || !placed.contains(active)) active = placed.isEmpty() ? nullptr : placed.first();
    if (active) {
        activate(active);
    } else {
        emit activePanelChanged(nullptr);
    }
    changed();
}

// ---------------------------------------------------------------- dragging

void PanelManager::beginDrag(PanelGroup* group, Panel* panel) {
    if (!group) return;
    dragGroup_ = group;
    dragPanel_ = (panel && group->count() > 1) ? panel : nullptr;
    dragTabbing_ = false;
    hovered_ = {};
    for (DockHost* h : hosts()) {
        h->overlay()->setGeometry(h->rect());
        h->overlay()->raise();
        h->overlay()->show();
    }
}

void PanelManager::updateDrag(const QPoint& globalPos, bool tabbing) {
    if (!dragGroup_) return;
    dragTabbing_ = tabbing;
    hovered_ = dropTargetAt(globalPos, tabbing);
    refreshOverlays();
}

void PanelManager::cancelDrag() {
    dragGroup_ = nullptr;
    dragPanel_ = nullptr;
    dragTabbing_ = false;
    hovered_ = {};
    for (DockHost* h : hosts()) h->overlay()->hide();
}

void PanelManager::endDrag(const QPoint& globalPos, bool tabbing) {
    if (!dragGroup_) return;
    const DropTarget target = dropTargetAt(globalPos, tabbing);
    PanelGroup* group = dragGroup_;
    Panel* panel = dragPanel_;
    const QSize size = panel ? panel->size() + QSize(kTitleBarWidth, 0) : group->size();
    cancelDrag();

    switch (target.kind) {
    case DropTarget::Kind::None: {
        // Dropped away from every drop zone: the window floats (p.78).
        const QRect geometry(globalPos - QPoint(kTitleBarWidth / 2, 10), size.expandedTo(QSize(200, 120)));
        if (panel) {
            floatPanel(panel, geometry);
        } else {
            floatGroup(group, geometry);
        }
        break;
    }
    case DropTarget::Kind::Split:
        if (panel) {
            dock(panel, target.group, target.side);
        } else {
            dockGroup(group, target.group, target.side);
        }
        break;
    case DropTarget::Kind::Edge:
    case DropTarget::Kind::Fill: {
        PanelGroup* moving = takeForMove(panel ? nullptr : group, panel);
        if (target.kind == DropTarget::Kind::Fill) {
            target.host->fill(moving);
        } else {
            target.host->insertAtEdge(moving, target.side);
        }
        finishMove(moving);
        break;
    }
    case DropTarget::Kind::Tab:
        if (panel) {
            tab(panel, target.group);
        } else {
            Panel* current = group->current();
            for (Panel* p : QList<Panel*>(group->panels())) {
                group->takePanel(p);
                target.group->addPanel(p);
            }
            discardGroupIfEmpty(group);
            removeEmptyFloatingWindows();
            activate(current);
            changed();
        }
        break;
    }
}

QList<DropTarget> PanelManager::dropZones(const DockHost* host, bool tabbing) const {
    QList<DropTarget> zones;
    if (!dragGroup_ || !host) return zones;
    auto* mutableHost = const_cast<DockHost*>(host);
    const QRect hr = host->rect();
    const QList<PanelGroup*> groups = host->groups();
    const bool movingWhole = !dragPanel_;

    if (groups.isEmpty()) {
        if (!tabbing) {
            zones.append({DropTarget::Kind::Fill, mutableHost, nullptr, DockSide::Left, hr.adjusted(8, 8, -8, -8), hr});
        }
        return zones;
    }
    if (tabbing) {
        // Alt-drag onto another window's title bar tabs the windows (p.76).
        for (PanelGroup* g : groups) {
            if (g == dragGroup_) continue;
            const QRect r(g->titleBar()->mapTo(host, QPoint()), g->titleBar()->size());
            zones.append({DropTarget::Kind::Tab, mutableHost, g, DockSide::Left, r, r});
        }
        return zones;
    }
    // Moving the only group of a host next to itself changes nothing.
    if (movingWhole && groups.size() == 1 && groups.first() == dragGroup_) return zones;

    // Along the host's edges: dock across its whole width or height.
    const int t = 10;
    const int w = hr.width();
    const int h = hr.height();
    zones.append({DropTarget::Kind::Edge, mutableHost, nullptr, DockSide::Left, QRect(0, h / 4, t, h / 2),
                  QRect(0, 0, w / 3, h)});
    zones.append({DropTarget::Kind::Edge, mutableHost, nullptr, DockSide::Right, QRect(w - t, h / 4, t, h / 2),
                  QRect(w - w / 3, 0, w / 3, h)});
    zones.append({DropTarget::Kind::Edge, mutableHost, nullptr, DockSide::Top, QRect(w / 4, 0, w / 2, t),
                  QRect(0, 0, w, h / 3)});
    zones.append({DropTarget::Kind::Edge, mutableHost, nullptr, DockSide::Bottom, QRect(w / 4, h - t, w / 2, t),
                  QRect(0, h - h / 3, w, h / 3)});

    // Next to each window.
    for (PanelGroup* g : groups) {
        if (movingWhole && g == dragGroup_) continue;
        const QRect r(g->mapTo(host, QPoint()), g->size());
        const int inset = 14;
        const int th = std::clamp(std::min(r.width(), r.height()) / 6, 8, 22);
        const int lh = r.height() / 2;
        const int lw = r.width() / 2;
        const int cx = r.left() + r.width() / 2;
        const int cy = r.top() + r.height() / 2;
        zones.append({DropTarget::Kind::Split, mutableHost, g, DockSide::Left,
                      QRect(r.left() + inset, cy - lh / 2, th, lh), QRect(r.left(), r.top(), r.width() / 2, r.height())});
        zones.append({DropTarget::Kind::Split, mutableHost, g, DockSide::Right,
                      QRect(r.right() - inset - th + 1, cy - lh / 2, th, lh),
                      QRect(r.left() + r.width() - r.width() / 2, r.top(), r.width() / 2, r.height())});
        zones.append({DropTarget::Kind::Split, mutableHost, g, DockSide::Top,
                      QRect(cx - lw / 2, r.top() + inset, lw, th), QRect(r.left(), r.top(), r.width(), r.height() / 2)});
        zones.append({DropTarget::Kind::Split, mutableHost, g, DockSide::Bottom,
                      QRect(cx - lw / 2, r.bottom() - inset - th + 1, lw, th),
                      QRect(r.left(), r.top() + r.height() - r.height() / 2, r.width(), r.height() / 2)});
    }
    return zones;
}

DropTarget PanelManager::dropTargetAt(const QPoint& globalPos, bool tabbing) const {
    if (!dragGroup_) return {};
    // Floating windows sit above the main window; the newest on top.
    QList<DockHost*> order;
    const QList<FloatingWindow*> windows = floatingWindows();
    for (auto it = windows.crbegin(); it != windows.crend(); ++it) order << (*it)->host();
    order << mainHost_;
    for (DockHost* host : order) {
        if (!host->isVisible()) continue;
        const QPoint local = host->mapFromGlobal(globalPos);
        if (!host->rect().contains(local)) continue;
        for (const DropTarget& zone : dropZones(host, tabbing)) {
            if (zone.zone.contains(local)) return zone;
        }
        return {};
    }
    return {};
}

void PanelManager::refreshOverlays() {
    for (DockHost* h : hosts()) h->overlay()->update();
}

bool PanelManager::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        if (Panel* p = panelAncestor(watched); p && p != active_ && panels_.contains(p)) {
            activate(p);
        }
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        if (isDragging()) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape && event->type() == QEvent::KeyPress) {
                cancelDrag();
                return true;
            }
            if (key->key() == Qt::Key_Alt) {
                updateDrag(QCursor::pos(), event->type() == QEvent::KeyPress);
            }
        }
        break;
    case QEvent::Show:
    case QEvent::Hide:
        // Floating windows come and go with the main window.
        if (watched == owner_) {
            for (FloatingWindow* w : floatingWindows()) w->setVisible(event->type() == QEvent::Show);
        }
        break;
    default:
        break;
    }
    return false;
}

} // namespace lumenis::ui
