// Audio EQ (lumenis/model/audio_eq.h; Editing Guide p.736-745; docs/spec/audio-eq.md): the bands'
// limits and shapes, setting the effect on segments, and the sound: a sequence with EQ'd
// segments is mixed through media::AudioMix and compared sample by sample with the same filters
// run here in double precision, and rendering in pieces or from the middle must match rendering
// it all at once.
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/audio_eq.h"
#include "lumenis/model/sequence.h"

#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <numbers>

using namespace lumenis;
using namespace lumenis::media;
using lumenis::model::AudioEq;

namespace {

constexpr int kRate = 48000;
constexpr std::int64_t kPerFrame = kRate / 25;

double tone(std::int64_t n, double hz) {
    return 0.25 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) / kRate);
}

// Two tones, 100 Hz and 5 kHz, so each band has something to work on.
double source(std::int64_t n) {
    return tone(n, 100) + tone(n, 5000);
}

bool writeSource(const QString& path, std::int64_t samples) {
    WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_f32le");
    s.channels = 1;
    MediaWriter w;
    if (!w.open(path, s)) return false;
    std::vector<float> v(static_cast<std::size_t>(samples));
    for (std::int64_t n = 0; n < samples; ++n) v[static_cast<std::size_t>(n)] = static_cast<float>(source(n));
    return w.writeAudio(v.data(), static_cast<int>(samples)) && w.finish();
}

// The EQ run in double precision (direct form I), from silence.
std::vector<double> filtered(const AudioEq& eq, std::int64_t from, std::int64_t count) {
    std::vector<double> x(static_cast<std::size_t>(count));
    for (std::int64_t n = 0; n < count; ++n) x[static_cast<std::size_t>(n)] = static_cast<double>(static_cast<float>(source(from + n)));
    for (const model::Biquad& f : model::eqFilters(eq, kRate)) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (double& v : x) {
            const double y = f.b0 * v + f.b1 * x1 + f.b2 * x2 - f.a1 * y1 - f.a2 * y2;
            x2 = x1;
            x1 = v;
            y2 = y1;
            y1 = y;
            v = y;
        }
    }
    return x;
}

double rmsDb(const std::vector<double>& v, std::size_t from, std::size_t to) {
    double sum = 0;
    for (std::size_t i = from; i < to; ++i) sum += v[i] * v[i];
    return 10 * std::log10(sum / static_cast<double>(to - from));
}

} // namespace

class TestAudioEq : public QObject {
    Q_OBJECT

private slots:
    void limitsAndShapes() {
        AudioEq e;
        QVERIFY(model::isFlat(e));
        e.low = 30;
        e.mid = -40;
        e.high = 3.14;
        e.lowTurnover = 110;
        e.highTurnover = 100000;
        e.midFrequency = 10;
        const AudioEq c = model::clampEq(e);
        QCOMPARE(c.low, 15.0);
        QCOMPARE(c.mid, -20.0);
        QCOMPARE(c.high, 3.1);
        QCOMPARE(c.lowTurnover, 120);
        QCOMPARE(c.highTurnover, 15000);
        QCOMPARE(c.midFrequency, 50.0);
        // A flat EQ changes nothing anywhere.
        for (double hz : {20.0, 1000.0, 20000.0}) QVERIFY(std::abs(model::eqResponse(AudioEq{}, hz)) < 1e-9);
        // The low shelf boosts below its turnover and leaves the top alone.
        AudioEq low;
        low.low = 12;
        low.lowTurnover = 240;
        QVERIFY(std::abs(model::eqResponse(low, 20) - 12) < 0.3);
        QVERIFY(std::abs(model::eqResponse(low, 5000)) < 0.1);
        // The high shelf cuts above its turnover.
        AudioEq high;
        high.high = -20;
        high.highTurnover = 6000;
        QVERIFY(std::abs(model::eqResponse(high, 18000) + 20) < 1.5);
        QVERIFY(std::abs(model::eqResponse(high, 200)) < 0.1);
        // The midrange: its full gain at its centre; 1/4 octave is narrower than 2 octaves.
        AudioEq narrow;
        narrow.mid = -15;
        narrow.midFrequency = 1000;
        narrow.wideMid = false;
        AudioEq wide = narrow;
        wide.wideMid = true;
        QVERIFY(std::abs(model::eqResponse(narrow, 1000) + 15) < 0.01);
        QVERIFY(std::abs(model::eqResponse(wide, 1000) + 15) < 0.01);
        QVERIFY(model::eqResponse(narrow, 1500) > model::eqResponse(wide, 1500) + 5);
    }

    void setOnSegments() {
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.sourceTrack = QStringLiteral("A1");
        a.length = 20;
        seq.splice(QStringLiteral("A1"), 0, {a, model::Segment::filler(10), a, a});
        AudioEq e;
        e.low = 6;
        // Whole segments that overlap the range (p.738): frames 15-35 touch the first and third.
        QCOMPARE(model::setEq(seq, QStringLiteral("A1"), 15, 35, e), 2);
        const model::Track* t = seq.track(QStringLiteral("A1"));
        QCOMPARE(t->segments.at(0).eq, std::optional(e));
        QVERIFY(!t->segments.at(1).eq);
        QCOMPARE(t->segments.at(2).eq, std::optional(e));
        QVERIFY(!t->segments.at(3).eq);
        // Saved and read back; an EQ keeps an edit from being a match frame.
        QCOMPARE(model::Sequence::fromJson(seq.toJson()).toJson(), seq.toJson());
        QVERIFY(!model::Sequence::isMatchFrame(t->segments.at(2), t->segments.at(3)));
        // Without an EQ, they are removed.
        QCOMPARE(model::setEq(seq, QStringLiteral("A1"), 0, 100, std::nullopt), 2);
        QVERIFY(!seq.track(QStringLiteral("A1"))->segments.at(0).eq);
        // Not on video tracks.
        QCOMPARE(model::setEq(seq, QStringLiteral("V1"), 0, 100, e), 0);
    }

    // The sound: the mix of an EQ'd segment against the reference, rendered whole, in chunks and
    // from the middle.
    void sound() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("tones.wav"));
        QVERIFY(writeSource(path, 4 * kRate));
        LinkOptions o;
        o.editRate = Rational(25);
        const QList<model::BinItem> linked = linkFiles({path}, o);
        QCOMPARE(linked.size(), 1);
        const model::BinItem clip = linked.first();

        AudioEq eq;
        eq.low = 12;
        eq.lowTurnover = 240;
        eq.mid = -6;
        eq.midFrequency = 2000;
        eq.high = -15;
        eq.highTurnover = 6000;
        // A1: 50 frames of the clip from its frame 10, panned hard left so the left channel is the
        // filtered sound at unity gain.
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment s;
        s.clipId = clip.id;
        s.sourceTrack = QStringLiteral("A1");
        s.sourceIn = 10;
        s.length = 50;
        s.pan = -100;
        s.eq = eq;
        seq.splice(QStringLiteral("A1"), 0, {s});
        model::BinItem item;
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        seq.storeIn(item);
        auto lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip.id) return clip;
            return std::nullopt;
        };
        const std::int64_t count = 50 * kPerFrame;
        const std::vector<double> want = filtered(eq, 10 * kPerFrame, count);

        // Whole, in uneven chunks, and from frame 20 on (a seek: the filters settle first).
        auto render = [&](std::int64_t from, std::int64_t length, int chunk) {
            AudioMix mix(audioPieces(item, lookup));
            std::vector<float> stereo(2 * static_cast<std::size_t>(length));
            for (std::int64_t at = 0; at < length; at += chunk) {
                const int n = static_cast<int>(std::min<std::int64_t>(chunk, length - at));
                mix.render(from + at, n, stereo.data() + 2 * at);
            }
            return stereo;
        };
        auto residual = [&](const std::vector<float>& stereo, std::int64_t from) {
            double peak = 0;
            for (std::size_t i = 0; 2 * i < stereo.size(); ++i) {
                peak = std::max(peak, std::abs(static_cast<double>(stereo[2 * i]) - want[static_cast<std::size_t>(from) + i]));
            }
            return peak <= 0 ? -400.0 : 20 * std::log10(peak);
        };
        const double whole = residual(render(0, count, static_cast<int>(count)), 0);
        const double chunks = residual(render(0, count, 1000), 0);
        const double seek = residual(render(20 * kPerFrame, 30 * kPerFrame, 777), 20 * kPerFrame);
        qInfo("residuals: whole %.1f, chunks %.1f, seek %.1f dBFS", whole, chunks, seek);
        QVERIFY(whole <= -120);
        QVERIFY(chunks <= -120);
        QVERIFY(seek <= -100);
        // And it did something: the 100 Hz tone up by about 12 dB, the 5 kHz one down.
        QVERIFY(rmsDb(want, 0, static_cast<std::size_t>(count)) > 20 * std::log10(0.25 / std::sqrt(2.0)) + 6);

        // Not In, or flat: unfiltered.
        s.eq->in = false;
        model::Sequence plain = model::Sequence::withDefaultTracks();
        plain.splice(QStringLiteral("A1"), 0, {s});
        plain.storeIn(item);
        AudioMix off(audioPieces(item, lookup));
        std::vector<float> stereo(2 * 100);
        off.render(0, 100, stereo.data());
        for (int i = 0; i < 100; ++i) {
            const double got = static_cast<double>(stereo[2 * static_cast<std::size_t>(i)]);
            QVERIFY(std::abs(got - static_cast<double>(static_cast<float>(source(10 * kPerFrame + i)))) < 1e-6);
        }
    }
};

QTEST_GUILESS_MAIN(TestAudioEq)
#include "tst_audio_eq.moc"
