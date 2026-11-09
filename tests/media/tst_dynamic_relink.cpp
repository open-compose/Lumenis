// Tests MultiRez media and Dynamic Relink's quality matching (Editing Guide ch.31, p.1099-1121, p.1169-1171; 8.6a,
// lumenis/media/dynamic_relink.h): a linked clip transcoded twice without Create new clips has three media, and the
// setting's rules choose among them; Offline and back; the setting read from its values; the frame rate pool.
#include "lumenis/media/consolidate.h"
#include "lumenis/media/dynamic_relink.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"

#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace lumenis;
using namespace lumenis::media;

namespace {

bool writeVideo(const QString& path, int frames) {
    WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    MediaWriter w;
    if (!w.open(path, s)) return false;
    for (int i = 0; i < frames; ++i) {
        QImage frame(s.width, s.height, QImage::Format_RGB32);
        frame.fill(QColor(20 * i % 255, 80, 160));
        if (!w.writeVideo(frame)) return false;
    }
    return w.finish();
}

MediaCandidate candidate(const QString& id, const Rational& rate, int seconds) {
    MediaCandidate c;
    c.mediaId = id;
    c.video = true;
    c.rate = rate;
    c.raster = QSize(1920, 1080);
    c.resolution = QStringLiteral("Avid DNx HQ");
    c.codecFamily = QStringLiteral("Avid DNx");
    c.container = QStringLiteral("MXF");
    c.created = QDateTime::fromSecsSinceEpoch(1767225600 + seconds); // 2026-01-01 plus seconds
    return c;
}

} // namespace

class TestDynamicRelink : public QObject {
    Q_OBJECT

private slots:
    // The setting's values (settings.json keys) into DynamicRelinkSettings; "any" is no filter.
    void settingsFromValues() {
        const QJsonObject values{{QStringLiteral("enabled"), true},
                                 {QStringLiteral("overrideWorking"), true},
                                 {QStringLiteral("working.videoMethod"), QStringLiteral("minimalBandwidth")},
                                 {QStringLiteral("target.videoMethod"), QStringLiteral("specificResolution")},
                                 {QStringLiteral("target.compression"), QStringLiteral("Avid DNx HQ")},
                                 {QStringLiteral("target.raster"), QStringLiteral("any")},
                                 {QStringLiteral("target.noMatch"), QStringLiteral("offline")},
                                 {QStringLiteral("target.videoMultiple"), QStringLiteral("lowestBandwidth")}};
        const DynamicRelinkSettings s = DynamicRelinkSettings::fromValues(values);
        QVERIFY(s.enabled);
        QVERIFY(!s.mixedRates);
        QCOMPARE(s.working.video, RelinkRules::Method::MinimalBandwidth);
        // Override: only the Target tab is used (p.1169).
        QCOMPARE(s.active().video, RelinkRules::Method::SpecificResolution);
        QCOMPARE(s.active().compression, QStringLiteral("Avid DNx HQ"));
        QVERIFY(s.active().raster.isEmpty());
        QCOMPARE(s.active().noMatch, RelinkRules::NoMatch::Offline);
        QVERIFY(!s.active().preferTargetRaster);
        QVERIFY(!(s == DynamicRelinkSettings{}));
        QVERIFY(DynamicRelinkSettings::fromValues({}) == DynamicRelinkSettings{});
        QCOMPARE(codecFamilyOf(QStringLiteral("dnxhd")), QStringLiteral("Avid DNx"));
        QCOMPARE(codecFamilyOf(QStringLiteral("pcm_s24le")), QStringLiteral("PCM"));
        QCOMPARE(containerOf(QStringLiteral("mov,mp4,m4a,3gp,3g2,mj2"), QStringLiteral("a.mp4")), QStringLiteral("MP4"));
        QCOMPARE(containerOf(QStringLiteral("mov,mp4,m4a,3gp,3g2,mj2"), QStringLiteral("a.mov")), QStringLiteral("QuickTime"));
    }

    // Media at the project's rate first; with none, the most recent of all (p.1169); Allow Mixed Frame Rate Media
    // makes the Frame Rate a preference (p.1110).
    void frameRates() {
        const QList<MediaCandidate> both{candidate(QStringLiteral("p50"), Rational(50), 9),
                                         candidate(QStringLiteral("p25"), Rational(25), 1)};
        RelinkRules r;
        RelinkChoice c = chooseMedia(both, std::nullopt, r, Rational(25), QSize(1920, 1080), false);
        QVERIFY(c.media && c.matched);
        QCOMPARE(c.media->mediaId, QStringLiteral("p25"));
        // Mixed rates allowed: the most recent of all.
        c = chooseMedia(both, std::nullopt, r, Rational(25), QSize(1920, 1080), true);
        QCOMPARE(c.media->mediaId, QStringLiteral("p50"));
        // None at the project's rate: the most recent online, whatever the rules.
        r.video = RelinkRules::Method::SpecificResolution;
        r.compression = QStringLiteral("Avid DNx LB");
        r.noMatch = RelinkRules::NoMatch::Offline;
        c = chooseMedia({candidate(QStringLiteral("p50"), Rational(50), 9)}, std::nullopt, r, Rational(25),
                        QSize(1920, 1080), false);
        QVERIFY(c.media);
        QCOMPARE(c.media->mediaId, QStringLiteral("p50"));
        // A preferred frame rate.
        r.compression.clear();
        r.frameRate = QStringLiteral("25");
        c = chooseMedia(both, std::nullopt, r, Rational(25), QSize(1920, 1080), true);
        QCOMPARE(c.media->mediaId, QStringLiteral("p25"));
        // Nothing to choose from: offline.
        QVERIFY(!chooseMedia({}, std::nullopt, r, Rational(25), QSize(1920, 1080), false).media);
    }

    // A linked clip transcoded twice without Create new clips (p.354-355): three media, chosen by the rules.
    void multiRezClip() {
        if (!patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("Shot.mov"));
        QVERIFY(writeVideo(source, 10));
        LinkOptions lo;
        lo.editRate = Rational(25);
        auto clip = linkClip(*probe(source), lo);
        QVERIFY(clip);
        const QJsonObject original = clip->link;
        const ClipLookup lookup = [&](const QString&, const QString&) -> std::optional<model::BinItem> { return *clip; };
        ConsolidateOptions options;
        options.mode = ConsolidateOptions::Mode::Transcode;
        options.newClips = false;
        options.creation.videoDrive = dir.path();
        options.creation.raster = QSize(256, 144);
        options.creation.resolution = *findResolution(QStringLiteral("dnx_lb"));
        ConsolidateResult r = consolidate({*clip}, QStringLiteral("bin1"), lookup, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.added.isEmpty());
        QCOMPARE(r.associated.size(), 1);
        options.creation.raster = QSize(512, 288);
        options.creation.resolution = *findResolution(QStringLiteral("dnx_hq"));
        r = consolidate({*clip}, QStringLiteral("bin1"), lookup, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));

        MediaDatabase db;
        db.scan({dir.path()});
        QCOMPARE(db.setsOf(model::mobIdOf(*clip)).size(), 2);
        const QList<MediaCandidate> all = mediaCandidates(*clip, db);
        QCOMPARE(all.size(), 3);
        QCOMPARE(all.constFirst().resolution, QStringLiteral("Avid DNx HQ")); // newest first
        QCOMPARE(all.constFirst().raster, QSize(512, 288));
        QVERIFY(currentMedia(*clip, all)->affinity);

        DynamicRelinkSettings s;
        s.enabled = true;
        const QSize project(512, 288);
        // Most Recent: the HQ media.
        model::BinItem item = *clip;
        RelinkChoice choice;
        QVERIFY(dynamicRelink(item, db, s, Rational(25), project, &choice));
        QVERIFY(choice.matched);
        auto link = Link::fromJson(item.link);
        QVERIFY(link && link->isManaged());
        QCOMPARE(currentMedia(item, all)->resolution, QStringLiteral("Avid DNx HQ"));
        QCOMPARE(affinityLink(item), original);
        // Unchanged a second time.
        QVERIFY(!dynamicRelink(item, db, s, Rational(25), project));
        // Minimal bandwidth: the 64x36 ffv1 file or the LB media, whichever has fewer bytes a frame.
        s.working.video = RelinkRules::Method::MinimalBandwidth;
        QVERIFY(dynamicRelink(item, db, s, Rational(25), project, &choice));
        const auto smallest = std::min_element(all.begin(), all.end(), [](const MediaCandidate& a, const MediaCandidate& b) {
            return a.bandwidth < b.bandwidth;
        });
        QCOMPARE(choice.media->mediaId, smallest->mediaId);
        // Specific Resolution: the LB media.
        s.working.video = RelinkRules::Method::SpecificResolution;
        s.working.compression = QStringLiteral("Avid DNx LB");
        dynamicRelink(item, db, s, Rational(25), project, &choice);
        QVERIFY(choice.matched);
        QCOMPARE(currentMedia(item, all)->resolution, QStringLiteral("Avid DNx LB"));
        QCOMPARE(currentMedia(item, all)->raster, QSize(256, 144));
        // No match: Keep Existing keeps the LB media, unmatched.
        s.working.compression = QStringLiteral("Avid DNx 444");
        QVERIFY(!dynamicRelink(item, db, s, Rational(25), project, &choice));
        QVERIFY(!choice.matched);
        QCOMPARE(currentMedia(item, all)->resolution, QStringLiteral("Avid DNx LB"));
        // Use highest quality: the project's raster first.
        s.working.noMatch = RelinkRules::NoMatch::HighestQuality;
        QVERIFY(dynamicRelink(item, db, s, Rational(25), project, &choice));
        QCOMPARE(currentMedia(item, all)->resolution, QStringLiteral("Avid DNx HQ"));
        // Offline: Media Offline, the media kept (p.1170).
        s.working.noMatch = RelinkRules::NoMatch::Offline;
        QVERIFY(dynamicRelink(item, db, s, Rational(25), project, &choice));
        QVERIFY(!choice.media);
        QVERIFY(Link::fromJson(item.link)->unlinked);
        QVERIFY(!currentMedia(item, all));
        QCOMPARE(mediaCandidates(item, db).size(), 3);
        // A codec family filter that the linked file matches: back on the affinity media.
        s.working.compression.clear();
        s.working.codecFamily = QStringLiteral("Other");
        QVERIFY(dynamicRelink(item, db, s, Rational(25), project, &choice));
        QVERIFY(choice.media && choice.media->affinity);
        QCOMPARE(item.link, original);
        // Off: nothing; restoring puts the affinity link back.
        s.working.codecFamily = QStringLiteral("Avid DNx");
        QVERIFY(dynamicRelink(item, db, s, Rational(25), project));
        s.enabled = false;
        QVERIFY(!dynamicRelink(item, db, s, Rational(25), project));
        QVERIFY(restoreAffinityMedia(item));
        QCOMPARE(item.link, original);
        QVERIFY(!restoreAffinityMedia(item));
        // The media database finding the files again keeps the affinity link.
        s.enabled = true;
        QVERIFY(dynamicRelink(item, db, s, Rational(25), project));
        QVERIFY(db.resolve(item));
        QCOMPARE(affinityLink(item), original);
    }
};

QTEST_MAIN(TestDynamicRelink)
#include "tst_dynamic_relink.moc"
