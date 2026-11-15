// The IMF window and CPLs into bins (11.5c1; docs/spec/imf.md; Editing Guide p.333-337): a package of MXF track files
// explored, its root, IMP and CPL in the sidebar with their metadata, and the CPL dropped into a bin as a sequence of
// clips linked to its track files.
#include "lumenis/ext/provider.h"
#include "lumenis/interop/imf.h"
#include "lumenis/media/media.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/imf_window.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#include <memory>
#include <vector>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void write(const QString& path, const QString& text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) qFatal("cannot write %s", qPrintable(path));
    f.write(text.toUtf8());
}

// A package of a 12-frame picture track file and a stereo sound one, at 24 fps.
QString makePackage(const QString& root, const QUuid& cplId) {
    QDir().mkpath(root);
    const QUuid video = QUuid::createUuid();
    const QUuid audio = QUuid::createUuid();
    const QUuid pklId = QUuid::createUuid();
    {
        media::WriterSettings s;
        s.format = QStringLiteral("mxf");
        s.videoCodec = QStringLiteral("dnxhd");
        s.videoProfile = QStringLiteral("dnxhr_lb");
        s.pixelFormat = QStringLiteral("yuv422p");
        s.width = 256;
        s.height = 144;
        s.frameRate = Rational(24);
        media::MediaWriter w;
        if (!w.open(root + QStringLiteral("/video.mxf"), s)) qFatal("no video track file");
        QImage picture(256, 144, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        for (int f = 0; f < 12; ++f) w.writeVideo(picture);
        w.finish();
    }
    {
        media::WriterSettings s;
        // Sound as WAVE: FFmpeg's MXF muxer takes no sound-only file, and the reader does not mind the container.
        s.format = QStringLiteral("wav");
        s.audioCodec = QStringLiteral("pcm_s24le");
        s.channels = 2;
        s.sampleRate = 48000;
        media::MediaWriter w;
        QString error;
        if (!w.open(root + QStringLiteral("/audio.wav"), s, &error)) qFatal("no audio track file: %s", qPrintable(error));
        std::vector<float> v(2 * 24000, 0.1f);
        w.writeAudio(v.data(), 24000);
        w.finish();
    }
    auto resource = [](const QUuid& file, const QString& rate, int duration) {
        return QStringLiteral("<Resource><Id>%1</Id><EditRate>%2</EditRate><IntrinsicDuration>%3</IntrinsicDuration>"
                              "<EntryPoint>0</EntryPoint><SourceDuration>%3</SourceDuration><TrackFileId>%4</TrackFileId></Resource>")
            .arg(interop::imf::urn(QUuid::createUuid()), rate).arg(duration).arg(interop::imf::urn(file));
    };
    write(root + QStringLiteral("/CPL_promo.xml"),
          QStringLiteral("<?xml version=\"1.0\"?>\n<CompositionPlaylist xmlns=\"http://www.smpte-ra.org/schemas/2067-3/2016\">"
                         "<Id>%1</Id><IssueDate>2026-11-15T12:00:00Z</IssueDate><Issuer>Studio</Issuer><Creator>Cutthulhu</Creator>"
                         "<ContentTitle>Promo</ContentTitle><ContentKind>promotion</ContentKind><EditRate>24 1</EditRate>"
                         "<SegmentList><Segment><Id>%2</Id><SequenceList>"
                         "<MainImageSequence><Id>%3</Id><TrackId>%4</TrackId><ResourceList>%5</ResourceList></MainImageSequence>"
                         "<MainAudioSequence><Id>%6</Id><TrackId>%7</TrackId><ResourceList>%8</ResourceList></MainAudioSequence>"
                         "</SequenceList></Segment></SegmentList></CompositionPlaylist>\n")
              .arg(interop::imf::urn(cplId), interop::imf::urn(QUuid::createUuid()), interop::imf::urn(QUuid::createUuid()),
                   interop::imf::urn(QUuid::createUuid()), resource(video, QStringLiteral("24 1"), 12),
                   interop::imf::urn(QUuid::createUuid()), interop::imf::urn(QUuid::createUuid()),
                   resource(audio, QStringLiteral("48000 1"), 24000)));
    write(root + QStringLiteral("/PKL_promo.xml"),
          QStringLiteral("<?xml version=\"1.0\"?>\n<PackingList><Id>%1</Id><Annotation>Promo IMP</Annotation><Issuer>Studio</Issuer>"
                         "<Creator>Cutthulhu</Creator><AssetList><Asset><Id>%2</Id><Type>text/xml</Type></Asset>"
                         "<Asset><Id>%3</Id><Type>application/mxf</Type></Asset><Asset><Id>%4</Id><Type>application/mxf</Type></Asset>"
                         "</AssetList></PackingList>\n")
              .arg(interop::imf::urn(pklId), interop::imf::urn(cplId), interop::imf::urn(video), interop::imf::urn(audio)));
    auto asset = [](const QUuid& id, const QString& path, bool pkl) {
        return QStringLiteral("<Asset><Id>%1</Id>%2<ChunkList><Chunk><Path>%3</Path></Chunk></ChunkList></Asset>")
            .arg(interop::imf::urn(id), pkl ? QStringLiteral("<PackingList>true</PackingList>") : QString(), path);
    };
    write(root + QStringLiteral("/ASSETMAP.xml"),
          QStringLiteral("<?xml version=\"1.0\"?>\n<AssetMap><Id>%1</Id><AnnotationText>Promo package</AnnotationText>"
                         "<Creator>Cutthulhu</Creator><Issuer>Studio</Issuer><AssetList>%2%3%4%5</AssetList></AssetMap>\n")
              .arg(interop::imf::urn(QUuid::createUuid()), asset(pklId, QStringLiteral("PKL_promo.xml"), true),
                   asset(cplId, QStringLiteral("CPL_promo.xml"), false), asset(video, QStringLiteral("video.mxf"), false),
                   asset(audio, QStringLiteral("audio.wav"), false)));
    return root + QStringLiteral("/CPL_promo.xml");
}

} // namespace

class TestImfUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void exploreAndDragIntoABin() {
        QTemporaryDir dir;
        const QUuid cplId = QUuid::createUuid();
        const QString root = dir.filePath(QStringLiteral("Promo_IMF"));
        const QString cpl = makePackage(root, cplId);
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
        project.settings.format.editRate = Rational(24);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();

        // Tools > IMF Window opens the panel (p.335).
        QAction* open = commands.action(QStringLiteral("tools.imf_window"));
        QVERIFY(open && open->isEnabled());
        open->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("IMF"))));
        ImfWindow* imf = window.imfWindow();
        QVERIFY(imf);

        // Explore: the root folder chosen; root, IMP and CPL in the sidebar, green.
        imf->setFolderChooser([&] { return root; });
        imf->exploreButton()->click();
        QCOMPARE(imf->packages().size(), 1);
        QTreeWidget* tree = imf->sidebar();
        QCOMPARE(tree->topLevelItemCount(), 1);
        QTreeWidgetItem* rootItem = tree->topLevelItem(0);
        QVERIFY(rootItem);
        QCOMPARE(rootItem->text(0), QStringLiteral("Promo_IMF"));
        QCOMPARE(rootItem->childCount(), 1);
        QTreeWidgetItem* impItem = rootItem->child(0);
        QVERIFY(impItem);
        QTreeWidgetItem* cplItem = impItem->child(0);
        QVERIFY(cplItem);
        QCOMPARE(cplItem->text(0), QStringLiteral("Promo"));
        QCOMPARE(ImfWindow::statusColor(cplItem), QColor(0x30, 0xb0, 0x40));
        // The metadata of the root, the IMP and the CPL (p.335).
        QVERIFY(imf->metadata().contains(QStringLiteral("Annotation: Promo package")));
        QVERIFY(imf->select(root + QStringLiteral("/PKL_promo.xml")));
        QVERIFY(imf->metadata().contains(QStringLiteral("Annotation: Promo IMP")));
        QVERIFY(imf->select(cpl));
        const QStringList meta = imf->metadata();
        QVERIFY2(meta.contains(QStringLiteral("Content Title: Promo")), qPrintable(meta.join('\n')));
        QVERIFY(meta.contains(QStringLiteral("CPL File Name: CPL_promo.xml")));
        QVERIFY2(meta.contains(QStringLiteral("Duration: 12 frames")) || meta.contains(QStringLiteral("Duration: 12 frame(s)")), qPrintable(meta.join(QLatin1Char('\n'))));
        QVERIFY(meta.contains(QStringLiteral("Track Files: 2")));
        // A CPL drags as its file.
        std::unique_ptr<QMimeData> data(tree->model()->mimeData({tree->indexFromItem(cplItem)}));
        QVERIFY(data);
        const QList<QUrl> urls = data->urls();
        QCOMPARE(urls.size(), 1);
        QCOMPARE(urls.at(0).toLocalFile(), QFileInfo(cpl).absoluteFilePath());

        // Dropped into a bin: the track files as clips and a 12-frame sequence of them (p.336).
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.importFiles(binId, {cpl});
        QCOMPARE(ids.size(), 3);
        const model::BinItem* sequence = bin->bin().item(ids.last());
        QVERIFY(sequence);
        QCOMPARE(sequence->kind, model::BinItemKind::Sequence);
        QCOMPARE(sequence->name, QStringLiteral("Promo"));
        const model::Sequence seq = model::Sequence::fromItem(*sequence);
        QCOMPARE(seq.track(QStringLiteral("V1"))->length(), 12);
        QVERIFY(seq.track(QStringLiteral("A2")));
        QCOMPARE(seq.track(QStringLiteral("A1"))->length(), 12);
        const model::BinItem* video = bin->bin().item(ids.first());
        QVERIFY(video && video->fields.value(QStringLiteral("IMF Track File")).startsWith(QStringLiteral("urn:uuid:")));

        // Deleting the root takes it off the sidebar, nothing off the disk (p.338).
        imf->removePackage(root);
        QCOMPARE(tree->topLevelItemCount(), 0);
        QVERIFY(QFileInfo::exists(cpl));
        // Not a package: the reason.
        QString error;
        QVERIFY(!imf->addPackage(dir.filePath(QStringLiteral("Film")), &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestImfUi)
#include "tst_imf_ui.moc"
