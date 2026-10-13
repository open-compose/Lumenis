#include "lumenis/ui/panels.h"

#include "lumenis/ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QStackedWidget>

#include <algorithm>
#include <cmath>

namespace lumenis::ui {

namespace {

// Vertical title bar geometry (guide p.75-77): a grey box at the top (drags
// the whole group), the tab menu button, one tab per panel (close button,
// then the name reading upwards), and grey space below (also drags the
// group).
constexpr int kBoxHeight = 14;
constexpr int kMenuHeight = 20;
constexpr int kTabsTop = kBoxHeight + kMenuHeight + 2;
constexpr int kCloseHeight = 18;
constexpr int kTabGap = 2;

int roundUp(int value, int step) {
    return (value + step - 1) / step * step;
}

class PanelTitleBar : public QWidget {
public:
    PanelTitleBar(PanelGroup& group, PanelManager& manager) : QWidget(&group), group_(group), manager_(manager) {
        setObjectName(QStringLiteral("titleBar"));
        setFixedWidth(kTitleBarWidth);
        setMinimumHeight(kTabsTop);
    }

    QSize sizeHint() const override { return {kTitleBarWidth, 120}; }

protected:
    struct Hit {
        enum Kind { None, Box, Menu, Tab, Close, Space } kind = None;
        int index = -1;
    };

    QFont tabFont() const {
        QFont f = font();
        f.setPixelSize(11);
        f.setWeight(QFont::DemiBold);
        return f;
    }

    // Tab lengths are rounded so that small differences in how each
    // platform measures text do not move anything (Phase 1 gate 1).
    int tabLength(const Panel* panel) const {
        const int text = QFontMetrics(tabFont()).horizontalAdvance(panel->title());
        return kCloseHeight + roundUp(text + 12, 10);
    }

    QList<QRect> tabRects() const {
        QList<QRect> rects;
        int y = kTabsTop;
        for (const Panel* p : group_.panels()) {
            const int h = tabLength(p);
            rects << QRect(0, y, width(), h);
            y += h + kTabGap;
        }
        return rects;
    }

    Hit hitTest(const QPoint& pos) const {
        if (pos.y() < kBoxHeight) return {Hit::Box, -1};
        if (pos.y() < kBoxHeight + kMenuHeight) return {Hit::Menu, -1};
        const QList<QRect> tabs = tabRects();
        for (int i = 0; i < tabs.size(); ++i) {
            if (tabs[i].contains(pos)) {
                return {pos.y() < tabs[i].top() + kCloseHeight ? Hit::Close : Hit::Tab, i};
            }
        }
        return {Hit::Space, -1};
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor::fromRgba(group_.isActive() ? kTitleBarActiveColor : kTitleBarColor));
        p.setRenderHint(QPainter::Antialiasing);

        // Grey box with hatching.
        const QRect box(2, 2, width() - 4, kBoxHeight - 3);
        p.fillRect(box, QColor(0x6c, 0x6c, 0x6c));
        p.save();
        p.setClipRect(box);
        p.setPen(QPen(QColor(0x9a, 0x9a, 0x9a), 1));
        for (int x = -box.height(); x < box.width(); x += 4) {
            p.drawLine(QPointF(box.left() + x, box.bottom() + 1), QPointF(box.left() + x + box.height(), box.top()));
        }
        p.restore();

        // Tab menu: a dark circle with a downward triangle.
        const QPointF menuCentre(width() / 2.0, kBoxHeight + kMenuHeight / 2.0);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x14, 0x14, 0x14));
        p.drawEllipse(menuCentre, 7, 7);
        QPainterPath triangle;
        triangle.moveTo(menuCentre + QPointF(-3.5, -1.5));
        triangle.lineTo(menuCentre + QPointF(3.5, -1.5));
        triangle.lineTo(menuCentre + QPointF(0, 3));
        triangle.closeSubpath();
        p.setBrush(QColor(0xf0, 0xf0, 0xf0));
        p.drawPath(triangle);

        const QList<QRect> tabs = tabRects();
        const Panel* current = group_.current();
        p.setFont(tabFont());
        for (int i = 0; i < tabs.size(); ++i) {
            const Panel* panel = group_.panels().at(i);
            const bool isCurrent = panel == current;
            const QColor tabColor = QColor::fromRgba(isCurrent ? kTabCurrentColor : kTabColor);
            p.setPen(Qt::NoPen);
            p.setBrush(tabColor);
            p.drawRect(tabs[i]);

            // Close button.
            const QPointF c(width() / 2.0, tabs[i].top() + kCloseHeight / 2.0);
            p.setBrush(isCurrent ? QColor(0xd8, 0xd8, 0xd8) : QColor(0xa8, 0xa8, 0xa8));
            p.drawEllipse(c, 6, 6);
            p.setPen(QPen(tabColor, 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c + QPointF(-2.5, -2.5), c + QPointF(2.5, 2.5));
            p.drawLine(c + QPointF(-2.5, 2.5), c + QPointF(2.5, -2.5));

            // Name, reading upwards.
            const QRect text(tabs[i].left(), tabs[i].top() + kCloseHeight, tabs[i].width(),
                             tabs[i].height() - kCloseHeight);
            p.save();
            p.translate(text.left(), text.bottom() + 1);
            p.rotate(-90);
            p.setPen(palette().color(QPalette::BrightText));
            p.drawText(QRect(0, 0, text.height(), text.width()), Qt::AlignCenter, panel->title());
            p.restore();
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        press_ = hitTest(event->position().toPoint());
        pressGlobal_ = event->globalPosition().toPoint();
        Panel* panel = (press_.index >= 0 && press_.index < group_.count()) ? group_.panels().at(press_.index) : nullptr;
        switch (press_.kind) {
        case Hit::Tab:
            manager_.activate(panel);
            break;
        case Hit::Menu:
            showTabMenu();
            press_ = {};
            break;
        case Hit::Box:
        case Hit::Space:
            manager_.activate(group_.current());
            break;
        default:
            break;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton)) return;
        const QPoint global = event->globalPosition().toPoint();
        const bool tabbing = event->modifiers() & Qt::AltModifier;
        if (!dragging_) {
            if ((global - pressGlobal_).manhattanLength() < QApplication::startDragDistance()) return;
            if (press_.kind != Hit::Tab && press_.kind != Hit::Box && press_.kind != Hit::Space) return;
            Panel* panel = press_.kind == Hit::Tab ? group_.panels().value(press_.index) : nullptr;
            dragging_ = true;
            setCursor(Qt::ClosedHandCursor);
            manager_.beginDrag(&group_, panel);
        }
        manager_.updateDrag(global, tabbing);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        const Hit pressed = press_;
        press_ = {};
        if (dragging_) {
            dragging_ = false;
            unsetCursor();
            if (manager_.isDragging()) {
                // May move this title bar's group elsewhere or delete it later.
                manager_.endDrag(event->globalPosition().toPoint(), event->modifiers() & Qt::AltModifier);
            }
            return;
        }
        const Hit released = hitTest(event->position().toPoint());
        if (pressed.kind == Hit::Close && released.kind == Hit::Close && pressed.index == released.index) {
            if (Panel* panel = group_.panels().value(pressed.index)) {
                manager_.closePanel(panel);
            }
        }
    }

    // The tab menu lists the group's panels (p.94).
    void showTabMenu() {
        QMenu menu(this);
        for (Panel* panel : group_.panels()) {
            QAction* a = menu.addAction(panel->title());
            a->setCheckable(true);
            a->setChecked(panel == group_.current());
            QPointer<Panel> target(panel);
            connect(a, &QAction::triggered, this, [this, target] {
                if (target) manager_.activate(target);
            });
        }
        menu.exec(mapToGlobal(QPoint(width(), kBoxHeight)));
    }

private:
    PanelGroup& group_;
    PanelManager& manager_;
    Hit press_;
    QPoint pressGlobal_;
    bool dragging_ = false;
};

class FlatSplitterHandle : public QSplitterHandle {
public:
    using QSplitterHandle::QSplitterHandle;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter(this).fillRect(rect(), QColor::fromRgba(kSplitterColor));
    }
};

class PanelSplitter : public QSplitter {
public:
    explicit PanelSplitter(Qt::Orientation o) : QSplitter(o) {
        setObjectName(o == Qt::Horizontal ? QStringLiteral("hsplit") : QStringLiteral("vsplit"));
        setHandleWidth(kSplitterHandle);
        setChildrenCollapsible(false);
    }

    void setProportions(const QList<int>& proportions) {
        pending_ = proportions;
        applyPending();
    }

protected:
    QSplitterHandle* createHandle() override { return new FlatSplitterHandle(orientation(), this); }

    void resizeEvent(QResizeEvent* event) override {
        QSplitter::resizeEvent(event);
        applyPending();
    }

    void showEvent(QShowEvent* event) override {
        QSplitter::showEvent(event);
        applyPending();
    }

private:
    void applyPending() {
        const int extent = orientation() == Qt::Horizontal ? width() : height();
        if (pending_.isEmpty() || extent <= 1) return;
        const int available = extent - handleWidth() * (count() - 1);
        long long total = 0;
        for (int p : pending_) total += p;
        QList<int> sizes;
        for (int p : pending_) sizes << static_cast<int>(total > 0 ? available * p / total : 0);
        pending_.clear();
        setSizes(sizes);
    }

    QList<int> pending_;
};

// Paints the green drop zones and the white preview over a host while a
// title bar is dragged (p.75-76).
class DropOverlay : public QWidget {
public:
    explicit DropOverlay(DockHost& host) : QWidget(&host), host_(host) {
        setObjectName(QStringLiteral("dropOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const PanelManager& m = host_.manager();
        const bool tabbing = m.tabbingDrag();
        const DropTarget& hovered = m.hoveredTarget();
        const bool hoveredHere = hovered.host == &host_ && hovered.kind != DropTarget::Kind::None;
        QPainter p(this);
        QColor green = QColor::fromRgba(kDropZoneColor);
        for (const DropTarget& zone : m.dropZones(&host_, tabbing)) {
            const bool hot = hoveredHere && zone.kind == hovered.kind && zone.zone == hovered.zone;
            green.setAlpha(hot ? 255 : (tabbing ? 90 : 190));
            p.fillRect(zone.zone, green);
        }
        if (hoveredHere && !tabbing) {
            p.fillRect(hovered.preview, QColor(255, 255, 255, 70));
            p.setPen(QPen(QColor(255, 255, 255, 200), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(hovered.preview.adjusted(1, 1, -1, -1));
        }
    }

private:
    DockHost& host_;
};

} // namespace

// ---------------------------------------------------------------- Panel

Panel::Panel(const QString& id, const QString& title, QWidget* content, QWidget* parent)
    : QWidget(parent), title_(title), content_(content) {
    setObjectName(id);
    setAutoFillBackground(true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    if (content_) {
        layout->addWidget(content_);
    }
}

void Panel::setTitle(const QString& title) {
    if (title == title_) return;
    title_ = title;
    emit titleChanged();
}

PanelGroup* Panel::group() const {
    for (QWidget* w = parentWidget(); w; w = w->parentWidget()) {
        if (auto* g = qobject_cast<PanelGroup*>(w)) return g;
    }
    return nullptr;
}

// ---------------------------------------------------------------- PanelGroup

PanelGroup::PanelGroup(PanelManager& manager, QWidget* parent) : QWidget(parent), manager_(manager) {
    setObjectName(QStringLiteral("panelGroup"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    titleBar_ = new PanelTitleBar(*this, manager);
    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("stack"));
    layout->addWidget(titleBar_);
    layout->addWidget(stack_, 1);
}

void PanelGroup::insertPanel(int index, Panel* panel) {
    if (!panel || panels_.contains(panel)) return;
    if (index < 0 || index > count()) index = count();
    panels_.insert(index, panel);
    stack_->insertWidget(index, panel);
    connect(panel, &Panel::titleChanged, titleBar_, qOverload<>(&QWidget::update));
    if (count() == 1) {
        setCurrent(panel);
    }
    titleBar_->update();
}

void PanelGroup::takePanel(Panel* panel) {
    const auto index = panels_.indexOf(panel);
    if (index < 0) return;
    const bool wasCurrent = panel == current();
    panels_.removeAt(index);
    stack_->removeWidget(panel);
    disconnect(panel, nullptr, titleBar_, nullptr);
    panel->setParent(nullptr);
    titleBar_->update();
    if (wasCurrent) {
        emit currentChanged();
    }
}

Panel* PanelGroup::current() const {
    return qobject_cast<Panel*>(stack_->currentWidget());
}

void PanelGroup::setCurrent(Panel* panel) {
    if (!panels_.contains(panel)) return;
    if (stack_->currentWidget() != panel) {
        stack_->setCurrentWidget(panel);
        emit currentChanged();
    }
    titleBar_->update();
}

DockHost* PanelGroup::host() const {
    for (QWidget* w = parentWidget(); w; w = w->parentWidget()) {
        if (auto* h = qobject_cast<DockHost*>(w)) return h;
    }
    return nullptr;
}

void PanelGroup::setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    titleBar_->update();
}

// ---------------------------------------------------------------- DockHost

DockHost::DockHost(PanelManager& manager, QWidget* parent) : QWidget(parent), manager_(manager) {
    setObjectName(QStringLiteral("dockHost"));
    overlay_ = new DropOverlay(*this);
}

QSplitter* DockHost::createSplitter(Qt::Orientation orientation) {
    return new PanelSplitter(orientation);
}

void DockHost::setProportions(QSplitter* splitter, const QList<int>& proportions) {
    if (auto* s = dynamic_cast<PanelSplitter*>(splitter)) {
        s->setProportions(proportions);
    } else {
        splitter->setSizes(proportions);
    }
}

namespace {

void collectGroups(QWidget* node, QList<PanelGroup*>& out) {
    if (auto* g = qobject_cast<PanelGroup*>(node)) {
        out << g;
    } else if (auto* s = qobject_cast<QSplitter*>(node)) {
        for (int i = 0; i < s->count(); ++i) collectGroups(s->widget(i), out);
    }
}

} // namespace

QList<PanelGroup*> DockHost::groups() const {
    QList<PanelGroup*> out;
    if (root_) collectGroups(root_, out);
    return out;
}

void DockHost::replace(QWidget* old, QWidget* replacement) {
    if (old == root_) {
        root_ = replacement;
        replacement->setParent(this);
        replacement->setGeometry(rect());
        replacement->show();
        overlay_->raise();
        return;
    }
    if (auto* s = qobject_cast<QSplitter*>(old->parentWidget())) {
        const QList<int> sizes = s->sizes();
        s->replaceWidget(s->indexOf(old), replacement);
        s->setSizes(sizes);
        replacement->show();
    }
}

void DockHost::insert(PanelGroup* group, PanelGroup* target, DockSide side, double fraction) {
    if (!group || !target || target->host() != this) return;
    const Qt::Orientation o = (side == DockSide::Left || side == DockSide::Right) ? Qt::Horizontal : Qt::Vertical;
    const bool before = side == DockSide::Left || side == DockSide::Top;
    int extent = o == Qt::Horizontal ? target->width() : target->height();
    if (extent <= 1) extent = 1000;
    const int added = std::max(1, static_cast<int>(std::lround(extent * fraction)));
    const int kept = std::max(1, extent - added);

    auto* parent = qobject_cast<QSplitter*>(target->parentWidget());
    if (parent && parent->orientation() == o) {
        const int index = parent->indexOf(target);
        QList<int> sizes = parent->sizes();
        sizes[index] = kept;
        sizes.insert(before ? index : index + 1, added);
        parent->insertWidget(before ? index : index + 1, group);
        parent->setSizes(sizes);
        group->show();
        return;
    }
    QSplitter* split = createSplitter(o);
    replace(target, split);
    split->addWidget(before ? static_cast<QWidget*>(group) : target);
    split->addWidget(before ? static_cast<QWidget*>(target) : group);
    split->setSizes(before ? QList<int>{added, kept} : QList<int>{kept, added});
    target->show();
    group->show();
    overlay_->raise();
}

void DockHost::insertAtEdge(PanelGroup* group, DockSide side, double fraction) {
    if (!group) return;
    if (!root_) {
        fill(group);
        return;
    }
    const Qt::Orientation o = (side == DockSide::Left || side == DockSide::Right) ? Qt::Horizontal : Qt::Vertical;
    const bool before = side == DockSide::Left || side == DockSide::Top;
    int extent = o == Qt::Horizontal ? width() : height();
    if (extent <= 1) extent = 1000;
    const int added = std::max(1, static_cast<int>(std::lround(extent * fraction)));

    auto* split = qobject_cast<QSplitter*>(root_);
    if (split && split->orientation() == o) {
        QList<int> sizes = split->sizes();
        int total = 0;
        for (int s : sizes) total += s;
        for (int& s : sizes) s = total > 0 ? s * (extent - added) / total : 1;
        sizes.insert(before ? 0 : static_cast<int>(sizes.size()), added);
        split->insertWidget(before ? 0 : split->count(), group);
        split->setSizes(sizes);
        group->show();
        return;
    }
    QWidget* old = root_;
    split = createSplitter(o);
    replace(old, split);
    split->addWidget(before ? static_cast<QWidget*>(group) : old);
    split->addWidget(before ? old : static_cast<QWidget*>(group));
    const int kept = std::max(1, extent - added);
    split->setSizes(before ? QList<int>{added, kept} : QList<int>{kept, added});
    old->show();
    group->show();
}

void DockHost::fill(PanelGroup* group) {
    if (root_ || !group) return;
    setRoot(group);
}

void DockHost::remove(PanelGroup* group) {
    if (!group) return;
    if (group == root_) {
        root_ = nullptr;
        group->setParent(nullptr);
        update();
        return;
    }
    auto* split = qobject_cast<QSplitter*>(group->parentWidget());
    if (!split) return;
    group->setParent(nullptr);
    if (split->count() == 1) {
        QWidget* only = split->widget(0);
        replace(split, only);
        split->hide();
        split->setParent(nullptr);
        split->deleteLater();
    }
}

QWidget* DockHost::takeRoot() {
    QWidget* r = root_;
    root_ = nullptr;
    if (r) r->setParent(nullptr);
    update();
    return r;
}

void DockHost::setRoot(QWidget* root) {
    if (root_ == root) return;
    root_ = root;
    if (root_) {
        root_->setParent(this);
        root_->setGeometry(rect());
        root_->show();
    }
    overlay_->raise();
    update();
}

void DockHost::paintEvent(QPaintEvent*) {
    QPainter(this).fillRect(rect(), QColor::fromRgba(kHostBackground));
}

void DockHost::resizeEvent(QResizeEvent*) {
    if (root_) root_->setGeometry(rect());
    overlay_->setGeometry(rect());
}

// ---------------------------------------------------------------- FloatingWindow

FloatingWindow::FloatingWindow(PanelManager& manager, QWidget* owner)
    : QWidget(owner, Qt::Tool), manager_(manager) {
    setObjectName(QStringLiteral("floatingWindow"));
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    host_ = new DockHost(manager, this);
    layout->addWidget(host_);
}

void FloatingWindow::updateTitle() {
    const QList<PanelGroup*> groups = host_->groups();
    const Panel* p = groups.isEmpty() ? nullptr : groups.first()->current();
    setWindowTitle(p ? p->title() : QString());
}

void FloatingWindow::closeEvent(QCloseEvent* event) {
    event->accept();
    manager_.floatingWindowClosed(this);
}

} // namespace lumenis::ui
