// AutoSequence (Bin > AutoSequence, Clip > AutoSequence; Editing Guide p.499-500;
// docs/spec/autosequence.md): clips placed by source timecode with filler between, or one after another
// with Alt; Camera groups on their own tracks; related clips; the name and the Record monitor.
#include "lumenis/ext/provider.h"
#include "lumenis/model/auto_sequence.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;
using namespace lumenis::ui;

namespace {

BinItem clip(const QString& name, const QString& start, std::int64_t frames, const QString& camera = {}) {
    BinItem c;
    c.id = model::newItemId();
    c.mobId = MobId::generate();
    c.name = name;
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = frames;
    c.fields.insert(QStringLiteral("Start"), start);
    c.fields.insert(QStringLiteral("Tape"), QStringLiteral("TAPE1"));
    if (!camera.isEmpty()) c.fields.insert(QStringLiteral("Camera"), camera);
    return c;
}

// "a(50) -50 b(50)" style for a track.
QString layout(const Sequence& seq, const QString& track) {
    QStringList out;
    if (const Track* t = seq.track(track)) {
        for (const Segment& s : t->segments) out << (s.isFiller() ? QStringLiteral("-%1").arg(s.length) : QStringLiteral("%1(%2)").arg(s.name).arg(s.length));
    }
    return out.join(QLatin1Char(' '));
}

} // namespace

class TestAutoSequence : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void model() {
        const BinItem a = clip(QStringLiteral("a"), QStringLiteral("01:00:00:00"), 50);
        const BinItem b = clip(QStringLiteral("b"), QStringLiteral("01:00:04:00"), 50); // frame 100
        // In timecode order with filler where there are gaps (p.499); the earliest Start begins it.
        AutoSequenceResult r = autoSequence({b, a}, QStringLiteral("bin"), false);
        QVERIFY(r.ok());
        QCOMPARE(layout(r.sequence, QStringLiteral("V1")), QStringLiteral("a(50) -50 b(50)"));
        QCOMPARE(layout(r.sequence, QStringLiteral("A2")), QStringLiteral("a(50) -50 b(50)"));
        QCOMPARE(r.start, 25 * 3600);
        QCOMPARE(r.name, QStringLiteral("TAPE1"));
        // Alt: "a sequence without gaps by placing the clips in ascending timecode order" (p.500).
        r = autoSequence({b, a}, QStringLiteral("bin"), true);
        QCOMPARE(layout(r.sequence, QStringLiteral("V1")), QStringLiteral("a(50) b(50)"));
        // Unrelated overlapping clips are refused.
        const BinItem c = clip(QStringLiteral("c"), QStringLiteral("01:00:01:00"), 50);
        r = autoSequence({a, c}, QStringLiteral("bin"), false);
        QVERIFY(!r.ok());
        // A subclip of `a` that overlaps it: the master is chosen.
        BinItem sub = clip(QStringLiteral("a.sub"), QStringLiteral("01:00:00:10"), 20);
        sub.kind = BinItemKind::Subclip;
        ClipSource master;
        master.kind = ClipSource::Kind::Clip;
        master.mob = a.mobId;
        sub.sources << master;
        r = autoSequence({a, sub, b}, QStringLiteral("bin"), false);
        QVERIFY(r.ok());
        QCOMPARE(r.dropped, QStringList{QStringLiteral("a.sub")});
        QCOMPARE(layout(r.sequence, QStringLiteral("V1")), QStringLiteral("a(50) -50 b(50)"));
        // Camera groups go on their own tracks (p.499).
        const BinItem cam = clip(QStringLiteral("cam b"), QStringLiteral("01:00:01:00"), 25, QStringLiteral("B"));
        r = autoSequence({a, b, cam}, QStringLiteral("bin"), false);
        QVERIFY(r.ok());
        QCOMPARE(layout(r.sequence, QStringLiteral("V1")), QStringLiteral("a(50) -50 b(50)"));
        QCOMPARE(layout(r.sequence, QStringLiteral("V2")), QStringLiteral("-25 cam b(25)"));
        QCOMPARE(layout(r.sequence, QStringLiteral("A3")), QStringLiteral("-25 cam b(25)"));
        // Sequences and effects are not clips.
        BinItem seqItem = a;
        seqItem.kind = BinItemKind::Sequence;
        QVERIFY(!autoSequence({seqItem}, QStringLiteral("bin"), false).ok());
    }

    void inTheApplication() {
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
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* doc = window.binContainer().openBin(binId);
        const BinItem a = clip(QStringLiteral("a"), QStringLiteral("01:00:00:00"), 50);
        const BinItem b = clip(QStringLiteral("b"), QStringLiteral("01:00:04:00"), 50);
        doc->addItem(a);
        doc->addItem(b);
        BinView* view = window.binContainer().viewFor(doc);
        QString error;
        QVERIFY(window.autoSequence(false, &error).isEmpty());
        QVERIFY(!error.isEmpty()); // nothing selected
        view->selectIds({a.id, b.id});
        const QString id = window.autoSequence(false, &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        // "The sequence also appears in the bin with the same name as the tape name ... with a .xx (.01,
        // .02, .03) extension", in the Record monitor (p.500), starting at the first clip's timecode.
        const BinItem* made = doc->bin().item(id);
        QVERIFY(made);
        QCOMPARE(made->name, QStringLiteral("TAPE1.01"));
        QCOMPARE(made->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
        QCOMPARE(window.composer().state(MonitorId::Record).current()->item.id, id);
        QCOMPARE(layout(Sequence::fromItem(*made), QStringLiteral("V1")), QStringLiteral("a(50) -50 b(50)"));
        view->selectIds({a.id, b.id});
        const QString second = window.autoSequence(true, &error);
        QCOMPARE(doc->bin().item(second)->name, QStringLiteral("TAPE1.02"));
    }
};

QTEST_MAIN(TestAutoSequence)
#include "tst_autosequence.moc"
