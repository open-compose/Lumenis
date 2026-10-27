// The Timeline's Track Control panel and track display (Editing Guide p.592-602, p.677-680;
// docs/spec/track-control.md): the panel's Waveform, Clip Volume/Pan, Inactive, Solo, Mute and insert
// buttons, Allow Per Track Settings, per track settings kept with the sequence, Enlarge and Reduce
// Track, Track Color, the Segment and Mark IO Selection colours, and Show Markers.
#include "lumenis/ext/provider.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QJsonArray>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

int count(const QImage& image, QRgb colour) {
    int n = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) n += image.pixel(x, y) == colour ? 1 : 0;
    }
    return n;
}

QAction* menuItem(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
        if (a->menu()) {
            if (QAction* found = menuItem(a->menu(), text)) return found;
        }
    }
    return nullptr;
}

QMenu* subMenu(QMenu* menu, const QString& name) {
    return menu->findChild<QMenu*>(name);
}

model::Segment clip(const QString& track, std::int64_t length) {
    model::Segment g;
    g.binId = QStringLiteral("bin");
    g.clipId = QStringLiteral("clip");
    g.name = QStringLiteral("Shot");
    g.sourceTrack = track;
    g.length = length;
    return g;
}

// The Record monitor's sequence.
model::Sequence recorded(MainWindow& window) {
    return model::Sequence::fromItem(window.composer().state(MonitorId::Record).current()->item);
}

} // namespace

class TestTrackControl : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // The per track settings go with the sequence (p.677) and the Clipboard leaves them behind.
    void json() {
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Track* a1 = seq.track(QStringLiteral("A1"));
        a1->showWaveform = true;
        a1->showClipVolume = true;
        a1->showPan = true;
        a1->inactive = true;
        a1->color = QStringLiteral("#123456");
        a1->heightStep = 3;
        seq.track(QStringLiteral("V1"))->heightStep = -2;
        const model::Sequence back = model::Sequence::fromJson(seq.toJson());
        QCOMPARE(back, seq);
        // Bad values are dropped or clamped; video tracks have no audio settings.
        QJsonObject o = seq.toJson();
        QJsonArray tracks = o.value(QStringLiteral("tracks")).toArray();
        QJsonObject v1 = tracks.at(0).toObject();
        v1.insert(QStringLiteral("display"), QJsonObject{{QStringLiteral("waveform"), true},
                                                         {QStringLiteral("color"), QStringLiteral("red")},
                                                         {QStringLiteral("height"), 99}});
        v1.insert(QStringLiteral("inactive"), true);
        tracks.replace(0, v1);
        o.insert(QStringLiteral("tracks"), tracks);
        const model::Sequence bad = model::Sequence::fromJson(o);
        const model::Track* bv = bad.track(QStringLiteral("V1"));
        QVERIFY(!bv->showWaveform && !bv->inactive && bv->color.isEmpty());
        QCOMPARE(bv->heightStep, model::kTrackHeightSteps);
        a1->segments = {clip(QStringLiteral("A1"), 10)};
        const model::Sequence part = seq.excerpt({QStringLiteral("A1")}, 0, 10);
        const model::Track* pa = part.track(QStringLiteral("A1"));
        QVERIFY(pa && !pa->showWaveform && !pa->inactive && pa->color.isEmpty() && pa->heightStep == 0);
    }

    // An inactive track plays without its plug-ins or automation (p.602).
    void inactivePlayback() {
        model::BinItem item;
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::TrackInsert in;
        in.effect = QStringLiteral("gain");
        in.name = QStringLiteral("Gain");
        seq.track(QStringLiteral("A1"))->inserts[0] = in;
        seq.storeIn(item);
        QVERIFY(media::trackInserts(item).contains(QStringLiteral("A1")));
        seq.track(QStringLiteral("A1"))->inactive = true;
        seq.storeIn(item);
        QVERIFY(!media::trackInserts(item).contains(QStringLiteral("A1")));
    }

    void panel() {
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
        model::BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {clip(QStringLiteral("V1"), 100)});
        seq.splice(QStringLiteral("A1"), 0, {clip(QStringLiteral("A1"), 100)});
        seq.splice(QStringLiteral("A2"), 0, {clip(QStringLiteral("A2"), 100)});
        seq.storeIn(item);
        window.composer().load(MonitorId::Record, QStringLiteral("bin"), item);
        Timeline& timeline = window.timeline();
        timeline.resize(1000, 400);
        timeline.show();
        timeline.showEntireSequence();
        settle();
        TimelineView* view = timeline.view();
        auto row = [view](const QString& track) {
            for (const TimelineView::Row& r : view->rows()) {
                if (r.track == track) return r;
            }
            return TimelineView::Row{};
        };

        // The panel: hidden at first, then shown from the Fast menu or the button above the
        // track buttons (p.602), on audio tracks only.
        const int narrow = view->headerWidth();
        QVERIFY(view->controlButton(row(QStringLiteral("A1")), TimelineView::Waveform).isEmpty());
        QAction* panelItem = menuItem(timeline.fastMenu(), QStringLiteral("Track Control Panel"));
        QVERIFY(panelItem && panelItem->isEnabled());
        panelItem->trigger();
        QVERIFY(timeline.display().trackControlPanel);
        QVERIFY(view->headerWidth() > narrow);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->controlPanelButton().center());
        QVERIFY(!timeline.display().trackControlPanel);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->controlPanelButton().center());
        QVERIFY(timeline.display().trackControlPanel);
        QVERIFY(view->controlButton(row(QStringLiteral("V1")), TimelineView::Waveform).isEmpty());
        for (int c = TimelineView::Waveform; c < TimelineView::InsertA + model::kTrackInserts; ++c) {
            QVERIFY2(!view->controlButton(row(QStringLiteral("A1")), c).isEmpty(), qPrintable(QString::number(c)));
        }

        // Waveform on A1 alone: Allow Per Track Settings comes on and A1's row grows.
        const int plain = row(QStringLiteral("A2")).height;
        QTest::mouseClick(view, Qt::LeftButton, {}, view->controlButton(row(QStringLiteral("A1")), TimelineView::Waveform).center());
        QVERIFY(timeline.display().perTrack);
        QVERIFY(recorded(window).track(QStringLiteral("A1"))->showWaveform);
        QVERIFY(!recorded(window).track(QStringLiteral("A2"))->showWaveform);
        QVERIFY(timeline.showsWaveform(*recorded(window).track(QStringLiteral("A1"))));
        QVERIFY(row(QStringLiteral("A1")).height > plain);
        QCOMPARE(row(QStringLiteral("A2")).height, plain);
        // Alt+click: every audio track (p.678).
        QTest::mouseClick(view, Qt::LeftButton, Qt::AltModifier,
                          view->controlButton(row(QStringLiteral("A2")), TimelineView::Waveform).center());
        QVERIFY(recorded(window).track(QStringLiteral("A1"))->showWaveform);
        QVERIFY(recorded(window).track(QStringLiteral("A2"))->showWaveform);
        // Allow Per Track Settings off hides them but keeps them (p.679).
        menuItem(timeline.fastMenu(), QStringLiteral("Allow Per Track Settings"))->trigger();
        QVERIFY(!timeline.display().perTrack);
        QVERIFY(!timeline.showsWaveform(*recorded(window).track(QStringLiteral("A1"))));
        QVERIFY(recorded(window).track(QStringLiteral("A1"))->showWaveform);
        timeline.setPerTrackSettings(true);

        // Clip Volume/Pan: volume on one track, pan on another (p.679).
        timeline.setTrackAudioData(QStringLiteral("A1"), true, model::AutomationParam::Volume);
        timeline.setTrackAudioData(QStringLiteral("A2"), false, model::AutomationParam::Pan);
        QCOMPARE(timeline.automationShown(*recorded(window).track(QStringLiteral("A1"))), model::AutomationParam::Volume);
        QCOMPARE(timeline.automationShown(*recorded(window).track(QStringLiteral("A2"))), model::AutomationParam::Pan);
        QVERIFY(timeline.showsClipVolume(*recorded(window).track(QStringLiteral("A1"))));
        QVERIFY(!timeline.showsClipVolume(*recorded(window).track(QStringLiteral("A2"))));
        std::unique_ptr<QMenu> dataMenu(timeline.trackAudioDataMenu(QStringLiteral("A2"), false, view));
        QVERIFY(menuItem(dataMenu.get(), QStringLiteral("Pan"))->isChecked());
        QVERIFY(!menuItem(dataMenu.get(), QStringLiteral("Volume"))->isChecked());
        menuItem(dataMenu.get(), QStringLiteral("Volume"))->trigger();
        QCOMPARE(timeline.automationShown(*recorded(window).track(QStringLiteral("A2"))), model::AutomationParam::Volume);

        // Inactive (p.602).
        QTest::mouseClick(view, Qt::LeftButton, {}, view->controlButton(row(QStringLiteral("A1")), TimelineView::Inactive).center());
        QVERIFY(recorded(window).track(QStringLiteral("A1"))->inactive);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->controlButton(row(QStringLiteral("A1")), TimelineView::Inactive).center());
        QVERIFY(!recorded(window).track(QStringLiteral("A1"))->inactive);

        // Solo and Mute are the Audio Mixer's (p.698-699).
        AudioMixer& mixer = window.audioMixer();
        QTest::mouseClick(view, Qt::LeftButton, {}, view->controlButton(row(QStringLiteral("A2")), TimelineView::Solo).center());
        QVERIFY(mixer.isSoloed(QStringLiteral("A2")));
        QVERIFY(timeline.isSoloMuted(QStringLiteral("A2"), true));
        QTest::mouseClick(view, Qt::LeftButton, {}, view->controlButton(row(QStringLiteral("A2")), TimelineView::Solo).center());
        QVERIFY(!mixer.isSoloed(QStringLiteral("A2")));
        QTest::mouseClick(view, Qt::LeftButton, {}, view->controlButton(row(QStringLiteral("A1")), TimelineView::Mute).center());
        QVERIFY(mixer.isMuted(QStringLiteral("A1")));
        mixer.setSoloMute(QStringLiteral("A1"), false, false);
        QVERIFY(!timeline.isSoloMuted(QStringLiteral("A1"), false));

        // An insert button opens the Audio Track Effect tool on its track and insert (p.602, p.792).
        QSignalSpy tool(&timeline, &Timeline::trackInsertRequested);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->controlButton(row(QStringLiteral("A2")), TimelineView::InsertA + 1).center());
        QCOMPARE(tool.size(), 1);
        QCOMPARE(tool.at(0).at(0).toString(), QStringLiteral("A2"));
        QCOMPARE(tool.at(0).at(1).toInt(), 1);
        // Dragging an insert button moves the insert; Alt+drag copies it (p.795).
        model::TrackInsert gain;
        gain.effect = QStringLiteral("gain");
        gain.name = QStringLiteral("Gain");
        QVERIFY(timeline.setTrackInsert(QStringLiteral("A1"), 0, gain, QStringLiteral("Insert")));
        auto drag = [&](const QString& from, int fromSlot, const QString& to, int toSlot, Qt::KeyboardModifiers mods) {
            const QPoint a = view->controlButton(row(from), TimelineView::InsertA + fromSlot).center();
            const QPoint b = view->controlButton(row(to), TimelineView::InsertA + toSlot).center();
            QTest::mousePress(view, Qt::LeftButton, mods, a);
            QMouseEvent move(QEvent::MouseMove, b, view->mapToGlobal(b), Qt::NoButton, Qt::LeftButton, mods);
            QCoreApplication::sendEvent(view, &move);
            QTest::mouseRelease(view, Qt::LeftButton, mods, b);
        };
        drag(QStringLiteral("A1"), 0, QStringLiteral("A2"), 2, {});
        QVERIFY(!recorded(window).track(QStringLiteral("A1"))->inserts[0]);
        QVERIFY(recorded(window).track(QStringLiteral("A2"))->inserts[2]);
        drag(QStringLiteral("A2"), 2, QStringLiteral("A1"), 4, Qt::AltModifier);
        QVERIFY(recorded(window).track(QStringLiteral("A2"))->inserts[2]);
        QVERIFY(recorded(window).track(QStringLiteral("A1"))->inserts[4]);
        QCOMPARE(tool.size(), 1);

        // Enlarge and Reduce Track: the selected tracks (p.594).
        for (const QString& t : {QStringLiteral("V1"), QStringLiteral("A2")}) timeline.patch().setRecordSelected(t, false);
        const int a1 = row(QStringLiteral("A1")).height;
        const int a2 = row(QStringLiteral("A2")).height;
        QVERIFY(timeline.resizeTracks(1));
        QVERIFY(row(QStringLiteral("A1")).height > a1);
        QCOMPARE(row(QStringLiteral("A2")).height, a2);
        QCOMPARE(recorded(window).track(QStringLiteral("A1"))->heightStep, 1);
        for (int i = 0; i < 20; ++i) timeline.resizeTracks(1);
        QCOMPARE(recorded(window).track(QStringLiteral("A1"))->heightStep, model::kTrackHeightSteps);
        QVERIFY(!timeline.resizeTracks(1));
        for (int i = 0; i < 20; ++i) timeline.resizeTracks(-1);
        QCOMPARE(recorded(window).track(QStringLiteral("A1"))->heightStep, -model::kTrackHeightSteps / 2);
        timeline.patch().setRecordSelected(QStringLiteral("A1"), false);
        QVERIFY(!timeline.resizeTracks(1));
        QVERIFY(!timeline.lastMessage().isEmpty());

        // Track Color: dimmed without a selected track (p.593), else on the selected tracks (p.597).
        QMenu* fast = timeline.fastMenu();
        QVERIFY(!subMenu(fast, QStringLiteral("trackColor"))->isEnabled());
        timeline.patch().setRecordSelected(QStringLiteral("V1"), true);
        fast = timeline.fastMenu();
        QMenu* trackColor = subMenu(fast, QStringLiteral("trackColor"));
        QVERIFY(trackColor->isEnabled());
        trackColor->actions().at(1)->trigger(); // the palette's first colour
        const QString hex = recorded(window).track(QStringLiteral("V1"))->color;
        QVERIFY(!hex.isEmpty());
        QVERIFY(recorded(window).track(QStringLiteral("A1"))->color.isEmpty());
        settle();
        QVERIFY(count(view->grab().toImage(), QColor(hex).rgb()) > 100);
        subMenu(timeline.fastMenu(), QStringLiteral("trackColor"))->actions().at(0)->trigger(); // Default
        QVERIFY(recorded(window).track(QStringLiteral("V1"))->color.isEmpty());

        // Segment Selection's colour fills selected segments (p.593).
        timeline.setSelectionColors(QStringLiteral("#13579b"), QString());
        timeline.setSelection({model::SegmentRef{QStringLiteral("V1"), 0}});
        settle();
        QVERIFY(count(view->grab().toImage(), QColor(QStringLiteral("#13579b")).rgb()) > 100);
        subMenu(timeline.fastMenu(), QStringLiteral("segmentSelection"))->actions().at(0)->trigger();
        QVERIFY(timeline.display().segmentSelectionColor.isEmpty());
        QVERIFY(subMenu(timeline.fastMenu(), QStringLiteral("markIOSelection")));

        // Show Markers: All, None and each colour (p.599).
        QMenu* markers = subMenu(timeline.fastMenu(), QStringLiteral("showMarkers"));
        menuItem(markers, QStringLiteral("None"))->trigger();
        for (const model::MarkerColor& c : model::markerColors()) QVERIFY(!timeline.showsMarkers(c.name));
        markers = subMenu(timeline.fastMenu(), QStringLiteral("showMarkers"));
        QAction* red = menuItem(markers, model::markerColors().first().name);
        QVERIFY(red->isCheckable() && !red->isChecked());
        red->trigger();
        QVERIFY(timeline.showsMarkers(model::markerColors().first().name));
        menuItem(subMenu(timeline.fastMenu(), QStringLiteral("showMarkers")), QStringLiteral("All"))->trigger();
        for (const model::MarkerColor& c : model::markerColors()) QVERIFY(timeline.showsMarkers(c.name));
    }
};

QTEST_MAIN(TestTrackControl)
#include "tst_track_control.moc"
