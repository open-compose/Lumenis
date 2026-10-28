// Bin > Select's Offline Items, Media Relatives, Sources and Unreferenced Clips (Editing Guide
// p.266-267; docs/spec/bin-select.md), File > Reveal File (p.360) and Windows > Close All But Active
// (p.296).
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/bin_relations.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;
using namespace lumenis::ui;

namespace {

BinItem clip(const QString& name, const QString& file = {}) {
    BinItem c;
    c.id = model::newItemId();
    c.mobId = MobId::generate();
    c.name = name;
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = 100;
    if (!file.isEmpty()) c.link = QJsonObject{{QStringLiteral("path"), file}};
    return c;
}

BinItem subclipOf(const BinItem& master) {
    BinItem s = clip(master.name + QStringLiteral(".sub"));
    s.kind = BinItemKind::Subclip;
    s.link = master.link;
    ClipSource src;
    src.kind = ClipSource::Kind::Clip;
    src.mob = master.mobId;
    s.sources << src;
    return s;
}

BinItem sequenceOf(const QString& name, const QString& binId, const QList<BinItem>& clips) {
    BinItem item;
    item.id = model::newItemId();
    item.mobId = MobId::generate();
    item.name = name;
    item.kind = BinItemKind::Sequence;
    item.editRate = Rational(25);
    Sequence seq = Sequence::withDefaultTracks();
    QList<Segment> segs;
    for (const BinItem& c : clips) {
        Segment s;
        s.binId = binId;
        s.clipId = c.id;
        s.sourceMob = c.mobId;
        s.name = c.name;
        s.sourceTrack = QStringLiteral("V1");
        s.length = 10;
        segs << s;
    }
    seq.splice(QStringLiteral("V1"), 0, segs);
    seq.storeIn(item);
    return item;
}

QStringList sorted(QStringList l) {
    l.sort();
    return l;
}

} // namespace

class TestBinSelect : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void inTheApplication() {
        QTemporaryDir dir;
        const QString file = dir.filePath("a.mov");
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
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
        BinContainer& bins = window.binContainer();
        const QString oneId = bins.newBin();
        BinDocument* one = bins.openBin(oneId);
        const BinItem a = clip(QStringLiteral("A"), file);
        const BinItem b = clip(QStringLiteral("B"), dir.filePath("missing.mov"));
        const BinItem as = subclipOf(a);
        const BinItem s = sequenceOf(QStringLiteral("S"), oneId, {a, as});
        for (const BinItem& i : {a, b, as, s}) one->addItem(i);
        const QString twoId = bins.newBin();
        BinDocument* two = bins.openBin(twoId);
        const BinItem c = clip(QStringLiteral("C"));
        const BinItem t = sequenceOf(QStringLiteral("T"), oneId, {b});
        for (const BinItem& i : {c, t}) two->addItem(i);
        BinView* v1 = bins.viewFor(one);
        BinView* v2 = bins.viewFor(two);

        // Offline Items in the active bin (p.266): B's file is missing; C has no media; T uses B.
        bins.openBin(oneId);
        QCOMPARE(window.binSelect(QStringLiteral("offline_items")), 1);
        QCOMPARE(v1->selectedIds(), QStringList{b.id});
        bins.openBin(twoId);
        QCOMPARE(window.binSelect(QStringLiteral("offline_items")), 2);
        QCOMPARE(sorted(v2->selectedIds()), sorted({c.id, t.id}));

        // Media Relatives (p.266-267): A's subclip and the sequence using it, in all open bins.
        v1->selectIds({a.id});
        v2->selectIds({});
        QCOMPARE(window.binSelect(QStringLiteral("media_relatives")), 3);
        QCOMPARE(sorted(v1->selectedIds()), sorted({a.id, as.id, s.id}));
        QVERIFY(v2->selectedIds().isEmpty());
        v1->selectIds({b.id});
        QCOMPARE(window.binSelect(QStringLiteral("media_relatives")), 2);
        QCOMPARE(v1->selectedIds(), QStringList{b.id});
        QCOMPARE(v2->selectedIds(), QStringList{t.id});

        // Sources (p.267): S uses A and its subclip; T, in the other bin, uses B.
        v1->selectIds({s.id});
        v2->selectIds({});
        window.binSelect(QStringLiteral("sources"));
        QCOMPARE(sorted(v1->selectedIds()), sorted({a.id, as.id}));
        v1->selectIds({});
        v2->selectIds({t.id});
        window.binSelect(QStringLiteral("sources"));
        QCOMPARE(v1->selectedIds(), QStringList{b.id});
        QVERIFY(v2->selectedIds().isEmpty());

        // Unreferenced Clips (p.267): only C is used by nothing.
        QCOMPARE(window.binSelect(QStringLiteral("unreferenced_clips")), 1);
        QCOMPARE(v2->selectedIds(), QStringList{c.id});
        QVERIFY(v1->selectedIds().isEmpty());

        // Reveal File (p.360): A's media file.
        QString revealed;
        window.setRevealHook([&](const QString& path) { revealed = path; });
        bins.openBin(oneId);
        v1->selectIds({a.id});
        QCOMPARE(window.revealFile(), file);
        QCOMPARE(revealed, file);
        v1->selectIds({b.id}); // its file is missing
        QVERIFY(window.revealFile().isEmpty());

        // Close All But Active (p.296).
        commands.action(QStringLiteral("windows.close_all_but_active"))->trigger();
        QVERIFY(bins.document(oneId));
        QVERIFY(!bins.document(twoId));
    }
};

QTEST_MAIN(TestBinSelect)
#include "tst_bin_select.moc"
