#include "lumenis/model/bin.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dialogs.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QMimeData>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QSplitter>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

struct Project {
    QTemporaryDir dir;
    model::ProjectEntry entry;

    explicit Project(const QString& name = QStringLiteral("Doc")) {
        model::ProjectSettings s;
        s.name = name;
        s.format = projectFormatPresets().front();
        entry = *model::ProjectStore(dir.path()).create(s);
    }
};

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

model::BinItem clip(const QString& name, model::BinItemKind kind = model::BinItemKind::MasterClip) {
    model::BinItem i;
    i.id = model::newItemId();
    i.kind = kind;
    i.name = name;
    i.editRate = Rational(25);
    return i;
}

// A project with three bins, the first open.
struct Fixture {
    Project project;
    BinContainer container;
    QStringList bins;

    Fixture() {
        FavoriteBins::paths().clear();
        FavoriteBins::atTop() = true;
        container.resize(1000, 600);
        container.show();
        container.setProject(project.entry.folder, project.entry.settings.name, Rational(25));
        bins << container.activeDocument()->nodeId();
        bins << container.newBin();
        bins << container.newBin();
        settle();
    }
    ~Fixture() { FavoriteBins::paths().clear(); }

    BinDocument* doc(int i) const { return container.document(bins.at(i)); }
    BinView* view(int i) const { return container.viewFor(doc(i)); }
    BinTabGroup* group(int i) const { return container.binArea()->groupOf(view(i)); }

    QStringList treeNames(const QModelIndex& parent = {}) const {
        QStringList out;
        BinTreeModel* m = container.treeModel();
        for (int r = 0; r < m->rowCount(parent); ++r) out << m->index(r, 0, parent).data().toString();
        return out;
    }
};

} // namespace

class TestBinDock : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void binsOpenTabbedTogether() {
        Fixture f;
        BinDockArea* area = f.container.binArea();
        // Double-clicked bins open tabbed in the right panel (p.240).
        QCOMPARE(area->groups().size(), 1);
        QCOMPARE(area->groups().first()->count(), 3);
        QCOMPARE(f.container.tabs()->tabText(2), QStringLiteral("Doc.2"));
    }

    void dockingSubdividesTheBins() {
        Fixture f;
        BinDockArea* area = f.container.binArea();
        // A bin dragged to a group's right edge sits beside it (p.241).
        f.container.dockBin(f.doc(1), f.group(0), BinDropSide::Right);
        QCOMPARE(area->groups().size(), 2);
        auto* split = qobject_cast<QSplitter*>(area->root());
        QVERIFY(split);
        QCOMPARE(split->orientation(), Qt::Horizontal);
        QCOMPARE(area->groups().at(1)->views(), QList<BinView*>{f.view(1)});

        // Below the new group: a vertical split inside the horizontal one.
        f.container.dockBin(f.doc(2), f.group(1), BinDropSide::Bottom);
        QCOMPARE(area->groups().size(), 3);
        auto* inner = qobject_cast<QSplitter*>(split->widget(1));
        QVERIFY(inner);
        QCOMPARE(inner->orientation(), Qt::Vertical);

        // Tabbed back: empty groups go and their splitters fold away.
        f.container.dockBin(f.doc(2), f.group(0), BinDropSide::Tab);
        f.container.dockBin(f.doc(1), f.group(0), BinDropSide::Tab);
        settle();
        QCOMPARE(area->groups().size(), 1);
        QVERIFY(qobject_cast<BinTabGroup*>(area->root()));
        QCOMPARE(area->groups().first()->count(), 3);
        // A bin cannot be docked beside itself when it is alone.
        f.container.dockBin(f.doc(0), f.group(0), BinDropSide::Left);
        QCOMPARE(area->groups().size(), 2);
        const int before = static_cast<int>(area->groups().size());
        f.container.dockBin(f.doc(0), f.group(0), BinDropSide::Right);
        QCOMPARE(area->groups().size(), before);
    }

    void dropZonesAreEdgesOrTabs() {
        Fixture f;
        BinDockArea* area = f.container.binArea();
        settle();
        const QRect r(area->groups().first()->mapTo(area, QPoint()), area->groups().first()->size());
        QCOMPARE(area->zoneAt(QPoint(r.left() + 3, r.center().y()), false).side, BinDropSide::Left);
        QCOMPARE(area->zoneAt(QPoint(r.right() - 3, r.center().y()), false).side, BinDropSide::Right);
        QCOMPARE(area->zoneAt(QPoint(r.center().x(), r.bottom() - 3), false).side, BinDropSide::Bottom);
        QCOMPARE(area->zoneAt(r.center(), false).side, BinDropSide::Tab);
        // With Alt, only tabbing (p.243).
        QCOMPARE(area->zoneAt(QPoint(r.left() + 3, r.center().y()), true).side, BinDropSide::Tab);
        // The hovered zone is drawn green.
        area->setHoveredZone(area->zoneAt(QPoint(r.left() + 3, r.center().y()), false));
        QVERIFY(area->findChild<QWidget*>(QString(), Qt::FindDirectChildrenOnly));
        area->setHoveredZone({});

        // A dropped bin tab goes to the zone.
        const BinDockArea::Zone right = area->zoneAt(QPoint(r.right() - 3, r.center().y()), false);
        emit area->binDropped(f.bins.at(2), right);
        QCOMPARE(area->groups().size(), 2);
        QCOMPARE(area->groups().last()->views(), QList<BinView*>{f.view(2)});
    }

    void sidebarBinsDroppedOnTheArea() {
        Fixture f;
        f.container.closeBin(f.doc(1));
        f.container.closeBin(f.doc(2));
        settle();
        BinDockArea* area = f.container.binArea();
        const QRect r(area->groups().first()->mapTo(area, QPoint()), area->groups().first()->size());
        // Without Alt the zones show; the closed bins open there (p.243).
        emit area->nodesDropped({f.bins.at(1), f.bins.at(2)}, area->zoneAt(QPoint(r.center().x(), r.top() + 3), false));
        QCOMPARE(area->groups().size(), 2);
        QCOMPARE(area->groups().first()->views(), (QList<BinView*>{f.view(1), f.view(2)}));
        QCOMPARE(qobject_cast<QSplitter*>(area->root())->orientation(), Qt::Vertical);
        // The tree's drag data is what the area reads.
        QMimeData* mime = f.container.treeModel()->mimeData({f.container.treeModel()->indexOf(f.bins.at(0))});
        QCOMPARE(QString::fromUtf8(mime->data(binNodesMime())), f.bins.at(0));
        delete mime;
    }

    void floatingBinContainers() {
        Fixture f;
        // Released away from any Bin Container, a bin floats in a new one
        // whose Sidebar is closed (p.241).
        FloatingBinContainer* floating = f.container.floatBin(f.doc(1), QRect(100, 100, 500, 300));
        QVERIFY(floating);
        QVERIFY(floating->isVisible());
        QVERIFY(!floating->isSidebarVisible());
        QCOMPARE(f.container.floatingContainers().size(), 1);
        QCOMPARE(f.container.areaOf(f.view(1)), floating->area());
        QCOMPARE(f.container.binArea()->views().size(), 2);
        QCOMPARE(f.container.areas().size(), 2);
        floating->setSidebarVisible(true);
        QVERIFY(floating->isSidebarVisible());
        QCOMPARE(floating->sidebar()->model()->rowCount(), f.container.treeModel()->rowCount());

        // Another bin docks into it.
        f.container.dockBin(f.doc(2), floating->area()->groups().first(), BinDropSide::Right);
        QCOMPARE(floating->area()->views().size(), 2);
        // Open bins in every container are remembered.
        QVERIFY(f.container.saveAll());
        QCOMPARE(f.container.bins()->openBins.size(), 3);

        // It closes when its last bin closes or leaves (p.243).
        f.container.closeBin(f.doc(2));
        f.container.dockBin(f.doc(1), f.container.binArea()->groups().first(), BinDropSide::Tab);
        settle();
        QVERIFY(f.container.floatingContainers().isEmpty());
        QCOMPARE(f.container.binArea()->views().size(), 2);
    }

    void itemsMoveAndCopyBetweenBins() {
        Fixture f;
        BinDocument* a = f.doc(0);
        BinDocument* b = f.doc(1);
        model::BinDisplay d = a->bin().display;
        d.customColumns << QStringLiteral("Shot");
        a->setDisplay(d);
        model::BinItem one = clip(QStringLiteral("One"));
        one.fields.insert(QStringLiteral("Shot"), QStringLiteral("CU"));
        const model::BinItem two = clip(QStringLiteral("Two"));
        a->addItem(one);
        a->addItem(two);

        // Dragging moves (p.260).
        QCOMPARE(f.container.transferItems(a->nodeId(), {one.id}, b->nodeId(), false), QStringList{one.id});
        QVERIFY(!a->bin().item(one.id));
        QCOMPARE(b->bin().item(one.id)->name, QStringLiteral("One"));
        // Custom columns come along (p.260).
        QVERIFY(b->bin().display.customColumns.contains(QStringLiteral("Shot")));
        QCOMPARE(b->bin().item(one.id)->fields.value(QStringLiteral("Shot")), QStringLiteral("CU"));
        // With the copy key, the original stays, under its own name.
        QCOMPARE(f.container.transferItems(a->nodeId(), {two.id}, b->nodeId(), true), QStringList{two.id});
        QVERIFY(a->bin().item(two.id));
        QCOMPARE(b->bin().item(two.id)->name, QStringLiteral("Two"));
        // Not into the same bin.
        QVERIFY(f.container.transferItems(a->nodeId(), {two.id}, a->nodeId(), true).isEmpty());

        // To a closed bin in the Sidebar: it opens with the items selected
        // (p.243).
        f.container.closeBin(f.doc(2));
        settle();
        QMimeData* mime = f.view(0)->itemModel()->mimeData({f.view(0)->itemModel()->index(0, 1)});
        BinTreeModel* tree = f.container.treeModel();
        const QModelIndex target = tree->indexOf(f.bins.at(2));
        QVERIFY(tree->canDropMimeData(mime, Qt::MoveAction, -1, 0, target));
        QVERIFY(!tree->canDropMimeData(mime, Qt::MoveAction, 0, 0, QModelIndex())); // not between entries
        QVERIFY(tree->dropMimeData(mime, Qt::MoveAction, -1, 0, target));
        delete mime;
        settle();
        BinDocument* opened = f.container.document(f.bins.at(2));
        QVERIFY(opened);
        QVERIFY(opened->bin().item(two.id));
        QCOMPARE(f.container.viewFor(opened)->selectedIds(), QStringList{two.id});
        QVERIFY(!a->bin().item(two.id));

        // A bin view takes items from another bin, not from itself.
        BinItemModel* m = f.view(1)->itemModel();
        QMimeData* own = m->mimeData({m->index(0, 1)});
        QVERIFY(!m->canDropMimeData(own, Qt::MoveAction, -1, -1, {}));
        QVERIFY(f.view(0)->itemModel()->canDropMimeData(own, Qt::MoveAction, -1, -1, {}));
        delete own;
    }

    // Locking prevents deletion (p.265): a move leaves locked items in the
    // source bin, as Delete does, and moves the others.
    void movingLockedItemsCopiesThem() {
        Fixture f;
        BinDocument* a = f.doc(0);
        BinDocument* b = f.doc(1);
        model::BinItem locked = clip(QStringLiteral("Locked"));
        locked.locked = true;
        const model::BinItem free = clip(QStringLiteral("Free"));
        a->addItem(locked);
        a->addItem(free);
        QCOMPARE(f.container.transferItems(a->nodeId(), {locked.id, free.id}, b->nodeId(), false),
                 (QStringList{locked.id, free.id}));
        QVERIFY(a->bin().item(locked.id));
        QVERIFY(!a->bin().item(free.id));
        QVERIFY(b->bin().item(locked.id) && b->bin().item(free.id));
    }

    // Alt/Option (p.260) or Ctrl/Command (p.261) copies, in every drop path.
    void copyModifiers() {
        QVERIFY(isCopyDrop(Qt::CopyAction, Qt::NoModifier));
        QVERIFY(isCopyDrop(Qt::MoveAction, Qt::AltModifier));
        QVERIFY(isCopyDrop(Qt::MoveAction, Qt::ControlModifier));
        QVERIFY(!isCopyDrop(Qt::MoveAction, Qt::ShiftModifier));
        QVERIFY(!isCopyDrop(Qt::MoveAction, Qt::NoModifier));

        // The Script view reads the drop event's modifiers. Its event filter
        // takes the drop directly: Qt delivers a Drop only during a drag.
        Fixture f;
        const model::BinItem one = clip(QStringLiteral("One"));
        f.doc(0)->addItem(one);
        f.view(1)->setMode(model::BinViewMode::Script);
        settle();
        BinScriptView* script = f.view(1)->scriptView();
        QSignalSpy dropped(script, &BinScriptView::itemsDropped);
        QMimeData mime;
        mime.setData(binItemsMime(), (f.bins.at(0) + QLatin1Char('\n') + one.id).toUtf8());
        // A move drop: only the modifiers ask for a copy.
        QDropEvent ctrl(QPointF(5, 5), Qt::MoveAction, &mime, Qt::NoButton, Qt::ControlModifier);
        QObject* filter = script;
        QVERIFY(filter->eventFilter(script->widget(), &ctrl));
        QDropEvent plain(QPointF(5, 5), Qt::MoveAction, &mime, Qt::NoButton, Qt::NoModifier);
        QVERIFY(filter->eventFilter(script->widget(), &plain));
        QCOMPARE(dropped.count(), 2);
        QVERIFY(dropped.at(0).at(2).toBool());
        QVERIFY(!dropped.at(1).at(2).toBool());
        settle();
    }

    // A bin whose file cannot be written stays open with its changes, and
    // the failure is reported.
    void closingABinThatCannotBeSaved() {
        Fixture f;
        BinDocument* a = f.doc(1);
        a->addItem(clip(QStringLiteral("Unsaved")));
        QVERIFY(a->isDirty());
        // A directory where the bin file should be: the write fails, even
        // for a privileged user.
        QVERIFY(QFile::remove(a->path()));
        QVERIFY(QDir().mkpath(a->path() + QStringLiteral("/blocked")));
        QSignalSpy failed(&f.container, &BinContainer::saveFailed);
        QString error;
        QVERIFY(!f.container.closeBin(a, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), a->name());
        settle();
        QCOMPARE(f.container.document(f.bins.at(1)), a);
        QVERIFY(a->isDirty());
        QVERIFY(f.container.viewFor(a));
        // Deleting the bin is refused too: nothing goes to the Trash unsaved.
        QVERIFY(!f.container.deleteNode(f.bins.at(1)));
        QVERIFY(!f.container.bins()->isInTrash(f.bins.at(1)));
        // Once it can be written, it closes.
        QVERIFY(QDir(a->path()).removeRecursively());
        QVERIFY(f.container.closeBin(a));
        settle();
        QVERIFY(!f.container.document(f.bins.at(1)));
        QCOMPARE(model::BinFile::read(f.container.nodePath(f.bins.at(1)))->items.size(), 1);
    }

    void favoriteBins() {
        Fixture f;
        // Add Bin to Favorites (p.293): the folder appears, at the top.
        QVERIFY(f.container.addToFavorites(f.doc(1)));
        QVERIFY(!f.container.addToFavorites(f.doc(1)));
        QCOMPARE(f.treeNames().first(), QStringLiteral("Favorite Bins"));
        const QModelIndex folder = f.container.treeModel()->indexOf(FavoriteBins::folderId());
        QCOMPARE(f.treeNames(folder), QStringList{"Doc.1"});
        const QString favorite = FavoriteBins::idFor(FavoriteBins::paths().first());
        // A favorite of this project's own bin is that bin.
        QCOMPARE(f.container.openBin(favorite), f.doc(1));

        // At the bottom, if the settings say so.
        FavoriteBins::atTop() = false;
        f.container.refreshTree();
        QCOMPARE(f.treeNames().last(), QStringLiteral("Favorite Bins"));

        // Another project sees it, and opens it from where it is.
        const QJsonArray stored = FavoriteBins::toJson();
        f.container.saveAll();
        Project other(QStringLiteral("Other"));
        BinContainer second;
        FavoriteBins::paths().clear();
        QVERIFY(FavoriteBins::fromJson(stored));
        QVERIFY(!FavoriteBins::fromJson(stored)); // unchanged
        second.setProject(other.entry.folder, other.entry.settings.name, Rational(25));
        BinDocument* opened = second.openBin(favorite);
        QVERIFY(opened);
        QCOMPARE(opened->nodeId(), favorite);
        QCOMPARE(opened->name(), QStringLiteral("Doc.1"));
        // Delete takes it off the list only.
        QSignalSpy changed(&second, &BinContainer::favoritesChanged);
        QVERIFY(second.deleteNode(favorite));
        QCOMPARE(changed.count(), 1);
        QVERIFY(FavoriteBins::paths().isEmpty());
        QVERIFY(QFileInfo::exists(FavoriteBins::pathOf(favorite)));
        second.setProject({}, {}, Rational(25));
    }

    void setBinDisplayHidesObjectTypes() {
        Fixture f;
        BinDocument* a = f.doc(0);
        a->addItem(clip(QStringLiteral("Clip")));
        a->addItem(clip(QStringLiteral("Tape"), model::BinItemKind::Source));
        a->addItem(clip(QStringLiteral("Render"), model::BinItemKind::RenderedEffect));
        // By default, all but source clips and rendered effects (p.288).
        QCOMPARE(f.view(0)->visibleCount(), 1);
        SetBinDisplayDialog dialog(a->bin().display);
        QVERIFY(dialog.box(model::BinItemKind::MasterClip)->isChecked());
        QVERIFY(!dialog.box(model::BinItemKind::Source)->isChecked());
        dialog.box(model::BinItemKind::Source)->setChecked(true);
        dialog.box(model::BinItemKind::MasterClip)->setChecked(false);
        f.view(0)->setHiddenKinds(dialog.hiddenKinds());
        QCOMPARE(f.view(0)->visibleCount(), 1);
        QCOMPARE(a->bin().item(f.view(0)->visibleIds().first())->name, QStringLiteral("Tape"));
        QVERIFY(f.container.saveBin(a));
        QCOMPARE(model::BinFile::read(a->path())->display.hiddenKinds,
                 (QList<model::BinItemKind>{model::BinItemKind::MasterClip, model::BinItemKind::RenderedEffect}));
    }

    // "Show clips created by user" off hides all but what the system made:
    // source clips and rendered effects (p.288).
    void setBinDisplayHidesClipsCreatedByUser() {
        Fixture f;
        BinDocument* a = f.doc(0);
        const model::BinItem tape = clip(QStringLiteral("Tape"), model::BinItemKind::Source);
        a->addItem(clip(QStringLiteral("Clip")));
        a->addItem(clip(QStringLiteral("Sequence"), model::BinItemKind::Sequence));
        a->addItem(tape);
        f.view(0)->setHiddenKinds({});
        QCOMPARE(f.view(0)->visibleCount(), 3);
        SetBinDisplayDialog dialog(a->bin().display);
        dialog.findChild<QCheckBox*>(QStringLiteral("showUserClips"))->setChecked(false);
        model::BinDisplay d = a->bin().display;
        d.showUserClips = dialog.showUserClips();
        a->setDisplay(d);
        QCOMPARE(f.view(0)->visibleIds(), QStringList{tape.id});
        d.showUserClips = true;
        a->setDisplay(d);
        QCOMPARE(f.view(0)->visibleCount(), 3);
    }

    // Items not placed by the user (reference clips) show only with Show reference clips (p.288);
    // tst_bins checks that the mark is saved.
    void setBinDisplayShowsReferenceClips() {
        Fixture f;
        BinDocument* a = f.doc(0);
        model::BinItem referenced = clip(QStringLiteral("Referenced"));
        referenced.userPlaced = false;
        a->addItem(clip(QStringLiteral("Placed")));
        a->addItem(referenced);
        QCOMPARE(f.view(0)->visibleCount(), 1);
        model::BinDisplay d = a->bin().display;
        d.showReferenceClips = true;
        a->setDisplay(d);
        QCOMPARE(f.view(0)->visibleCount(), 2);
    }

    void quickFilterSearchesSelectedColumns() {
        Fixture f;
        BinView* v = f.view(0);
        model::BinItem a = clip(QStringLiteral("Alpha"));
        a.fields.insert(QStringLiteral("Comments"), QStringLiteral("beta take"));
        f.doc(0)->addItem(a);
        f.doc(0)->addItem(clip(QStringLiteral("Beta")));
        v->setColumns(QStringList(f.doc(0)->bin().display.columns) << QStringLiteral("Comments"));
        v->setQuickFilter(QStringLiteral("beta"));
        QCOMPARE(v->visibleCount(), 1); // Name only
        // With a column selected, that column (p.294).
        v->selectHeadings({QStringLiteral("Comments")});
        QCOMPARE(v->visibleCount(), 1);
        QCOMPARE(v->visibleIds(), QStringList{a.id});
    }

    void timecodeRangeSift() {
        Fixture f;
        BinView* v = f.view(0);
        model::BinItem a = clip(QStringLiteral("A"));
        a.fields.insert(QStringLiteral("Start"), QStringLiteral("01:00:00:00"));
        a.fields.insert(QStringLiteral("End"), QStringLiteral("01:00:10:00"));
        model::BinItem b = clip(QStringLiteral("B"));
        b.fields.insert(QStringLiteral("Start"), QStringLiteral("02:00:00:00"));
        b.fields.insert(QStringLiteral("End"), QStringLiteral("02:00:10:00"));
        f.doc(0)->addItem(a);
        f.doc(0)->addItem(b);
        v->setColumns({QStringLiteral("Start"), QStringLiteral("End")});
        // The range is offered once both columns show (p.290).
        QVERIFY(model::siftRangesFor(f.doc(0)->bin().display.columns).contains(QStringLiteral("Start to End Range")));
        model::BinSift sift;
        sift.find = {{QStringLiteral("1000512"), QStringLiteral("Start to End Range")}};
        v->sift(sift);
        QCOMPARE(v->visibleIds(), QStringList{a.id});
    }

    void tabAndEnterMoveBetweenCells() {
        Fixture f;
        BinView* v = f.view(0);
        f.doc(0)->addItem(clip(QStringLiteral("One")));
        f.doc(0)->addItem(clip(QStringLiteral("Two")));
        v->setColumns({QStringLiteral("Comments"), QStringLiteral("Take")});
        QTableView* t = v->table();
        t->setFocus();
        const QModelIndex name = t->model()->index(0, BinItemModel::kNameColumn);
        t->setCurrentIndex(name);
        t->edit(name);
        settle();
        auto* editor = t->findChild<QLineEdit*>();
        QVERIFY(editor);
        editor->setText(QStringLiteral("First"));
        // Tab: the next column (p.270).
        QTest::keyClick(editor, Qt::Key_Tab);
        settle();
        QCOMPARE(f.doc(0)->bin().items.first().name, QStringLiteral("First"));
        QCOMPARE(t->currentIndex().column(), v->itemModel()->columnOf(QStringLiteral("Comments")));
        QCOMPARE(t->currentIndex().row(), 0);
        // Enter: the next row, same column.
        auto* comments = t->findChild<QLineEdit*>();
        QVERIFY(comments);
        comments->setText(QStringLiteral("good"));
        QTest::keyClick(comments, Qt::Key_Return);
        settle();
        QCOMPARE(f.doc(0)->bin().items.first().fields.value(QStringLiteral("Comments")), QStringLiteral("good"));
        QCOMPARE(t->currentIndex().row(), 1);
        QCOMPARE(t->currentIndex().column(), v->itemModel()->columnOf(QStringLiteral("Comments")));
    }

    void projectBinContainerAndCommands() {
        Project project;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 800);
        window.show();
        window.setProject(project.entry);
        FavoriteBins::paths().clear();

        // Tools > Project (Ctrl+9): a Sidebar-only container named Project
        // (p.243).
        QAction* projectCommand = commands.action(QStringLiteral("tools.project"));
        QCOMPARE(projectCommand->shortcut(), QKeySequence(QStringLiteral("Ctrl+9")));
        projectCommand->trigger();
        Panel* panel = window.panels().panel(QStringLiteral("Project"));
        QVERIFY(window.panels().isOpen(panel));
        QVERIFY(window.panels().isFloating(panel));
        QCOMPARE(panel->title(), QStringLiteral("Project"));
        auto* sidebar = qobject_cast<BinSidebar*>(panel->content());
        QVERIFY(sidebar);
        QCOMPARE(sidebar->model()->rowCount(), 1);
        // Closing it leaves the bins open.
        BinContainer& bins = window.binContainer();
        window.panels().closePanel(panel);
        QCOMPARE(bins.openDocuments().size(), 1);

        // Bin > Add Bin to Favorites, and Set Bin Display is there.
        commands.action(QStringLiteral("bin.add_bin_to_favorites"))->trigger();
        QCOMPARE(FavoriteBins::paths().size(), 1);
        QVERIFY(commands.action(QStringLiteral("bin.set_bin_display"))->isEnabled());
        FavoriteBins::paths().clear();
        window.clearProject();
    }
};

QTEST_MAIN(TestBinDock)
#include "tst_bin_dock.moc"
