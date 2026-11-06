// The SubCap effect in the application (7.4a; docs/spec/subcap.md, FX Guide ch.14): its caption drawn over the picture;
// Import Caption Data of an Avid DS file into the effect under the position; the Master Caption List editing a caption
// with two rows; Export Caption Data as EBU N19; the Global Properties dialog's information, a stylesheet made the
// default and applied to the next SubCap effect.
#include "lumenis/ext/provider.h"
#include "lumenis/media/compositor.h"
#include "lumenis/media/media.h"
#include "lumenis/model/subcap.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/subcap_dialogs.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
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
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    for (int i = 0; i < 300; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(Qt::darkGray);
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

} // namespace

class TestSubCapUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void picture() {
        // White text on its half-opaque black box near the bottom centre; nothing with Bypass Effect or without text.
        model::Effect e{QStringLiteral("Generator"), QStringLiteral("SubCap"), {}};
        const media::Picture black = media::Picture::solid(1920, 1080, QColor(50, 50, 50));
        QCOMPARE(media::subcapPicture(e, black).toImage().pixelColor(960, 900), QColor(50, 50, 50));
        model::setCaptionText(e, QStringLiteral("WWWWWWWWWWWWWWWW"));
        const QImage shown = media::subcapPicture(e, black).toImage();
        // The anchor is 420 pixels below the centre (Y -420): the box ends there, the text above it.
        int bright = 0;
        for (int x = 700; x < 1220; ++x) bright += qGray(shown.pixel(x, 1080 / 2 + 420 - 30)) > 200 ? 1 : 0;
        QVERIFY2(bright > 40, qPrintable(QString::number(bright)));
        QVERIFY(qGray(shown.pixel(960, 200)) == 50);
        QVERIFY(qGray(shown.pixel(960, 1080 / 2 + 420 - 2)) < 50); // the box darkens the picture
        QJsonObject p = e.parameters.value(QLatin1String(model::kSubCapCategory)).toObject();
        p.insert(QStringLiteral("Bypass Effect"), true);
        e.parameters.insert(QLatin1String(model::kSubCapCategory), p);
        QCOMPARE(media::subcapPicture(e, black).toImage().pixelColor(960, 1080 / 2 + 420 - 30), QColor(50, 50, 50));
    }

    void captionsInTheApplication() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A001.mov"));
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
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.linkFiles(binId, {movie}).size(), 1);
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), bin->bin().items.first());
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        // A SubCap effect over the whole of V1, the position on it, V1 selected (p.380-381).
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("SubCap"), [](model::Sequence& s) {
            s.track(QStringLiteral("V1"))->segments.first().effect = model::Effect{QStringLiteral("Generator"), QStringLiteral("SubCap"), {}};
            return true;
        }));
        composer.state(MonitorId::Record).setPosition(10);
        auto seq = [&] { return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item); };
        const model::Sequence before = seq();
        for (const model::Track& t : before.tracks()) window.timeline().patch().setRecordSelected(t.name(), t.name() == QLatin1String("V1"));
        QVERIFY(window.timeline().currentEffect());

        // Import Caption Data: an Avid DS file at the sequence's timecode (it starts at 01:00:00:00).
        const QString ds = dir.filePath(QStringLiteral("captions.txt"));
        {
            QFile f(ds);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("01:00:02:00 01:00:03:00\nBob, are you there?\n\n01:00:04:18 01:00:06:13\nYes, Sam.\n");
        }
        QString chosen = ds;
        bool masterOk = true;
        QString masterText;
        window.setSubcapHooks([&](bool) { return chosen; }, [](ExportCaptionsDialog&) { return true; },
                              [&](MasterCaptionListDialog& d) {
                                  if (masterText.isEmpty()) return masterOk;
                                  // Empty captions are listed too (the subdivided effect's other pieces).
                                  const auto entries = d.entries();
                                  for (int i = 0; i < entries.size(); ++i)
                                      if (entries[i].text.startsWith(QLatin1String("Bob"))) d.setText(i, masterText);
                                  return masterOk;
                              });
        QVERIFY(window.subcapAction(QStringLiteral("import")));
        const model::Sequence imported = seq();
        const model::Track* v1 = imported.track(QStringLiteral("V1"));
        QCOMPARE(v1->segments.size(), 5);
        QCOMPARE(model::captionText(*v1->segments[1].effect), QStringLiteral("Bob, are you there?"));
        QCOMPARE(v1->segments[1].length, 25);

        // The Master Caption List: two rows in the first caption (p.384).
        masterText = QStringLiteral("Bob,\nare you there?");
        QVERIFY(window.subcapAction(QStringLiteral("masterList")));
        const model::Sequence edited = seq();
        QCOMPARE(model::captionText(*edited.track(QStringLiteral("V1"))->segments[1].effect), masterText);

        // Export Caption Data as EBU N19 at the sequence's timecode.
        const QString stl = dir.filePath(QStringLiteral("captions.stl"));
        chosen = stl;
        QVERIFY(window.subcapAction(QStringLiteral("export")));
        QFile f(stl);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const auto back = model::parseStl(f.readAll());
        QVERIFY(back);
        QCOMPARE(back->size(), 2);
        QCOMPARE(back->first().start, std::int64_t(3600 * 25 + 50));
        QCOMPARE(back->first().text, masterText);

        // Global Properties: the information goes to every SubCap effect (Sequence); a stylesheet made the default
        // seeds the next SubCap effect (p.387-388).
        window.setSubcapHooks([&](bool) { return chosen; }, {}, {}, [](SubcapPropertiesDialog& d) {
            d.setField(QStringLiteral("Publisher"), QStringLiteral("Cutthulhu Pictures"));
            d.setField(QStringLiteral("Language"), QStringLiteral("09"));
            if (!d.createStylesheet(QStringLiteral("House")) || !d.setDefaultStylesheet(QStringLiteral("House"))) return false;
            return d.stylesheets() == QStringList{QStringLiteral("House")};
        });
        QVERIFY(window.subcapAction(QStringLiteral("globalProperties")));
        const model::Sequence styled = seq();
        for (const model::Segment& g : styled.track(QStringLiteral("V1"))->segments) {
            QCOMPARE(g.effect->parameters.value(QLatin1String(model::kSubCapPropertiesCategory)).toObject().value(QStringLiteral("Publisher")).toString(),
                     QStringLiteral("Cutthulhu Pictures"));
        }
        QCOMPARE(model::subcapStlInfo(seq()).languageCode, QStringLiteral("09"));
        QVERIFY(QFileInfo::exists(QDir(window.subcapStylesheetFolder()).filePath(QStringLiteral("House.xml"))));
        QCOMPARE(defaultSubcapStylesheet(window.subcapStylesheetFolder()), QStringLiteral("House"));
        model::Effect fresh{QStringLiteral("Generator"), QStringLiteral("SubCap"), {}};
        QVERIFY(applySubcapStylesheet(QDir(window.subcapStylesheetFolder()).filePath(QStringLiteral("House.xml")), fresh));
        QCOMPARE(fresh.parameters.value(QLatin1String(model::kSubCapPropertiesCategory)).toObject().value(QStringLiteral("Publisher")).toString(),
                 QStringLiteral("Cutthulhu Pictures"));
        QVERIFY(model::captionText(fresh).isEmpty());
    }
};

QTEST_MAIN(TestSubCapUi)
#include "tst_subcap_ui.moc"
