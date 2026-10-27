// Audio waveforms in the Timeline (Editing Guide p.677-680; docs/spec/waveforms.md): Audio Data >
// Waveform and the Waveform Options, the plot's size (Ctrl+Alt+L/K), Show Marked Waveforms (p.1218),
// mismatched sample rates drawn white (p.680), the project's WaveformCache folder, and the channel
// order of surround tracks (Display Channel Order, p.1148). What is drawn is counted in the view's
// pixels: the plot is black (or white) on the audio segments' colour.
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
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

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A mono WAV at `rate`: a second of silence, then a second at 0.8.
QString writeWav(const QString& path, int rate) {
    media::WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_f32le");
    s.channels = 1;
    s.sampleRate = rate;
    media::MediaWriter w;
    if (!w.open(path, s)) return {};
    std::vector<float> v(2 * static_cast<std::size_t>(rate), 0.0f);
    for (int i = rate; i < 2 * rate; ++i) v[static_cast<std::size_t>(i)] = 0.8f;
    return w.writeAudio(v.data(), 2 * rate) && w.finish() ? path : QString();
}

// Pixels of the view exactly this colour.
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

} // namespace

class TestWaveformUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void waveforms() {
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
        const QString at48 = writeWav(dir.filePath(QStringLiteral("a.wav")), 48000);
        const QString at44 = writeWav(dir.filePath(QStringLiteral("b.wav")), 44100);
        QVERIFY(!at48.isEmpty() && !at44.isEmpty());
        const QString binId = window.binContainer().newBin();
        QVERIFY(window.binContainer().openBin(binId));
        const QStringList ids = window.linkFiles(binId, {at48, at44});
        QCOMPARE(ids.size(), 2);
        // A1: a.wav's 50 frames, then b.wav's.
        model::BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        QList<model::Segment> segments;
        for (const QString& id : ids) {
            model::Segment g;
            g.binId = binId;
            g.clipId = id;
            g.sourceTrack = QStringLiteral("A1");
            g.length = 50;
            segments << g;
        }
        seq.splice(QStringLiteral("A1"), 0, segments);
        seq.storeIn(item);
        window.composer().load(MonitorId::Record, binId, item);
        Timeline& timeline = window.timeline();
        timeline.resize(1000, 400);
        timeline.show();
        timeline.showEntireSequence();
        settle();
        // A first paint asks for the waveforms; once they are worked out the next one draws them.
        auto grab = [&] {
            timeline.view()->grab();
            timeline.waveforms().wait();
            settle();
            return timeline.view()->grab().toImage();
        };
        const QRgb black = Timeline::kWaveformColor;
        const QRgb white = Timeline::kMismatchedWaveformColor;
        const int without = count(grab(), black);

        // Audio Data > Waveform (p.678).
        QMenu* fast = timeline.fastMenu();
        QAction* waveform = menuItem(fast, QStringLiteral("Waveform"));
        QVERIFY(waveform && waveform->isEnabled() && waveform->isCheckable());
        waveform->trigger();
        QVERIFY(timeline.display().waveform);
        const int plain = count(grab(), black) - without;
        QVERIFY2(plain > 200, qPrintable(QString::number(plain)));
        // The cache folder is the project's (p.677).
        QCOMPARE(timeline.waveforms().folder(), QDir(project.folder).filePath(QStringLiteral("WaveformCache")));
        QVERIFY(!QDir(timeline.waveforms().folder()).entryList({QStringLiteral("*.wfm")}).isEmpty());

        // The plot's size without the track's (p.678): smaller, then larger again.
        QVERIFY(commands.action(QStringLiteral("timeline.reduce_waveform")));
        QVERIFY(timeline.perform(QStringLiteral("timeline.reduce_waveform")));
        QVERIFY(timeline.perform(QStringLiteral("timeline.reduce_waveform")));
        const int smaller = count(grab(), black) - without;
        QVERIFY2(smaller < plain, qPrintable(QStringLiteral("%1 %2").arg(smaller).arg(plain)));
        QVERIFY(timeline.perform(QStringLiteral("timeline.enlarge_waveform")));
        QVERIFY(timeline.perform(QStringLiteral("timeline.enlarge_waveform")));
        QCOMPARE(timeline.display().waveformZoom, 0);
        for (int i = 0; i < 4; ++i) QVERIFY(timeline.perform(QStringLiteral("timeline.enlarge_waveform")));
        QVERIFY(!timeline.perform(QStringLiteral("timeline.enlarge_waveform"))); // at the limit
        for (int i = 0; i < 4; ++i) timeline.perform(QStringLiteral("timeline.reduce_waveform"));

        // Waveform Options (p.679): Output Levels follows the clip gain; Full Height extends.
        QVERIFY(menuItem(fast = timeline.fastMenu(), QStringLiteral("Output Levels")));
        model::Sequence quiet = seq;
        for (model::Segment& g : quiet.track(QStringLiteral("A1"))->segments) g.gain = -40;
        quiet.storeIn(item);
        window.composer().load(MonitorId::Record, binId, item);
        timeline.showEntireSequence();
        const int sourceLevels = count(grab(), black) - without;
        timeline.setWaveformLevels(true, false);
        const int outputLevels = count(grab(), black) - without;
        QVERIFY2(outputLevels < sourceLevels, qPrintable(QStringLiteral("%1 %2").arg(outputLevels).arg(sourceLevels)));
        timeline.setWaveformLevels(true, true);
        const int fullHeight = count(grab(), black) - without;
        QVERIFY2(fullHeight > outputLevels, qPrintable(QStringLiteral("%1 %2").arg(fullHeight).arg(outputLevels)));
        timeline.setWaveformLevels(false, false);
        seq.storeIn(item);
        window.composer().load(MonitorId::Record, binId, item);
        timeline.showEntireSequence();

        // Show Mismatched Sample Rates (p.680): the 44.1 kHz clip's plot white.
        const int whiteBefore = count(grab(), white);
        window.settings().setValue(QStringLiteral("Audio Project"), QStringLiteral("showMismatchedRates"), true);
        const int whiteAfter = count(grab(), white);
        QVERIFY2(whiteAfter > whiteBefore + 100, qPrintable(QStringLiteral("%1 %2").arg(whiteAfter).arg(whiteBefore)));
        window.settings().setValue(QStringLiteral("Audio Project"), QStringLiteral("showMismatchedRates"), false);

        // Show Marked Waveforms (p.1218): only between the marks.
        model::MonitorState& marks = window.composer().state(MonitorId::Record);
        marks.setPosition(60);
        marks.markIn();
        marks.setPosition(70);
        marks.markOut();
        window.settings().setValue(QStringLiteral("Timeline"), QStringLiteral("showMarkedWaveforms"), true);
        QVERIFY(timeline.options().markedWaveforms);
        const int marked = count(grab(), black) - without;
        QVERIFY2(marked < plain / 2, qPrintable(QStringLiteral("%1 %2").arg(marked).arg(plain)));

        // Off again.
        timeline.setWaveform(false);
        QCOMPARE(count(grab(), black), without);
    }

    // Display Channel Order (p.1148): a 5.1 track's channels L C R Ls Rs Lfe in SMPTE order
    // L R C Lfe Ls Rs, or Pro Tools order as they are; 7.1 likewise (p.689).
    void channelOrder() {
        Composer composer;
        Timeline timeline(composer);
        QCOMPARE(timeline.waveformChannels(model::AudioFormat::Mono), (QList<int>{0}));
        QCOMPARE(timeline.waveformChannels(model::AudioFormat::Stereo), (QList<int>{0, 1}));
        QCOMPARE(timeline.waveformChannels(model::AudioFormat::Surround51), (QList<int>{0, 2, 1, 5, 3, 4}));
        QCOMPARE(timeline.waveformChannels(model::AudioFormat::Surround71), (QList<int>{0, 2, 1, 7, 3, 4, 5, 6}));
        Timeline::Options o = timeline.options();
        o.proToolsChannelOrder = true;
        timeline.setOptions(o);
        QCOMPARE(timeline.waveformChannels(model::AudioFormat::Surround51), (QList<int>{0, 1, 2, 3, 4, 5}));
        QCOMPARE(timeline.waveformChannels(model::AudioFormat::Surround71), (QList<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    }
};

QTEST_MAIN(TestWaveformUi)
#include "tst_waveform_ui.moc"
