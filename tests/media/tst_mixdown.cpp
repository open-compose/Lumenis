// Audio Mixdown (media/mixdown.h; Editing Guide p.731-732; docs/spec/audio-mixdown.md) and ROADMAP
// Phase 5 gate 1, the null test: a sequence of sine-wave clips with gains, pans and filler is mixed
// down between marks, Mono and Dual Mono, with the Master fader; the managed MXF media made is read
// back and compared sample by sample with the same mix worked out here in double precision from the
// sines themselves. What is left over must be at or below -120 dBFS (24-bit PCM rounds to about
// -144 dBFS).
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/mixdown.h"
#include "lumenis/model/sequence.h"

#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <numbers>

using namespace lumenis;
using namespace lumenis::media;

namespace {

constexpr int kRate = 48000;
const Rational kEditRate(25);
constexpr std::int64_t kPerFrame = kRate / 25;

// The two sources, sample n.
double tone1(std::int64_t n) {
    return 0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(n) / kRate);
}
double tone2(std::int64_t n) {
    return 0.3 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(n) / kRate + 0.3);
}

bool writeTone(const QString& path, double (*tone)(std::int64_t), std::int64_t samples) {
    WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_f32le"); // the floats exactly
    s.channels = 1;
    MediaWriter w;
    if (!w.open(path, s)) return false;
    std::vector<float> v(static_cast<std::size_t>(samples));
    for (std::int64_t n = 0; n < samples; ++n) v[static_cast<std::size_t>(n)] = static_cast<float>(tone(n));
    return w.writeAudio(v.data(), static_cast<int>(samples)) && w.finish();
}

// One segment of the test sequence, as the reference mixes it.
struct Part {
    QString track;
    int trackNumber;
    double (*tone)(std::int64_t);
    std::int64_t at;       // sequence frame
    std::int64_t sourceIn; // clip frame
    std::int64_t length;   // frames
    double gainDb;
    int pan;               // 0 follows the track's side: odd left, even right (p.627)
};

// The equal-power pan law playback uses (media::panGains), in double.
void reference(int pan, double gainDb, double* left, double* right) {
    const double p = pan / 100.0;
    const double angle = (p + 1.0) * std::numbers::pi / 4.0;
    const double g = std::pow(10.0, gainDb / 20.0);
    *left = std::cos(angle) * g;
    *right = std::sin(angle) * g;
}

// Peak of |a - b| in dBFS.
double residualDb(const std::vector<double>& want, const std::vector<float>& got) {
    double peak = 0;
    const std::size_t n = std::min(want.size(), got.size());
    for (std::size_t i = 0; i < n; ++i) peak = std::max(peak, std::abs(want[i] - static_cast<double>(got[i])));
    return peak <= 0 ? -400.0 : 20.0 * std::log10(peak);
}

} // namespace

class TestMixdown : public QObject {
    Q_OBJECT

private slots:
    void nullTest_data() {
        QTest::addColumn<int>("format");
        QTest::newRow("mono") << static_cast<int>(MixdownFormat::Mono);
        QTest::newRow("dual mono") << static_cast<int>(MixdownFormat::DualMono);
    }

    void nullTest() {
        QFETCH(int, format);
        const auto fmt = static_cast<MixdownFormat>(format);
        QTemporaryDir dir;
        const QString one = dir.filePath(QStringLiteral("one.wav"));
        const QString two = dir.filePath(QStringLiteral("two.wav"));
        QVERIFY(writeTone(one, tone1, 4 * kRate));
        QVERIFY(writeTone(two, tone2, 4 * kRate));
        LinkOptions o;
        o.editRate = kEditRate;
        const QList<model::BinItem> linked = linkFiles({one, two}, o);
        QCOMPARE(linked.size(), 2);
        const model::BinItem& c1 = linked.at(0);
        const model::BinItem& c2 = linked.at(1);

        // A1: 5 frames of filler, then tone 1 from frame 10 for 40 frames at -6 dB, 30% left.
        // A2: tone 2 from frame 0 for 30 frames at +3 dB, 40% right; then tone 1 from frame 50 for
        // 20 frames at 0 dB and centred, which follows A2's side, the right.
        const QList<Part> parts{
            {QStringLiteral("A1"), 1, tone1, 5, 10, 40, -6.0, -30},
            {QStringLiteral("A2"), 2, tone2, 0, 0, 30, 3.0, 40},
            {QStringLiteral("A2"), 2, tone1, 30, 50, 20, 0.0, 0},
        };
        model::Sequence seq = model::Sequence::withDefaultTracks();
        std::int64_t a1 = 0;
        std::int64_t a2 = 0;
        for (const Part& p : parts) {
            std::int64_t& end = p.track == QLatin1String("A1") ? a1 : a2;
            if (p.at > end) {
                seq.splice(p.track, end, {model::Segment::filler(p.at - end)});
                end = p.at;
            }
            model::Segment s;
            const model::BinItem& clip = p.tone == tone1 ? c1 : c2;
            s.clipId = clip.id;
            s.binId = QStringLiteral("bin");
            s.name = clip.name;
            s.sourceTrack = QStringLiteral("A1");
            s.sourceIn = p.sourceIn;
            s.length = p.length;
            s.gain = p.gainDb;
            s.pan = p.pan;
            seq.splice(p.track, end, {s});
            end += p.length;
        }
        model::BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = kEditRate;
        seq.storeIn(item);

        MixdownRequest r;
        r.sequence = item;
        r.tracks = {QStringLiteral("A1"), QStringLiteral("A2")};
        r.start = 2; // the marks: frames 2 to 59
        r.end = 60;
        r.format = fmt;
        r.masterGainDb = -1.5;
        r.includeMaster = true;
        r.clips = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == c1.id) return c1;
            if (id == c2.id) return c2;
            return std::nullopt;
        };
        const QString drive = dir.filePath(QStringLiteral("Drive"));
        QVERIFY(QDir().mkpath(drive));
        MediaCreation creation;
        creation.videoDrive = drive;
        creation.bitDepth = 24;
        QString error;
        const auto made = mixdownClip(r, QStringLiteral("Cut Mixdown"), QStringLiteral("01:00:00:02"), creation, &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(made->kind, model::BinItemKind::AudioClip);
        QCOMPARE(made->durationFrames, std::int64_t{58});
        QCOMPARE(made->fields.value(QStringLiteral("Tracks")),
                 fmt == MixdownFormat::Mono ? QStringLiteral("A1") : QStringLiteral("A1-2"));
        QCOMPARE(made->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:02"));
        const auto link = linkOf(*made);
        QVERIFY(link && link->isManaged());
        QCOMPARE(link->audioPaths.size(), mixdownChannels(fmt));

        // The reference, sample by sample.
        const std::int64_t first = r.start * kPerFrame;
        const std::int64_t count = (r.end - r.start) * kPerFrame;
        const double master = std::pow(10.0, r.masterGainDb / 20.0);
        std::vector<double> left(static_cast<std::size_t>(count), 0.0);
        std::vector<double> right(static_cast<std::size_t>(count), 0.0);
        for (const Part& p : parts) {
            double l = 0;
            double rr = 0;
            reference(p.pan != 0 ? p.pan : (p.trackNumber % 2 == 1 ? -100 : 100), p.gainDb, &l, &rr);
            const double g = std::pow(10.0, p.gainDb / 20.0);
            for (std::int64_t n = 0; n < p.length * kPerFrame; ++n) {
                const std::int64_t out = p.at * kPerFrame + n - first;
                if (out < 0 || out >= count) continue;
                const double v = p.tone(p.sourceIn * kPerFrame + n);
                if (fmt == MixdownFormat::Mono) {
                    left[static_cast<std::size_t>(out)] += v * g * master;
                } else {
                    left[static_cast<std::size_t>(out)] += v * l * master;
                    right[static_cast<std::size_t>(out)] += v * rr * master;
                }
            }
        }
        // The media, read back.
        for (int c = 0; c < mixdownChannels(fmt); ++c) {
            AudioReader reader;
            QVERIFY(reader.open(link->audioPaths.at(c)));
            QCOMPARE(reader.sampleRate(), kRate);
            const std::vector<float> got = reader.samples(0, static_cast<int>(count));
            QCOMPARE(static_cast<std::int64_t>(got.size()), count);
            const double db = residualDb(c == 0 ? left : right, got);
            qInfo("track %d: residual %.1f dBFS", c + 1, db);
            QVERIFY2(db <= -120.0, qPrintable(QStringLiteral("track %1: the residual is %2 dBFS").arg(c + 1).arg(db)));
        }
    }

    // Without tracks, or without frames between the marks, nothing is made.
    void refusals() {
        MixdownRequest r;
        r.sequence.kind = model::BinItemKind::Sequence;
        r.sequence.editRate = kEditRate;
        r.start = 0;
        r.end = 10;
        MediaCreation creation;
        QString error;
        QVERIFY(!mixdownClip(r, QStringLiteral("x"), {}, creation, &error));
        QVERIFY(error.contains(QStringLiteral("tracks")));
        r.tracks = {QStringLiteral("A1")};
        r.end = 0;
        QVERIFY(!mixdownClip(r, QStringLiteral("x"), {}, creation, &error));
        QVERIFY(error.contains(QStringLiteral("marks")));
    }
};

QTEST_GUILESS_MAIN(TestMixdown)
#include "tst_mixdown.moc"
