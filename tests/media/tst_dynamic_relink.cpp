// Tests MultiRez media and Dynamic Relink's quality matching (Editing Guide ch.31, p.1099-1121, p.1169-1171; 8.6a,
// lumenis/media/dynamic_relink.h): a linked clip transcoded twice without Create new clips has three media, and the
// setting's rules choose among them; Offline and back; the setting read from its values; the frame rate pool.
#include "lumenis/media/consolidate.h"
#include "lumenis/model/sequence.h"
#include "lumenis/media/dynamic_relink.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/core/mob_id.h"

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

    // Cross Rate Dynamic Relink (p.1109-1110): Specific Resolution's Frame Rate takes media at a multiple of the project's
    // rate, not in the Source monitor; with Allow Mixed Frame Rate Media the closest multiple when none is exact.
    void crossRate() {
        const QList<MediaCandidate> both{candidate(QStringLiteral("p25"), Rational(25), 9),
                                         candidate(QStringLiteral("p50"), Rational(50), 1)};
        RelinkRules r;
        r.video = RelinkRules::Method::SpecificResolution;
        r.frameRate = QStringLiteral("50");
        RelinkChoice c = chooseMedia(both, both.at(0), r, Rational(25), QSize(1920, 1080), false, true);
        QVERIFY(c.matched);
        QCOMPARE(c.media->mediaId, QStringLiteral("p50"));
        // The Source monitor: no cross-rate media, so no match and the existing media kept.
        c = chooseMedia(both, both.at(0), r, Rational(25), QSize(1920, 1080), false, false);
        QVERIFY(!c.matched);
        QCOMPARE(c.media->mediaId, QStringLiteral("p25"));
        // 23.976 is not a multiple of 25.
        const QList<MediaCandidate> film{candidate(QStringLiteral("p25"), Rational(25), 9),
                                         candidate(QStringLiteral("p23"), Rational(24000, 1001), 1)};
        r.frameRate = QStringLiteral("23.976");
        QVERIFY(!chooseMedia(film, film.at(0), r, Rational(25), QSize(1920, 1080), false, true).matched);
        // Preferred rate 100, none exact: the closest multiple, 50.
        r.frameRate = QStringLiteral("100");
        c = chooseMedia(both, both.at(0), r, Rational(25), QSize(1920, 1080), true, true);
        QVERIFY(c.matched);
        QCOMPARE(c.media->mediaId, QStringLiteral("p50"));
    }

    // Partial and shared media (p.1100-1102, p.1116-1118): media for part of the clip are half online and not played; a
    // clip sharing the source and timecode is associated; Delete offers only the clip's own (affinity) media; the Video
    // column follows the media linked to.
    void partialAndShared() {
        if (!patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("Shot.mov"));
        QVERIFY(writeVideo(source, 10));
        LinkOptions lo;
        lo.editRate = Rational(25);
        auto clip = linkClip(*probe(source), lo);
        QVERIFY(clip);
        QVERIFY(!clip->fields.value(QStringLiteral("Source File")).isEmpty());
        const QString video = clip->fields.value(QStringLiteral("Video"));
        MediaCreation creation;
        creation.videoDrive = dir.path();
        creation.raster = QSize(256, 144);
        // The whole clip in LB and frames 2 to 6 in HQ, both naming the clip; the whole clip in SQ as a new clip.
        creation.master = model::mobIdOf(*clip);
        creation.resolution = *findResolution(QStringLiteral("dnx_lb"));
        QVERIFY(transcodeClip(*clip, 0, -1, creation));
        creation.resolution = *findResolution(QStringLiteral("dnx_hq"));
        QVERIFY(transcodeClip(*clip, 2, 6, creation));
        creation.master = MobId();
        creation.resolution = *findResolution(QStringLiteral("dnx_sq"));
        const auto other = transcodeClip(*clip, 0, -1, creation);
        QVERIFY(other);
        QVERIFY(!(model::mobIdOf(*other) == model::mobIdOf(*clip)));
        MediaDatabase db;
        db.scan({dir.path()});
        const QHash<QString, MediaCoverage> coverage = multiRezCoverage(*clip, db);
        QCOMPARE(coverage.value(QStringLiteral("Avid DNx LB")), MediaCoverage::Full);
        QCOMPARE(coverage.value(QStringLiteral("Avid DNx HQ")), MediaCoverage::Partial);
        QCOMPARE(coverage.value(QStringLiteral("Avid DNx SQ")), MediaCoverage::Full);
        QCOMPARE(coverage.value(QStringLiteral("Avid DNx 444"), MediaCoverage::None), MediaCoverage::None);
        QCOMPARE(multiRezCellText(MediaCoverage::Full), QStringLiteral("\u25CF"));
        QCOMPARE(multiRezCellText(MediaCoverage::Partial), QStringLiteral("\u25D0"));
        QCOMPARE(multiRezCellText(MediaCoverage::None), QStringLiteral("\u25CB"));
        QVERIFY(multiRezHeadings().contains(QStringLiteral("Avid DNx HQ")));
        QVERIFY(multiRezHeadings().contains(QStringLiteral("48 kHz")));
        // The new clip sees the original's LB media too (they share the source and its whole span).
        QCOMPARE(multiRezCoverage(*other, db).value(QStringLiteral("Avid DNx LB")), MediaCoverage::Full);
        // Played: the linked file, LB and SQ, not the partial HQ.
        const QList<MediaCandidate> all = mediaCandidates(*clip, db);
        QCOMPARE(all.size(), 3);
        for (const MediaCandidate& c : all) QVERIFY(c.resolution != QLatin1String("Avid DNx HQ"));
        // Delete: the LB and HQ media named by the clip, not the other clip's SQ.
        const QMap<QString, QStringList> affinity = affinityMediaFiles(*clip, db);
        QCOMPARE(affinity.keys(), (QStringList{QStringLiteral("Avid DNx HQ"), QStringLiteral("Avid DNx LB")}));
        QVERIFY(!affinityMediaFiles(*other, db).contains(QStringLiteral("Avid DNx LB")));
        // The Video column follows the media.
        DynamicRelinkSettings s;
        s.enabled = true;
        s.working.video = RelinkRules::Method::SpecificResolution;
        s.working.compression = QStringLiteral("Avid DNx LB");
        model::BinItem item = *clip;
        QVERIFY(dynamicRelink(item, db, s, Rational(25), QSize(256, 144)));
        QCOMPARE(item.fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx LB"));
        s.working.compression = QStringLiteral("Avid DNx SQ");
        QVERIFY(dynamicRelink(item, db, s, Rational(25), QSize(256, 144)));
        QCOMPARE(item.fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx SQ"));
        QVERIFY(restoreAffinityMedia(item));
        QCOMPARE(item.fields.value(QStringLiteral("Video")), video);
        QCOMPARE(item.link, clip->link);
    }

    // Transcode of a sequence without Create new clips (p.354-355, p.1118): the clip keeps the media of the range the
    // sequence uses, partly online; the sequence and its segments are left as they are.
    void sequenceWithoutNewClips() {
        if (!patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("Shot.mov"));
        QVERIFY(writeVideo(source, 20));
        LinkOptions lo;
        lo.editRate = Rational(25);
        auto clip = linkClip(*probe(source), lo);
        QVERIFY(clip);
        model::Sequence sequence = model::Sequence::withDefaultTracks();
        model::Segment a;
        a.clipId = clip->id;
        a.binId = QStringLiteral("bin1");
        a.sourceTrack = QStringLiteral("V1");
        a.sourceIn = 4;
        a.length = 6;
        sequence.track(QStringLiteral("V1"))->segments = {a};
        model::BinItem seq;
        seq.id = model::newItemId();
        seq.kind = model::BinItemKind::Sequence;
        seq.name = QStringLiteral("Cut 1");
        seq.editRate = Rational(25);
        sequence.storeIn(seq);
        const ClipLookup lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip->id) return *clip;
            return std::nullopt;
        };
        ConsolidateOptions options;
        options.mode = ConsolidateOptions::Mode::Transcode;
        options.newClips = false;
        options.handles = 1;
        options.creation.raster = QSize(256, 144);
        options.creation.resolution = *findResolution(QStringLiteral("dnx_lb"));
        options.creation.videoDrive = dir.path();
        const ConsolidateResult r = consolidate({seq}, QStringLiteral("bin1"), lookup, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.added.isEmpty());
        QVERIFY(r.changed.isEmpty());
        QCOMPARE(r.associated.size(), 1);
        QCOMPARE(r.associated.constFirst().id, clip->id);
        MediaDatabase db;
        db.scan({dir.path()});
        QCOMPARE(db.setsOf(model::mobIdOf(*clip)).size(), 1);
        QCOMPARE(db.setsOf(model::mobIdOf(*clip)).constFirst().any().frames, 8); // frames 3 to 11 with a frame of handles
        QCOMPARE(multiRezCoverage(*clip, db).value(QStringLiteral("Avid DNx LB")), MediaCoverage::Partial);
        QCOMPARE(mediaCandidates(*clip, db).size(), 1); // the linked file only
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
