// The Find window (Edit > Find, Ctrl+F; Editing Guide p.417, p.428-431; docs/spec/find.md): text find
// in every bin of the project, open or closed, filters, Ignore Case and Whole Words, the Current Bin with
// Ctrl+G, the Timeline and Monitors tab, the Markers tab and Select Columns.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/text_find.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/find_window.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;
using namespace lumenis::ui;

namespace {

BinItem clip(const QString& name, std::int64_t frames) {
    BinItem c;
    c.id = model::newItemId();
    c.name = name;
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = frames;
    return c;
}

Marker marker(const QString& comment, std::int64_t frame, const QString& track = QStringLiteral("V1")) {
    Marker m;
    m.id = model::newItemId();
    m.color = QStringLiteral("Red");
    m.track = track;
    m.frame = frame;
    m.comment = comment;
    return m;
}

QStringList column(QTableWidget* table, int c) {
    QStringList out;
    for (int r = 0; r < table->rowCount(); ++r) out << table->item(r, c)->text();
    return out;
}

} // namespace

class TestFindWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void matching() {
        TextFindOptions o;
        QVERIFY(textMatches(QStringLiteral("Interview Take 2"), QStringLiteral("take"), o));
        o.ignoreCase = false;
        QVERIFY(!textMatches(QStringLiteral("Interview Take 2"), QStringLiteral("take"), o));
        o.ignoreCase = true;
        o.wholeWords = true;
        QVERIFY(!textMatches(QStringLiteral("Interviewer"), QStringLiteral("interview"), o));
        QVERIFY(textMatches(QStringLiteral("the interview, again"), QStringLiteral("interview"), o));
        // Special characters are found as typed (p.428).
        QVERIFY(textMatches(QStringLiteral("shot #3 (alt)"), QStringLiteral("#3 (alt)"), TextFindOptions{}));
        // Any column, including one not shown (p.428), and filters on a column (p.430).
        BinItem c = clip(QStringLiteral("A001"), 10);
        c.fields.insert(QStringLiteral("Comments"), QStringLiteral("great sunset"));
        c.fields.insert(QStringLiteral("Scene"), QStringLiteral("12"));
        QVERIFY(itemMatches(c, QStringLiteral("sunset"), {}, TextFindOptions{}));
        QVERIFY(itemMatches(c, QStringLiteral("sunset"), {{QStringLiteral("Scene"), QStringLiteral("12")}}, TextFindOptions{}));
        QVERIFY(!itemMatches(c, QStringLiteral("sunset"), {{QStringLiteral("Scene"), QStringLiteral("13")}}, TextFindOptions{}));
        QVERIFY(!itemMatches(c, QStringLiteral("sunset"), {{QStringLiteral("Name"), QStringLiteral("sunset")}}, TextFindOptions{}));
    }

    void inTheApplication() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        BinContainer& bins = window.binContainer();
        // Bin one stays open; bin two is saved and closed (p.428: bins need not be open).
        const QString oneId = bins.newBin();
        BinDocument* one = bins.openBin(oneId);
        BinItem interview = clip(QStringLiteral("Interview Take 1"), 100);
        model::setMarkers(interview, {marker(QStringLiteral("laugh here"), 30)});
        one->addItem(interview);
        BinItem broll = clip(QStringLiteral("B-roll"), 100);
        broll.fields.insert(QStringLiteral("Comments"), QStringLiteral("interview location"));
        one->addItem(broll);
        const QString twoId = bins.newBin();
        BinDocument* two = bins.openBin(twoId);
        const BinItem other = clip(QStringLiteral("Interview Take 2"), 80);
        two->addItem(other);
        QVERIFY(two->save());
        QVERIFY(bins.closeBin(two));
        QVERIFY(!bins.document(twoId));

        commands.action(QStringLiteral("edit.find"))->trigger();
        FindWindow* find = window.findWindow();
        QVERIFY(find && find->isVisible());
        QCOMPARE(find->tabs()->count(), 4);
        QVERIFY(find->tabs()->isTabEnabled(FindWindow::ScriptTab)); // scripts are searched (8.3e)

        // Clips and Sequences, Bins in Project: every column of every bin.
        find->setText(QStringLiteral("interview"));
        QCOMPARE(find->find(), 3);
        QStringList names = column(find->results(), 0);
        QVERIFY(names.contains(QStringLiteral("Interview Take 2")) && names.contains(QStringLiteral("B-roll")));
        // Whole words and case.
        find->setText(QStringLiteral("Take"));
        find->setIgnoreCase(false);
        QCOMPARE(find->find(), 2);
        find->setText(QStringLiteral("take"));
        QCOMPARE(find->find(), 0);
        find->setIgnoreCase(true);
        // A filter on a column (the menu fills after a search, p.430).
        find->setText(QStringLiteral("interview"));
        find->find();
        find->setFilter(0, QStringLiteral("Name"), QStringLiteral("take 1"));
        QCOMPARE(find->find(), 1);
        QCOMPARE(find->filterCount(), 1);
        find->removeFilter(0); // the last filter only clears its text
        QCOMPARE(find->filterCount(), 1);
        QCOMPARE(find->find(), 3);
        // Double-click: into the Source monitor with Load into monitor.
        const int row = static_cast<int>(column(find->results(), 0).indexOf(QStringLiteral("Interview Take 2")));
        find->openResult(row);
        QCOMPARE(window.composer().state(MonitorId::Source).current()->item.name, QStringLiteral("Interview Take 2"));
        // Select Columns.
        find->setColumnsAsk([](const QStringList&) { return std::optional<QStringList>(QStringList{QStringLiteral("Comments")}); });
        QMetaObject::invokeMethod(find, "chooseColumns");
        QCOMPARE(find->results()->horizontalHeaderItem(2)->text(), QStringLiteral("Comments"));

        // Current Bin selects the matches in the last active bin; Ctrl+G the next one (p.429).
        find->setScope(FindWindow::Scope::CurrentBin);
        find->setText(QStringLiteral("interview"));
        QCOMPARE(find->find(), 2);
        BinView* view = bins.viewFor(one);
        QCOMPARE(view->selectedIds(), QStringList{interview.id});
        commands.action(QStringLiteral("edit.find_again"))->trigger();
        QCOMPARE(view->selectedIds(), QStringList{broll.id});

        // Markers: comments across the project (p.417); double-click loads the clip at the marker (p.418).
        find->tabs()->setCurrentIndex(FindWindow::MarkersTab);
        find->setText(QStringLiteral("LAUGH"));
        QCOMPARE(find->find(), 1);
        find->openResult(0);
        QCOMPARE(window.composer().state(MonitorId::Source).current()->item.id, interview.id);
        QCOMPARE(window.composer().state(MonitorId::Source).position(), 30);

        // Timeline and Monitors (p.430-431): markers and clip names in the Record monitor's sequence.
        BinItem seqItem;
        seqItem.id = model::newItemId();
        seqItem.name = QStringLiteral("Cut");
        seqItem.kind = BinItemKind::Sequence;
        seqItem.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        Segment a;
        a.clipId = interview.id;
        a.name = QStringLiteral("Interview Take 1");
        a.sourceTrack = QStringLiteral("V1");
        a.length = 50;
        Segment b = a;
        b.name = QStringLiteral("B-roll");
        seq.splice(QStringLiteral("V1"), 0, {a, b, a});
        seq.storeIn(seqItem);
        model::setMarkers(seqItem, {marker(QStringLiteral("interview cue"), 60)});
        one->addItem(seqItem);
        window.composer().load(MonitorId::Record, oneId, seqItem);
        find->tabs()->setCurrentIndex(FindWindow::TimelineTab);
        find->setText(QStringLiteral("interview"));
        QCOMPARE(find->find(), 3); // two clips by name, one marker
        QCOMPARE(column(find->results(), 6), (QStringList{"0", "60", "100"}));
        find->setTimelineKinds(false, true, false);
        QCOMPARE(find->find(), 2);
        // Ctrl+G: the next position that is not the current one (p.431).
        window.composer().moveTo(MonitorId::Record, 0);
        QVERIFY(find->findNext());
        QCOMPARE(window.composer().state(MonitorId::Record).position(), 100);
        QVERIFY(find->findNext());
        QCOMPARE(window.composer().state(MonitorId::Record).position(), 0);
    }
};

QTEST_MAIN(TestFindWindow)
#include "tst_find_window.moc"
