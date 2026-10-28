// Timeline Clip Notes (Editing Guide p.483-486; docs/spec/clip-notes.md): Add Timeline Clip Note,
// notes in Clip Text, and the Timeline Clip Notes window: editing and deleting, columns, sorting,
// Show as Frames, Gang mode, Quick Find, Export to Text and Tools > Timeline Clip Notes.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/clip_notes.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::model;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

Segment seg(const QString& clipId, const QString& track, std::int64_t length) {
    Segment s;
    s.binId = QStringLiteral("bin");
    s.clipId = clipId;
    s.name = clipId;
    s.sourceTrack = track;
    s.length = length;
    return s;
}

QAction* menuItem(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

Sequence recorded(Composer& composer) {
    return Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
}

QString noteOf(Composer& composer, const QString& track, int index) {
    return recorded(composer).track(track)->segments.at(index).note;
}

} // namespace

class TestClipNotes : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void json() {
        Sequence seq = Sequence::withDefaultTracks();
        Segment s = seg(QStringLiteral("a"), QStringLiteral("V1"), 10);
        s.note = QStringLiteral("city lights");
        seq.splice(QStringLiteral("V1"), 0, {s});
        QCOMPARE(Sequence::fromJson(seq.toJson()).track(QStringLiteral("V1"))->segments.at(0).note, QStringLiteral("city lights"));
    }

    void notes() {
        Composer composer;
        Timeline timeline(composer);
        timeline.resize(900, 300);
        timeline.show();
        BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("a"), QStringLiteral("V1"), 50), seg(QStringLiteral("b"), QStringLiteral("V1"), 75)});
        seq.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("c"), QStringLiteral("A1"), 30)});
        seq.storeIn(item);
        composer.load(MonitorId::Record, QStringLiteral("bin"), item);
        timeline.showEntireSequence();
        settle();

        // Right-click > Add Timeline Clip Note on the selected clips (p.483), one undo step.
        std::unique_ptr<QMenu> menu(timeline.contextMenu({}, SegmentRef{QStringLiteral("V1"), 50}));
        QAction* add = menuItem(menu.get(), QStringLiteral("Add Timeline Clip Note"));
        QVERIFY(add && add->isEnabled());
        timeline.setNoteAsk([](const QString& current) -> std::optional<QString> {
            return current.isEmpty() ? QStringLiteral("keeper") : current + QStringLiteral("!");
        });
        add->trigger();
        QCOMPARE(noteOf(composer, QStringLiteral("V1"), 1), QStringLiteral("keeper"));
        QVERIFY(timeline.lastMessage().isEmpty());
        timeline.setSelection({SegmentRef{QStringLiteral("V1"), 0}, SegmentRef{QStringLiteral("A1"), 0}});
        timeline.setNoteAsk([](const QString&) { return std::optional<QString>(QStringLiteral("good audio intro")); });
        QVERIFY(timeline.addClipNote());
        QCOMPARE(noteOf(composer, QStringLiteral("V1"), 0), QStringLiteral("good audio intro"));
        QCOMPARE(noteOf(composer, QStringLiteral("A1"), 0), QStringLiteral("good audio intro"));
        timeline.perform(QStringLiteral("edit.undo"));
        QVERIFY(noteOf(composer, QStringLiteral("A1"), 0).isEmpty());
        timeline.perform(QStringLiteral("edit.redo"));
        // Cancel changes nothing; filler takes no note.
        timeline.setNoteAsk([](const QString&) { return std::optional<QString>(); });
        QVERIFY(!timeline.addClipNote());
        timeline.setSelection({});
        std::unique_ptr<QMenu> onFiller(timeline.contextMenu({}, SegmentRef{QStringLiteral("A1"), 30}));
        QVERIFY(!menuItem(onFiller.get(), QStringLiteral("Add Timeline Clip Note"))->isEnabled());

        // Clip Text > Timeline Clip Notes shows them (p.483).
        const Sequence shown = timeline.shown();
        QVERIFY(!timeline.clipText(*shown.track(QStringLiteral("V1")), shown.track(QStringLiteral("V1"))->segments.at(1)).contains(QStringLiteral("keeper")));
        menuItem(timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("clipText")), QStringLiteral("Timeline Clip Notes"))->trigger();
        QVERIFY(timeline.clipText(*shown.track(QStringLiteral("V1")), shown.track(QStringLiteral("V1"))->segments.at(1)).endsWith(QStringLiteral("keeper")));

        // The window (p.484): a row a note, sorted by Start, End being the last frame.
        ClipNotesWindow window(timeline);
        QTableWidget* table = window.table();
        QCOMPARE(table->rowCount(), 3);
        QVERIFY(!window.isColumnShown(ClipNotesWindow::FrameColumn));
        QCOMPARE(table->item(2, ClipNotesWindow::NameColumn)->text(), QStringLiteral("b"));
        QCOMPARE(table->item(2, ClipNotesWindow::StartColumn)->text(), QStringLiteral("01:00:02:00"));
        QCOMPARE(table->item(2, ClipNotesWindow::EndColumn)->text(), QStringLiteral("01:00:04:24"));
        QCOMPARE(table->item(2, ClipNotesWindow::NoteColumn)->text(), QStringLiteral("keeper"));
        // Show as Frames (p.484).
        window.setDurationAsFrames(true);
        QCOMPARE(table->item(2, ClipNotesWindow::DurationColumn)->text(), QStringLiteral("75"));
        // Sorting by a column, and back.
        table->sortByColumn(ClipNotesWindow::DurationColumn, Qt::DescendingOrder);
        QCOMPARE(table->item(0, ClipNotesWindow::NameColumn)->text(), QStringLiteral("b"));
        table->sortByColumn(ClipNotesWindow::StartColumn, Qt::AscendingOrder);

        // Editing a note in place (p.484).
        int bRow = -1;
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->item(r, ClipNotesWindow::NameColumn)->text() == QLatin1String("b")) bRow = r;
        }
        QVERIFY(bRow >= 0);
        QVERIFY(table->item(bRow, ClipNotesWindow::NoteColumn)->flags() & Qt::ItemIsEditable);
        QVERIFY(!(table->item(bRow, ClipNotesWindow::NameColumn)->flags() & Qt::ItemIsEditable));
        table->item(bRow, ClipNotesWindow::NoteColumn)->setText(QStringLiteral("city lights"));
        settle();
        QCOMPARE(noteOf(composer, QStringLiteral("V1"), 1), QStringLiteral("city lights"));

        // Quick Find (p.486).
        window.quickFind()->setText(QStringLiteral("CITY"));
        int shownRows = 0;
        for (int r = 0; r < table->rowCount(); ++r) shownRows += table->isRowHidden(r) ? 0 : 1;
        QCOMPARE(shownRows, 1);

        // Export to Text (p.485): the shown rows and columns, tab-delimited.
        window.setColumnShown(ClipNotesWindow::TrackColumn, false);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("notes.txt"));
        QVERIFY(window.exportText(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text)); // CRLF on Windows
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0), QStringLiteral("Clip Name\tStart\tEnd\tDuration\tTimeline Clip Note"));
        QCOMPARE(lines.at(1), QStringLiteral("b\t01:00:02:00\t01:00:04:24\t75\tcity lights"));
        window.quickFind()->clear();

        // Gang mode selects the clips in the Timeline and moves to a single one (p.485).
        window.gangButton()->setChecked(true);
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->item(r, ClipNotesWindow::NameColumn)->text() == QLatin1String("b")) table->selectRow(r);
        }
        QCOMPARE(timeline.selection(), (QList<SegmentRef>{SegmentRef{QStringLiteral("V1"), 50}}));
        QCOMPARE(composer.state(MonitorId::Record).position(), 50);

        // The Frame column and its size (p.485-486).
        window.setColumnShown(ClipNotesWindow::FrameColumn, true);
        const int height = window.frameHeight();
        window.resizeFrames(1);
        QCOMPARE(window.frameHeight(), height * 2);
        std::unique_ptr<QMenu> fast(window.fastMenu());
        QVERIFY(menuItem(fast.get(), QStringLiteral("Enlarge Frame"))->isEnabled());
        QVERIFY(menuItem(fast.get(), QStringLiteral("Export to Text...")));

        // Delete (p.484).
        table->clearSelection();
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->item(r, ClipNotesWindow::NameColumn)->text() == QLatin1String("c")) table->selectRow(r);
        }
        window.deleteSelected();
        QVERIFY(noteOf(composer, QStringLiteral("A1"), 0).isEmpty());
        QCOMPARE(table->rowCount(), 2);
        std::unique_ptr<QMenu> context(window.contextMenu(0));
        QVERIFY(menuItem(context.get(), QStringLiteral("Delete"))->isEnabled());
    }

    // Tools > Timeline Clip Notes (p.484).
    void toolsMenu() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        QAction* tool = commands.action(QStringLiteral("tools.timeline_clip_notes"));
        QVERIFY(tool && tool->isEnabled());
        tool->trigger();
        QVERIFY(window.clipNotesWindow() && window.clipNotesWindow()->isVisible());
        window.clipNotesWindow()->close();
    }
};

QTEST_MAIN(TestClipNotes)
#include "tst_clip_notes.moc"
