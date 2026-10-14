#include "lumenis/model/bin.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QSignalSpy>
#include <QTabWidget>
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

struct Fixture {
    Project project;
    BinContainer container;

    Fixture() {
        container.resize(900, 500);
        container.show();
        container.setProject(project.entry.folder, project.entry.settings.name, Rational(25));
    }

    QStringList tabTexts() const {
        QStringList out;
        for (int i = 0; i < container.tabs()->count(); ++i) out << container.tabs()->tabText(i);
        return out;
    }

    QStringList treeNames(const QModelIndex& parent = {}) const {
        QStringList out;
        BinTreeModel* m = container.treeModel();
        for (int r = 0; r < m->rowCount(parent); ++r) out << m->index(r, 0, parent).data().toString();
        return out;
    }

    static void settle() {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
};

} // namespace

class TestBinContainer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void showsTheProjectBinAndOpensIt() {
        Fixture f;
        QCOMPARE(f.treeNames(), QStringList{"Doc"});
        QCOMPARE(f.tabTexts(), QStringList{"Doc"});
        QVERIFY(f.container.activeDocument());
        // An open bin is dimmed in the list (p.295).
        const QModelIndex doc = f.container.treeModel()->index(0, 0);
        QVERIFY(doc.data(Qt::ForegroundRole).isValid());
        QCOMPARE(f.container.treeModel()->index(0, BinTreeModel::kSizeColumn).data().toString(), QStringLiteral("1K"));
    }

    void createsNumberedBinsAndRenamesThem() {
        Fixture f;
        const QString id = f.container.newBin();
        QVERIFY(!id.isEmpty());
        QCOMPARE(f.treeNames(), (QStringList{"Doc", "Doc.1"}));
        QCOMPARE(f.tabTexts(), (QStringList{"Doc", "Doc.1"}));

        // Typing a new name in the list renames the bin and its file.
        BinTreeModel* m = f.container.treeModel();
        QVERIFY(m->setData(m->indexOf(id), QStringLiteral("Interviews"), Qt::EditRole));
        Fixture::settle();
        QCOMPARE(f.treeNames(), (QStringList{"Doc", "Interviews"}));
        QCOMPARE(f.tabTexts(), (QStringList{"Doc", "Interviews"}));
        QVERIFY(QFileInfo::exists(QDir(f.project.entry.folder).filePath(QStringLiteral("Interviews.cthbin"))));
        QVERIFY(!m->setData(m->indexOf(id), QStringLiteral("bad/name"), Qt::EditRole));
    }

    void marksUnsavedBinsAndSavesThem() {
        Fixture f;
        BinDocument* doc = f.container.activeDocument();
        const QString seq = f.container.newSequence();
        QVERIFY(!seq.isEmpty());
        QVERIFY(doc->isDirty());
        QCOMPARE(f.tabTexts(), QStringList{"* Doc"});
        QCOMPARE(doc->bin().items.first().name, QStringLiteral("Untitled Sequence.01"));
        QCOMPARE(doc->bin().items.first().editRate, Rational(25)); // the project's
        QCOMPARE(f.container.activeView()->selectedIds(), QStringList{seq});

        QVERIFY(f.container.saveBin(doc));
        QCOMPARE(f.tabTexts(), QStringList{"Doc"});
        const auto onDisk = model::BinFile::read(doc->path());
        QVERIFY(onDisk);
        QCOMPARE(onDisk->items.size(), 1);
    }

    void duplicatesDeletesAndRespectsLocks() {
        Fixture f;
        BinView* view = f.container.activeView();
        const QString a = f.container.newSequence();
        f.container.newSequence();
        view->selectIds({a});
        const QStringList copies = f.container.duplicateSelection();
        QCOMPARE(copies.size(), 1);
        QCOMPARE(view->document().bin().item(copies.first())->name, QStringLiteral("Untitled Sequence.01.Copy.01"));
        QCOMPARE(view->selectedIds(), copies);

        f.container.selectAll();
        QCOMPARE(view->selectedIds().size(), 3);
        // A locked item stays.
        model::BinItem locked = *view->document().bin().item(a);
        view->document().removeItems({a});
        locked.locked = true;
        view->document().addItem(locked);
        f.container.selectAll();
        QCOMPARE(f.container.deleteSelection(), 2);
        QCOMPARE(view->document().bin().items.size(), 1);
        QVERIFY(view->document().bin().items.first().locked);
    }

    void namesCanBeTypedInTheBin() {
        Fixture f;
        BinView* view = f.container.activeView();
        f.container.newSequence();
        QAbstractItemModel* m = view->table()->model();
        QVERIFY(m->setData(m->index(0, BinItemModel::kNameColumn), QStringLiteral("Rough Cut"), Qt::EditRole));
        QCOMPARE(view->document().bin().items.first().name, QStringLiteral("Rough Cut"));
        QCOMPARE(m->headerData(BinItemModel::kNameColumn, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Name"));
        // Duration is shown, not typed into.
        const int duration = static_cast<int>(model::Bin::defaultColumns().indexOf(QStringLiteral("Duration"))) + 2;
        QCOMPARE(m->index(0, duration).data().toString(), QStringLiteral("0:00"));
        QVERIFY(!(m->flags(m->index(0, duration)) & Qt::ItemIsEditable));
    }

    void filtersItemsInTheBin() {
        Fixture f;
        BinView* view = f.container.activeView();
        f.container.newSequence();
        f.container.newSequence();
        QAbstractItemModel* m = view->table()->model();
        m->setData(m->index(1, BinItemModel::kNameColumn), QStringLiteral("Trailer"), Qt::EditRole);
        view->setQuickFilter(QStringLiteral("trail"));
        QCOMPARE(view->visibleCount(), 1);
        view->setQuickFilter({});
        QCOMPARE(view->visibleCount(), 2);
    }

    void foldersTrashAndEmptying() {
        Fixture f;
        const QString bin = f.container.newBin();
        const QString folder = f.container.newFolder();
        QVERIFY(f.container.moveNode(bin, folder, -1));
        Fixture::settle();
        QCOMPARE(f.treeNames(), (QStringList{"Doc", "Untitled Folder"}));
        QCOMPARE(f.treeNames(f.container.treeModel()->indexOf(folder)), QStringList{"Doc.1"});

        // Deleting the folder closes its open bin and fills the Trash (p.297).
        QVERIFY(f.container.isOpen(bin));
        QVERIFY(f.container.deleteNode(folder));
        QVERIFY(!f.container.isOpen(bin));
        QCOMPARE(f.treeNames(), (QStringList{"Doc", "Trash"}));
        const QString path = f.container.bins()->binPath(bin);
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(f.container.emptyTrash());
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(f.treeNames(), QStringList{"Doc"});
    }

    void flatViewAndBinFilter() {
        Fixture f;
        const QString folder = f.container.newFolder();
        const QString bin = f.container.newBin(folder);
        f.container.renameNode(bin, QStringLiteral("Selects"));
        Fixture::settle();
        QCOMPARE(f.treeNames(), (QStringList{"Doc", "Untitled Folder"}));
        f.container.setFlatView(true);
        QCOMPARE(f.treeNames(), (QStringList{"Doc", "Selects"}));
        f.container.setFlatView(false);
        f.container.setBinFilter(QStringLiteral("sel"));
        QCOMPARE(f.treeNames(), QStringList{"Selects"});
        f.container.setBinFilter({});
        QCOMPARE(f.treeNames(), (QStringList{"Doc", "Untitled Folder"}));
    }

    void reopensTheBinsOpenLastTime() {
        Project project;
        QString second;
        {
            BinContainer c;
            c.setProject(project.entry.folder, project.entry.settings.name, Rational(25));
            second = c.newBin();
            c.newSequence(); // unsaved, into Doc.1
            c.closeBin(c.document(c.bins()->bins().first()->id));
        }
        BinContainer c;
        c.setProject(project.entry.folder, project.entry.settings.name, Rational(25));
        QCOMPARE(c.openDocuments().size(), 1);
        QCOMPARE(c.activeDocument()->nodeId(), second);
        // Changes were saved when the container went.
        QCOMPARE(c.activeDocument()->bin().items.size(), 1);
        c.setProject({}, {}, Rational(25));
        QVERIFY(!c.hasProject());
        QVERIFY(c.binArea()->views().isEmpty());
    }

    void opensBinsFromOtherProjectsInItalic() {
        Fixture f;
        Project other(QStringLiteral("Other"));
        const QString foreign = QDir(other.entry.folder).filePath(QStringLiteral("Other.cthbin"));
        const QString id = f.container.openOtherBin(foreign);
        QVERIFY(!id.isEmpty());
        QCOMPARE(f.treeNames(), (QStringList{"Doc", "Other Bins"}));
        BinTreeModel* m = f.container.treeModel();
        QVERIFY(m->indexOf(id).data(Qt::FontRole).value<QFont>().italic());
        QCOMPARE(f.container.activeDocument()->name(), QStringLiteral("Other"));
    }

    void sidebarOnlyAndHiddenSidebar() {
        Fixture f;
        f.container.setSidebarOnly(true);
        QVERIFY(f.container.isSidebarOnly());
        QVERIFY(f.container.binArea()->isHidden());
        f.container.setSidebarOnly(false);
        f.container.setSidebarVisible(false);
        QVERIFY(!f.container.isSidebarVisible());
        QVERIFY(f.container.binArea()->isVisible());
        f.container.setSidebarVisible(true);
        QVERIFY(f.container.isSidebarVisible());
    }

    void deleteKeyInABin() {
        Fixture f;
        BinView* view = f.container.activeView();
        f.container.newSequence();
        view->selectAll();
        view->table()->setFocus();
        QTest::keyClick(view->table(), Qt::Key_Delete);
        QVERIFY(view->document().bin().items.isEmpty());
    }

    void mainWindowCommands() {
        Project project;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.show();
        QAction* newSequence = commands.action(QStringLiteral("timeline.new.sequence"));
        QAction* saveAll = commands.action(QStringLiteral("file.save_all"));
        QVERIFY(!newSequence->isEnabled()); // no project
        window.setProject(project.entry);
        QVERIFY(newSequence->isEnabled());
        QVERIFY(saveAll->isEnabled());
        BinContainer& bins = window.binContainer();
        QCOMPARE(bins.openDocuments().size(), 1);
        QCOMPARE(window.panels().panel(QStringLiteral("BinContainer"))->title(), QStringLiteral("Bins"));

        newSequence->trigger();
        QCOMPARE(bins.activeDocument()->bin().items.size(), 1);
        QVERIFY(bins.activeDocument()->isDirty());
        saveAll->trigger();
        QVERIFY(!bins.activeDocument()->isDirty());

        window.panels().activate(window.panels().panel(QStringLiteral("BinContainer")));
        bins.selectAll();
        commands.action(QStringLiteral("edit.duplicate"))->trigger();
        QCOMPARE(bins.activeDocument()->bin().items.size(), 2);
        // Edit commands leave bins alone when another window is active.
        window.panels().activate(window.panels().panel(QStringLiteral("Timeline")));
        commands.action(QStringLiteral("edit.delete"))->trigger();
        QCOMPARE(bins.activeDocument()->bin().items.size(), 2);

        QAction* sidebarOnly = commands.action(QStringLiteral("bin.show_sidebar_only"));
        sidebarOnly->trigger();
        QVERIFY(bins.isSidebarOnly());
        sidebarOnly->trigger();
        QVERIFY(!bins.isSidebarOnly());

        commands.action(QStringLiteral("file.close_bin"))->trigger();
        Fixture::settle();
        QCOMPARE(bins.openDocuments().size(), 0);

        window.clearProject();
        QVERIFY(!newSequence->isEnabled());
        QVERIFY(!bins.hasProject());
    }
};

QTEST_MAIN(TestBinContainer)
#include "tst_bin_container.moc"
