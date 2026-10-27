// The Clip menu's audio commands (Editing Guide p.216, p.714, p.753-754; docs/spec/clip-audio.md):
// Apply Gain and Center Pan on bin clips, their effect on Source monitor playback, Auto VO, the two
// dialogs, and the commands in the application. Clip gain and pan carrying into edits is scenario
// 17-clip-gain-pan.json.
#include "lumenis/ext/provider.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/clip_audio.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/clip_audio_dialogs.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

BinItem clip(const char* id) {
    BinItem c;
    c.id = QString::fromLatin1(id);
    c.name = QString::fromLatin1(id);
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = 50;
    c.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A1-2"));
    return c;
}

model::Segment material(const char* name, std::int64_t length) {
    model::Segment s;
    s.clipId = QString::fromLatin1(name);
    s.name = s.clipId;
    s.sourceTrack = QStringLiteral("A1");
    s.length = length;
    return s;
}

// The names of a track's segments, "-" for filler, each with its length.
QStringList layout(const model::Sequence& seq, const char* track) {
    QStringList out;
    const model::Track* t = seq.track(QString::fromLatin1(track));
    if (!t) return out;
    for (const model::Segment& s : t->segments) {
        out << QStringLiteral("%1:%2").arg(s.isFiller() ? QStringLiteral("-") : s.name).arg(s.length);
    }
    return out;
}

} // namespace

class TestClipAudio : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // Apply Gain sets (and overrides) the clip's gain, clamped to 12..-96 dB; Center Pan gives it a
    // centred pan; sequences take neither; both survive the bin file.
    void clipGainAndPan() {
        BinItem c = clip("c");
        QVERIFY(model::applyClipGain(c, -6.0));
        QCOMPARE(model::clipGain(c), -6.0);
        QVERIFY(model::applyClipGain(c, -3.0)); // overrides, does not add (p.216)
        QCOMPARE(model::clipGain(c), -3.0);
        QVERIFY(model::applyClipGain(c, 40.0));
        QCOMPARE(model::clipGain(c), model::kClipGainMax);
        QVERIFY(!model::clipPan(c));
        QVERIFY(model::centerClipPan(c));
        QCOMPARE(model::clipPan(c), std::optional(0));
        BinItem seq;
        seq.kind = BinItemKind::Sequence;
        QVERIFY(!model::applyClipGain(seq, -6.0));
        QVERIFY(!model::centerClipPan(seq));
        QTemporaryDir dir;
        model::Bin bin;
        bin.name = QStringLiteral("Clips");
        c.id = QStringLiteral("c");
        bin.items << c;
        const QString path = dir.filePath(QStringLiteral("Clips") + model::BinFile::suffix());
        QVERIFY(model::BinFile::write(path, bin));
        const auto back = model::BinFile::read(path);
        QVERIFY(back && !back->items.isEmpty());
        QCOMPARE(back->items.first().audioGain, model::kClipGainMax);
        QCOMPARE(back->items.first().audioPan, std::optional(0));
    }

    // Auto VO (p.753-754): Extract segments removes the time of A2's material from every track; Keep
    // segments keeps only that time.
    void autoVo() {
        model::Sequence seq;
        seq.ensureTrack(QStringLiteral("A1"), model::AudioFormat::Mono)->segments = {material("I", 30)};
        // B-roll on A2 at 5-10 and 20-25.
        seq.ensureTrack(QStringLiteral("A2"), model::AudioFormat::Mono)->segments = {
            model::Segment::filler(5), material("B", 5), model::Segment::filler(10), material("R", 5), model::Segment::filler(5)};
        const auto extracted = model::autoVo(seq, QStringLiteral("A2"), false);
        QVERIFY(extracted);
        // The interview stays, cut where the B-roll was taken out.
        QCOMPARE(layout(*extracted, "A1"), (QStringList{QStringLiteral("I:5"), QStringLiteral("I:10"), QStringLiteral("I:5")}));
        QCOMPARE(extracted->track(QStringLiteral("A2"))->length(), 20);
        const auto kept = model::autoVo(seq, QStringLiteral("A2"), true);
        QVERIFY(kept);
        QCOMPARE(kept->track(QStringLiteral("A1"))->length(), 10);
        QCOMPARE(layout(*kept, "A2"), (QStringList{QStringLiteral("B:5"), QStringLiteral("R:5")}));
        QVERIFY(!model::autoVo(seq, QStringLiteral("V1"), false));
        QCOMPARE(model::autoVoName(QStringLiteral("News")), QStringLiteral("News.vo"));
    }

    void dialogs() {
        ApplyGainDialog gain(-4.0);
        QCOMPARE(gain.gainBox()->value(), -4.0);
        QCOMPARE(gain.gainBox()->minimum(), -96.0);
        QCOMPARE(gain.gainBox()->maximum(), 12.0);
        gain.gainBox()->setValue(3.5);
        QCOMPARE(gain.gain(), 3.5);
        AutoVoDialog vo({QStringLiteral("A1"), QStringLiteral("A2")});
        QVERIFY(vo.extractButton()->isChecked());
        vo.trackMenu()->setCurrentIndex(1);
        vo.keepButton()->setChecked(true);
        QCOMPARE(vo.track(), QStringLiteral("A2"));
        QVERIFY(vo.keep());
    }

    // The commands on a bin's selection.
    void inTheApplication() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        bin->addItem(clip("one"));
        bin->addItem(clip("two"));
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({QStringLiteral("one"), QStringLiteral("two")});
        double asked = 99;
        bool confirmed = false;
        MainWindow::ClipAudioHooks hooks;
        hooks.gain = [&asked](double current) {
            asked = current;
            return std::optional(-9.0);
        };
        hooks.confirmCenterPan = [&confirmed] {
            confirmed = true;
            return true;
        };
        hooks.autoVo = [](const QStringList& tracks) { return std::optional(std::pair{tracks.last(), false}); };
        window.setClipAudioHooks(hooks);
        commands.action(QStringLiteral("clip.audio.apply_gain"))->trigger();
        QCOMPARE(asked, 0.0);
        QCOMPARE(bin->bin().item(QStringLiteral("one"))->audioGain, -9.0);
        QCOMPARE(bin->bin().item(QStringLiteral("two"))->audioGain, -9.0);
        commands.action(QStringLiteral("clip.audio.center_pan"))->trigger();
        QVERIFY(confirmed);
        QCOMPARE(bin->bin().item(QStringLiteral("two"))->audioPan, std::optional(0));
        // Auto VO on a sequence makes "<name>.vo" beside it.
        BinItem seqItem = model::newSequence(bin->bin(), Rational(25), QDateTime::currentDateTimeUtc());
        model::Sequence seq = model::Sequence::withDefaultTracks();
        seq.track(QStringLiteral("A1"))->segments = {material("one", 20)};
        seq.track(QStringLiteral("A2"))->segments = {model::Segment::filler(10), material("two", 10)};
        seq.storeIn(seqItem);
        bin->addItem(seqItem);
        view->selectIds({seqItem.id});
        QCOMPARE(window.autoVo(), 1);
        const model::BinItem& made = bin->bin().items.last();
        QCOMPARE(made.name, seqItem.name + QStringLiteral(".vo"));
        QCOMPARE(model::Sequence::fromItem(made).track(QStringLiteral("A1"))->length(), 10);
        // The original stays as it was.
        QCOMPARE(model::Sequence::fromItem(*bin->bin().item(seqItem.id)).track(QStringLiteral("A1"))->length(), 20);
        // Auto VO on clips does nothing.
        view->selectIds({QStringLiteral("one")});
        QCOMPARE(window.autoVo(), 0);
    }
};

QTEST_MAIN(TestClipAudio)
#include "tst_clip_audio.moc"
