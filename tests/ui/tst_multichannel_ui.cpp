// Multichannel tracks in the application (Editing Guide p.279-281, p.671-672, p.734-736;
// docs/spec/multichannel.md): the Modify dialog's Set Multichannel Audio, Clip > Modify > Modify Clip
// on a bin's selection, and Split Track to Mono leaving a copy of the sequence in its bin. Editing and
// splitting are also in the edit scenarios (tests/scenarios/12-multichannel.json); the model in
// tests/model/tst_multichannel.cpp. Surround mixing's Audio Mixer buttons and the Audio Mixdown
// dialog's surround formats (docs/spec/surround.md) are here too; the mixing itself is in
// tests/media/tst_surround.cpp.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/multichannel.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/mixdown_dialog.h"
#include "lumenis/ui/modify_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QRadioButton>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::AudioFormat;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

BinItem clip(const char* id, const char* tracks, const char* formats = "") {
    BinItem c;
    c.id = QString::fromLatin1(id);
    c.name = QString::fromLatin1(id);
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = 50;
    c.fields.insert(QStringLiteral("Tracks"), QString::fromLatin1(tracks));
    if (*formats) c.fields.insert(QStringLiteral("Track Formats"), QString::fromLatin1(formats));
    return c;
}

} // namespace

class TestMultichannelUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // Set Multichannel Audio (p.671-672).
    void modifyDialog() {
        const QList<BinItem> clips{clip("one", "V1 A1-8", "Stereo A1A2"), clip("two", "A1-6")};
        ModifyDialog d(clips);
        // The options, Set Multichannel Audio the one available (p.280).
        QCOMPARE(d.optionsMenu()->currentText(), QStringLiteral("Set Multichannel Audio"));
        QCOMPARE(d.optionsMenu()->count(), 12);
        // Four pairs; the clips differ on A1A2, so it starts Mixed.
        QCOMPARE(d.pairCount(), 4);
        QVERIFY(!d.pairFormat(0));
        QCOMPARE(d.formatButton(0)->text(), QStringLiteral("Mixed"));
        QCOMPARE(d.pairFormat(1), std::optional(AudioFormat::Mono));
        // Mixed changes nothing.
        QStringList errors;
        QList<BinItem> out = d.apply(clips, &errors);
        QCOMPARE(out.size(), 2);
        QCOMPARE(out.at(0).fields.value(QStringLiteral("Track Formats")), QStringLiteral("Stereo A1A2"));
        QVERIFY(!out.at(1).fields.contains(QStringLiteral("Track Formats")));
        // Clicking cycles: Mono, Stereo, 5.1 (three pairs, their buttons dimmed), 7.1, Mono.
        d.formatButton(1)->click();
        QCOMPARE(d.pairFormat(1), std::optional(AudioFormat::Stereo));
        d.formatButton(1)->click();
        QCOMPARE(d.pairFormat(1), std::optional(AudioFormat::Surround51));
        QVERIFY(d.covered(2) && d.covered(3));
        QVERIFY(!d.formatButton(2)->isEnabled());
        d.formatButton(1)->click(); // 7.1 does not fit from A3 in eight tracks
        QCOMPARE(d.pairFormat(1), std::optional(AudioFormat::Mono));
        QVERIFY(!d.covered(2));
        // 5.1 on A3-A8 for both: it fits the first clip but not the six-track second one.
        d.setPairFormat(1, AudioFormat::Surround51);
        d.setPairFormat(0, AudioFormat::Mono);
        out = d.apply(clips, &errors);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().fields.value(QStringLiteral("Track Formats")), QStringLiteral("5.1 A3A4A5A6A7A8"));
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().startsWith(QLatin1String("two")));
        // Sequences and subclips cannot be modified (p.672).
        BinItem sub = clip("sub", "A1-2");
        sub.kind = BinItemKind::Subclip;
        errors.clear();
        ModifyDialog subDialog({sub});
        subDialog.setPairFormat(0, AudioFormat::Stereo);
        QVERIFY(subDialog.apply({sub}, &errors).isEmpty());
        QCOMPARE(errors.size(), 1);
    }

    // Clip > Modify > Modify Clip on the bin's selection, and Split Track to Mono keeping a copy.
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
        bin->addItem(clip("band", "V1 A1-4"));
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({QStringLiteral("band")});
        window.setModifyHook([](ModifyDialog& d) {
            d.setPairFormat(0, AudioFormat::Stereo);
            return true;
        });
        QVERIFY(commands.action(QStringLiteral("clip.modify.modify_clip"))->isEnabled());
        commands.action(QStringLiteral("clip.modify.modify_clip"))->trigger();
        QCOMPARE(bin->bin().item(QStringLiteral("band"))->fields.value(QStringLiteral("Track Formats")), QStringLiteral("Stereo A1A2"));
        // The source tracks follow: A1 stereo, A2 and A3 the other channels.
        QCOMPARE(model::sourceTracksOf(*bin->bin().item(QStringLiteral("band"))), (QStringList{"V1", "A1", "A2", "A3"}));

        // A sequence with a stereo track; Split All Tracks leaves the copy in the bin.
        BinItem seqItem = model::newSequence(bin->bin(), Rational(25), QDateTime::currentDateTimeUtc());
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment s;
        s.clipId = QStringLiteral("band");
        s.binId = binId;
        s.name = QStringLiteral("band");
        s.sourceTrack = QStringLiteral("A1");
        s.length = 10;
        seq.ensureTrack(QStringLiteral("A3"), AudioFormat::Stereo)->segments = {s};
        seq.storeIn(seqItem);
        bin->addItem(seqItem);
        window.composer().load(MonitorId::Record, binId, seqItem);
        const int items = static_cast<int>(bin->bin().items.size());
        commands.action(QStringLiteral("timeline.split_all_tracks"))->trigger();
        const model::Sequence after = model::Sequence::fromItem(window.composer().state(MonitorId::Record).current()->item);
        QCOMPARE(after.track(QStringLiteral("A3"))->format, AudioFormat::Mono);
        QVERIFY(after.track(QStringLiteral("A4")));
        QCOMPARE(static_cast<int>(bin->bin().items.size()), items + 1);
        QVERIFY(bin->bin().items.last().name.startsWith(seqItem.name + QStringLiteral(".Copy")));
        // Nothing left to split.
        QVERIFY(!window.timeline().splitTracksToMono(QString()));
        QVERIFY(!window.timeline().lastMessage().isEmpty());

        // The Sequence Mix Format button (p.686): an undoable change to the sequence.
        AudioMixer& mixer = window.audioMixer();
        QToolButton* format = mixer.sequenceMixButton();
        QVERIFY(format->isEnabled());
        QCOMPARE(format->text(), QStringLiteral("Stereo Sequence"));
        QCOMPARE(format->menu()->actions().size(), 3);
        format->menu()->findChild<QAction*>(QStringLiteral("sequenceMix:5.1"))->trigger();
        auto mixFormat = [&window] {
            return model::Sequence::fromItem(window.composer().state(MonitorId::Record).current()->item).mixFormat();
        };
        QCOMPARE(mixFormat(), AudioFormat::Surround51);
        QCOMPARE(format->text(), QStringLiteral("5.1 Sequence"));
        QVERIFY(!mixer.setSequenceMixFormat(AudioFormat::Surround51)); // no change
        QVERIFY(window.timeline().perform(QStringLiteral("edit.undo")));
        QCOMPARE(mixFormat(), AudioFormat::Stereo);
        QCOMPARE(format->text(), QStringLiteral("Stereo Sequence"));
        QVERIFY(mixer.setSequenceMixFormat(AudioFormat::Surround71));

        // The Mix Mode Selection button (p.689, p.691-692): its choice is the Audio Project setting.
        QToolButton* mix = mixer.mixModeButton();
        QCOMPARE(mix->menu()->findChildren<QAction*>().size() >= 10, true);
        mix->menu()->findChild<QAction*>(QStringLiteral("mixMode:5.1-smpte"))->trigger();
        QCOMPARE(mixer.monitorMix(), media::MonitorMix::Smpte51);
        QCOMPARE(mix->text(), QStringLiteral("5.1 SMPTE"));
        QCOMPARE(window.settings().text(QStringLiteral("Audio Project"), QStringLiteral("mixMode")), QStringLiteral("5.1-smpte"));
        mixer.setMixMode(QStringLiteral("direct")); // not one of ours: ignored
        QCOMPARE(mixer.monitorMix(), media::MonitorMix::Smpte51);
        // What plays is the sequence's 7.1 mix on 5.1 SMPTE speakers.
        const auto playing = window.mixOf(*window.composer().state(MonitorId::Record).current(), {});
        QCOMPARE(playing->channels(), 6);
        QCOMPARE(playing->monitor(), media::MonitorMix::Smpte51);
        QCOMPARE(playing->rightChannel(), 1);
        mixer.setMixMode(QStringLiteral("stereo"));
    }

    // The Audio Mixdown dialog's Stereo, 5.1 and 7.1 (p.732): "a stereo or surround sound mixdown
    // goes to the next available stereo or surround sound track", else a new one.
    // Clip > Modify > Set Format on a sequence (p.1294; 8.7c1): the dialog starts on it for sequences, the project's
    // format first; another edit rate duplicates the sequence, the same one modifies it.
    void setFormatOnSequences() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
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
        BinItem seqItem = model::newSequence(bin->bin(), Rational(25), QDateTime::currentDateTimeUtc());
        seqItem.name = QStringLiteral("Cut");
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment s;
        s.clipId = QStringLiteral("band");
        s.name = QStringLiteral("band");
        s.sourceTrack = QStringLiteral("V1");
        s.length = 50;
        seq.track(QStringLiteral("V1"))->segments = {s};
        seq.storeIn(seqItem);
        bin->addItem(seqItem);
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({seqItem.id});
        auto indexOf = [](const ModifyDialog& d, const std::string& label) {
            const QList<ProjectFormat> list = d.formats();
            for (int i = 0; i < list.size(); ++i) {
                if (list.at(i).label() == label) return i;
            }
            return -1;
        };
        bool startedOnSetFormat = false;
        std::string first;
        window.setModifyHook([&](ModifyDialog& d) {
            startedOnSetFormat = d.setsFormat();
            first = d.formats().first().label();
            d.chooseFormat(indexOf(d, "1080p/24"));
            return true;
        });
        const int items = static_cast<int>(bin->bin().items.size());
        QCOMPARE(window.modifySelection(), 1);
        QVERIFY(startedOnSetFormat);
        QCOMPARE(first, project.settings.format.label());
        QCOMPARE(static_cast<int>(bin->bin().items.size()), items + 1);
        const BinItem& made = bin->bin().items.last();
        QCOMPARE(made.name, QStringLiteral("Cut 24 fps"));
        QCOMPARE(made.editRate, Rational(24));
        QCOMPARE(made.durationFrames, 48);
        // The same edit rate: modified in place.
        window.setModifyHook([&](ModifyDialog& d) {
            d.chooseFormat(indexOf(d, "1080i/50"));
            return true;
        });
        view->selectIds({seqItem.id});
        QCOMPARE(window.modifySelection(), 1);
        QCOMPARE(static_cast<int>(bin->bin().items.size()), items + 1);
        QCOMPARE(bin->bin().item(seqItem.id)->fields.value(QStringLiteral("Format")), QStringLiteral("1080i/50"));
    }

    void mixdownFormats() {
        AudioMixdownDialog::Setup setup;
        setup.sourceTracks = {QStringLiteral("A1")};
        setup.emptyTracks = {QStringLiteral("A2"), QStringLiteral("A3"), QStringLiteral("A4"), QStringLiteral("A5")};
        setup.formats = {{QStringLiteral("A3"), AudioFormat::Surround51}, {QStringLiteral("A5"), AudioFormat::Stereo}};
        setup.bins = {{QStringLiteral("b"), QStringLiteral("Bin")}};
        AudioMixdownDialog d(setup);
        for (const char* name : {"Mono", "Dual Mono", "Stereo", "5.1", "7.1"}) {
            auto* b = d.findChild<QRadioButton*>(QStringLiteral("mixdownFormat:") + QString::fromLatin1(name));
            QVERIFY(b && b->isEnabled());
        }
        auto targets = [&d] {
            QStringList out;
            for (int i = 0; i < d.targetBox()->count(); ++i) out << d.targetBox()->itemText(i);
            return out;
        };
        // Mono tracks only for Mono and Dual Mono.
        d.setFormat(media::MixdownFormat::Mono);
        QCOMPARE(targets(), (QStringList{"A2", "A4", "New Track"}));
        d.setFormat(media::MixdownFormat::DualMono);
        QCOMPARE(targets(), (QStringList{"A2 and A4", "New Tracks"}));
        d.setFormat(media::MixdownFormat::Surround51);
        QCOMPARE(d.format(), media::MixdownFormat::Surround51);
        QCOMPARE(targets(), (QStringList{"A3", "New Track"}));
        QCOMPARE(d.targetTracks(), QStringList{QStringLiteral("A3")});
        d.setFormat(media::MixdownFormat::Stereo);
        QCOMPARE(targets(), (QStringList{"A5", "New Track"}));
        d.setFormat(media::MixdownFormat::Surround71);
        QCOMPARE(targets(), (QStringList{"New Track"}));
        QVERIFY(d.targetTracks().isEmpty());
    }
};

QTEST_MAIN(TestMultichannelUi)
#include "tst_multichannel_ui.moc"
