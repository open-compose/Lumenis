// Timeline commands of the UI completion pass (Editing Guide p.462, p.613-614, p.635, p.644-645;
// docs/spec/timeline-commands.md): Add Filler at Start, Find Black Hole and Find Flash Frame, Rename
// Track and Create Sequence Based on Selection.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QMenu>
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

QStringList names(const Sequence& seq, const QString& track) {
    QStringList out;
    for (const Segment& s : seq.track(track)->segments) out << (s.isFiller() ? QStringLiteral("-%1").arg(s.length) : s.name);
    return out;
}

QAction* menuItem(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

} // namespace

class TestTimelineCommands : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void model() {
        // V1: a (10) b (3) - (5) c (20); A1: x (30).
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("a"), QStringLiteral("V1"), 10), seg(QStringLiteral("b"), QStringLiteral("V1"), 3),
                                              Segment::filler(5), seg(QStringLiteral("c"), QStringLiteral("V1"), 20)});
        seq.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("x"), QStringLiteral("A1"), 30)});
        // Black holes: V1's filler at 13; A1 ends at 30 before the sequence does (38).
        QCOMPARE(seq.findBlackHole({QStringLiteral("V1")}, -1), 13);
        QCOMPARE(seq.findBlackHole({QStringLiteral("V1")}, 13), std::nullopt);
        QCOMPARE(seq.findBlackHole({QStringLiteral("A1")}, -1), 30);
        // Flash frames: "The default is 10, which indicates the system will detect clips with 9 or
        // fewer frames" (p.645).
        QCOMPARE(seq.findFlashFrame({QStringLiteral("V1")}, -1, 10), 10);
        QCOMPARE(seq.findFlashFrame({QStringLiteral("V1")}, 10, 10), std::nullopt);
        QCOMPARE(seq.findFlashFrame({QStringLiteral("V1")}, -1, 11), 0);
        // Add Filler at Start on every track; not on an empty sequence.
        Sequence filled = seq;
        QVERIFY(filled.addFillerAtStart(25));
        QCOMPARE(names(filled, QStringLiteral("V1")).first(), QStringLiteral("-25"));
        QCOMPARE(names(filled, QStringLiteral("A1")), (QStringList{"-25", "x"}));
        QVERIFY(filled.track(QStringLiteral("A2"))->segments.isEmpty());
        QVERIFY(!Sequence::withDefaultTracks().addFillerAtStart(25));
        Sequence locked = seq;
        locked.track(QStringLiteral("A1"))->locked = true;
        QVERIFY(!locked.addFillerAtStart(25));
        // Selection copy: only tracks with something selected, the rest filler, trailing filler gone.
        seq.track(QStringLiteral("V1"))->customName = QStringLiteral("Picture");
        const Sequence copy = seq.selectionCopy({SegmentRef{QStringLiteral("V1"), 10}});
        QCOMPARE(copy.trackNames(), QStringList{QStringLiteral("V1")});
        QCOMPARE(names(copy, QStringLiteral("V1")), (QStringList{"-10", "b"}));
        QCOMPARE(copy.track(QStringLiteral("V1"))->customName, QStringLiteral("Picture"));
        // Custom names go with the sequence.
        QCOMPARE(Sequence::fromJson(seq.toJson()).track(QStringLiteral("V1"))->customName, QStringLiteral("Picture"));
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
        settle();
        const QString binId = window.binContainer().newBin();
        QVERIFY(window.binContainer().openBin(binId));
        BinDocument* doc = window.binContainer().document(binId);
        QVERIFY(doc);
        BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("a"), QStringLiteral("V1"), 10), seg(QStringLiteral("b"), QStringLiteral("V1"), 3),
                                              Segment::filler(5), seg(QStringLiteral("c"), QStringLiteral("V1"), 20)});
        seq.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("x"), QStringLiteral("A1"), 38)});
        seq.storeIn(item);
        doc->addItem(item);
        window.composer().load(MonitorId::Record, binId, item);
        // The record sequence's bin comes first in the Select a Bin list (ours).
        QStringList offered;
        window.setBinChoiceHook([&offered](const QStringList& bins) {
            offered = bins;
            return 0;
        });
        Timeline& timeline = window.timeline();
        timeline.show();
        settle();
        auto recorded = [&] { return Sequence::fromItem(window.composer().state(MonitorId::Record).current()->item); };

        // Find Black Hole and Find Flash Frame on the selected tracks (p.644-645).
        timeline.patch().setRecordSelected(QStringLiteral("A1"), false);
        timeline.patch().setRecordSelected(QStringLiteral("A2"), false);
        QVERIFY(commands.action(QStringLiteral("timeline.find_black_hole"))->isEnabled());
        commands.action(QStringLiteral("timeline.find_black_hole"))->trigger();
        QCOMPARE(window.composer().state(MonitorId::Record).position(), 13);
        QVERIFY(!timeline.findGap(false));
        QVERIFY(!timeline.lastMessage().isEmpty());
        window.composer().moveTo(MonitorId::Record, 0);
        commands.action(QStringLiteral("timeline.find_flash_frame"))->trigger();
        QCOMPARE(window.composer().state(MonitorId::Record).position(), 10);

        // Add Filler at Start: Start Filler Duration, 2 seconds by default (ours), one undo step.
        commands.action(QStringLiteral("timeline.add_filler_at_start"))->trigger();
        QCOMPARE(names(recorded(), QStringLiteral("V1")).first(), QStringLiteral("-50"));
        timeline.perform(QStringLiteral("edit.undo"));
        QCOMPARE(names(recorded(), QStringLiteral("V1")).first(), QStringLiteral("a"));
        window.settings().setValue(QStringLiteral("Timeline"), QStringLiteral("startFiller"), 1);
        settle();
        QVERIFY(timeline.addFillerAtStart());
        QCOMPARE(names(recorded(), QStringLiteral("V1")).first(), QStringLiteral("-25"));
        std::unique_ptr<QMenu> menu(timeline.contextMenu());
        QVERIFY(menuItem(menu.get(), QStringLiteral("Add Filler at Start")));

        // Rename Track (p.635): the track button's menu, kept with the sequence; the Audio Mixer
        // shows it with the track number; Remove gives the default back.
        std::unique_ptr<QMenu> trackMenu(timeline.contextMenu(QStringLiteral("A1")));
        QAction* rename = menuItem(trackMenu.get(), QStringLiteral("Rename Track"));
        QVERIFY(rename && rename->isEnabled());
        timeline.setRenameAsk([](const QString&) { return std::optional<QString>(QStringLiteral("Dialog")); });
        rename->trigger();
        QCOMPARE(recorded().track(QStringLiteral("A1"))->customName, QStringLiteral("Dialog"));
        window.audioMixer().refresh();
        QCOMPARE(window.audioMixer().strip(QStringLiteral("A1"))->trackButton()->text(), QStringLiteral("A1 Dialog"));
        timeline.setRenameAsk([](const QString& current) {
            return current == QLatin1String("Dialog") ? std::optional<QString>(QString()) : std::nullopt;
        });
        timeline.renameTrack(QStringLiteral("A1"));
        QVERIFY(recorded().track(QStringLiteral("A1"))->customName.isEmpty());

        // Create Sequence Based on Selection (p.613-614): a new sequence in the bin, selected.
        std::unique_ptr<QMenu> noSelection(timeline.contextMenu());
        QVERIFY(!menuItem(noSelection.get(), QStringLiteral("Create Sequence Based on Selection"))->isEnabled());
        timeline.setSelection({SegmentRef{QStringLiteral("V1"), 25 + 10}});
        const qsizetype before = doc->bin().items.size();
        std::unique_ptr<QMenu> withSelection(timeline.contextMenu());
        QAction* create = menuItem(withSelection.get(), QStringLiteral("Create Sequence Based on Selection"));
        QVERIFY(create && create->isEnabled());
        create->trigger();
        QCOMPARE(doc->bin().items.size(), before + 1);
        if (!offered.isEmpty()) QCOMPARE(offered.first(), doc->name());
        const BinItem& made = doc->bin().items.constLast();
        QVERIFY(made.name.startsWith(QStringLiteral("Cut.Copy.")));
        QVERIFY(made.id != item.id);
        const Sequence copy = Sequence::fromItem(made);
        QCOMPARE(copy.trackNames(), QStringList{QStringLiteral("V1")});
        QCOMPARE(names(copy, QStringLiteral("V1")), (QStringList{"-35", "b"}));
        QCOMPARE(window.binContainer().viewFor(doc)->selectedIds(), QStringList{made.id});
    }
};

QTEST_MAIN(TestTimelineCommands)
#include "tst_timeline_commands.moc"
