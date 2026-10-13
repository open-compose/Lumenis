#include "lumenis/model/bin.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dialogs.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

struct Project {
    QTemporaryDir dir;
    model::ProjectEntry entry;

    Project() {
        model::ProjectSettings s;
        s.name = QStringLiteral("Doc");
        s.format = projectFormatPresets().front();
        entry = *model::ProjectStore(dir.path()).create(s);
    }
};

// A bin with three clips.
struct Fixture {
    Project project;
    BinContainer container;
    BinView* view = nullptr;
    QStringList ids;

    Fixture() {
        container.resize(900, 500);
        container.show();
        container.setProject(project.entry.folder, project.entry.settings.name, Rational(25));
        view = container.activeView();
        const struct {
            const char* name;
            int frames;
            const char* scene;
            const char* comments;
        } clips[] = {
            {"Interview 10", 250, "2", "close-up"},
            {"interview 2", 100, "1", "wide"},
            {"B-roll", 500, "2", "close-up of hands"},
        };
        for (const auto& c : clips) {
            model::BinItem i;
            i.id = model::newItemId();
            i.name = QLatin1String(c.name);
            i.durationFrames = c.frames;
            i.editRate = Rational(25);
            i.fields.insert(QStringLiteral("Scene"), QLatin1String(c.scene));
            i.fields.insert(QStringLiteral("Comments"), QLatin1String(c.comments));
            doc().addItem(i);
            ids << i.id;
        }
    }

    BinDocument& doc() const { return view->document(); }

    QStringList names() const {
        QStringList out;
        for (const model::BinItem& i : doc().bin().items) out << i.name;
        return out;
    }

    QStringList headings() const {
        QStringList out;
        QHeaderView* h = view->table()->horizontalHeader();
        for (int v = 0; v < h->count(); ++v) {
            out << view->itemModel()->heading(h->logicalIndex(v));
        }
        return out;
    }

    static void settle() {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
};

} // namespace

class TestBinViews : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void switchesBetweenTextFrameAndScriptViews() {
        Fixture f;
        auto* stack = f.view->findChild<QStackedWidget*>();
        QCOMPARE(stack->currentWidget(), f.view->table());
        QVERIFY(f.view->viewMenu()->isEnabled());

        f.view->selectIds({f.ids.at(1)});
        f.view->findChild<QAbstractButton*>(QStringLiteral("frameViewButton"))->click();
        QCOMPARE(f.view->mode(), model::BinViewMode::Frame);
        QCOMPARE(stack->currentWidget(), f.view->frameView());
        // The Bin View menu works in Text view only (p.248).
        QVERIFY(!f.view->viewMenu()->isEnabled());
        // The selection is shared by the views.
        QCOMPARE(f.view->selectedIds(), QStringList{f.ids.at(1)});
        QCOMPARE(f.view->frameView()->model()->rowCount(), 3);

        f.view->findChild<QAbstractButton*>(QStringLiteral("scriptViewButton"))->click();
        QCOMPARE(stack->currentWidget(), f.view->scriptView());
        QCOMPARE(f.view->scriptView()->findChildren<QPlainTextEdit*>(QStringLiteral("scriptText")).size(), 3);
        QCOMPARE(f.view->selectedIds(), QStringList{f.ids.at(1)});

        // The view is saved with the bin.
        QVERIFY(f.container.saveBin(&f.doc()));
        QCOMPARE(model::BinFile::read(f.doc().path())->display.mode, model::BinViewMode::Script);
    }

    void binViewMenuAppliesAndSavesViews() {
        Fixture f;
        BinViewLibrary::views().clear();
        f.view->applyView(QStringLiteral("Capture"));
        QCOMPARE(f.doc().bin().display.columns, (QStringList{"Start", "End", "Tape", "Tracks", "Video"}));
        QCOMPARE(f.view->viewMenu()->currentText(), QStringLiteral("Capture"));
        QVERIFY(!f.view->viewMenu()->font().italic());

        // A changed view reads "<view>.1" in italic (p.249).
        f.view->hideColumns({QStringLiteral("Tape")});
        QVERIFY(f.doc().bin().display.viewModified);
        QCOMPARE(f.view->viewMenu()->currentText(), QStringLiteral("Capture.1"));
        QVERIFY(f.view->viewMenu()->font().italic());

        // Save As keeps the view for every bin.
        f.view->saveViewAs(QStringLiteral("Logging"));
        QCOMPARE(f.view->viewMenu()->currentText(), QStringLiteral("Logging"));
        QVERIFY(f.view->viewMenu()->findData(QStringLiteral("Logging")) >= 0);
        QTemporaryDir dir;
        {
            QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
            BinViewLibrary::save(settings);
        }
        BinViewLibrary::views().clear();
        {
            QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
            BinViewLibrary::load(settings);
        }
        QVERIFY(BinViewLibrary::find(QStringLiteral("Logging")));
        QCOMPARE(BinViewLibrary::find(QStringLiteral("Logging"))->columns, (QStringList{"Start", "End", "Tracks", "Video"}));
        BinViewLibrary::views().clear();
    }

    void chosenColumnsGoBesideTheSelectedOne() {
        Fixture f;
        QCOMPARE(f.headings(), (QStringList{"", "Name", "Creation Date", "Duration", "Drive", "IN-OUT", "TapeID"}));
        f.view->selectHeadings({QStringLiteral("Duration")});
        f.view->setColumns(QStringList(f.doc().bin().display.columns) << QStringLiteral("Scene"));
        QCOMPARE(f.headings(),
                 (QStringList{"", "Name", "Creation Date", "Duration", "Scene", "Drive", "IN-OUT", "TapeID"}));
        // With nothing selected, at the far right (p.247).
        f.view->selectHeadings({});
        f.view->setColumns(QStringList(f.doc().bin().display.columns) << QStringLiteral("Comments"));
        QCOMPARE(f.headings().last(), QStringLiteral("Comments"));
        // Hide Column.
        f.view->hideColumns({QStringLiteral("Drive"), QStringLiteral("IN-OUT")});
        QCOMPARE(f.headings(), (QStringList{"", "Name", "Creation Date", "Duration", "Scene", "TapeID", "Comments"}));

        // The Bin Column Selection dialog: All/None, and Name is not offered.
        ChooseColumnsDialog dialog(QStringList(model::binHeadings()).mid(1), f.doc().bin().display.columns);
        QStringList shown = f.doc().bin().display.columns;
        shown.sort();
        QStringList chosen = dialog.chosen();
        chosen.sort();
        QCOMPARE(chosen, shown);
        auto* allNone = dialog.findChild<QAbstractButton*>(QStringLiteral("allNone"));
        allNone->click();
        QCOMPARE(dialog.chosen().size(), model::binHeadings().size() - 1);
        allNone->click();
        QVERIFY(dialog.chosen().isEmpty());
    }

    void sortsByDoubleClickingAHeading() {
        Fixture f;
        QHeaderView* header = f.view->table()->horizontalHeader();
        const int name = BinItemModel::kNameColumn;
        f.view->selectIds({f.ids.at(0)});
        emit header->sectionDoubleClicked(name);
        QCOMPARE(f.names(), (QStringList{"B-roll", "interview 2", "Interview 10"}));
        // The selection stays (p.247).
        QCOMPARE(f.view->selectedIds(), QStringList{f.ids.at(0)});
        emit header->sectionDoubleClicked(name);
        QCOMPARE(f.names(), (QStringList{"Interview 10", "interview 2", "B-roll"}));

        // Sort Again after adding a clip.
        emit header->sectionDoubleClicked(name);
        model::BinItem extra;
        extra.id = model::newItemId();
        extra.name = QStringLiteral("A-roll");
        f.doc().addItem(extra);
        QCOMPARE(f.names().last(), QStringLiteral("A-roll"));
        f.view->sortAgain();
        QCOMPARE(f.names().first(), QStringLiteral("A-roll"));
    }

    void sortsOnSeveralSelectedHeadings() {
        Fixture f;
        f.view->setColumns({QStringLiteral("Duration"), QStringLiteral("Scene")});
        QVERIFY(!f.view->sortSelected()); // nothing selected: Sort is dimmed (p.247)
        // The leftmost selected heading comes first, whatever the click order.
        f.view->selectHeadings({QStringLiteral("Duration"), QStringLiteral("Scene")});
        // Move Scene left of Duration.
        QHeaderView* header = f.view->table()->horizontalHeader();
        header->moveSection(header->visualIndex(f.view->itemModel()->columnOf(QStringLiteral("Scene"))),
                            header->visualIndex(f.view->itemModel()->columnOf(QStringLiteral("Duration"))));
        Fixture::settle();
        QCOMPARE(f.doc().bin().display.columns, (QStringList{"Scene", "Duration"}));
        f.view->selectHeadings({QStringLiteral("Duration"), QStringLiteral("Scene")});
        QVERIFY(f.view->sortSelected());
        QCOMPARE(f.names(), (QStringList{"interview 2", "Interview 10", "B-roll"}));
        QCOMPARE(f.doc().bin().display.sort.first().heading, QStringLiteral("Scene"));
    }

    void siftsTheBin() {
        Fixture f;
        model::BinSift sift;
        sift.find = {{QStringLiteral("close-up"), {}}};
        // "Any" searches the shown columns: Comments is not shown yet.
        f.view->sift(sift);
        QCOMPARE(f.view->visibleCount(), 0);
        f.view->setColumns(QStringList(f.doc().bin().display.columns) << QStringLiteral("Comments"));
        QCOMPARE(f.view->visibleCount(), 2);
        QCOMPARE(f.container.tabs()->tabText(0), QStringLiteral("* Doc (sifted)"));

        f.view->setSiftShown(false); // Show Unsifted
        QCOMPARE(f.view->visibleCount(), 3);
        QCOMPARE(f.container.tabs()->tabText(0), QStringLiteral("* Doc"));
        f.view->setSiftShown(true); // Show Sifted
        QCOMPARE(f.view->visibleCount(), 2);

        // Sift Selected Items (p.289).
        f.view->setSiftShown(false);
        f.view->selectIds({f.ids.at(1)});
        f.view->siftSelected();
        QCOMPARE(f.view->visibleIds(), QStringList{f.ids.at(1)});

        // The Custom Sift dialog reads its rows.
        CustomSiftDialog dialog({QStringLiteral("Name"), QStringLiteral("Comments")}, {});
        QCOMPARE(dialog.findChildren<QLineEdit*>().size(), 2 * CustomSiftDialog::kRows);
        dialog.setRow(false, 0, QStringLiteral("wide"), QStringLiteral("Comments"));
        dialog.setRow(true, 1, QStringLiteral("B-"), QString());
        const model::BinSift read = dialog.sift();
        QCOMPARE(read.find, (QList<model::SiftRow>{{QStringLiteral("wide"), QStringLiteral("Comments")}}));
        QCOMPARE(read.also, (QList<model::SiftRow>{{QStringLiteral("B-"), QString()}}));
        f.view->sift(read);
        QCOMPARE(f.view->visibleIds(), (QStringList{f.ids.at(1), f.ids.at(2)}));
    }

    void customColumns() {
        Fixture f;
        const QString untitled = f.view->addCustomColumn();
        QCOMPARE(untitled, QStringLiteral("Untitled")); // p.269
        QCOMPARE(f.headings().last(), untitled);
        QVERIFY(f.view->renameColumn(untitled, QStringLiteral("Shot")));
        QVERIFY(!f.view->renameColumn(QStringLiteral("Shot"), QStringLiteral("Duration"))); // a standard heading
        QVERIFY(!f.view->renameColumn(QStringLiteral("Duration"), QStringLiteral("Length")));
        QCOMPARE(f.headings().last(), QStringLiteral("Shot"));

        // Custom and modifiable cells can be typed into (p.278).
        BinItemModel* m = f.view->itemModel();
        const int shot = m->columnOf(QStringLiteral("Shot"));
        QVERIFY(m->flags(m->index(0, shot)) & Qt::ItemIsEditable);
        QVERIFY(m->setData(m->index(0, shot), QStringLiteral("CU"), Qt::EditRole));
        QCOMPARE(f.doc().bin().items.first().fields.value(QStringLiteral("Shot")), QStringLiteral("CU"));
        QVERIFY(!(m->flags(m->index(0, m->columnOf(QStringLiteral("Duration")))) & Qt::ItemIsEditable));
        f.view->setColumns(QStringList(f.doc().bin().display.columns) << QStringLiteral("Take"));
        QVERIFY(m->setData(m->index(0, m->columnOf(QStringLiteral("Take"))), QStringLiteral("3"), Qt::EditRole));

        // Deleting a standard column hides it; deleting a custom one deletes
        // what was typed in it (p.268).
        f.view->deleteColumns({QStringLiteral("Drive"), QStringLiteral("Shot")}, false);
        QVERIFY(!f.headings().contains(QStringLiteral("Drive")));
        QVERIFY(!f.headings().contains(QStringLiteral("Shot")));
        QVERIFY(!f.doc().bin().display.customColumns.contains(QStringLiteral("Shot")));
        QVERIFY(!f.doc().bin().items.first().fields.contains(QStringLiteral("Shot")));
    }

    void coloursAndLocks() {
        Fixture f;
        f.view->setColumns(QStringList{QStringLiteral("Color"), QStringLiteral("Lock")});
        f.view->selectIds({f.ids.at(0), f.ids.at(2)});
        f.view->setClipColor(QStringLiteral("#c83232"));
        QCOMPARE(f.doc().bin().item(f.ids.at(0))->color, QStringLiteral("#c83232"));
        QVERIFY(f.doc().bin().item(f.ids.at(1))->color.isEmpty());
        BinItemModel* m = f.view->itemModel();
        const QModelIndex color = m->index(0, m->columnOf(QStringLiteral("Color")));
        QCOMPARE(color.data(Qt::DecorationRole).value<QColor>(), QColor(0xc8, 0x32, 0x32));
        QCOMPARE(color.data(Qt::ToolTipRole).toString(), QStringLiteral("Red"));

        // Right-click in the Color column offers colours; elsewhere, Set Clip
        // Color and Set Background Color.
        QTableView* table = f.view->table();
        const QRect cell = table->visualRect(table->model()->index(0, m->columnOf(QStringLiteral("Color"))));
        QMenu* colours = f.view->contextMenu(cell.center(), table);
        QVERIFY(colours->actions().size() > model::clipColors().size());
        colours->deleteLater();
        const QRect nameCell = table->visualRect(table->model()->index(0, BinItemModel::kNameColumn));
        QMenu* menu = f.view->contextMenu(nameCell.center(), table);
        QStringList texts;
        for (QAction* a : menu->actions()) texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("Set Clip Color")));
        QVERIFY(texts.contains(QStringLiteral("Set Background Color")));
        menu->deleteLater();

        f.view->selectIds({f.ids.at(1)});
        f.view->setLocked(true);
        QVERIFY(f.doc().bin().item(f.ids.at(1))->locked);
        QCOMPARE(m->index(1, m->columnOf(QStringLiteral("Lock"))).data().toString(), QStringLiteral("Locked"));
        f.view->setBackground(QStringLiteral("#303050"));
        QCOMPARE(f.view->table()->palette().color(QPalette::Base), QColor(0x30, 0x30, 0x50));
        QCOMPARE(f.view->frameView()->palette().color(QPalette::Base), QColor(0x30, 0x30, 0x50));
    }

    void frameViewSizesAndAlignment() {
        Fixture f;
        f.view->setMode(model::BinViewMode::Frame);
        BinFrameView* frames = f.view->frameView();
        const QSize start = frames->cellSize();
        f.view->enlargeFrame();
        QVERIFY(frames->cellSize().width() > start.width());
        // Seven steps at most (p.250).
        for (int i = 0; i < 10; ++i) f.view->enlargeFrame();
        QCOMPARE(f.doc().bin().display.frameSize, model::BinDisplay::kMaxFrameSize);
        for (int i = 0; i < 10; ++i) f.view->reduceFrame();
        QCOMPARE(f.doc().bin().display.frameSize, 0);
        f.view->enlargeFrame();
        f.view->enlargeFrame();
        Fixture::settle();

        // Fill Window places every frame and remembers it.
        frames->fillWindow();
        QCOMPARE(f.doc().bin().display.framePositions.size(), 3);
        const QSize c = frames->cellSize();
        QCOMPARE(frames->framePosition(f.ids.at(0)), QPoint(0, 0));
        QCOMPARE(frames->framePosition(f.ids.at(1)).y(), 0);

        // Align to Grid moves frames to grid points, without stacking them.
        QHash<QString, QPoint> moved = f.doc().bin().display.framePositions;
        moved.insert(f.ids.at(2), QPoint(c.width() / 3, c.height() + 5));
        moved.insert(f.ids.at(1), QPoint(5, 7));
        f.doc().setFramePositions(moved);
        frames->doItemsLayout();
        QCOMPARE(frames->framePosition(f.ids.at(1)), QPoint(5, 7));
        frames->alignToGrid(false);
        QList<QPoint> points;
        for (const QString& id : f.ids) points << frames->framePosition(id);
        for (const QPoint& p : points) {
            QCOMPARE(p.x() % c.width(), 0);
            QCOMPARE(p.y() % c.height(), 0);
            QCOMPARE(points.count(p), 1);
        }
        QCOMPARE(f.doc().bin().display.framePositions.value(f.ids.at(2)), QPoint(0, c.height()));

        // Placed frames come back when the bin is reopened.
        QVERIFY(f.container.saveBin(&f.doc()));
        const auto saved = model::BinFile::read(f.doc().path());
        QCOMPARE(saved->display.framePositions.value(f.ids.at(2)), QPoint(0, c.height()));

        // Fill Sorted lays them out in the bin's order again.
        frames->fillSorted();
        QVERIFY(f.doc().bin().display.framePositions.isEmpty());
        QCOMPARE(frames->framePosition(f.ids.at(0)), QPoint(0, 0));

        // The tooltip has Duration, Format and Creation Date (p.250).
        const QString tip = frames->model()->index(0, BinItemModel::kNameColumn).data(BinItemModel::kSummaryRole).toString();
        QVERIFY(tip.contains(QStringLiteral("Duration: 10:00")));
        QVERIFY(tip.contains(QStringLiteral("Format:")));
        QVERIFY(tip.contains(QStringLiteral("Creation Date:")));

        // Snap to Grid is in the bin's menu in Frame view (p.252).
        QMenu* menu = f.view->contextMenu(QPoint(1, 1), frames);
        QStringList texts;
        for (QAction* a : menu->actions()) texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("Snap to Grid")));
        menu->deleteLater();
        f.view->setSnapToGrid(model::SnapToGrid::Enable);
        QCOMPARE(f.doc().bin().display.snap, model::SnapToGrid::Enable);
    }

    void scriptViewTextAndOrder() {
        Fixture f;
        f.view->setMode(model::BinViewMode::Script);
        QWidget* row = f.view->scriptView()->rowFor(f.ids.at(1));
        QVERIFY(row);
        auto* text = row->findChild<QPlainTextEdit*>(QStringLiteral("scriptText"));
        QVERIFY(f.container.saveBin(&f.doc()));
        QVERIFY(!f.doc().isDirty());
        QTest::keyClicks(text, QStringLiteral("He looks away."));
        QCOMPARE(f.doc().bin().item(f.ids.at(1))->script, QStringLiteral("He looks away."));
        QVERIFY(f.doc().isDirty());
        // Typing does not rebuild the rows, so the text box keeps the focus.
        QCOMPARE(f.view->scriptView()->rowFor(f.ids.at(1)), row);

        // Clips dragged to a new place; Text view shows the same order (p.254).
        f.doc().moveItems({f.ids.at(2)}, 0);
        QCOMPARE(f.names(), (QStringList{"B-roll", "Interview 10", "interview 2"}));
        f.doc().moveItems({f.ids.at(2)}, 3);
        QCOMPARE(f.names(), (QStringList{"Interview 10", "interview 2", "B-roll"}));

        // Clicking a row selects it.
        Fixture::settle();
        QWidget* other = f.view->scriptView()->rowFor(f.ids.at(0));
        QTest::mouseClick(other, Qt::LeftButton, {}, QPoint(2, 2));
        QCOMPARE(f.view->selectedIds(), QStringList{f.ids.at(0)});
    }

    void menuCommands() {
        Project project;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.show();
        window.setProject(project.entry);
        BinContainer& bins = window.binContainer();
        commands.action(QStringLiteral("timeline.new.sequence"))->trigger();
        commands.action(QStringLiteral("timeline.new.sequence"))->trigger();
        BinView* view = bins.activeView();
        QVERIFY(view);
        const QStringList all = view->visibleIds();
        view->selectIds({all.first()});
        commands.action(QStringLiteral("bin.select.reverse"))->trigger();
        QCOMPARE(view->selectedIds(), QStringList{all.last()});

        commands.action(QStringLiteral("clip.lock_bin_selection"))->trigger();
        QVERIFY(view->document().bin().item(all.last())->locked);
        commands.action(QStringLiteral("clip.unlock_bin_selection"))->trigger();
        QVERIFY(!view->document().bin().item(all.last())->locked);

        commands.action(QStringLiteral("bin.sift_selected_items"))->trigger();
        QCOMPARE(view->visibleCount(), 1);
        commands.action(QStringLiteral("bin.show_unsifted"))->trigger();
        QCOMPARE(view->visibleCount(), 2);

        // Enlarge Frame works in Frame and Script views.
        const int size = view->document().bin().display.frameSize;
        commands.action(QStringLiteral("edit.enlarge_frame"))->trigger();
        QCOMPARE(view->document().bin().display.frameSize, size);
        view->setMode(model::BinViewMode::Frame);
        commands.action(QStringLiteral("edit.enlarge_frame"))->trigger();
        QCOMPARE(view->document().bin().display.frameSize, size + 1);
        commands.action(QStringLiteral("edit.reduce_frame"))->trigger();
        QCOMPARE(view->document().bin().display.frameSize, size);
        QVERIFY(commands.action(QStringLiteral("bin.align_and_fill.fill_window"))->isEnabled());
        QVERIFY(commands.action(QStringLiteral("bin.align_and_fill.align_columns"))->isEnabled());

        // Edit > Delete with a heading selected removes the column.
        view->setMode(model::BinViewMode::Text);
        view->selectHeadings({QStringLiteral("Drive")});
        window.panels().activate(window.panels().panel(QStringLiteral("BinContainer")));
        commands.action(QStringLiteral("edit.delete"))->trigger();
        QVERIFY(!view->document().bin().display.columns.contains(QStringLiteral("Drive")));
        QCOMPARE(view->document().bin().items.size(), 2);
        window.clearProject();
    }
};

QTEST_MAIN(TestBinViews)
#include "tst_bin_views.moc"
