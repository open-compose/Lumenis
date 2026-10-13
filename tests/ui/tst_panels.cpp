#include "lumenis/ui/panels.h"
#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QSplitter>
#include <QTest>

using namespace lumenis::ui;

namespace {

QJsonObject tabs(const QStringList& ids) {
    QJsonArray a;
    for (const QString& id : ids) a.append(id);
    return {{QStringLiteral("tabs"), a}, {QStringLiteral("current"), 0}};
}

// Bin Container | Composer over Timeline, as in the default layout.
QJsonObject standardLayout() {
    const QJsonObject top{{QStringLiteral("split"), QStringLiteral("h")},
                          {QStringLiteral("sizes"), QJsonArray{38, 62}},
                          {QStringLiteral("children"), QJsonArray{tabs({"BinContainer"}), tabs({"Composer"})}}};
    return {{QStringLiteral("version"), 1},
            {QStringLiteral("main"),
             QJsonObject{{QStringLiteral("split"), QStringLiteral("v")},
                         {QStringLiteral("sizes"), QJsonArray{52, 48}},
                         {QStringLiteral("children"), QJsonArray{top, tabs({"Timeline"})}}}},
            {QStringLiteral("active"), QStringLiteral("Composer")}};
}

struct Fixture {
    QWidget owner;
    PanelManager manager{&owner};
    Panel* bins = new Panel(QStringLiteral("BinContainer"), QStringLiteral("Bin Container"));
    Panel* composer = new Panel(QStringLiteral("Composer"), QStringLiteral("Composer"));
    Panel* timeline = new Panel(QStringLiteral("Timeline"), QStringLiteral("Timeline"));

    Fixture() {
        auto* layout = new QHBoxLayout(&owner);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(manager.mainHost());
        manager.addPanel(bins);
        manager.addPanel(composer);
        manager.addPanel(timeline);
        manager.restoreLayout(standardLayout());
        owner.resize(1000, 700);
        owner.show();
    }

    DockHost* host() const { return manager.mainHost(); }

    // Geometry of a panel's group in owner coordinates.
    QRect rectOf(const Panel* p) const {
        const PanelGroup* g = p->group();
        return QRect(g->mapTo(&owner, QPoint()), g->size());
    }

    // Lets deleteLater() run.
    static void settle() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
};

// Every splitter in a host has at least two children: removing panels
// collapses the ones left with one.
void checkTree(QWidget* node, int depth = 0) {
    QVERIFY(depth < 20);
    if (auto* s = qobject_cast<QSplitter*>(node)) {
        QVERIFY2(s->count() >= 2, "splitter with fewer than two children");
        for (int i = 0; i < s->count(); ++i) checkTree(s->widget(i), depth + 1);
    } else {
        QVERIFY(qobject_cast<PanelGroup*>(node));
    }
}

QStringList ids(const QList<PanelGroup*>& groups) {
    QStringList out;
    for (const PanelGroup* g : groups) {
        QStringList tabIds;
        for (const Panel* p : g->panels()) tabIds << p->id();
        out << tabIds.join(QLatin1Char('+'));
    }
    return out;
}

} // namespace

class TestPanels : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void restoresTheStandardArrangement() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer", "Composer", "Timeline"}));
        const QRect bins = f.rectOf(f.bins);
        const QRect composer = f.rectOf(f.composer);
        const QRect timeline = f.rectOf(f.timeline);
        QVERIFY(bins.right() < composer.left());
        QCOMPARE(bins.top(), composer.top());
        QVERIFY(composer.bottom() < timeline.top());
        QCOMPARE(timeline.left(), 0);
        QCOMPARE(timeline.width(), f.owner.width());
        // Proportions from the layout, within a splitter handle.
        QVERIFY(qAbs(bins.width() - (f.owner.width() - kSplitterHandle) * 38 / 100) <= 2);
        QCOMPARE(f.manager.activePanel(), f.composer);
        QVERIFY(f.composer->group()->isActive());
        QVERIFY(!f.bins->group()->isActive());
        QCOMPARE(f.composer->group()->titleBar()->width(), kTitleBarWidth);
    }

    void docksBesideAnotherWindowAndCollapsesSplitters() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.dock(f.timeline, f.bins->group(), DockSide::Right);
        Fixture::settle();
        checkTree(f.host()->root());
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer", "Timeline", "Composer"}));
        QVERIFY(f.rectOf(f.timeline).left() > f.rectOf(f.bins).right());
        QCOMPARE(f.rectOf(f.timeline).top(), 0);

        f.manager.dock(f.timeline, f.composer->group(), DockSide::Bottom);
        Fixture::settle();
        checkTree(f.host()->root());
        QVERIFY(f.rectOf(f.timeline).top() > f.rectOf(f.composer).bottom());
        QCOMPARE(f.rectOf(f.timeline).left(), f.rectOf(f.composer).left());
    }

    void docksAlongTheHostEdge() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.dockAtEdge(f.bins, f.host(), DockSide::Left, 0.25);
        Fixture::settle();
        checkTree(f.host()->root());
        // The Bin Container now runs the full height on the left.
        QCOMPARE(f.rectOf(f.bins).height(), f.owner.height());
        QCOMPARE(f.rectOf(f.bins).left(), 0);
        QVERIFY(f.rectOf(f.timeline).left() > f.rectOf(f.bins).right());
    }

    void tabsWindowsTogetherAndApart() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.tab(f.composer, f.bins->group());
        Fixture::settle();
        checkTree(f.host()->root());
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer+Composer", "Timeline"}));
        QCOMPARE(f.bins->group()->current(), f.composer);
        QCOMPARE(f.manager.activePanel(), f.composer);

        // A single tab can be docked on its own (p.78).
        f.manager.dock(f.composer, f.bins->group(), DockSide::Right);
        Fixture::settle();
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer", "Composer", "Timeline"}));
    }

    void floatsAndDocksAgain() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        FloatingWindow* w = f.manager.floatPanel(f.composer);
        Fixture::settle();
        QVERIFY(w);
        QVERIFY(w->isVisible());
        QVERIFY(f.manager.isFloating(f.composer));
        QCOMPARE(w->windowTitle(), QStringLiteral("Composer"));
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer", "Timeline"}));
        checkTree(f.host()->root());

        // Panels can be docked inside a floating window too (p.79).
        f.manager.dock(f.bins, f.composer->group(), DockSide::Left);
        Fixture::settle();
        QCOMPARE(ids(w->host()->groups()), (QStringList{"BinContainer", "Composer"}));
        QCOMPARE(ids(f.host()->groups()), (QStringList{"Timeline"}));

        // Docking everything back closes the empty floating window.
        f.manager.dock(f.bins, f.timeline->group(), DockSide::Top);
        f.manager.dock(f.composer, f.bins->group(), DockSide::Right);
        Fixture::settle();
        QVERIFY(f.manager.floatingWindows().isEmpty());
        QVERIFY(!f.manager.isFloating(f.composer));
    }

    void floatsAllAndTheActivePanel() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.tab(f.timeline, f.composer->group());
        f.manager.floatActive(); // the Timeline, alone, out of its tab group
        Fixture::settle();
        QCOMPARE(f.manager.floatingWindows().size(), 1);
        QVERIFY(f.manager.isFloating(f.timeline));
        QVERIFY(!f.manager.isFloating(f.composer));

        f.manager.floatAll();
        Fixture::settle();
        QCOMPARE(f.manager.floatingWindows().size(), 3);
        QVERIFY(f.host()->isEmpty());
        for (Panel* p : {f.bins, f.composer, f.timeline}) QVERIFY(f.manager.isFloating(p));
    }

    void closingLeavesABlankPanelWhenAsked() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.setBlankPanelOnClose(true);
        const QRect before = f.rectOf(f.composer);
        PanelGroup* group = f.composer->group();
        f.manager.closePanel(f.composer);
        Fixture::settle();
        QVERIFY(!f.manager.isOpen(f.composer));
        QCOMPARE(group->count(), 1);
        QVERIFY(group->current()->isBlank());
        QCOMPARE(QRect(group->mapTo(&f.owner, QPoint()), group->size()), before);

        // Opening it again replaces the blank panel (p.83).
        f.manager.showPanel(f.composer);
        Fixture::settle();
        QCOMPARE(f.composer->group(), group);
        QCOMPARE(group->count(), 1);
        for (Panel* p : f.manager.panels()) QVERIFY(!p->isBlank());
    }

    void closingLetsNeighboursFillTheSpace() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.closePanel(f.composer);
        Fixture::settle();
        checkTree(f.host()->root());
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer", "Timeline"}));
        QCOMPARE(f.rectOf(f.bins).width(), f.owner.width());
        QVERIFY(f.manager.activePanel());

        // Reopened without a place to return to, a tool floats (p.94).
        f.manager.showPanel(f.composer);
        Fixture::settle();
        QVERIFY(f.manager.isFloating(f.composer));
        QCOMPARE(f.manager.activePanel(), f.composer);
    }

    void addsBlankPanels() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.activate(f.bins);
        Panel* blank = f.manager.addBlankPanel();
        Fixture::settle();
        QVERIFY(blank->isBlank());
        QCOMPARE(blank->title(), QStringLiteral("Blank Panel"));
        QCOMPARE(ids(f.host()->groups()).value(1), blank->id());
        // Closing a blank panel removes it altogether.
        f.manager.closePanel(blank);
        Fixture::settle();
        QCOMPARE(f.manager.panels().size(), 3);
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer", "Composer", "Timeline"}));
    }

    void layoutsRoundTrip() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.tab(f.timeline, f.bins->group());
        f.manager.floatPanel(f.composer, QRect(200, 150, 500, 300));
        f.manager.activate(f.timeline);
        Fixture::settle();
        const QJsonObject saved = f.manager.saveLayout();

        f.manager.restoreLayout(standardLayout());
        Fixture::settle();
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer", "Composer", "Timeline"}));
        QVERIFY(f.manager.floatingWindows().isEmpty());

        f.manager.restoreLayout(saved);
        Fixture::settle();
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer+Timeline"}));
        QCOMPARE(f.manager.floatingWindows().size(), 1);
        QCOMPARE(ids(f.manager.floatingWindows().first()->host()->groups()), (QStringList{"Composer"}));
        QCOMPARE(f.manager.activePanel(), f.timeline);
        QCOMPARE(f.manager.saveLayout().value(QStringLiteral("main")), saved.value(QStringLiteral("main")));
    }

    void ignoresUnknownPanelsAndLeavesUnmentionedOnesClosed() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        QJsonObject layout{{QStringLiteral("version"), 1},
                           {QStringLiteral("main"),
                            QJsonObject{{QStringLiteral("split"), QStringLiteral("h")},
                                        {QStringLiteral("sizes"), QJsonArray{1, 1}},
                                        {QStringLiteral("children"),
                                         QJsonArray{tabs({"NoSuchTool"}), tabs({"Timeline", "Gone"})}}}}};
        f.manager.restoreLayout(layout);
        Fixture::settle();
        QCOMPARE(ids(f.host()->groups()), (QStringList{"Timeline"}));
        QVERIFY(!f.manager.isOpen(f.bins));
        QVERIFY(!f.manager.isOpen(f.composer));
        // A layout from another version is not applied.
        f.manager.restoreLayout(QJsonObject{{QStringLiteral("version"), 99}});
        QCOMPARE(ids(f.host()->groups()), (QStringList{"Timeline"}));
    }

    void dropZonesWhileDragging() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.beginDrag(f.composer->group(), nullptr);
        QVERIFY(f.host()->overlay()->isVisible());
        int split = 0;
        int edge = 0;
        for (const DropTarget& z : f.manager.dropZones(f.host(), false)) {
            QVERIFY(z.group != f.composer->group()); // not beside itself
            if (z.kind == DropTarget::Kind::Split) ++split;
            if (z.kind == DropTarget::Kind::Edge) ++edge;
        }
        QCOMPARE(split, 8); // four sides of the other two windows
        QCOMPARE(edge, 4);
        // With Alt, the other windows' title bars are the targets (p.76).
        const QList<DropTarget> tabZones = f.manager.dropZones(f.host(), true);
        QCOMPARE(tabZones.size(), 2);
        for (const DropTarget& z : tabZones) {
            QCOMPARE(z.kind, DropTarget::Kind::Tab);
            QCOMPARE(z.zone.width(), kTitleBarWidth);
        }
        f.manager.cancelDrag();
        QVERIFY(!f.host()->overlay()->isVisible());
        QVERIFY(!f.manager.isDragging());
    }

    void dragAndDropWithTheMouse() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        QWidget* bar = f.composer->group()->titleBar();
        // The grey space below the tabs drags the whole window (p.77).
        const QPoint grab(bar->width() / 2, bar->height() - 10);
        QPoint target;
        f.manager.beginDrag(f.composer->group(), nullptr);
        for (const DropTarget& z : f.manager.dropZones(f.host(), false)) {
            if (z.group == f.timeline->group() && z.side == DockSide::Bottom) {
                target = f.host()->mapToGlobal(z.zone.center());
            }
        }
        f.manager.cancelDrag();
        QVERIFY(!target.isNull());

        QTest::mousePress(bar, Qt::LeftButton, {}, grab);
        QTest::mouseMove(bar, bar->mapFromGlobal(target));
        QVERIFY(f.manager.isDragging());
        QCOMPARE(f.manager.hoveredTarget().kind, DropTarget::Kind::Split);
        QTest::mouseRelease(bar, Qt::LeftButton, {}, bar->mapFromGlobal(target));
        Fixture::settle();
        QVERIFY(!f.manager.isDragging());
        checkTree(f.host()->root());
        QVERIFY(f.rectOf(f.composer).top() > f.rectOf(f.timeline).bottom());
    }

    void altDragTabs() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        QWidget* binBar = f.bins->group()->titleBar();
        const QPoint onBar = binBar->mapToGlobal(QPoint(kTitleBarWidth / 2, binBar->height() / 2));
        f.manager.beginDrag(f.timeline->group(), nullptr);
        f.manager.updateDrag(onBar, true);
        QCOMPARE(f.manager.hoveredTarget().kind, DropTarget::Kind::Tab);
        f.manager.endDrag(onBar, true);
        Fixture::settle();
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer+Timeline", "Composer"}));
        QCOMPARE(f.manager.activePanel(), f.timeline);
    }

    void droppingAwayFromTheZonesFloats() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        // The middle of a window is not a drop zone.
        const QPoint middle = f.owner.mapToGlobal(f.rectOf(f.bins).center());
        f.manager.beginDrag(f.bins->group(), nullptr);
        f.manager.endDrag(middle, false);
        Fixture::settle();
        QVERIFY(f.manager.isFloating(f.bins));
        QCOMPARE(f.manager.floatingWindows().size(), 1);
    }

    void escapeCancelsADrag() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.beginDrag(f.bins->group(), nullptr);
        QTest::keyClick(&f.owner, Qt::Key_Escape);
        QVERIFY(!f.manager.isDragging());
        QCOMPARE(ids(f.host()->groups()), (QStringList{"BinContainer", "Composer", "Timeline"}));
    }

    void cyclesWindowsAndTabs() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        f.manager.activate(f.bins);
        f.manager.cycleWindows(1);
        QCOMPARE(f.manager.activePanel(), f.composer);
        f.manager.cycleWindows(1);
        QCOMPARE(f.manager.activePanel(), f.timeline);
        f.manager.cycleWindows(1);
        QCOMPARE(f.manager.activePanel(), f.bins);
        f.manager.cycleWindows(-1);
        QCOMPARE(f.manager.activePanel(), f.timeline);

        f.manager.tab(f.composer, f.timeline->group());
        f.manager.activate(f.timeline);
        f.manager.cycleTabs();
        QCOMPARE(f.manager.activePanel(), f.composer);
        f.manager.cycleTabs();
        QCOMPARE(f.manager.activePanel(), f.timeline);
    }

    void clickingAPanelMakesItActive() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        QTest::mouseClick(f.timeline, Qt::LeftButton, {}, QPoint(50, 50));
        QCOMPARE(f.manager.activePanel(), f.timeline);
        QVERIFY(f.timeline->group()->isActive());
    }

    void closingAFloatingWindowClosesItsPanels() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        FloatingWindow* w = f.manager.floatPanel(f.composer);
        f.manager.tab(f.bins, f.composer->group());
        Fixture::settle();
        w->close();
        Fixture::settle();
        QVERIFY(f.manager.floatingWindows().isEmpty());
        QVERIFY(!f.manager.isOpen(f.composer));
        QVERIFY(!f.manager.isOpen(f.bins));
        QCOMPARE(f.manager.activePanel(), f.timeline);
    }

    void floatingWindowsFollowTheOwner() {
        Fixture f;
        QVERIFY(QTest::qWaitForWindowExposed(&f.owner));
        FloatingWindow* w = f.manager.floatPanel(f.composer);
        QVERIFY(w->isVisible());
        f.owner.hide();
        QVERIFY(!w->isVisible());
        f.owner.show();
        QVERIFY(w->isVisible());
    }
};

QTEST_MAIN(TestPanels)
#include "tst_panels.moc"
