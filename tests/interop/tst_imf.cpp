// IMF packages (11.5c; docs/spec/imf.md; Editing Guide p.333-340): an Original's asset map, packing list and composition
// playlist read from its root folder, a Supplemental that uses the Original's track files, and a composition made into
// a native sequence.
#include "lumenis/interop/imf.h"
#include "lumenis/model/multichannel.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::interop;

namespace {

const QUuid kVideo = QUuid::fromString(QStringLiteral("11111111-1111-1111-1111-111111111111"));
const QUuid kAudio = QUuid::fromString(QStringLiteral("22222222-2222-2222-2222-222222222222"));
const QUuid kInsert = QUuid::fromString(QStringLiteral("33333333-3333-3333-3333-333333333333"));

void write(const QString& path, const QByteArray& text) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) qFatal("cannot write %s", qPrintable(path));
    f.write(text);
}

// A CPL in the 2016 namespaces: one segment with a picture virtual track of `image` resources and a sound one.
QByteArray cpl(const QUuid& id, const QString& title, const QString& image, const QString& audio) {
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<CompositionPlaylist xmlns=\"http://www.smpte-ra.org/schemas/2067-3/2016\" xmlns:cc=\"http://www.smpte-ra.org/schemas/2067-2/2016\">\n"
        "  <Id>%1</Id>\n"
        "  <Annotation>Reel one</Annotation>\n"
        "  <IssueDate>2026-11-15T12:00:00Z</IssueDate>\n"
        "  <Issuer>Studio</Issuer>\n"
        "  <Creator>Cutthulhu</Creator>\n"
        "  <ContentTitle>%2</ContentTitle>\n"
        "  <ContentKind>feature</ContentKind>\n"
        "  <EditRate>24 1</EditRate>\n"
        "  <EssenceDescriptorList><EssenceDescriptor><Id>urn:uuid:44444444-4444-4444-4444-444444444444</Id></EssenceDescriptor></EssenceDescriptorList>\n"
        "  <SegmentList>\n"
        "    <Segment>\n"
        "      <Id>urn:uuid:55555555-5555-5555-5555-555555555555</Id>\n"
        "      <SequenceList>\n"
        "        <cc:MainImageSequence>\n"
        "          <Id>urn:uuid:66666666-6666-6666-6666-666666666666</Id>\n"
        "          <TrackId>urn:uuid:77777777-7777-7777-7777-777777777777</TrackId>\n"
        "          <ResourceList>%3</ResourceList>\n"
        "        </cc:MainImageSequence>\n"
        "        <cc:MainAudioSequence>\n"
        "          <Id>urn:uuid:88888888-8888-8888-8888-888888888888</Id>\n"
        "          <TrackId>urn:uuid:99999999-9999-9999-9999-999999999999</TrackId>\n"
        "          <ResourceList>%4</ResourceList>\n"
        "        </cc:MainAudioSequence>\n"
        "      </SequenceList>\n"
        "    </Segment>\n"
        "  </SegmentList>\n"
        "</CompositionPlaylist>\n"
        "").arg(imf::urn(id), title, image, audio).toUtf8();
}

QString resource(const QUuid& file, const char* rate, int intrinsic, int entry, int duration, int repeat = 1) {
    return QStringLiteral("<Resource><Id>%1</Id><EditRate>%2</EditRate><IntrinsicDuration>%3</IntrinsicDuration>"
                          "<EntryPoint>%4</EntryPoint><SourceDuration>%5</SourceDuration><RepeatCount>%6</RepeatCount>"
                          "<TrackFileId>%7</TrackFileId></Resource>")
        .arg(imf::urn(QUuid::createUuid()), QLatin1String(rate)).arg(intrinsic).arg(entry).arg(duration).arg(repeat).arg(imf::urn(file));
}

QByteArray pkl(const QUuid& id, const QList<std::pair<QUuid, QString>>& assets) {
    QString list;
    for (const auto& [asset, type] : assets) {
        list += QStringLiteral("<Asset><Id>%1</Id><Hash>AAAA</Hash><Size>10</Size><Type>%2</Type></Asset>").arg(imf::urn(asset), type);
    }
    return QStringLiteral("<?xml version=\"1.0\"?><PackingList xmlns=\"http://www.smpte-ra.org/schemas/2067-2/2016/PKL\">\n"
        "<Id>%1</Id><Annotation>IMP</Annotation><IssueDate>2026-11-15T12:00:00Z</IssueDate><Issuer>Studio</Issuer><Creator>Cutthulhu</Creator>\n"
        "<AssetList>%2</AssetList></PackingList>").arg(imf::urn(id), list).toUtf8();
}

QByteArray assetMap(const QList<std::tuple<QUuid, QString, bool>>& assets) {
    QString list;
    for (const auto& [id, path, packing] : assets) {
        list += QStringLiteral("<Asset><Id>%1</Id>%2<ChunkList><Chunk><Path>%3</Path></Chunk></ChunkList></Asset>")
                    .arg(imf::urn(id), packing ? QStringLiteral("<PackingList>true</PackingList>") : QString(), path);
    }
    return QStringLiteral("<?xml version=\"1.0\"?><AssetMap xmlns=\"http://www.smpte-ra.org/schemas/429-9/2007/AM\">\n"
        "<Id>%1</Id><AnnotationText>Show</AnnotationText><Creator>Cutthulhu</Creator><VolumeCount>1</VolumeCount>\n"
        "<IssueDate>2026-11-15T12:00:00Z</IssueDate><Issuer>Studio</Issuer><AssetList>%2</AssetList></AssetMap>")
        .arg(imf::urn(QUuid::createUuid()), list).toUtf8();
}

model::BinItem clip(const QString& name, const QString& tracks) {
    model::BinItem c;
    c.id = model::newItemId();
    c.mobId = MobId::generate();
    c.kind = model::BinItemKind::MasterClip;
    c.name = name;
    c.editRate = Rational(24);
    c.durationFrames = 1000;
    c.fields.insert(QStringLiteral("Tracks"), tracks);
    return c;
}

} // namespace

class TestImf : public QObject {
    Q_OBJECT

private slots:
    void originalAndSupplemental() {
        QTemporaryDir dir;
        // The Original: a picture track file and a stereo sound track file, 100 frames.
        const QString original = dir.filePath(QStringLiteral("Show_Original"));
        QVERIFY(QDir().mkpath(original));
        const QUuid cplId = QUuid::createUuid();
        const QUuid pklId = QUuid::createUuid();
        write(original + QStringLiteral("/CPL_show.xml"),
              cpl(cplId, QStringLiteral("Show"), resource(kVideo, "24 1", 100, 0, 100), resource(kAudio, "48000 1", 200000, 0, 200000)));
        write(original + QStringLiteral("/PKL_show.xml"),
              pkl(pklId, {{cplId, QStringLiteral("text/xml")}, {kVideo, QStringLiteral("application/mxf")}, {kAudio, QStringLiteral("application/mxf")}}));
        write(original + QStringLiteral("/ASSETMAP.xml"),
              assetMap({{pklId, QStringLiteral("PKL_show.xml"), true}, {cplId, QStringLiteral("CPL_show.xml"), false},
                        {kVideo, QStringLiteral("video.mxf"), false}, {kAudio, QStringLiteral("audio.mxf"), false}}));
        QString error;
        QStringList warnings;
        const auto pack = imf::readPackage(original, &error, &warnings);
        QVERIFY2(pack, qPrintable(error));
        QVERIFY(warnings.isEmpty());
        QCOMPARE(pack->name, QStringLiteral("Show_Original"));
        QCOMPARE(pack->annotation, QStringLiteral("Show"));
        QCOMPARE(pack->issuer, QStringLiteral("Studio"));
        QCOMPARE(pack->imps.size(), 1);
        QCOMPARE(pack->imps.first().assets.size(), 3);
        QCOMPARE(pack->compositions.size(), 1);
        const imf::Composition& c = pack->compositions.first();
        QCOMPARE(c.id, cplId);
        QCOMPARE(c.contentTitle, QStringLiteral("Show"));
        QCOMPARE(c.editRate, Rational(24));
        QCOMPARE(c.segments.size(), 1);
        QCOMPARE(c.segments.first().sequences.size(), 2);
        QVERIFY(c.segments.first().sequences.first().isImage());
        QCOMPARE(c.segments.first().sequences.at(1).resources.first().editRate, Rational(48000));
        QCOMPARE(c.trackFiles(), (QList<QUuid>{kVideo, kAudio}));
        QVERIFY(!pack->isSupplemental());
        QCOMPARE(pack->assets.value(kVideo), QDir(original).absoluteFilePath(QStringLiteral("video.mxf")));
        QVERIFY(imf::isComposition(original + QStringLiteral("/CPL_show.xml")));
        QVERIFY(!imf::isComposition(original + QStringLiteral("/PKL_show.xml")));

        // A Supplemental: an insert of its own between two pieces of the Original's picture, repeated twice.
        const QString supplemental = dir.filePath(QStringLiteral("Show_Supplemental"));
        QVERIFY(QDir().mkpath(supplemental));
        const QUuid cpl2 = QUuid::createUuid();
        const QUuid pkl2 = QUuid::createUuid();
        write(supplemental + QStringLiteral("/CPL_edit.xml"),
              cpl(cpl2, QStringLiteral("Show edit"),
                  resource(kVideo, "24 1", 100, 0, 40) + resource(kInsert, "24 1", 10, 0, 10, 2) + resource(kVideo, "24 1", 100, 60, 40),
                  resource(kAudio, "48000 1", 200000, 0, 200000)));
        write(supplemental + QStringLiteral("/PKL_edit.xml"),
              pkl(pkl2, {{cpl2, QStringLiteral("text/xml")}, {kInsert, QStringLiteral("application/mxf")}}));
        write(supplemental + QStringLiteral("/ASSETMAP.xml"),
              assetMap({{pkl2, QStringLiteral("PKL_edit.xml"), true}, {cpl2, QStringLiteral("CPL_edit.xml"), false},
                        {kInsert, QStringLiteral("insert.mxf"), false}}));
        auto supp = imf::readPackage(supplemental, &error);
        QVERIFY(supp);
        QVERIFY(supp->isSupplemental());
        QCOMPARE(supp->missingTrackFiles(), (QList<QUuid>{kVideo, kAudio}));
        supp->addAssets(*pack);
        QVERIFY(!supp->isSupplemental());

        // Into a native sequence: V1 40 + 10 + 10 + 40 frames; the stereo sound file on A1 and A2.
        const model::BinItem video = clip(QStringLiteral("video"), QStringLiteral("V1"));
        const model::BinItem insert = clip(QStringLiteral("insert"), QStringLiteral("V1"));
        const model::BinItem audio = clip(QStringLiteral("audio"), QStringLiteral("A1-2"));
        QCOMPARE(model::audioChannelsOf(audio), 2);
        auto clipOf = [&](const QUuid& id) -> const model::BinItem* {
            if (id == kVideo) return &video;
            if (id == kInsert) return &insert;
            if (id == kAudio) return &audio;
            return nullptr;
        };
        QStringList seqWarnings;
        const model::BinItem item = imf::compositionSequence(supp->compositions.first(), clipOf, QDateTime::currentDateTimeUtc(), &seqWarnings);
        QVERIFY(seqWarnings.isEmpty());
        QCOMPARE(item.name, QStringLiteral("Show edit"));
        QCOMPARE(item.editRate, Rational(24));
        const model::Sequence seq = model::Sequence::fromItem(item);
        const model::Track* v1 = seq.track(QStringLiteral("V1"));
        QVERIFY(v1);
        QCOMPARE(v1->segments.size(), 4);
        QCOMPARE(v1->segments.at(0).name, QStringLiteral("video"));
        QCOMPARE(v1->segments.at(0).length, 40);
        QCOMPARE(v1->segments.at(1).name, QStringLiteral("insert"));
        QCOMPARE(v1->segments.at(2).name, QStringLiteral("insert"));
        QCOMPARE(v1->segments.at(3).sourceIn, 60);
        QCOMPARE(v1->length(), 100);
        // 200000 samples at 48 kHz are 100 frames at 24.
        QCOMPARE(seq.track(QStringLiteral("A1"))->length(), 100);
        QCOMPARE(seq.track(QStringLiteral("A2"))->segments.first().sourceTrack, QStringLiteral("A2"));
        // A track file that is not linked is filler, with a warning.
        const model::BinItem partial = imf::compositionSequence(supp->compositions.first(), [&](const QUuid& id) -> const model::BinItem* {
            return id == kInsert ? nullptr : clipOf(id);
        }, QDateTime::currentDateTimeUtc(), &seqWarnings);
        QCOMPARE(seqWarnings.size(), 1);
        QVERIFY(model::Sequence::fromItem(partial).track(QStringLiteral("V1"))->segments.at(1).isFiller());

        // No asset map: no package.
        QVERIFY(!imf::readPackage(dir.filePath(QStringLiteral("nothing")), &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestImf)
#include "tst_imf.moc"
