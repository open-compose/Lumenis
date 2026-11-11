// The Sequence Report (9.2c; docs/spec/sequence-report.md; Editing Guide p.368-371): effects and where they are (a
// transition, a segment effect, a nested one, a relationship-only correction, pan/volume), the options that leave them
// out, Use Marks and Enabled Tracks Only, the clip and source summaries with offline clips, tapes, import paths and Mob
// IDs, CSV; and in the application the bin's Sequence Report with Apply Changes, the file and the report opened.
#include "lumenis/ext/provider.h"
#include "lumenis/media/sequence_report.h"
#include "lumenis/interop/aaf_export.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/sequence_report_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

model::Segment clipSegment(const QString& id, const QString& name, std::int64_t length) {
    model::Segment s;
    s.clipId = id;
    s.binId = QStringLiteral("bin");
    s.name = name;
    s.sourceTrack = QStringLiteral("V1");
    s.length = length;
    return s;
}

model::BinItem clipItem(const QString& id, const QString& name) {
    model::BinItem c;
    c.id = id;
    c.kind = model::BinItemKind::MasterClip;
    c.name = name;
    c.mobId = MobId::generate();
    return c;
}

// V1: Harbor (10), Bridge (10) with a 4-frame Dissolve at the cut and a Blur, Harbor relationship-corrected; V2: filler
// and a nest whose V1 has a Mosaic; A1: Harbor with gain.
media::SequenceReportInput sampleInput() {
    model::Sequence seq;
    model::Track* v1 = seq.ensureTrack(QStringLiteral("V1"));
    model::Segment harbor = clipSegment(QStringLiteral("h"), QStringLiteral("Harbor"), 10);
    harbor.sourceCorrection.values.insert(QStringLiteral("hsl/controls/master/gain"), 1.2);
    harbor.sourceRelationship = model::CorrectionRelationship::SourceTape;
    model::Segment bridge = clipSegment(QStringLiteral("b"), QStringLiteral("Bridge"), 10);
    model::Transition dissolve;
    dissolve.effect = {QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}};
    dissolve.duration = 4;
    bridge.transition = dissolve;
    bridge.effect = model::Effect{QStringLiteral("Image"), QStringLiteral("Blur Effect"), {}};
    v1->segments = {harbor, bridge};
    seq.ensureTrack(QStringLiteral("V2"));
    model::Segment nested = model::Segment::filler(6);
    model::Sequence inner;
    model::Segment mosaic = clipSegment(QStringLiteral("b"), QStringLiteral("Bridge"), 6);
    mosaic.effect = model::Effect{QStringLiteral("Image"), QStringLiteral("Mosaic Effect"), {}};
    inner.ensureTrack(QStringLiteral("V1"))->segments = {mosaic};
    model::setNest(nested, inner);
    nested.effect = model::Effect{QStringLiteral("Image"), QStringLiteral("Resize"), {}};
    seq.track(QStringLiteral("V2"))->segments = {model::Segment::filler(12), nested};
    model::Track* a1 = seq.ensureTrack(QStringLiteral("A1"));
    model::Segment sound = clipSegment(QStringLiteral("h"), QStringLiteral("Harbor"), 10);
    sound.sourceTrack = QStringLiteral("A1");
    sound.gain = -6;
    a1->segments = {sound};

    media::SequenceReportInput in;
    in.binName = QStringLiteral("Reels");
    in.name = QStringLiteral("Cut 1");
    in.startFrame = 3600 * 25;
    in.rate = Rational(25);
    in.project = QStringLiteral("Harbor");
    in.sequence = seq;
    static const model::BinItem harborClip = [] {
        model::BinItem c = clipItem(QStringLiteral("h"), QStringLiteral("Harbor"));
        c.fields.insert(QStringLiteral("Tape"), QStringLiteral("TAPE01"));
        return c;
    }();
    static const model::BinItem bridgeClip = [] {
        model::BinItem c = clipItem(QStringLiteral("b"), QStringLiteral("Bridge"));
        c.fields.insert(QStringLiteral("Source File"), QStringLiteral("Bridge.mov"));
        c.fields.insert(QStringLiteral("Source Path"), QStringLiteral("/shoot"));
        return c;
    }();
    in.clip = [](const model::Segment& s) -> std::optional<model::BinItem> {
        if (s.clipId == QLatin1String("h")) return harborClip;
        if (s.clipId == QLatin1String("b")) return bridgeClip;
        return std::nullopt;
    };
    in.offline = [](const model::BinItem& c) { return c.name == QLatin1String("Bridge"); };
    return in;
}

QStringList names(const QList<media::ReportedEffect>& effects) {
    QStringList out;
    for (const auto& e : effects) out << QStringLiteral("%1@%2-%3:%4").arg(e.name).arg(e.start).arg(e.end).arg(e.track);
    return out;
}

} // namespace

class TestSequenceReport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void effects() {
        const media::SequenceReportInput in = sampleInput();
        media::SequenceReportOptions o;
        // Every effect, in track order: the correction and transition and Blur on V1, the nest's Resize and its Mosaic
        // (frames 12-17 of the sequence, the nest's track in parentheses), pan/volume on A1.
        QCOMPARE(names(media::reportedEffects(in, o)),
                 QStringList({QStringLiteral("Color Correction@0-9:V1"), QStringLiteral("Dissolve@8-11:V1"),
                              QStringLiteral("Blur Effect@10-19:V1"), QStringLiteral("Resize@12-17:V2"),
                              QStringLiteral("Mosaic Effect@12-17:(V2) V1"), QStringLiteral("Pan/Volume@0-9:A1")}));
        o.skipNonRenderable = true;
        o.skipRelationshipOnly = true;
        QCOMPARE(media::reportedEffects(in, o).size(), 4);
        o.nestedOnly = true;
        QCOMPARE(names(media::reportedEffects(in, o)), QStringList({QStringLiteral("Mosaic Effect@12-17:(V2) V1")}));
        o = {};
        o.missingOnly = true;
        QVERIFY(media::reportedEffects(in, o).isEmpty());
        // Use Marks: what lies in frames 0-5; Enabled Tracks Only: A1.
        media::SequenceReportInput marked = in;
        marked.markIn = 0;
        marked.markOut = 5;
        o = {};
        o.useMarks = true;
        QCOMPARE(media::reportedEffects(marked, o).size(), 2);
        marked.enabledTracks = {QStringLiteral("A1")};
        o.enabledTracksOnly = true;
        QCOMPARE(names(media::reportedEffects(marked, o)), QStringList({QStringLiteral("Pan/Volume@0-9:A1")}));
    }

    void report() {
        const media::SequenceReportInput in = sampleInput();
        media::SequenceReportOptions o;
        QString text = media::sequenceReport(in, o);
        QVERIFY(text.startsWith(QStringLiteral("Sequence Report")));
        QVERIFY(text.contains(QStringLiteral("Starting TC  01:00:00:00")));
        QVERIFY(text.contains(QStringLiteral("Effects found: 6")));
        QVERIFY(text.contains(QStringLiteral("Blend  Dissolve  1")));
        // The location list in timecode, the nested effect indented.
        QVERIFY(text.contains(QStringLiteral("V1  01:00:00:08  01:00:00:12  Dissolve")));
        QVERIFY(text.contains(QStringLiteral("    (V2) V1  01:00:00:12  01:00:00:18  Mosaic Effect")));
        // Clip summary: Harbor twice (V1, A1), Bridge on V1 and in the nest, offline.
        QVERIFY(text.contains(QStringLiteral("Clips found: 4")));
        QVERIFY(text.contains(QStringLiteral("Master Clip  V1  01:00:00:10  Offline  Bridge")));
        QVERIFY(!text.contains(QStringLiteral("Mob ID")));
        // Source summary: the tape and the import path.
        QVERIFY(text.contains(QStringLiteral("Tape sources found: 1")));
        QVERIFY(text.contains(QStringLiteral("TAPE01  1")));
        QVERIFY(text.contains(QStringLiteral("/shoot/Bridge.mov")));
        // UIDs; offline only; CSV.
        o.showUid = true;
        o.offlineOnly = true;
        text = media::sequenceReport(in, o);
        QVERIFY(text.contains(QStringLiteral("Mob ID")));
        QVERIFY(text.contains(QStringLiteral("Clips found: 2")));
        QVERIFY(!text.contains(QStringLiteral("TAPE01")));
        o = {};
        o.csv = true;
        text = media::sequenceReport(in, o);
        QVERIFY(text.contains(QStringLiteral("V1,01:00:00:08,01:00:00:12,Dissolve")));
        QVERIFY(text.contains(QStringLiteral("Sequence,Cut 1")));
        o = {};
        o.effectSummary = o.effectLocations = o.sourceSummary = false;
        text = media::sequenceReport(in, o);
        QVERIFY(!text.contains(QStringLiteral("Effect Summary")) && text.contains(QStringLiteral("Clip Summary")));
    }

    void aafPopup();

    void fromTheBin() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Report"));
        project.settings.name = QStringLiteral("Report");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        model::BinItem item;
        item.id = model::newItemId();
        item.kind = model::BinItemKind::Sequence;
        item.name = QStringLiteral("Cut 1");
        item.editRate = Rational(25);
        sampleInput().sequence.storeIn(item);
        item.kind = model::BinItemKind::Sequence;
        bin->addItem(item);
        BinView* view = window.binContainer().activeView();
        view->selectIds({item.id});

        // The dialog: the sequence's name and start; Apply Changes renames it and moves its start.
        int opened = 0;
        QString openedPath;
        window.setOpenReportHook([&](const QString& path) {
            ++opened;
            openedPath = path;
        });
        window.setReportFileHook([&](const QString& suggested) {
            // Named as Apply Changes renamed it.
            if (!suggested.endsWith(QStringLiteral("Cut 2.txt"))) return QString();
            return dir.filePath(QStringLiteral("report.txt"));
        });
        window.setSequenceReportHook([&](SequenceReportDialog& dialog) {
            if (dialog.name() != QStringLiteral("Cut 1") || dialog.startTimecode() != QStringLiteral("01:00:00:00")) return false;
            dialog.findChild<QLineEdit*>(QStringLiteral("reportName"))->setText(QStringLiteral("Cut 2"));
            dialog.findChild<QLineEdit*>(QStringLiteral("reportStartingTc"))->setText(QStringLiteral("10:00:00:00"));
            dialog.findChild<QPushButton*>(QStringLiteral("reportApplyChanges"))->click();
            dialog.findChild<QCheckBox*>(QStringLiteral("reportClipSummary"))->setChecked(false);
            return true;
        });
        QAction* command = commands.action(QStringLiteral("bin.sequence_report"));
        QVERIFY(command);
        command->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(opened, 1);
        QCOMPARE(openedPath, dir.filePath(QStringLiteral("report.txt")));
        QCOMPARE(bin->bin().item(item.id)->name, QStringLiteral("Cut 2"));
        QFile file(openedPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(QStringLiteral("Sequence  Cut 2")));
        QVERIFY(text.contains(QStringLiteral("Starting TC  10:00:00:00")));
        QVERIFY(!text.contains(QStringLiteral("Clip Summary")));
        QVERIFY(text.contains(QStringLiteral("Effect Location List")));
        // Nothing selected: nothing to report.
        view->selectIds({});
        QCOMPARE(window.sequenceReport(false), 0);
    }
};

void TestSequenceReport::aafPopup() {
    QTemporaryDir dir;
    // Two offline clips cut together with a Dissolve, written as AAF.
    model::BinItem harbor = clipItem(model::newItemId(), QStringLiteral("Harbor"));
    harbor.editRate = Rational(25);
    harbor.durationFrames = 50;
    harbor.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1"));
    model::BinItem bridge = clipItem(model::newItemId(), QStringLiteral("Bridge"));
    bridge.editRate = Rational(25);
    bridge.durationFrames = 50;
    bridge.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1"));
    model::Sequence seq;
    model::Segment a = clipSegment(harbor.id, harbor.name, 20);
    a.sourceMob = harbor.mobId;
    model::Segment b = clipSegment(bridge.id, bridge.name, 20);
    b.sourceMob = bridge.mobId;
    model::Transition dissolve;
    dissolve.effect = {QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}};
    dissolve.duration = 10;
    b.transition = dissolve;
    seq.ensureTrack(QStringLiteral("V1"))->segments = {a, b};
    model::BinItem cut;
    cut.id = model::newItemId();
    cut.kind = model::BinItemKind::Sequence;
    cut.name = QStringLiteral("Assembly");
    cut.editRate = Rational(25);
    cut.mobId = MobId::generate();
    seq.storeIn(cut);
    cut.kind = model::BinItemKind::Sequence;
    const QString aaf = dir.filePath(QStringLiteral("Assembly.aaf"));
    QVERIFY(interop::aaf::exportItems({harbor, bridge, cut}, aaf).ok);

    ext::ProviderRegistry providers;
    CommandRegistry commands;
    commands.loadBuiltin();
    MainWindow window(commands, providers);
    window.resize(1200, 760);
    window.show();
    window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
    model::ProjectEntry project;
    project.folder = dir.filePath(QStringLiteral("Popup"));
    project.settings.name = QStringLiteral("Popup");
    project.settings.format.editRate = Rational(25);
    QDir().mkpath(project.folder);
    window.setProject(project);
    QCoreApplication::processEvents();
    const QString binId = window.binContainer().newBin();
    BinDocument* bin = window.binContainer().openBin(binId);
    QVERIFY(bin);
    QStringList opened;
    window.setOpenReportHook([&](const QString& path) { opened << path; });
    // Off by default: no report.
    QVERIFY(!window.importFiles(binId, {aaf}).isEmpty());
    QVERIFY(opened.isEmpty());
    // On, as CSV, with markers at the effects (p.238).
    const QString imp = QStringLiteral("Import");
    window.settings().setValue(imp, QStringLiteral("popupReport"), true);
    window.settings().setValue(imp, QStringLiteral("reportFormat"), QStringLiteral("csv"));
    window.settings().setValue(imp, QStringLiteral("reportEffectMarkers"), true);
    const QStringList ids = window.importFiles(binId, {aaf});
    QCOMPARE(opened.size(), 1);
    QCOMPARE(QFileInfo(opened.first()).fileName(), QStringLiteral("Assembly.csv"));
    QCOMPARE(QFileInfo(opened.first()).absolutePath(), QDir(project.folder).filePath(QStringLiteral("Reports")));
    QFile file(opened.first());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QVERIFY2(text.contains(QStringLiteral(",Dissolve")), qPrintable(text));
    const model::BinItem* imported = nullptr;
    for (const QString& id : ids) {
        if (bin->bin().item(id)->kind == model::BinItemKind::Sequence) imported = bin->bin().item(id);
    }
    QVERIFY(imported);
    const QList<model::Marker> markers = model::markersOf(*imported);
    QCOMPARE(markers.size(), 1);
    QCOMPARE(markers.first().name, QStringLiteral("Dissolve"));
    QCOMPARE(markers.first().track, QStringLiteral("V1"));
    QCOMPARE(markers.first().frame, std::int64_t(15));
}

QTEST_MAIN(TestSequenceReport)
#include "tst_sequence_report.moc"
