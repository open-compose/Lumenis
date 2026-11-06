// File > Output > Digital Cut (7.3a; docs/spec/digital-cut.md, Editing Guide p.926-938): the Digital Cut tool with the
// built-in deck, a tape kept in a folder. A sequence recorded at its own timecode with black at the tail; an insert
// edit over part of it at a Mark IN; a crash record where the deck is; Local mode; an assemble edit blanking the tape
// after its OUT; the Sequence Track buttons; Preview leaving the tape as it was; No Deck.
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/media/tape_deck.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/digital_cut_tool.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

QColor colourOf(int n) { return QColor(n * 20, n * 20, n * 20); }

// A 10-frame 64 x 36 FFV1 movie at 25 fps, each frame its own grey, with stereo sound at 0.25.
bool writeMovie(const QString& path) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    s.audioCodec = QStringLiteral("pcm_s16le");
    s.channels = 2;
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    const std::vector<float> sound(2 * 1920, 0.25f);
    for (int i = 0; i < 10; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(colourOf(i));
        if (!w.writeVideo(image) || !w.writeAudio(sound.data(), 1920)) return false;
    }
    return w.finish();
}

int greyOn(const QString& tape, std::int64_t frame) {
    const QImage image = media::tapeFrameAt(tape, frame);
    return image.isNull() ? -1 : qGray(image.pixel(image.width() / 2, image.height() / 2));
}

constexpr std::int64_t kHour = 3600 * 25; // 01:00:00:00 at 25 fps

} // namespace

class TestDigitalCut : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void recordToTape() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A003.mov"));
        QVERIFY(writeMovie(movie));
        const QString tape = dir.filePath(QStringLiteral("Master 1"));
        QVERIFY(QDir().mkpath(tape));
        media::setTapeDeckFolders({tape, dir.filePath(QStringLiteral("Unplugged"))});
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Harbor"));
        project.settings.name = QStringLiteral("Harbor");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 256;
        project.settings.format.height = 144;
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        DigitalCutTool& tool = window.digitalCutTool();

        // No sequence, no deck: nothing.
        QCOMPARE(window.digitalCut(false), std::int64_t{-1});
        commands.action(QStringLiteral("file.output.digital_cut"))->trigger();
        QVERIFY(!tool.deckProvider());
        QVERIFY(!tool.findChild<QPushButton*>(QStringLiteral("dcPlay"))->isEnabled());

        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.linkFiles(binId, {movie}).size(), 1);
        const model::BinItem clip = bin->bin().items.first();
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), clip);
        composer.state(MonitorId::Source).setPosition(2);
        composer.state(MonitorId::Source).markIn();
        composer.state(MonitorId::Source).setPosition(5);
        composer.state(MonitorId::Source).markOut();
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        QVERIFY(composer.state(MonitorId::Record).current());

        // The deck menu: the folder decks, the missing one offline. "No Deck" until a deck is found.
        providers.add(std::make_shared<media::FolderTapeDeckProvider>());
        commands.action(QStringLiteral("file.output.digital_cut"))->trigger();
        QVERIFY(tool.chooseDeck(QStringLiteral("Master 1")));
        QVERIFY(tool.findChild<QPushButton*>(QStringLiteral("dcTrack_V1")));
        QVERIFY(tool.findChild<QPushButton*>(QStringLiteral("dcPlay"))->isEnabled());

        // Preview: the deck is cued, the tape unchanged (p.928).
        QCOMPARE(window.digitalCut(true), std::int64_t{0});
        QVERIFY(media::tapeRecordings(tape).isEmpty());

        // Insert edit at Sequence Time: the sequence's four frames from its start timecode (01:00:00:00), two frames of
        // black at the tail (p.929).
        DigitalCutTool::Options o = tool.options();
        o.addBlack = true;
        o.blackDuration = QStringLiteral("00:00:00:02");
        tool.setOptions(o);
        QCOMPARE(window.digitalCut(false), std::int64_t{6});
        QList<media::TapeRecording> recordings = media::tapeRecordings(tape);
        QCOMPARE(recordings.size(), 1);
        QCOMPARE(recordings.first().in, kHour);
        QCOMPARE(recordings.first().length, std::int64_t{6});
        QCOMPARE(greyOn(tape, kHour), colourOf(2).red());
        QCOMPARE(greyOn(tape, kHour + 3), colourOf(5).red());
        QCOMPARE(greyOn(tape, kHour + 4), 0); // black at the tail
        QCOMPARE(greyOn(tape, kHour + 6), -1); // blank tape
        QCOMPARE(media::tapePosition(tape), kHour + 6);

        // An insert edit at a Mark IN two frames in, with a frame offset of +1, video only (the Enable Track buttons):
        // frames 3-6 of the tape replaced, the rest kept.
        o.addBlack = false;
        o.start = DigitalCutTool::Start::MarkInTime;
        o.markIn = QStringLiteral("01:00:00:02");
        o.customOffset = true;
        o.frameOffset = 1;
        o.tapeAudio.clear();
        tool.setOptions(o);
        QCOMPARE(window.digitalCut(false), std::int64_t{4});
        recordings = media::tapeRecordings(tape);
        QCOMPARE(recordings.size(), 2);
        QCOMPARE(recordings.last().in, kHour + 3);
        QVERIFY(recordings.last().audio.isEmpty());
        QCOMPARE(greyOn(tape, kHour + 2), colourOf(4).red()); // the first cut
        QCOMPARE(greyOn(tape, kHour + 3), colourOf(2).red()); // the insert
        QCOMPARE(greyOn(tape, kHour + 6), colourOf(5).red());

        // Only the Sequence Track button A1: a sound-only recording. A crash record where the deck stopped.
        o.customOffset = false;
        o.sequenceTracks = {QStringLiteral("A1")};
        o.tapeVideo = false;
        o.tapeAudio = {1, 2};
        o.mode = ext::DeckRecordMode::Crash;
        tool.setOptions(o);
        QCOMPARE(tool.options().start, DigitalCutTool::Start::MarkInTime);
        o.start = DigitalCutTool::Start::IgnoreTime;
        tool.setOptions(o);
        const std::int64_t at = media::tapePosition(tape);
        QCOMPARE(window.digitalCut(false), std::int64_t{4});
        recordings = media::tapeRecordings(tape);
        QCOMPARE(recordings.last().in, at);
        QVERIFY(!recordings.last().video);
        QCOMPARE(recordings.last().mode, ext::DeckRecordMode::Crash);

        // An assemble edit from the start: the tape after its OUT is blank.
        o.sequenceTracks = {QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")};
        o.tapeVideo = true;
        o.mode = ext::DeckRecordMode::Assemble;
        o.start = DigitalCutTool::Start::SequenceTime;
        tool.setOptions(o);
        // Two frames only: Entire Sequence off, the Record monitor's marks.
        composer.state(MonitorId::Record).setPosition(0);
        composer.state(MonitorId::Record).markIn();
        composer.state(MonitorId::Record).setPosition(1);
        composer.state(MonitorId::Record).markOut();
        o.entireSequence = false;
        tool.setOptions(o);
        QCOMPARE(window.digitalCut(false), std::int64_t{2});
        QCOMPARE(greyOn(tape, kHour + 1), colourOf(3).red());
        QCOMPARE(greyOn(tape, kHour + 2), -1);
        QCOMPARE(greyOn(tape, kHour + 6), -1);

        // Local mode records where the deck is, as a crash record.
        o.remote = false;
        tool.setOptions(o);
        const std::int64_t here = media::tapePosition(tape);
        QCOMPARE(window.digitalCut(false), std::int64_t{2});
        QCOMPARE(media::tapeRecordings(tape).last().in, here);
        QCOMPARE(media::tapeRecordings(tape).last().mode, ext::DeckRecordMode::Crash);
        media::setTapeDeckFolders({});
    }
};

QTEST_MAIN(TestDigitalCut)
#include "tst_digital_cut.moc"
