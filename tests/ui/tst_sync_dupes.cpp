// Sync breaks, Dupe Detection and Clip Text in the Timeline (Editing Guide p.490-492, p.592,
// p.641-642; docs/spec/sync-dupes.md): model::syncBreaks and model::dupes, and the Fast menu's Sync
// Breaks, Dupe Detection and Clip Text items, drawn in the view.
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/sync_dupes.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QMenu>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

Segment seg(const QString& clipId, const QString& track, std::int64_t in, std::int64_t length) {
    Segment s;
    s.binId = QStringLiteral("bin");
    s.clipId = clipId;
    s.name = clipId;
    s.sourceTrack = track;
    s.sourceIn = in;
    s.length = length;
    return s;
}

QAction* menuItem(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

int count(const QImage& image, QRgb colour) {
    int n = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) n += image.pixel(x, y) == colour ? 1 : 0;
    }
    return n;
}

} // namespace

class TestSyncDupes : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void syncBreaks() {
        // Clip c's picture and sound cut in together: no break.
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("c"), QStringLiteral("V1"), 0, 50)});
        seq.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("c"), QStringLiteral("A1"), 0, 50)});
        QVERIFY(model::syncBreaks(seq).isEmpty());
        // Five frames extracted from A1's start: its sound is 5 frames ahead of the picture
        // (p.490-491), shown on both tracks.
        seq.extract(QStringLiteral("A1"), 0, 5);
        const QList<SyncBreak> breaks = model::syncBreaks(seq);
        QCOMPARE(breaks.size(), 2);
        QCOMPARE(breaks.at(0).track, QStringLiteral("V1"));
        QCOMPARE(breaks.at(0).offset, -5);
        QCOMPARE(breaks.at(0).length, 45);
        QCOMPARE(breaks.at(1).track, QStringLiteral("A1"));
        QCOMPARE(breaks.at(1).start, 0);
        QCOMPARE(breaks.at(1).offset, 5);
        // A second sound track still in sync keeps the picture in sync.
        seq.splice(QStringLiteral("A2"), 0, {seg(QStringLiteral("c"), QStringLiteral("A2"), 0, 50)});
        const QList<SyncBreak> again = model::syncBreaks(seq);
        QCOMPARE(again.size(), 1);
        QCOMPARE(again.at(0).track, QStringLiteral("A1"));
        // Another clip, or a track of the same kind, is no partner.
        Sequence other = Sequence::withDefaultTracks();
        other.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("c"), QStringLiteral("V1"), 0, 50)});
        other.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("d"), QStringLiteral("A1"), 7, 50)});
        other.splice(QStringLiteral("A2"), 0, {seg(QStringLiteral("d"), QStringLiteral("A2"), 0, 50)});
        QVERIFY(model::syncBreaks(other).isEmpty());
    }

    void dupes() {
        Sequence seq = Sequence::withDefaultTracks();
        // c frames 0-19, then d, then c frames 15-29: frames 15-19 are used twice.
        seq.splice(QStringLiteral("V1"), 0,
                   {seg(QStringLiteral("c"), QStringLiteral("V1"), 0, 20), seg(QStringLiteral("d"), QStringLiteral("V1"), 0, 10),
                    seg(QStringLiteral("c"), QStringLiteral("V1"), 15, 15)});
        const QList<Dupe> found = model::dupes(seq, {TrackKind::Video});
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.at(0).start, 15);
        QCOMPARE(found.at(0).length, 5);
        QCOMPARE(found.at(1).start, 30);
        QCOMPARE(found.at(1).length, 5);
        QCOMPARE(found.at(0).set, found.at(1).set);
        QVERIFY(model::dupes(seq, {TrackKind::Audio}).isEmpty());
        // Handles widen each clip before checking (p.1220): 0.5 frame adds a frame where they meet.
        Sequence near = Sequence::withDefaultTracks();
        near.splice(QStringLiteral("V1"), 0,
                    {seg(QStringLiteral("c"), QStringLiteral("V1"), 0, 10), seg(QStringLiteral("d"), QStringLiteral("V1"), 0, 10),
                     seg(QStringLiteral("c"), QStringLiteral("V1"), 10, 10)});
        QVERIFY(model::dupes(near, {TrackKind::Video}).isEmpty());
        const QList<Dupe> half = model::dupes(near, {TrackKind::Video}, 0.5);
        QCOMPARE(half.size(), 2);
        QCOMPARE(half.at(0).start, 9);
        QCOMPARE(half.at(0).length, 1);
        QCOMPARE(half.at(1).start, 20);
        QCOMPARE(half.at(1).length, 1);
        QCOMPARE(model::dupes(near, {TrackKind::Video}, 2).at(0).length, 2);
        // Orange first, then green (p.641).
        QCOMPARE(model::dupeColor(0), QStringLiteral("#f08c28"));
        QCOMPARE(model::dupeColor(1), QStringLiteral("#3cb44a"));
        QCOMPARE(model::dupeColor(10), model::dupeColor(0));
    }

    void inTheTimeline() {
        Composer composer;
        // The source clip: 200 frames, from tape T7, at a "DNxHD 145" resolution.
        BinItem clip;
        clip.id = QStringLiteral("c");
        clip.name = QStringLiteral("c");
        clip.kind = BinItemKind::MasterClip;
        clip.editRate = Rational(25);
        clip.durationFrames = 200;
        clip.fields.insert(QStringLiteral("Tape"), QStringLiteral("T7"));
        clip.fields.insert(QStringLiteral("Video"), QStringLiteral("DNxHD 145"));
        composer.setItemResolver([clip](const QString&, const QString& id) -> std::optional<BinItem> {
            if (id == clip.id) return clip;
            return std::nullopt;
        });
        Timeline timeline(composer);
        timeline.resize(900, 300);
        timeline.show();
        BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0,
                   {seg(QStringLiteral("c"), QStringLiteral("V1"), 0, 20), seg(QStringLiteral("c"), QStringLiteral("V1"), 10, 20)});
        seq.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("c"), QStringLiteral("A1"), 3, 40)});
        seq.storeIn(item);
        composer.load(MonitorId::Record, QStringLiteral("bin"), item);
        timeline.showEntireSequence();
        settle();

        // Clip Text (p.592).
        QMenu* fast = timeline.fastMenu();
        QMenu* clipText = fast->findChild<QMenu*>(QStringLiteral("clipText"));
        QVERIFY(clipText);
        for (const char* text : {"Clip Full Durations", "Clip Trans Duration", "Source Names", "Media Full Names", "Clip Resolution"}) {
            QVERIFY2(menuItem(clipText, QLatin1String(text))->isEnabled(), text);
        }
        QVERIFY(!menuItem(clipText, QStringLiteral("Group Clip Name"))->isEnabled());
        menuItem(clipText, QStringLiteral("Clip Full Durations"))->trigger();
        menuItem(timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("clipText")), QStringLiteral("Source Names"))->trigger();
        menuItem(timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("clipText")), QStringLiteral("Clip Resolution"))->trigger();
        const model::Sequence shown = timeline.shown();
        const QString text = timeline.clipText(*shown.track(QStringLiteral("V1")), shown.track(QStringLiteral("V1"))->segments.at(0));
        QVERIFY2(text.startsWith(QStringLiteral("c  ")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("T7")) && text.endsWith(QStringLiteral("DNxHD 145")), qPrintable(text));
        QVERIFY2(text.contains(composer.state(MonitorId::Record).durationText(200)), qPrintable(text));

        // Sync breaks: A1 runs 3 frames ahead of V1's first segment. The numbers are drawn in
        // BrightText on every track by default, on none with None.
        QCOMPARE(timeline.display().syncBreaks, Timeline::SyncBreaks::All);
        QVERIFY(timeline.showsSyncBreaks(TrackKind::Audio));
        QMenu* sync = timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("syncBreaks"));
        menuItem(sync, QStringLiteral("Video Tracks Only"))->trigger();
        QVERIFY(timeline.showsSyncBreaks(TrackKind::Video) && !timeline.showsSyncBreaks(TrackKind::Audio));
        menuItem(timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("syncBreaks")), QStringLiteral("None"))->trigger();
        QVERIFY(!timeline.showsSyncBreaks(TrackKind::Video));

        // Dupe Detection: the second V1 segment repeats frames 10-19; orange bars over them (p.641).
        const QRgb orange = QColor(model::dupeColor(0)).rgb();
        QCOMPARE(count(timeline.view()->grab().toImage(), orange), 0);
        menuItem(timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("dupeDetection")), QStringLiteral("Video"))->trigger();
        QVERIFY(timeline.display().dupeVideo);
        settle();
        QVERIFY(count(timeline.view()->grab().toImage(), orange) > 20);
        // A Timeline view keeps the choices (p.604).
        const Timeline::Display back = Timeline::displayFromJson(Timeline::displayToJson(timeline.display()));
        QCOMPARE(back, timeline.display());
    }
};

QTEST_MAIN(TestSyncDupes)
#include "tst_sync_dupes.moc"
