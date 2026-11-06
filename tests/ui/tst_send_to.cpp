// File > Output > Send To (7.1c2; docs/spec/send-to.md, Editing Guide p.863-867): .stt templates read and written,
// the Pro Tools options' exports; Send To > Pro Tools with QuickTime - Link to Audio writing a movie of the picture and
// an AAF of the sound, Auto Launch and Reveal file; Make New saving a template that the menu then lists and that
// exports with its own values into the folder it remembers.
#include "lumenis/ext/provider.h"
#include "lumenis/interop/aaf_import.h"
#include "lumenis/media/media.h"
#include "lumenis/model/send_to.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/send_to_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QCheckBox>
#include <QLineEdit>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

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
        image.fill(QColor(i * 20, 90, 160));
        if (!w.writeVideo(image) || !w.writeAudio(sound.data(), 1920)) return false;
    }
    return w.finish();
}

QAction* menuAction(QMenuBar* bar, const QString& text) {
    auto* menu = bar->findChild<QMenu*>(QStringLiteral("menu:file_output_send_to"));
    if (!menu) return nullptr;
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

} // namespace

class TestSendTo : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void templates() {
        QTemporaryDir dir;
        model::SendToTemplate t;
        t.name = QStringLiteral("ignored");
        t.exportValues = QJsonObject{{QStringLiteral("exportAs"), QStringLiteral("mp4")}};
        t.folder = QStringLiteral("/out");
        t.autoLaunch = true;
        t.application = QStringLiteral("/apps/player");
        t.reveal = true;
        QVERIFY(model::saveSendToTemplate(dir.filePath(QStringLiteral("Web Review")), t));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("Web Review.stt"))));
        const QList<model::SendToTemplate> back = model::loadSendToTemplates(dir.path());
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.first().name, QStringLiteral("Web Review")); // named by its file
        QCOMPARE(back.first().exportValues.value(QStringLiteral("exportAs")).toString(), QStringLiteral("mp4"));
        QCOMPARE(back.first().folder, QStringLiteral("/out"));
        QVERIFY(back.first().autoLaunch && back.first().reveal && back.first().autoLoad);
        QVERIFY(!model::SendToTemplate::fromJson(QJsonObject{}));

        // The Pro Tools options (p.863-864): one AAF, or a movie of the picture and an AAF of the sound.
        QCOMPARE(model::proToolsOptions().size(), 6);
        const QJsonObject base{{QStringLiteral("useMarks"), true}};
        auto link = model::proToolsExports(QStringLiteral("linkVideoAudio"), base);
        QCOMPARE(link.size(), 1);
        QCOMPARE(link.first().value(QStringLiteral("exportAs")).toString(), QStringLiteral("aaf"));
        QVERIFY(link.first().value(QStringLiteral("useMarks")).toBool()); // over the base
        auto quicktime = model::proToolsExports(QStringLiteral("quicktimeConsolidateAudio"), base);
        QCOMPARE(quicktime.size(), 2);
        QCOMPARE(quicktime.at(0).value(QStringLiteral("exportAs")).toString(), QStringLiteral("mov"));
        QCOMPARE(quicktime.at(0).value(QStringLiteral("movieTracks")).toString(), QStringLiteral("video"));
        QCOMPARE(quicktime.at(1).value(QStringLiteral("audioExportMethod")).toString(), QStringLiteral("consolidate"));
        QVERIFY(!quicktime.at(1).value(QStringLiteral("includeVideo")).toBool());
        QCOMPARE(model::proToolsExports(QStringLiteral("videoMixdownLinkAudio"), base).first().value(QStringLiteral("videoExportMethod")).toString(),
                 QStringLiteral("mixdown"));
        QVERIFY(model::proToolsExports(QStringLiteral("quicktimeEmbedAudio"), base).isEmpty());
    }

    void sendToProToolsAndMakeNew() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A005.mov"));
        QVERIFY(writeMovie(movie));
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
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QCOMPARE(window.linkFiles(binId, {movie}).size(), 1);
        const model::BinItem clip = bin->bin().items.first();
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), clip);
        composer.state(MonitorId::Source).setPosition(2);
        composer.state(MonitorId::Source).markIn();
        composer.state(MonitorId::Source).setPosition(5);
        composer.state(MonitorId::Source).markOut();
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const QString sequenceId = composer.state(MonitorId::Record).current()->item.id;
        window.binContainer().saveAll();
        window.binContainer().viewFor(bin)->selectIds({sequenceId});
        window.settings().setValue(QStringLiteral("Export"), QStringLiteral("movieCompression"), QStringLiteral("png"));

        // The menu lists Pro Tools above Make New.
        QVERIFY(menuAction(window.menuBar(), QStringLiteral("Pro Tools")));

        // Pro Tools, QuickTime - Link to Audio, into a folder, with Auto Launch and Reveal file.
        const QString out = dir.filePath(QStringLiteral("Turnover"));
        QString launched;
        QStringList loaded;
        QString revealed;
        window.setRevealHook([&](const QString& f) { revealed = f; });
        window.setSendToHooks(
            [&](SendToDialog& d) {
                d.setFolder(out);
                model::SendToTemplate t = d.sendToTemplate();
                // The movie is the picture only: PNG here, as DNxHR waits for legal review in some builds.
                QJsonObject values = t.exportValues;
                values.insert(QStringLiteral("movieCompression"), QStringLiteral("png"));
                d.setExportValues(values);
                d.findChild<QCheckBox*>(QStringLiteral("sendToAutoLaunch"))->setChecked(true);
                d.findChild<QLineEdit*>(QStringLiteral("sendToApplication"))->setText(QStringLiteral("/apps/protools"));
                d.findChild<QCheckBox*>(QStringLiteral("sendToReveal"))->setChecked(true);
                d.findChild<QLineEdit*>(QStringLiteral("sendToFileName"))->setText(QStringLiteral("Reel 1"));
                return true;
            },
            [](ProToolsOptionsDialog& d) {
                d.setOption(QStringLiteral("quicktimeLinkAudio"));
                return true;
            },
            {}, {}, [&](const QString& app, const QStringList& files) {
                launched = app;
                loaded = files;
            });
        menuAction(window.menuBar(), QStringLiteral("Pro Tools"))->trigger();
        const QString mov = QDir(out).filePath(QStringLiteral("Reel 1.mov"));
        const QString aaf = QDir(out).filePath(QStringLiteral("Reel 1.aaf"));
        auto info = media::probe(mov);
        QVERIFY(info);
        QVERIFY(info->firstVideo());
        QVERIFY(info->audio().empty());
        QCOMPARE(info->firstVideo()->length, 4);
        const auto imported = interop::aaf::importFile(aaf);
        QVERIFY(imported);
        for (const model::BinItem& i : imported->items) {
            if (i.kind == model::BinItemKind::Sequence) {
                QVERIFY(!model::Sequence::fromItem(i).track(QStringLiteral("V1")));
                QVERIFY(model::Sequence::fromItem(i).track(QStringLiteral("A1")));
            }
        }
        QCOMPARE(launched, QStringLiteral("/apps/protools"));
        QCOMPARE(loaded, (QStringList{mov, aaf}));
        QCOMPARE(revealed, mov);

        // Make New: a template of the Export setting's values, here a Graphic still, saved where the menu finds it.
        const QString saved = QDir(window.sendToFolder()).filePath(QStringLiteral("Stills.stt"));
        window.setSendToHooks(
            [&](SendToDialog& d) {
                QJsonObject values = d.sendToTemplate().exportValues;
                values.insert(QStringLiteral("exportAs"), QStringLiteral("graphic"));
                values.insert(QStringLiteral("graphicFormat"), QStringLiteral("png"));
                d.setExportValues(values);
                d.setFolder(dir.filePath(QStringLiteral("Stills")));
                return true;
            },
            {}, [&](const QString&) { return saved; });
        commands.action(QStringLiteral("file.output.send_to.make_new"))->trigger();
        QVERIFY(QFileInfo::exists(saved));
        window.refreshSendToMenu();
        QAction* stills = menuAction(window.menuBar(), QStringLiteral("Stills"));
        QVERIFY(stills);
        QVERIFY(stills->property(kDynamicMenuItem).toBool());
        // Sent with it: the still in the folder it was made with, the setting's own Export As untouched.
        window.setSendToHooks([](SendToDialog&) { return true; }, {}, {});
        stills->trigger();
        QCOMPARE(QDir(dir.filePath(QStringLiteral("Stills"))).entryList({QStringLiteral("*.png")}).size(), 1);
        QCOMPARE(window.settings().text(QStringLiteral("Export"), QStringLiteral("exportAs")), QStringLiteral("aaf"));
    }
};

QTEST_MAIN(TestSendTo)
#include "tst_send_to.moc"
