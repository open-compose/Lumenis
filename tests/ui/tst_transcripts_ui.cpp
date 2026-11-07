// Transcription in the application (8.2a; Editing Guide p.446-448, docs/spec/transcripts.md): master clips in the bins
// included in transcription transcribed on the Background Queue through the engine, Include/Exclude Bin in
// Transcription with the Sidebar's status, and "Include all new bins in transcription".
#include "lumenis/ext/provider.h"
#include "lumenis/ext/transcription.h"
#include "lumenis/media/media.h"
#include "lumenis/model/transcript.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/transcript_tool.h"
#include "lumenis/ui/speaker_names_dialog.h"
#include "lumenis/ui/find_window.h"
#include "lumenis/ui/transcript_dialogs.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/model/subcap.h"
#include "lumenis/model/monitor.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QMenu>
#include <QDir>
#include <QTemporaryDir>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QSignalSpy>
#include <QTest>
#include <QRegularExpression>
#include <QTextEdit>
#include <QTreeView>

#include <atomic>
#include <memory>
#include <cmath>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

bool writeTone(const QString& path, double seconds) {
    std::vector<float> samples(static_cast<std::size_t>(seconds * 48000));
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = 0.3f * static_cast<float>(std::sin(static_cast<double>(i) * 0.05));
    media::WriterSettings ws;
    ws.format = QStringLiteral("wav");
    ws.audioCodec = QStringLiteral("pcm_f32le");
    ws.channels = 1;
    ws.sampleRate = 48000;
    media::MediaWriter w;
    return w.open(path, ws) && w.writeAudio(samples.data(), static_cast<int>(samples.size())) && w.finish();
}

// Hears "Hello world." in anything, counting its calls.
class FakeProvider : public ext::TranscriptionProvider {
public:
    ext::ProviderInfo info() const override {
        return {"test.fake", "Fake", ext::Category::Transcription, ext::LicenseTier::OpenSource, "", ""};
    }
    ext::Transcript transcribe(std::span<const float>, int, const ext::TranscriptionOptions& options,
                               const ext::TranscriptionProgress&) override {
        ++calls;
        language = options.language;
        ext::Transcript t;
        t.words = {{"Hello", 0.1, 0.4, 1.0f, false}, {"world.", 0.5, 0.9, 1.0f, false}};
        return t;
    }
    std::atomic<int> calls{0};
    std::string language;
};

QString statusOf(BinContainer& bins, const QString& id) {
    const QModelIndex name = bins.treeModel()->indexOf(id);
    return bins.treeModel()->index(name.row(), BinTreeModel::kTranscriptionColumn, name.parent()).data().toString();
}

} // namespace

class TestTranscriptsUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void transcribesIncludedBins() {
        QTemporaryDir dir;
        qputenv("LUMENIS_TRANSCRIPTS", dir.filePath(QStringLiteral("Transcripts")).toLocal8Bit());
        const QString wav = dir.filePath(QStringLiteral("talk.wav"));
        const QString music = dir.filePath(QStringLiteral("music.wav"));
        QVERIFY(writeTone(wav, 2.0));
        QVERIFY(writeTone(music, 2.0));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        auto provider = std::make_shared<FakeProvider>();
        window.setTranscriber(provider);
        window.settings().setValue(QStringLiteral("Transcript"), QStringLiteral("languageHint"), QStringLiteral("de"));
        BinContainer& bins = window.binContainer();
        const QString talkBin = bins.newBin();
        const QString musicBin = bins.newBin();
        bins.openBin(talkBin);
        bins.openBin(musicBin);
        QCOMPARE(window.linkFiles(talkBin, {wav}).size(), qsizetype(1));
        QCOMPARE(window.linkFiles(musicBin, {music}).size(), qsizetype(1));
        QCOMPARE(statusOf(bins, musicBin), QStringLiteral("Transcription On"));
        // Exclude Bin from Transcription on the selected bin (p.447).
        bins.tree()->setCurrentIndex(bins.treeModel()->indexOf(musicBin));
        commands.action(QStringLiteral("bin.exclude_from_transcription"))->trigger();
        QVERIFY(!window.isBinTranscribed(musicBin));
        QCOMPARE(statusOf(bins, musicBin), QStringLiteral("Transcription Off"));
        QVERIFY(!model::TranscriptionBins(project.folder).isIncluded(musicBin)); // kept in the project
        // Only the included bin's clip is queued, once.
        QCOMPARE(window.queueTranscriptions(), 1);
        QCOMPARE(window.queueTranscriptions(), 0);
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(provider->calls.load(), 1);
        QCOMPARE(provider->language, std::string("de"));
        const model::TranscriptStore store;
        const QStringList mobs = store.mobs();
        QCOMPARE(mobs.size(), qsizetype(1));
        QCOMPARE(store.load(mobs.first())->lines.first().text(), QStringLiteral("Hello world."));
        QCOMPARE(window.queueTranscriptions(), 0); // transcribed already
        // Include it again: its clip is queued.
        commands.action(QStringLiteral("bin.include_in_transcription"))->trigger();
        QCOMPARE(statusOf(bins, musicBin), QStringLiteral("Transcription On"));
        QCOMPARE(window.queueTranscriptions(), 1);
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(store.mobs().size(), qsizetype(2));
        // "Include all new bins in transcription" off: new bins are excluded.
        window.settings().setValue(QStringLiteral("Transcript"), QStringLiteral("includeNewBins"), false);
        const QString later = bins.newBin();
        QVERIFY(!window.isBinTranscribed(later));
        qunsetenv("LUMENIS_TRANSCRIPTS");
    }

    // The Transcript Tool (p.442-444): a clip in the Source monitor and a sequence in the Record monitor shown, clicks
    // and selections moving the position and setting marks, search, Transcribe for a clip not transcribed.
    void transcriptTool() {
        QTemporaryDir dir;
        qputenv("LUMENIS_TRANSCRIPTS", dir.filePath(QStringLiteral("Transcripts")).toLocal8Bit());
        const QString wav = dir.filePath(QStringLiteral("talk.wav"));
        const QString other = dir.filePath(QStringLiteral("other.wav"));
        QVERIFY(writeTone(wav, 4.0));
        QVERIFY(writeTone(other, 2.0));
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
        auto provider = std::make_shared<FakeProvider>();
        window.setTranscriber(provider);
        BinContainer& bins = window.binContainer();
        const QString binId = bins.newBin();
        BinDocument* bin = bins.openBin(binId);
        const QStringList ids = window.linkFiles(binId, {wav, other});
        QCOMPARE(ids.size(), qsizetype(2));
        const model::BinItem talk = *bin->bin().item(ids[0]);
        const model::BinItem quiet = *bin->bin().item(ids[1]);
        model::Transcript t;
        t.mob = QString::fromStdString(model::mobIdOf(talk).toString());
        t.lines = {model::TranscriptLine{1, {{QStringLiteral("Good"), 0.0, 0.4}, {QStringLiteral("morning,"), 0.4, 0.8},
                                             {QStringLiteral("everyone."), 0.8, 1.2}}},
                   model::TranscriptLine{2, {{QStringLiteral("Morning!"), 2.0, 2.6}}}};
        QVERIFY(model::TranscriptStore().save(t));
        commands.action(QStringLiteral("tools.transcript_tool"))->trigger();
        TranscriptTool& tool = window.transcriptTool();
        Composer& composer = window.composer();
        composer.loadDefault(binId, talk);
        QCoreApplication::processEvents();
        QVERIFY(tool.sourceSide());
        QCOMPARE(tool.transcript().lines.size(), qsizetype(2));
        QVERIFY(tool.plainText().contains(QStringLiteral("Speaker 1: Good morning, everyone.")));
        QVERIFY(QRegularExpression(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d[:;]\\d\\d ")).match(tool.plainText()).hasMatch()); // Show Timecode
        // The playhead word follows the position.
        composer.moveTo(MonitorId::Source, 52);
        QCOMPARE(tool.playheadWord(), (std::optional<std::pair<int, int>>{{1, 0}}));
        // A click moves the position; a selection sets the marks (p.442).
        tool.clickWord(0, 2);
        QCOMPARE(composer.state(MonitorId::Source).position(), std::int64_t(20));
        tool.selectWords(0, 0, 2);
        QCOMPARE(composer.state(MonitorId::Source).in(), std::optional<std::int64_t>(0));
        QCOMPARE(composer.state(MonitorId::Source).out(), std::optional<std::int64_t>(19));
        // Search ignores case and punctuation; the arrows go to the next match.
        QCOMPARE(tool.search(QStringLiteral("morning")), 2);
        composer.moveTo(MonitorId::Source, 0);
        QVERIFY(tool.next());
        QCOMPARE(composer.state(MonitorId::Source).position(), std::int64_t(10));
        QVERIFY(tool.next());
        QCOMPARE(composer.state(MonitorId::Source).position(), std::int64_t(50));
        // Editing (p.445-446; 2026.8 guide p.446-447): text, a speaker's name, Correct Speaker ID with Group Speaker Lines,
        // the right-click menus, Revert Transcript.
        tool.editWords(0, 1, 2, QStringLiteral("afternoon,"));
        QVERIFY(tool.plainText().contains(QStringLiteral("Good afternoon, everyone.")));
        QCOMPARE(model::TranscriptStore().load(t.mob)->lines[0].words[1].start, 0.4); // timing kept
        tool.renameSpeaker(1, QStringLiteral("Ann"));
        QVERIFY(tool.plainText().contains(QStringLiteral("Ann: Morning!")));
        const int speakerPos = static_cast<int>(tool.plainText().indexOf(QStringLiteral("Ann")));
        QMenu* menu = tool.contextMenuAt(speakerPos + 1, &tool);
        QVERIFY(menu->findChild<QMenu*>(QStringLiteral("identifySpeaker")));
        QVERIFY(menu->findChild<QMenu*>(QStringLiteral("correctSpeaker")));
        delete menu;
        menu = tool.contextMenuAt(static_cast<int>(tool.plainText().indexOf(QStringLiteral("everyone"))) + 2, &tool);
        QStringList items;
        for (QAction* a : menu->actions()) items << a->text();
        QCOMPARE(items, (QStringList{QStringLiteral("Edit Text"), QStringLiteral("Start New Line"), QStringLiteral("Revert")}));
        delete menu;
        tool.correctSpeaker(1, 1); // Morning! is Speaker 1's too
        tool.setGroupSpeakers(true);
        QCOMPARE(tool.displayed().lines.size(), qsizetype(1));
        tool.setGroupSpeakers(false);
        QCOMPARE(tool.displayed().lines.size(), qsizetype(2));
        tool.startNewLine(0, 2);
        QCOMPARE(tool.displayed().lines.size(), qsizetype(3));
        tool.revertTranscript();
        QVERIFY(tool.plainText().contains(QStringLiteral("Good morning, everyone.")));
        QVERIFY(!model::TranscriptStore().load(t.mob)->edited());
        tool.replaceAll(QStringLiteral("everyone"), QStringLiteral("all"));
        QVERIFY(tool.plainText().contains(QStringLiteral("Good morning, all")));
        tool.revertTranscript();
        // A sequence in the Record monitor: the Record side, with the track button.
        model::BinItem seqItem;
        seqItem.id = model::newItemId();
        seqItem.name = QStringLiteral("Cut");
        seqItem.kind = model::BinItemKind::Sequence;
        seqItem.editRate = Rational(25);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment seg;
        seg.clipId = talk.id;
        seg.binId = binId;
        seg.name = talk.name;
        seg.sourceIn = 40;
        seg.length = 40;
        seg.sourceTrack = QStringLiteral("A1");
        seq.ensureTrack(QStringLiteral("A1"));
        seq.track(QStringLiteral("A1"))->segments = {seg};
        seq.storeIn(seqItem);
        bin->addItem(seqItem);
        composer.load(MonitorId::Record, binId, seqItem);
        QCoreApplication::processEvents();
        QVERIFY(!tool.sourceSide());
        QCOMPARE(tool.transcript().lines.size(), qsizetype(1));
        QCOMPARE(tool.transcript().lines[0].words[0].frame, std::int64_t(10));
        QVERIFY(tool.transcript().availableTracks.contains(QStringLiteral("A1")));
        // A clip not transcribed: the banner's Transcribe makes its transcript.
        composer.loadDefault(binId, quiet);
        QCoreApplication::processEvents();
        QVERIFY(tool.sourceSide());
        QCOMPARE(tool.transcript().notTranscribed.size(), qsizetype(1));
        QVERIFY(tool.findChild<QWidget*>(QStringLiteral("transcribe"))->isVisibleTo(&tool));
        QCOMPARE(window.transcribeNow(tool.transcript().notTranscribedMobs), 1);
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(tool.transcript().lines.size(), qsizetype(1));
        QVERIFY(tool.transcript().notTranscribed.isEmpty());
        qunsetenv("LUMENIS_TRANSCRIPTS");
    }

    // Manage Speaker Names (p.445): the project's names and those in its transcripts (dimmed when in no transcript), +,
    // rename, Merge and Clear/Delete, through the Transcript setting's Manage Speakers button.
    void manageSpeakerNames() {
        QTemporaryDir dir;
        qputenv("LUMENIS_TRANSCRIPTS", dir.filePath(QStringLiteral("Transcripts")).toLocal8Bit());
        const QString a = dir.filePath(QStringLiteral("a.wav"));
        const QString b = dir.filePath(QStringLiteral("b.wav"));
        QVERIFY(writeTone(a, 1.0));
        QVERIFY(writeTone(b, 1.0));
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
        BinContainer& bins = window.binContainer();
        const QString bin = bins.newBin();
        bins.openBin(bin);
        const auto clips = window.linkFiles(bin, {a, b});
        QCOMPARE(clips.size(), qsizetype(2));
        const model::TranscriptStore store;
        QStringList mobs;
        for (const model::BinItem& c : bins.document(bin)->bin().items) {
            model::Transcript t;
            t.mob = QString::fromStdString(model::mobIdOf(c).toString());
            t.clipName = c.name;
            t.lines = {model::TranscriptLine{1, {{QStringLiteral("hi"), 0.1, 0.3}}}, model::TranscriptLine{2, {{QStringLiteral("yo"), 0.4, 0.6}}}};
            t.speakers = {{1, mobs.isEmpty() ? QStringLiteral("Ann") : QStringLiteral("Anne")}, {2, QStringLiteral("Bob")}};
            QVERIFY(store.save(t));
            mobs << t.mob;
        }
        // The button in the Transcript setting's dialog asks for the window.
        const SettingKind* kind = SettingsSchema::builtIn().find(QStringLiteral("Transcript"));
        QVERIFY(kind);
        SettingsDialog settingsDialog(*kind, model::SettingEntry{QStringLiteral("Transcript"), {}, true, {}});
        auto* button = settingsDialog.findChild<QPushButton*>(QStringLiteral("manageSpeakers"));
        QVERIFY(button);
        QCOMPARE(button->text(), QStringLiteral("Manage Speakers"));
        QSignalSpy clicked(&settingsDialog, &SettingsDialog::buttonClicked);
        button->click();
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(clicked.first().first().toString(), QStringLiteral("manageSpeakers"));
        QVERIFY(!settingsDialog.values().contains(QStringLiteral("manageSpeakers")));

        SpeakerNamesDialog* d = window.manageSpeakers();
        QVERIFY(d);
        QCOMPARE(d->names(), QStringList({"Ann", "Anne", "Bob"}));
        QVERIFY(d->isUsed(QStringLiteral("Ann")));
        // "+": names added in advance, dimmed until used; Delete removes them from the list.
        d->add(QStringLiteral("Carol"));
        QCOMPARE(d->names(), QStringList({"Ann", "Anne", "Bob", "Carol"}));
        QVERIFY(!d->isUsed(QStringLiteral("Carol")));
        QCOMPARE(model::ProjectSpeakers(project.folder).names(), QStringList{"Carol"});
        QVERIFY(window.projectSpeakerNames().contains(QStringLiteral("Carol"))); // Identify Speaker offers it
        d->select({QStringLiteral("Carol")});
        QCOMPARE(d->clearButtonText(), QStringLiteral("Delete"));
        d->select({QStringLiteral("Carol"), QStringLiteral("Bob")});
        QCOMPARE(d->clearButtonText(), QStringLiteral("Clear"));
        d->select({QStringLiteral("Carol")});
        QVERIFY(d->clearSelected());
        QCOMPARE(d->names(), QStringList({"Ann", "Anne", "Bob"}));
        // Rename everywhere.
        d->rename(QStringLiteral("Bob"), QStringLiteral("Robert"));
        for (const QString& mob : mobs) QCOMPARE(store.load(mob)->speakerName(2), QStringLiteral("Robert"));
        // Merge Ann and Anne into one name.
        d->select({QStringLiteral("Ann"), QStringLiteral("Anne")});
        QVERIFY(d->mergeSelected());
        QCOMPARE(d->names(), QStringList({"Ann", "Robert"}));
        for (const QString& mob : mobs) QCOMPARE(store.load(mob)->speakerName(1), QStringLiteral("Ann"));
        // Clear: out of every transcript.
        d->select({QStringLiteral("Robert")});
        QVERIFY(d->clearSelected());
        QCOMPARE(d->names(), QStringList({"Ann"}));
        for (const QString& mob : mobs) QCOMPARE(store.load(mob)->speakerName(2), QStringLiteral("Speaker 2"));
        d->accept();
        qunsetenv("LUMENIS_TRANSCRIPTS");
    }

    // PhraseFind AI in the Find window (p.436-441): the PhraseFind AI button, Score and Transcription, the index status,
    // Load into monitor with the IN on the frame before the dialogue, Stop/Start indexing.
    void phraseFindWindow() {
        QTemporaryDir dir;
        qputenv("LUMENIS_TRANSCRIPTS", dir.filePath(QStringLiteral("Transcripts")).toLocal8Bit());
        const QString a = dir.filePath(QStringLiteral("talk.wav"));
        const QString b = dir.filePath(QStringLiteral("other.wav"));
        QVERIFY(writeTone(a, 4.0));
        QVERIFY(writeTone(b, 1.0));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        BinContainer& bins = window.binContainer();
        const QString bin = bins.newBin();
        bins.openBin(bin);
        QCOMPARE(window.linkFiles(bin, {a, b}).size(), qsizetype(2));
        QCOMPARE(window.phraseIndexStatus(), (std::pair<int, int>{0, 2}));
        const model::TranscriptStore store;
        const model::BinItem talk = bins.document(bin)->bin().items.first();
        model::Transcript t;
        t.mob = QString::fromStdString(model::mobIdOf(talk).toString());
        t.clipName = talk.name;
        t.lines = {model::TranscriptLine{1, {{QStringLiteral("The"), 2.0, 2.2}, {QStringLiteral("answer"), 2.2, 2.6}, {QStringLiteral("is"), 2.6, 2.8},
                                            {QStringLiteral("42."), 2.8, 3.2}}}};
        QVERIFY(store.save(t));
        QCOMPARE(window.phraseIndexStatus(), (std::pair<int, int>{1, 2}));

        commands.action(QStringLiteral("edit.find"))->trigger();
        FindWindow* find = window.findWindow();
        QVERIFY(find);
        QCOMPARE(find->phraseIndex()->value(), 1);
        QCOMPARE(find->phraseIndex()->maximum(), 2); // partly green: still indexing
        find->setText(QStringLiteral("the answer is 42"));
        QCOMPARE(find->phraseFind(), 1);
        QTableWidget* results = find->results();
        QCOMPARE(results->horizontalHeaderItem(2)->text(), QStringLiteral("Score"));
        QCOMPARE(results->horizontalHeaderItem(3)->text(), QStringLiteral("Transcription"));
        QCOMPARE(results->item(0, 2)->data(Qt::DisplayRole).toInt(), 100);
        QCOMPARE(results->item(0, 3)->text(), QStringLiteral("The answer is 42."));
        QCOMPARE(find->foundText(), QStringLiteral("Found: 1"));
        // Double-click: the Source monitor, the position and IN on the frame before "The" (frame 50).
        find->openResult(0);
        const model::MonitorState& source = window.composer().state(MonitorId::Source);
        QVERIFY(source.current());
        QCOMPARE(source.current()->item.id, talk.id);
        QCOMPARE(source.position(), std::int64_t(49));
        QCOMPARE(source.in(), std::optional<std::int64_t>(49));
        // A Transcription filter.
        find->setFilter(0, QStringLiteral("Transcription"), QStringLiteral("nothing like it"));
        QCOMPARE(find->phraseFind(), 0);
        find->setFilter(0, QString(), QString());
        // Settings > Stop PhraseFind AI Indexing stops the background transcription.
        std::unique_ptr<QMenu> menu(find->settingsMenu(nullptr));
        auto* toggle = menu->findChild<QAction*>(QStringLiteral("toggleIndexing"));
        QVERIFY(toggle);
        QCOMPARE(toggle->text(), QStringLiteral("Stop PhraseFind AI Indexing"));
        toggle->trigger();
        QVERIFY(!window.transcriptionIndexing());
        menu.reset(find->settingsMenu(nullptr));
        QCOMPARE(menu->findChild<QAction*>(QStringLiteral("toggleIndexing"))->text(), QStringLiteral("Start PhraseFind AI Indexing"));
        qunsetenv("LUMENIS_TRANSCRIPTS");
    }

    // Transcripts out (p.450-456): Export Transcript, Share Transcripts and Import through Manage Transcripts, Delete
    // Transcript, Export SubCap and Create SubCap on a sequence with the Transcribe window, Delete in Manage Transcripts.
    void transcriptsOut() {
        QTemporaryDir dir;
        qputenv("LUMENIS_TRANSCRIPTS", dir.filePath(QStringLiteral("Transcripts")).toLocal8Bit());
        const QString wav = dir.filePath(QStringLiteral("talk.wav"));
        QVERIFY(writeTone(wav, 4.0));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        BinContainer& bins = window.binContainer();
        const QString binId = bins.newBin();
        BinDocument* bin = bins.openBin(binId);
        QCOMPARE(window.linkFiles(binId, {wav}).size(), qsizetype(1));
        const model::BinItem talk = bin->bin().items.first();
        const model::TranscriptStore store;
        model::Transcript t;
        t.mob = QString::fromStdString(model::mobIdOf(talk).toString());
        t.clipName = talk.name;
        t.lines = {model::TranscriptLine{1, {{QStringLiteral("Roll"), 1.0, 1.4}, {QStringLiteral("camera."), 1.4, 2.0}}},
                   model::TranscriptLine{2, {{QStringLiteral("Action!"), 2.4, 3.0}}}};
        QVERIFY(store.save(t));
        // The file dialogs and the Transcribe window answered here.
        QStringList asked;
        QString saveAs = dir.filePath(QStringLiteral("talk.txt"));
        window.setTranscriptHooks(
            [&](const QString& kind, const QString&) {
                asked << kind;
                return kind == QLatin1String("folder") ? dir.path() : saveAs;
            },
            [](TranscribeTracksDialog& d) {
                d.setTracks({QStringLiteral("A1")});
                d.setCharacterLimit(0);
                return true;
            });
        BinView* view = bins.viewFor(bin);
        QVERIFY(view);
        QCOMPARE(bins.activeView(), view);
        view->selectIds({talk.id});
        // The bin's Transcript menu.
        std::unique_ptr<QMenu> menu(view->contextMenu(QPoint(1, 1), nullptr));
        auto* transcriptMenu = menu->findChild<QMenu*>(QStringLiteral("binTranscriptMenu"));
        QVERIFY(transcriptMenu);
        QVERIFY(transcriptMenu->findChild<QAction*>(QStringLiteral("bin.delete_transcript"))->isEnabled());
        QVERIFY(!transcriptMenu->findChild<QAction*>(QStringLiteral("bin.export_subcap"))->isEnabled());
        // Export Transcript.
        QCOMPARE(window.exportTranscripts(), 1);
        QFile text(saveAs);
        QVERIFY(text.open(QIODevice::ReadOnly));
        const QString exported = QString::fromUtf8(text.readAll());
        QVERIFY2(exported.contains(QStringLiteral("Speaker 1:\nRoll camera.")), qPrintable(exported));
        QVERIFY(exported.contains(QStringLiteral("Speaker 2:\nAction!")));
        // Share Transcripts, Delete Transcript, and Import through Manage Transcripts.
        saveAs = dir.filePath(QStringLiteral("shared.cttr"));
        QVERIFY(window.shareTranscripts());
        QCOMPARE(window.deleteTranscripts(), 1);
        QVERIFY(!store.has(t.mob));
        QCOMPARE(window.importSharedTranscripts(saveAs), 1);
        QVERIFY(store.has(t.mob));
        window.settings().setValue(QStringLiteral("Transcript"), QStringLiteral("languageHint"), QStringLiteral("de"));
        QString error;
        QCOMPARE(window.importSharedTranscripts(saveAs, &error), -1); // Language Hints must match
        QVERIFY(!error.isEmpty());
        window.settings().setValue(QStringLiteral("Transcript"), QStringLiteral("languageHint"), QString());

        // A sequence: the clip from frame 0 on A1 for 100 frames.
        model::BinItem seqItem;
        seqItem.id = model::newItemId();
        seqItem.name = QStringLiteral("Cut");
        seqItem.kind = model::BinItemKind::Sequence;
        seqItem.editRate = Rational(25);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment seg;
        seg.clipId = talk.id;
        seg.binId = binId;
        seg.name = talk.name;
        seg.sourceIn = 0;
        seg.length = 100;
        seg.sourceTrack = QStringLiteral("A1");
        seq.ensureTrack(QStringLiteral("A1"));
        seq.track(QStringLiteral("A1"))->segments = {seg};
        seq.storeIn(seqItem);
        bin->addItem(seqItem);
        view->selectIds({seqItem.id});
        saveAs = dir.filePath(QStringLiteral("cut.txt"));
        QVERIFY(window.exportSubCap());
        const auto captions = model::parseDsCaptions(QString::fromUtf8([&] {
            QFile f(saveAs);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }()), model::timecodeFormatOf(seqItem));
        QVERIFY(captions);
        QCOMPARE(captions->size(), qsizetype(2));
        QCOMPARE(captions->at(0).text, QStringLiteral("Roll camera."));
        // Create SubCap on the Record monitor's sequence: a new V2 with the captions.
        window.composer().load(MonitorId::Record, binId, seqItem);
        QVERIFY(window.createSubCap());
        const model::Sequence after = model::Sequence::fromItem(window.composer().state(MonitorId::Record).current()->item);
        const auto made = model::sequenceCaptions(after, {QStringLiteral("V2")});
        QCOMPARE(made.size(), qsizetype(2));
        QCOMPARE(made[0].start, std::int64_t(25));
        QCOMPARE(made[1].text, QStringLiteral("Action!"));

        // Manage Transcripts: the project's transcribed clip, deleted from there.
        ManageTranscriptsDialog* manage = window.manageTranscripts();
        QVERIFY(manage);
        QCOMPARE(manage->entries().size(), qsizetype(1));
        QCOMPARE(manage->entries().first().clip, talk.name);
        manage->select({t.mob});
        manage->findChild<QPushButton*>(QStringLiteral("deleteTranscripts"))->click();
        QVERIFY(!store.has(t.mob));
        QVERIFY(manage->entries().isEmpty());
        manage->accept();
        qunsetenv("LUMENIS_TRANSCRIPTS");
    }
};

QTEST_MAIN(TestTranscriptsUi)
#include "tst_transcripts_ui.moc"
