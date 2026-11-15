// The IMF window and CPLs into bins (11.5c1; docs/spec/imf.md; Editing Guide p.333-337): a package of MXF track files
// explored, its root, IMP and CPL in the sidebar with their metadata, and the CPL dropped into a bin as a sequence of
// clips linked to its track files. New IMF (11.5c2): sequences dropped on a draft, its red and yellow statuses, the
// Issuer and Creator filling the IMP and root, Export writing the package, read back green and into a bin again.
#include "lumenis/ext/provider.h"
#include "lumenis/interop/imf.h"
#include "lumenis/interop/imf_writer.h"
#include "lumenis/media/imf_export.h"
#include "lumenis/model/auto_sequence.h"
#include "lumenis/media/media.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/imf_window.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>
#include <QComboBox>
#include <QKeyEvent>
#include <QDropEvent>
#include <QLineEdit>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
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

// A sequence for a draft, as MainWindow::addImfSequences makes it.
ImfDraftCpl draftCpl(const QString& binId, const QString& itemId, const QString& name) {
    ImfDraftCpl c;
    c.binId = binId;
    c.itemId = itemId;
    c.sequenceName = name;
    return c;
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

    void formatsAndProfiles() {
        // The IMF Supported Formats (p.340-341) and the encoding profiles (p.1256-1257).
        QCOMPARE(media::imfEncodingProfiles().size(), 3);
        QVERIFY(media::imfFormatSupported(QSize(1920, 1080), Rational(24000, 1001), false, QStringLiteral("J2K IMF YCrCb")));
        QVERIFY(media::imfFormatSupported(QSize(3840, 2160), Rational(60), false, QStringLiteral("J2K IMF YCrCb")));
        QVERIFY(media::imfFormatSupported(QSize(4096, 2160), Rational(24), false, QStringLiteral("J2K IMF RGB 12 bit")));
        QString why;
        QVERIFY(!media::imfFormatSupported(QSize(4096, 2160), Rational(24), false, QStringLiteral("J2K IMF YCrCb"), &why));
        QVERIFY(!why.isEmpty());
        QVERIFY(!media::imfFormatSupported(QSize(4096, 2160), Rational(60), false, QStringLiteral("J2K IMF RGB 12 bit")));
        QVERIFY(!media::imfFormatSupported(QSize(1920, 1080), Rational(30000, 1001), true, QStringLiteral("J2K IMF YCrCb")));
        QVERIFY(!media::imfFormatSupported(QSize(1280, 720), Rational(24), false, QStringLiteral("J2K IMF YCrCb")));
        // MCA labels (bmx's labels file): 5.1 in the mix's order, the language tag with its region.
        media::ImfSoundOptions five;
        five.format = QStringLiteral("5.1");
        five.language = QStringLiteral("de");
        five.region = QStringLiteral("AT");
        QCOMPARE(media::imfMcaLabels(five), QStringLiteral("0\nchL, chan=0\nchC, chan=1\nchR, chan=2\nchLs, chan=3\nchRs, chan=4\n"
                                                           "chLFE, chan=5\nsg51\nggMPg, lang=de-AT\n"));
        QCOMPARE(media::imfSoundChannels(QStringLiteral("7.1")), 8);
        QVERIFY(media::imfRegions(QStringLiteral("fr")).contains(QStringLiteral("CA")));
        QCOMPARE(media::imfLanguageTag(QStringLiteral("en"), QString()), QStringLiteral("en"));
    }

    void supplementalAndAudioMapping() {
        QTemporaryDir dir;
        const QString originalRoot = dir.filePath(QStringLiteral("Source_IMF"));
        const QString cpl = makePackage(originalRoot, QUuid::createUuid());
#ifndef Q_OS_WIN
        // The stand-in raw2bmx keeps each labels file it is given.
        const QString tool = dir.filePath(QStringLiteral("raw2bmx"));
        {
            QFile f(tool);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\n"
                    "here=\"$(dirname \"$0\")\"\n"
                    "out=''; wav=''\n"
                    "while [ $# -gt 0 ]; do case \"$1\" in -o) out=\"$2\"; shift;; --wave) wav=\"$2\"; shift;;"
                    " --track-mca-labels) cat \"$3\" >> \"$here/labels.txt\"; echo '--' >> \"$here/labels.txt\"; shift; shift;; esac; shift; done\n"
                    "cp \"$wav\" \"$out\"\n");
            f.close();
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        }
        qputenv("LUMENIS_RAW2BMX", tool.toUtf8());
#endif
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
        commands.action(QStringLiteral("tools.imf_window"))->trigger();
        ImfWindow* imf = window.imfWindow();
        QVERIFY(imf);
        QVERIFY(imf->addPackage(originalRoot));

        // The Original's Promo in a bin, and a version whose frames 6-11 are another clip's.
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.importFiles(binId, {cpl});
        QCOMPARE(ids.size(), 3);
        const QString other = dir.filePath(QStringLiteral("other.mxf"));
        {
            media::WriterSettings w;
            w.format = QStringLiteral("mxf");
            w.videoCodec = QStringLiteral("dnxhd");
            w.videoProfile = QStringLiteral("dnxhr_lb");
            w.pixelFormat = QStringLiteral("yuv422p");
            w.width = 256;
            w.height = 144;
            w.frameRate = Rational(24);
            media::MediaWriter writer;
            QVERIFY(writer.open(other, w));
            QImage red(256, 144, QImage::Format_RGB32);
            red.fill(Qt::red);
            for (int f = 0; f < 8; ++f) writer.writeVideo(red);
            writer.finish();
        }
        const QStringList otherIds = window.importFiles(binId, {other});
        QCOMPARE(otherIds.size(), 1);
        const model::BinItem* otherClip = bin->bin().item(otherIds.first());
        QVERIFY(otherClip);
        const model::BinItem* promo = bin->bin().item(ids.last());
        QVERIFY(promo);
        model::Sequence seq = model::Sequence::fromItem(*promo);
        model::Segment replaced;
        replaced.binId = binId;
        replaced.clipId = otherClip->id;
        replaced.sourceMob = otherClip->mobId;
        replaced.name = otherClip->name;
        replaced.sourceTrack = QStringLiteral("V1");
        replaced.length = 6;
        seq.overwrite(QStringLiteral("V1"), 6, {replaced});
        model::BinItem version = model::newSequence(bin->bin(), Rational(24), QDateTime::currentDateTimeUtc(), QStringLiteral("01:00:00:00"));
        version.name = QStringLiteral("Promo v2");
        seq.storeIn(version);
        bin->addItem(version);
        // Its picture: frames 0-5 still the Original's video track file from its start, 6-11 to write.
        const QList<MainWindow::ImfPictureRun> runs = window.imfPictureRuns(seq, QFileInfo(originalRoot).absoluteFilePath());
        QCOMPARE(runs.size(), 2);
        QCOMPARE(runs.at(0).length, std::int64_t{6});
        QVERIFY(!runs.at(0).trackFile.isNull());
        QCOMPARE(runs.at(0).entry, std::int64_t{0});
        QCOMPARE(runs.at(0).intrinsic, std::int64_t{12});
        QVERIFY(runs.at(1).trackFile.isNull());
        QCOMPARE(runs.at(1).start, std::int64_t{6});

        // Dropped on the package's root: a Supplemental of it, beside it, with its Issuer and Creator (p.336).
        QTreeWidget* tree = imf->sidebar();
        QTreeWidgetItem* rootItem = tree->topLevelItem(0);
        QVERIFY(rootItem);
        {
            QMimeData mime;
            mime.setData(binItemsMime(), QStringLiteral("%1\n%2").arg(binId, version.id).toUtf8());
            const QPoint at = tree->visualItemRect(rootItem).center();
            QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &enter);
            QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &drop);
        }
        QCOMPARE(imf->drafts().size(), 1);
        const ImfDraft& draft = imf->drafts().at(0);
        QVERIFY(draft.isSupplemental());
        QCOMPARE(draft.original, QFileInfo(originalRoot).absoluteFilePath());
        QCOMPARE(draft.name, QStringLiteral("Source_IMF_SUPP"));
        QCOMPARE(draft.issuer, QStringLiteral("Studio"));
        QCOMPARE(imf->supplementOf(originalRoot), 0);
        QCOMPARE(draft.imps.at(0).cpls.size(), 1);
        // New IMF > IMF Supplemental with the Original selected makes a second one.
        QVERIFY(imf->select(originalRoot));
        QCOMPARE(imf->newSupplemental(), 1);
        imf->select(originalRoot);
        QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QVERIFY(imf->selectDraft(1));
        QApplication::sendEvent(imf, &del);
        QCOMPARE(imf->drafts().size(), 1);

        // The audio mapping (p.338-340): one row of All Active Tracks as stereo, then A1 as mono with MCA metadata
        // from its edit button, and a second row of a group of A1 and A2 without the Master Fader.
        QCOMPARE(imf->drafts().at(0).imps.at(0).cpls.at(0).audio.size(), 1);
        QVERIFY(imf->selectDraft(0, 0, 0));
        auto* source = imf->findChild<QComboBox*>(QStringLiteral("imfAudioSource"));
        QVERIFY(source);
        QCOMPARE(source->count(), 3); // All Active Tracks, A1 (Mono), A2 (Mono)
        QCOMPARE(source->itemText(1), QStringLiteral("A1 (Mono)"));
        ImfAudioTrack mono;
        mono.source = QStringLiteral("track:A1");
        mono.format = QStringLiteral("mono");
        imf->setAudioTrack(0, 0, 0, 0, mono);
        imf->setMcaEditor([](const ImfAudioTrack& t) {
            ImfAudioTrack out = t;
            out.contentKind = QStringLiteral("dialog");
            out.language = QStringLiteral("fr");
            out.region = QStringLiteral("CA");
            return std::optional<ImfAudioTrack>(out);
        });
        auto* mca = imf->findChild<QPushButton*>(QStringLiteral("imfAudioMca"));
        QVERIFY(mca);
        mca->click();
        QCOMPARE(imf->drafts().at(0).imps.at(0).cpls.at(0).audio.at(0).language, QStringLiteral("fr"));
        QVERIFY(mca->toolTip().contains(QStringLiteral("fr-CA")));
        auto* plus = imf->findChild<QPushButton*>(QStringLiteral("imfAudioAdd"));
        QVERIFY(plus);
        plus->click();
        QCoreApplication::processEvents(); // the pane is rebuilt after the click
        QCOMPARE(imf->drafts().at(0).imps.at(0).cpls.at(0).audio.size(), 2);
        QCOMPARE(imf->findChildren<QPushButton*>(QStringLiteral("imfAudioRemove")).size(), 2);
        imf->setGroupEditor([](const QList<std::pair<QString, QString>>& tracks) {
            ImfTrackGroup g;
            g.name = QStringLiteral("Both");
            for (const auto& t : tracks) g.tracks << t.first;
            g.masterFader = false;
            return std::optional<ImfTrackGroup>(g);
        });
        auto* grouping = imf->findChild<QPushButton*>(QStringLiteral("imfAudioGrouping"));
        QVERIFY(grouping && grouping->isEnabled());
        grouping->click();
        QCoreApplication::processEvents();
        QCOMPARE(imf->drafts().at(0).imps.at(0).cpls.at(0).groups.size(), 1);
        ImfAudioTrack both;
        both.source = QStringLiteral("group:Both");
        imf->setAudioTrack(0, 0, 0, 1, both);
        source = imf->findChildren<QComboBox*>(QStringLiteral("imfAudioSource")).value(1);
        QVERIFY(source);
        QCOMPARE(source->itemText(source->count() - 1), QStringLiteral("Both")); // "the group will appear in the tracks list at the bottom"
        for (const QString& field : {QStringLiteral("issuer"), QStringLiteral("creator")}) imf->setDraftField(0, 0, 0, field, QStringLiteral("Studio"));
        QVERIFY2(imf->draftReady(0), qPrintable(imf->missingFields(0).join(QLatin1Char(','))));
#ifdef Q_OS_WIN
        QSKIP("the stand-in raw2bmx is a POSIX shell script");
#else
        const QString written = window.exportImfDraft(0);
        QVERIFY2(!written.isEmpty(), qPrintable(ConsoleLog::instance().lines().join(QLatin1Char('\n')).right(600)));
        QCOMPARE(written, dir.filePath(QStringLiteral("Source_IMF_SUPP")));
        QString error;
        const auto supp = interop::imf::readPackage(written, &error);
        QVERIFY2(supp, qPrintable(error));
        QVERIFY(supp->isSupplemental());
        QCOMPARE(supp->compositions.size(), 1);
        const interop::imf::Composition& c = supp->compositions.at(0);
        QCOMPARE(c.segments.size(), 1);
        QCOMPARE(c.segments.at(0).sequences.size(), 3); // the picture and two IMF audio tracks
        const interop::imf::Sequence& image = c.segments.at(0).sequences.at(0);
        QVERIFY(image.isImage());
        QCOMPARE(image.resources.size(), 2);
        QCOMPARE(image.resources.at(0).trackFile, runs.at(0).trackFile); // the Original's
        QCOMPARE(image.resources.at(0).sourceDuration, std::int64_t{6});
        QCOMPARE(image.resources.at(1).sourceDuration, std::int64_t{6});
        QVERIFY(supp->missingTrackFiles().contains(runs.at(0).trackFile));
        QVERIFY(!supp->assets.contains(runs.at(0).trackFile)); // not in its own asset map
        // The labels raw2bmx was given: the mono track's, then the group's stereo.
        QFile labels(dir.filePath(QStringLiteral("labels.txt")));
        QVERIFY(labels.open(QIODevice::ReadOnly));
        const QString given = QString::fromUtf8(labels.readAll());
        QVERIFY2(given.startsWith(QStringLiteral("0\nchM1, chan=0\nsgM\nggDcm, lang=fr-CA\n--\n0\nchL, chan=0\nchR, chan=1\nsgST\nggMPg\n")),
                 qPrintable(given));
        // Listed green; its CPL into a bin plays the Original's frames and its own (the Original's map is added).
        QCOMPARE(imf->drafts().size(), 0);
        QCOMPARE(imf->packages().size(), 2);
        const QStringList back = window.importFiles(binId, {c.file});
        QVERIFY(back.size() >= 3);
        const model::Sequence imported = model::Sequence::fromItem(*bin->bin().item(back.last()));
        QCOMPARE(imported.track(QStringLiteral("V1"))->length(), 12);
        QCOMPARE(imported.track(QStringLiteral("V1"))->segments.size(), 2);
        QVERIFY(imported.track(QStringLiteral("A1")));
        qunsetenv("LUMENIS_RAW2BMX");
#endif
    }

    void newOriginalAndExport() {
        QTemporaryDir dir;
        const QString cpl = makePackage(dir.filePath(QStringLiteral("Source_IMF")), QUuid::createUuid());
        // On POSIX a stand-in for raw2bmx: it keeps its arguments and copies the WAVE input to the output.
#ifndef Q_OS_WIN
        const QString tool = dir.filePath(QStringLiteral("raw2bmx"));
        {
            QFile f(tool);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\n"
                    "echo \"$@\" > \"$(dirname \"$0\")/raw2bmx.args\"\n"
                    "out=''; wav=''\n"
                    "while [ $# -gt 0 ]; do case \"$1\" in -o) out=\"$2\"; shift;; --wave) wav=\"$2\"; shift;; esac; shift; done\n"
                    "cp \"$wav\" \"$out\"\n");
            f.close();
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        }
        qputenv("LUMENIS_RAW2BMX", tool.toUtf8());
#endif
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
        project.settings.format.editRate = Rational(24); // HD 1080p/24: in the IMF Supported Formats
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        commands.action(QStringLiteral("tools.imf_window"))->trigger();
        ImfWindow* imf = window.imfWindow();
        QVERIFY(imf);

        // A bin with the imported Promo (picture and sound) and a picture-only sequence of its video clip.
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.importFiles(binId, {cpl});
        QCOMPARE(ids.size(), 3);
        const model::BinItem* videoClip = bin->bin().item(ids.first());
        QVERIFY(videoClip);
        const model::AutoSequenceResult made = model::autoSequence({*videoClip}, binId, false);
        QVERIFY(made.ok());
        model::BinItem pictureOnly = model::newSequence(bin->bin(), Rational(24), QDateTime::currentDateTimeUtc(), QStringLiteral("01:00:00:00"));
        pictureOnly.name = QStringLiteral("Picture Only");
        made.sequence.storeIn(pictureOnly);
        bin->addItem(pictureOnly);

        // New IMF > IMF Original: an untitled root and IMP, red (p.337).
        QVERIFY(imf->newImfButton()->menu());
        const QList<QAction*> choices = imf->newImfButton()->menu()->actions();
        QCOMPARE(choices.size(), 2);
        // IMF Supplemental needs an Original on disk selected: none here, so no draft.
        choices.at(1)->trigger();
        QCOMPARE(imf->drafts().size(), 0);
        choices.at(0)->trigger();
        QCOMPARE(imf->drafts().size(), 1);
        QTreeWidget* tree = imf->sidebar();
        QTreeWidgetItem* draftRoot = tree->topLevelItem(0);
        QVERIFY(draftRoot);
        QCOMPARE(draftRoot->text(0), QStringLiteral("untitled"));
        QCOMPARE(ImfWindow::statusColor(draftRoot), ImfWindow::red());
        QVERIFY(!imf->exportButton()->isEnabled());

        // The picture-only sequence dropped on the draft's root: a CPL "CPL_Picture Only" in its IMP (p.336-337).
        {
            QMimeData mime;
            mime.setData(binItemsMime(), QStringLiteral("%1\n%2").arg(binId, pictureOnly.id).toUtf8());
            const QPoint at = tree->visualItemRect(draftRoot).center();
            QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &enter);
            QVERIFY(enter.isAccepted());
            QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &drop);
        }
        QCOMPARE(imf->drafts().at(0).imps.size(), 1);
        QCOMPARE(imf->drafts().size(), 1);
        QCOMPARE(imf->drafts().at(0).imps.at(0).cpls.size(), 1);
        draftRoot = tree->topLevelItem(0);
        QVERIFY(draftRoot);
        QTreeWidgetItem* impItem = draftRoot->child(0);
        QVERIFY(impItem);
        QTreeWidgetItem* cplItem = impItem->child(0);
        QVERIFY(cplItem);
        QCOMPARE(cplItem->text(0), QStringLiteral("CPL_Picture Only"));
        QCOMPARE(ImfWindow::statusColor(cplItem), ImfWindow::red());
        // Something other than a sequence is refused.
        QCOMPARE(window.addImfSequences(binId, {ids.first()}, 0), 0);

        // The CPL's fields typed in the right pane: Issuer and Creator fill the IMP's and the root's (p.336).
        QVERIFY(imf->selectDraft(0, 0, 0));
        auto* issuer = imf->findChild<QLineEdit*>(QStringLiteral("imfField_issuer"));
        QVERIFY(issuer);
        QTest::keyClicks(issuer, QStringLiteral("Studio"));
        auto* creator = imf->findChild<QLineEdit*>(QStringLiteral("imfField_creator"));
        QVERIFY(creator);
        QTest::keyClicks(creator, QStringLiteral("Editor"));
        auto* title = imf->findChild<QLineEdit*>(QStringLiteral("imfField_contentTitle"));
        QVERIFY(title);
        title->selectAll();
        QTest::keyClicks(title, QStringLiteral("Feature"));
        QCOMPARE(cplItem->text(0), QStringLiteral("CPL_Feature"));
        QCOMPARE(ImfWindow::statusColor(cplItem), ImfWindow::yellow());
        QCOMPARE(imf->drafts().at(0).imps.at(0).issuer, QStringLiteral("Studio"));
        QCOMPARE(imf->drafts().at(0).creator, QStringLiteral("Editor"));
        // The root still needs its directory: red, Export dimmed.
        QCOMPARE(ImfWindow::statusColor(tree->topLevelItem(0)), ImfWindow::red());
        QVERIFY(imf->missingFields(0).contains(QStringLiteral("Directory")));
        QVERIFY(!imf->exportButton()->isEnabled());
        QVERIFY(imf->selectDraft(0));
        imf->setDirectoryChooser([&] { return dir.filePath(QStringLiteral("Out")); });
        QDir().mkpath(dir.filePath(QStringLiteral("Out")));
        auto* browse = imf->findChild<QPushButton*>(QStringLiteral("imfBrowse"));
        QVERIFY(browse);
        browse->click();
        imf->setDraftField(0, -1, -1, QStringLiteral("name"), QStringLiteral("Feature_IMF"));
        imf->setDraftField(0, -1, -1, QStringLiteral("annotation"), QStringLiteral("Feature package"));
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Feature_IMF"));
        QCOMPARE(ImfWindow::statusColor(tree->topLevelItem(0)), ImfWindow::yellow());
        QVERIFY(imf->draftReady(0));
        QVERIFY(imf->exportButton()->isEnabled());

        // Export: the package on disk, listed green in the draft's place (p.334, p.337).
        imf->exportButton()->click();
        QCOMPARE(imf->drafts().size(), 0);
        const QString root = dir.filePath(QStringLiteral("Out/Feature_IMF"));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/ASSETMAP.xml")));
        QCOMPARE(imf->packages().size(), 1);
        QCOMPARE(ImfWindow::statusColor(tree->topLevelItem(0)), ImfWindow::green());
        QString error;
        const auto package = interop::imf::readPackage(root, &error);
        QVERIFY2(package, qPrintable(error));
        QCOMPARE(package->annotation, QStringLiteral("Feature package"));
        QCOMPARE(package->imps.size(), 1);
        QCOMPARE(package->imps.at(0).issuer, QStringLiteral("Studio"));
        QCOMPARE(package->compositions.size(), 1);
        const interop::imf::Composition& c = package->compositions.at(0);
        QCOMPARE(c.contentTitle, QStringLiteral("Feature"));
        QCOMPARE(c.creator, QStringLiteral("Editor"));
        QCOMPARE(c.trackFiles().size(), 1);
        // Each asset's hash in the packing list is its file's.
        for (const interop::imf::PackingList::Entry& e : package->imps.at(0).assets) {
            QCOMPARE(e.hash, interop::imf::fileHash(package->assets.value(e.id)));
        }
        // The picture track file: JPEG 2000 at the project's raster, 12 frames.
        const QString picture = package->assets.value(c.trackFiles().at(0));
        media::VideoReader reader;
        QVERIFY(reader.open(picture));
        QCOMPARE(reader.info().width, 1920);
        QCOMPARE(reader.frameCount(), std::int64_t{12});
        // FFmpeg's own IMF reader takes the composition, when ffprobe is installed (an outside check, ours).
        if (const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe")); !ffprobe.isEmpty()) {
            QProcess probe;
            probe.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("imf"),
                                  QStringLiteral("-show_entries"), QStringLiteral("format=duration"), QStringLiteral("-of"),
                                  QStringLiteral("csv=p=0"), c.file});
            QVERIFY(probe.waitForFinished(60000));
            const QString out = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
            const QString err = QString::fromUtf8(probe.readAllStandardError());
            // Not every FFmpeg has the IMF demuxer (Homebrew's on the Mac lacks it): then there is no outside check.
            if (err.contains(QLatin1String("Unknown input format"))) {
                qInfo("ffprobe has no IMF demuxer; the outside check is skipped");
            } else {
                QVERIFY2(probe.exitCode() == 0, qPrintable(err));
                QVERIFY2(std::abs(out.toDouble() - 0.5) < 0.01, qPrintable(out));
            }
        }
        // The CPL into a bin again: a 12-frame sequence.
        const QStringList again = window.importFiles(binId, {c.file});
        QCOMPARE(again.size(), 2);
        QCOMPARE(model::Sequence::fromItem(*bin->bin().item(again.last())).track(QStringLiteral("V1"))->length(), 12);
        // A second export to the same folder is refused: it exists.
        imf->addSequences(-1, {draftCpl(binId, pictureOnly.id, QStringLiteral("Picture Only"))});
        QCOMPARE(imf->drafts().size(), 1);
        for (const QString& field : {QStringLiteral("issuer"), QStringLiteral("creator")}) imf->setDraftField(0, 0, 0, field, QStringLiteral("x"));
        imf->setDraftField(0, -1, -1, QStringLiteral("directory"), dir.filePath(QStringLiteral("Out")));
        imf->setDraftField(0, -1, -1, QStringLiteral("name"), QStringLiteral("Feature_IMF"));
        QVERIFY(imf->draftReady(0));
        QVERIFY(window.exportImfDraft(0).isEmpty());
        QCOMPARE(imf->drafts().size(), 1);

#ifndef Q_OS_WIN
        // With sound (the imported Promo): the mix as one track file through raw2bmx, a MainAudioSequence in samples.
        imf->setDraftField(0, -1, -1, QStringLiteral("name"), QStringLiteral("Promo_Out"));
        imf->addSequences(0, {draftCpl(binId, ids.last(), QStringLiteral("Promo"))});
        QCOMPARE(imf->drafts().at(0).imps.size(), 2); // a later drag makes a new IMP (p.337-338)
        for (const QString& field : {QStringLiteral("issuer"), QStringLiteral("creator")}) {
            imf->setDraftField(0, 1, -1, field, QStringLiteral("y"));
            imf->setDraftField(0, 1, 0, field, QStringLiteral("y"));
        }
        const QString written = window.exportImfDraft(0);
        QVERIFY2(!written.isEmpty(), qPrintable(ConsoleLog::instance().lines().join(QLatin1Char('\n')).right(800)));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("raw2bmx.args"))));
        QFile args(dir.filePath(QStringLiteral("raw2bmx.args")));
        QVERIFY(args.open(QIODevice::ReadOnly));
        const QString used = QString::fromUtf8(args.readAll());
        QVERIFY2(used.contains(QStringLiteral("-t op1a --audio-layout imf")) && used.contains(QStringLiteral("-f 24/1")), qPrintable(used));
        const auto two = interop::imf::readPackage(written, &error);
        QVERIFY2(two, qPrintable(error));
        QCOMPARE(two->imps.size(), 2);
        QCOMPARE(two->compositions.size(), 2);
        const interop::imf::Composition* promo = nullptr;
        for (const interop::imf::Composition& p : two->compositions) {
            if (p.contentTitle == QLatin1String("Promo")) promo = &p;
        }
        QVERIFY(promo);
        QCOMPARE(promo->segments.size(), 1);
        QCOMPARE(promo->segments.at(0).sequences.size(), 2);
        const interop::imf::Sequence& sound = promo->segments.at(0).sequences.at(1);
        QVERIFY(sound.isAudio());
        QCOMPARE(sound.resources.at(0).editRate, Rational(48000));
        QCOMPARE(sound.resources.at(0).sourceDuration, std::int64_t{24000});
        qunsetenv("LUMENIS_RAW2BMX");
#endif
    }
};

QTEST_MAIN(TestImfUi)
#include "tst_imf_ui.moc"
