#include "lumenis/ui/bin_dock.h"

#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QIcon>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

// Docking bins inside a Bin Container (guide p.241-243): the tab bar that lets
// a tab be dragged out, the tab groups, and BinDockArea, which keeps the groups
// in a tree of QSplitters and works out the green drop zones. The area only
// arranges widgets and reports what the user did through signals;
// BinContainer (bin_container.cpp) decides what happens to the bins. The
// colours and sizes (kDropZoneColor, kSplitterHandle) are in theme.h.

namespace lumenis::ui {

// MIME types for drags between bins, the Sidebar and the Source Browser. The
// formats of their data are described in bin_dock.h.
QString binTabMime() {
    return QStringLiteral("application/x-cutthulhu-bin-tab");
}

QString binNodesMime() {
    return QStringLiteral("application/x-cutthulhu-bin-nodes");
}

QString binItemsMime() {
    return QStringLiteral("application/x-cutthulhu-bin-items");
}

QString mediaFilesMime() {
    return QStringLiteral("application/x-cutthulhu-media-files");
}

QString audioSuiteTemplateMime() {
    return QStringLiteral("application/x-cutthulhu-audiosuite-template");
}

QString customColorMime() {
    return QStringLiteral("application/x-cutthulhu-custom-color");
}

QString eqTemplateMime() {
    return QStringLiteral("application/x-cutthulhu-eq-template");
}

QString effectTemplateMime() {
    return QStringLiteral("application/x-cutthulhu-effect-template");
}

bool isCopyDrop(Qt::DropAction action, Qt::KeyboardModifiers modifiers) {
    return action == Qt::CopyAction || (modifiers & (Qt::AltModifier | Qt::ControlModifier));
}

namespace {

// Pixels the pointer must leave the tab bar by before a tab is dragged out
// rather than moved along the bar.
constexpr int kDragOutDistance = 12;

// The green drop zones (p.241), drawn over the area while dragging.
class DropOverlay : public QWidget {
public:
    explicit DropOverlay(BinDockArea& area) : QWidget(&area), area_(area) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const BinDockArea::Zone& z = area_.hoveredZone();
        if (!z.isValid()) return;
        QPainter p(this);
        QColor fill(kDropZoneColor);
        fill.setAlpha(110);
        p.fillRect(z.rect, fill);
        p.setPen(QPen(QColor(kDropZoneColor), 2));
        p.drawRect(z.rect.adjusted(1, 1, -1, -1));
    }

private:
    BinDockArea& area_;
};

// A splitter between groups. Its sides cannot be collapsed to nothing, so a
// group can always be seen and dropped on.
QSplitter* newSplitter(Qt::Orientation orientation) {
    auto* s = new QSplitter(orientation);
    s->setObjectName(QStringLiteral("binSplitter"));
    s->setChildrenCollapsible(false);
    s->setHandleWidth(kSplitterHandle);
    return s;
}

} // namespace

// ---------------------------------------------------------------- BinTabBar

BinTabBar::BinTabBar(QWidget* parent) : QTabBar(parent) {
    setObjectName(QStringLiteral("binTabBar"));
}

// Remembers which tab a left press was on, so that a later move can tell a
// drag out from a move along the bar.
void BinTabBar::mousePressEvent(QMouseEvent* event) {
    pressIndex_ = event->button() == Qt::LeftButton ? tabAt(event->position().toPoint()) : -1;
    pressPos_ = event->position().toPoint();
    QTabBar::mousePressEvent(event);
}

void BinTabBar::mouseMoveEvent(QMouseEvent* event) {
    const QPoint p = event->position().toPoint();
    if (pressIndex_ >= 0 && (event->buttons() & Qt::LeftButton) &&
        (p.y() < -kDragOutDistance || p.y() > height() + kDragOutDistance)) {
        // Out of the bar: end the bar's own tab move, then drag the bin. A
        // synthetic release ends QTabBar's movable-tab drag, so the bar is not
        // left mid-move while the bin is dragged.
        const int index = pressIndex_;
        pressIndex_ = -1;
        QMouseEvent release(QEvent::MouseButtonRelease, pressPos_, mapToGlobal(pressPos_), Qt::LeftButton,
                            Qt::NoButton, event->modifiers());
        QTabBar::mouseReleaseEvent(&release);
        emit dragOut(index);
        return;
    }
    QTabBar::mouseMoveEvent(event);
}

void BinTabBar::mouseReleaseEvent(QMouseEvent* event) {
    pressIndex_ = -1;
    QTabBar::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------- BinTabGroup

BinTabGroup::BinTabGroup(BinDockArea& area) : QTabWidget(&area), area_(area) {
    setObjectName(QStringLiteral("binTabs"));
    auto* bar = new BinTabBar(this);
    setTabBar(bar);
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(true);
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);

    // Every open bin, including those whose tabs do not fit (p.259).
    auto* menuButton = new QToolButton(this);
    menuButton->setObjectName(QStringLiteral("binTabMenu"));
    menuButton->setIcon(glyphIcon(GlyphIcon::FastMenu));
    menuButton->setToolTip(tr("Tab Menu"));
    menuButton->setAutoRaise(true);
    menuButton->setFixedSize(26, kToolButtonHeight);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(menuButton);
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
        menu->clear();
        for (int i = 0; i < count(); ++i) {
            QAction* a = menu->addAction(tabText(i));
            a->setCheckable(true);
            a->setChecked(i == currentIndex());
            connect(a, &QAction::triggered, this, [this, i] { setCurrentIndex(i); });
        }
    });
    menuButton->setMenu(menu);
    setCornerWidget(menuButton, Qt::TopRightCorner);

    // The group only reports; BinContainer closes, activates or moves bins.
    connect(this, &QTabWidget::tabCloseRequested, this, [this](int i) {
        if (auto* v = qobject_cast<BinView*>(widget(i))) emit area_.closeRequested(v);
    });
    connect(this, &QTabWidget::currentChanged, this, [this] {
        area_.setCurrentGroup(this);
        emit area_.currentViewChanged(currentView());
    });
    connect(bar, &BinTabBar::dragOut, this, [this](int i) {
        if (auto* v = qobject_cast<BinView*>(widget(i))) emit area_.tabDragged(v);
    });
    connect(tabBar(), &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) { emit area_.tabMenuRequested(tabBar()->mapToGlobal(pos)); });
}

QList<BinView*> BinTabGroup::views() const {
    QList<BinView*> out;
    for (int i = 0; i < count(); ++i) {
        if (auto* v = qobject_cast<BinView*>(widget(i))) out << v;
    }
    return out;
}

BinView* BinTabGroup::currentView() const {
    return qobject_cast<BinView*>(currentWidget());
}

// ---------------------------------------------------------------- BinDockArea

BinDockArea::BinDockArea(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("binArea"));
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    overlay_ = new DropOverlay(*this);
}

// Depth-first through the splitter tree, so the order is left to right and
// top to bottom.
QList<BinTabGroup*> BinDockArea::groups() const {
    QList<BinTabGroup*> out;
    std::function<void(QWidget*)> walk = [&](QWidget* w) {
        if (auto* g = qobject_cast<BinTabGroup*>(w)) {
            out << g;
        } else if (auto* s = qobject_cast<QSplitter*>(w)) {
            for (int i = 0; i < s->count(); ++i) walk(s->widget(i));
        }
    };
    if (root_) walk(root_);
    return out;
}

QList<BinView*> BinDockArea::views() const {
    QList<BinView*> out;
    for (BinTabGroup* g : groups()) out << g->views();
    return out;
}

BinTabGroup* BinDockArea::groupOf(const BinView* view) const {
    for (BinTabGroup* g : groups()) {
        if (g->indexOf(const_cast<BinView*>(view)) >= 0) return g;
    }
    return nullptr;
}

BinTabGroup* BinDockArea::currentGroup() const {
    const QList<BinTabGroup*> all = groups();
    if (current_ && all.contains(current_)) return current_;
    return all.isEmpty() ? nullptr : all.first();
}

void BinDockArea::setCurrentGroup(BinTabGroup* group) {
    current_ = group;
}

BinTabGroup* BinDockArea::newGroup() {
    return new BinTabGroup(*this);
}

// Puts `root` at the top of the tree (or empties the area with null). The
// overlay is raised again so it stays above the new root.
void BinDockArea::setRoot(QWidget* root) {
    root_ = root;
    if (root_) {
        root_->setParent(this);
        layout()->addWidget(root_);
        root_->show();
    }
    overlay_->raise();
}

// Puts `replacement` where `old` is in the tree, keeping the splitter's sizes.
// `old` is detached but not deleted.
void BinDockArea::replace(QWidget* old, QWidget* replacement) {
    if (old == root_) {
        layout()->removeWidget(old);
        setRoot(replacement);
        return;
    }
    auto* parent = qobject_cast<QSplitter*>(old->parentWidget());
    if (!parent) return;
    const int index = parent->indexOf(old);
    const QList<int> sizes = parent->sizes();
    parent->replaceWidget(index, replacement);
    parent->setSizes(sizes);
}

void BinDockArea::addView(BinView* view, const QString& title, BinTabGroup* target, BinDropSide side) {
    if (!target) target = currentGroup();
    if (!target) {
        // The first bin.
        BinTabGroup* g = newGroup();
        setRoot(g);
        g->addTab(view, title);
        g->setCurrentWidget(view);
        current_ = g;
        return;
    }
    if (side == BinDropSide::Tab) {
        target->addTab(view, title);
        target->setCurrentWidget(view);
        current_ = target;
        return;
    }
    // Subdivide: the target and a new group share its place (p.241).
    BinTabGroup* g = newGroup();
    g->addTab(view, title);
    const Qt::Orientation o =
        side == BinDropSide::Left || side == BinDropSide::Right ? Qt::Horizontal : Qt::Vertical;
    const bool before = side == BinDropSide::Left || side == BinDropSide::Top;
    // If the target already sits in a splitter running the same way, the new
    // group joins that splitter and takes half of the target's share.
    // Otherwise a new splitter takes the target's place and holds the target
    // and the new group half and half.
    auto* parent = qobject_cast<QSplitter*>(target->parentWidget());
    if (parent && parent->orientation() == o) {
        const int at = parent->indexOf(target);
        QList<int> sizes = parent->sizes();
        const int share = sizes.value(at) / 2;
        parent->insertWidget(before ? at : at + 1, g);
        sizes[at] -= share;
        sizes.insert(before ? at : at + 1, share);
        parent->setSizes(sizes);
    } else {
        const QSize space = target->size();
        QSplitter* s = newSplitter(o);
        replace(target, s);
        s->addWidget(before ? static_cast<QWidget*>(g) : target);
        s->addWidget(before ? static_cast<QWidget*>(target) : g);
        const int total = o == Qt::Horizontal ? space.width() : space.height();
        s->setSizes({total / 2, total - total / 2});
    }
    g->show();
    current_ = g;
}

// The view loses its parent; the caller owns it and must re-add or delete it.
void BinDockArea::takeView(BinView* view) {
    BinTabGroup* g = groupOf(view);
    if (!g) return;
    g->removeTab(g->indexOf(view));
    view->setParent(nullptr);
    if (g->count() == 0) removeGroup(g);
}

// Takes an empty group out of the tree and deletes it later; emits emptied()
// when it was the last.
void BinDockArea::removeGroup(BinTabGroup* group) {
    if (group == root_) {
        layout()->removeWidget(group);
        root_ = nullptr;
    } else if (auto* parent = qobject_cast<QSplitter*>(group->parentWidget())) {
        group->setParent(nullptr);
        if (parent->count() == 1) {
            // A splitter with one side left gives way to that side.
            QWidget* only = parent->widget(0);
            replace(parent, only);
            parent->deleteLater();
        }
    }
    if (current_ == group) current_ = nullptr;
    group->deleteLater();
    if (!root_) emit emptied();
}

void BinDockArea::setTitle(BinView* view, const QString& title) {
    if (BinTabGroup* g = groupOf(view)) g->setTabText(g->indexOf(view), title);
}

void BinDockArea::setTabColor(BinView* view, const QColor& color) {
    BinTabGroup* g = groupOf(view);
    if (!g) return;
    QIcon icon;
    if (color.isValid()) {
        QPixmap swatch(12, 12);
        swatch.fill(color);
        icon = QIcon(swatch);
    }
    g->setTabIcon(g->indexOf(view), icon);
}

void BinDockArea::showView(BinView* view) {
    if (BinTabGroup* g = groupOf(view)) {
        g->setCurrentWidget(view);
        current_ = g;
    }
}

// For each group: the whole group (Tab), then, unless `tabOnly`, a band along
// each edge a quarter of its size. The bands overlap the Tab zone and each
// other at the corners; zoneAt() picks between them.
QList<BinDockArea::Zone> BinDockArea::zones(bool tabOnly) const {
    QList<Zone> out;
    for (BinTabGroup* g : groups()) {
        const QRect r(g->mapTo(this, QPoint(0, 0)), g->size());
        out.append({g, BinDropSide::Tab, r});
        if (tabOnly) continue;
        const int w = std::max(16, r.width() / 4);
        const int h = std::max(16, r.height() / 4);
        out.append({g, BinDropSide::Left, QRect(r.left(), r.top(), w, r.height())});
        out.append({g, BinDropSide::Right, QRect(r.right() - w + 1, r.top(), w, r.height())});
        out.append({g, BinDropSide::Top, QRect(r.left(), r.top(), r.width(), h)});
        out.append({g, BinDropSide::Bottom, QRect(r.left(), r.bottom() - h + 1, r.width(), h)});
    }
    return out;
}

BinDockArea::Zone BinDockArea::zoneAt(const QPoint& pos, bool tabOnly) const {
    // An edge zone wins over its group's middle; the nearest edge wins.
    Zone best;
    int bestDistance = 0;
    for (const Zone& z : zones(tabOnly)) {
        if (!z.rect.contains(pos)) continue;
        if (z.side == BinDropSide::Tab) {
            if (!best.isValid()) best = z;
            continue;
        }
        const QRect g(z.group->mapTo(this, QPoint(0, 0)), z.group->size());
        int d = 0;
        switch (z.side) {
        case BinDropSide::Left: d = pos.x() - g.left(); break;
        case BinDropSide::Right: d = g.right() - pos.x(); break;
        case BinDropSide::Top: d = pos.y() - g.top(); break;
        case BinDropSide::Bottom: d = g.bottom() - pos.y(); break;
        case BinDropSide::Tab: break;
        }
        // `best` starts invalid with side Tab, so the first edge always wins
        // over no zone or a Tab zone.
        if (best.side == BinDropSide::Tab || d < bestDistance) {
            best = z;
            bestDistance = d;
        }
    }
    if (best.isValid() && best.side == BinDropSide::Tab) {
        // Tabbing shows the whole group.
        best.rect = QRect(best.group->mapTo(this, QPoint(0, 0)), best.group->size());
    }
    return best;
}

// The overlay covers the whole area and draws only the hovered zone.
void BinDockArea::setHoveredZone(const Zone& zone) {
    hovered_ = zone;
    overlay_->setGeometry(rect());
    overlay_->setVisible(zone.isValid());
    overlay_->raise();
    overlay_->update();
}

// The area takes bin tabs and Sidebar entries. Bin items are not taken here:
// they are dropped on the views inside the bins.
bool BinDockArea::accepts(const QMimeData* mime) {
    return mime->hasFormat(binTabMime()) || mime->hasFormat(binNodesMime());
}

void BinDockArea::dragEnterEvent(QDragEnterEvent* event) {
    if (!accepts(event->mimeData())) return;
    event->acceptProposedAction();
    setHoveredZone(zoneAt(event->position().toPoint(), event->modifiers() & Qt::AltModifier));
}

void BinDockArea::dragMoveEvent(QDragMoveEvent* event) {
    if (!accepts(event->mimeData())) return;
    // Alt tabs the bin into a group; without it, the edges subdivide (p.243).
    // An empty area takes the drop anywhere: the bin becomes its first group.
    const Zone z = zoneAt(event->position().toPoint(), event->modifiers() & Qt::AltModifier);
    if (z.isValid() || isEmpty()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
    setHoveredZone(z);
}

void BinDockArea::dragLeaveEvent(QDragLeaveEvent*) {
    setHoveredZone({});
}

// Reports the drop with the zone under the pointer; BinContainer moves the
// bin (binDropped) or opens the Sidebar's bins there (nodesDropped).
void BinDockArea::dropEvent(QDropEvent* event) {
    const Zone z = zoneAt(event->position().toPoint(), event->modifiers() & Qt::AltModifier);
    setHoveredZone({});
    const QMimeData* mime = event->mimeData();
    if (mime->hasFormat(binTabMime())) {
        event->acceptProposedAction();
        emit binDropped(QString::fromUtf8(mime->data(binTabMime())), z);
    } else if (mime->hasFormat(binNodesMime())) {
        event->acceptProposedAction();
        emit nodesDropped(QString::fromUtf8(mime->data(binNodesMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts),
                          z);
    }
}

void BinDockArea::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    overlay_->setGeometry(rect());
}

} // namespace lumenis::ui
