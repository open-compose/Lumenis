// Audio effects (lumenis/media/audio_fx.h; Editing Guide ch. 24; docs/spec/track-effects.md): the
// built-in effects' list and what each does to known signals, effects added from elsewhere, and
// Audio Track Effect inserts in media::AudioMix: a track runs through its inserts, in pieces as
// whole, after a seek as from the start, and in Audio Mixdown.
#include "lumenis/media/audio_fx.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/media/mixdown.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/sequence.h"

#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <numbers>

using namespace lumenis;
using namespace lumenis::media;

namespace {

constexpr int kRate = 48000;

std::vector<float> sine(double hz, double amplitude, int count) {
    std::vector<float> v(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        v[static_cast<std::size_t>(i)] = static_cast<float>(amplitude * std::sin(2 * std::numbers::pi * hz * i / kRate));
    }
    return v;
}

// The level in dB of the last `tail` samples.
double levelDb(const std::vector<float>& v, int tail) {
    double peak = 0;
    for (std::size_t i = v.size() - static_cast<std::size_t>(tail); i < v.size(); ++i) {
        peak = std::max(peak, std::abs(static_cast<double>(v[i])));
    }
    return 20 * std::log10(std::max(peak, 1e-12));
}

std::vector<float> run(const QString& id, const QMap<QString, double>& values, std::vector<float> input) {
    auto fx = makeAudioEffect(id);
    if (!fx) return {};
    fx->setAll(values);
    fx->process(input.data(), static_cast<int>(input.size()));
    return input;
}

// A test effect from "elsewhere": multiplies by its one parameter.
class Scale : public AudioEffect {
public:
    explicit Scale(const AudioEffectInfo& info) : info_(info) {}
    const AudioEffectInfo& info() const override { return info_; }
    void set(const QString& id, double value) override {
        if (id == QLatin1String("k")) k_ = value;
    }
    void reset() override {}
    void process(float* s, int n) override {
        for (int i = 0; i < n; ++i) s[i] *= static_cast<float>(k_);
    }
    int tail() const override { return 0; }

private:
    const AudioEffectInfo& info_;
    double k_ = 1;
};

AudioEffectInfo scaleInfo() {
    AudioEffectInfo info;
    info.id = QStringLiteral("test.scale");
    info.name = QStringLiteral("Scale");
    info.category = QStringLiteral("Test");
    info.vendor = QStringLiteral("Tests");
    AudioEffectParam k;
    k.id = QStringLiteral("k");
    k.name = QStringLiteral("K");
    k.min = 0;
    k.max = 4;
    k.defaultValue = 1;
    info.params << k;
    return info;
}

} // namespace

class TestAudioFx : public QObject {
    Q_OBJECT

private slots:
    void list() {
        const QList<AudioEffectInfo>& all = audioEffects();
        QStringList ids;
        for (const AudioEffectInfo& e : all) ids << e.id;
        QCOMPARE(ids.mid(0, 9), (QStringList{"builtin.compressor", "builtin.gate", "builtin.deesser", "builtin.eq",
                                             "builtin.delay", "builtin.chorus", "builtin.reverb", "builtin.lofi",
                                             "builtin.trim"}));
        QCOMPARE(QSet<QString>(ids.begin(), ids.end()).size(), ids.size());
        for (const AudioEffectInfo& e : all) {
            QVERIFY(!e.name.isEmpty() && !e.category.isEmpty());
            for (const AudioEffectParam& p : e.params) {
                QVERIFY2(p.min <= p.defaultValue && p.defaultValue <= p.max, qPrintable(e.id + QLatin1Char('.') + p.id));
            }
            auto fx = makeAudioEffect(e.id);
            QVERIFY(fx);
            QCOMPARE(fx->info().id, e.id);
            // Silence in, silence out (no effect makes sound of its own).
            std::vector<float> zeros(4800, 0.0f);
            fx->process(zeros.data(), 4800);
            for (float z : zeros) QCOMPARE(z, 0.0f);
        }
        QVERIFY(!makeAudioEffect(QStringLiteral("nobody.nothing")));
        QVERIFY(!findAudioEffect(QStringLiteral("nobody.nothing")));
        // Values are put in range.
        const AudioEffectInfo* trim = findAudioEffect(QStringLiteral("builtin.trim"));
        QVERIFY(trim);
        QCOMPARE(trim->defaults().value(QStringLiteral("gain")), 0.0);
        QCOMPARE(formatParam(*trim->param(QStringLiteral("gain")), -6), QStringLiteral("-6.0 dB"));
        QCOMPARE(formatParam(*trim->param(QStringLiteral("gain")), -96), QStringLiteral("-∞ dB"));
        QCOMPARE(formatParam(*trim->param(QStringLiteral("polarity")), 1), QStringLiteral("Invert"));
        const AudioEffectInfo* eq = findAudioEffect(QStringLiteral("builtin.eq"));
        QCOMPARE(formatParam(*eq->param(QStringLiteral("highFreq")), 8000), QStringLiteral("8.00 kHz"));
        QCOMPARE(formatParam(*eq->param(QStringLiteral("lowFreq")), 100), QStringLiteral("100 Hz"));
    }

    void registered() {
        registerAudioEffects({scaleInfo()}, [](const QString& id) -> std::unique_ptr<AudioEffect> {
            static const AudioEffectInfo info = scaleInfo();
            if (id != info.id) return nullptr;
            return std::make_unique<Scale>(info);
        });
        QVERIFY(findAudioEffect(QStringLiteral("test.scale")));
        QCOMPARE(audioEffects().last().id, QStringLiteral("test.scale"));
        const std::vector<float> out = run(QStringLiteral("test.scale"), {{QStringLiteral("k"), 2}}, {0.25f});
        QCOMPARE(out.front(), 0.5f);
        // A second registration of a known id is ignored.
        const qsizetype count = audioEffects().size();
        registerAudioEffects({scaleInfo()}, [](const QString&) { return std::unique_ptr<AudioEffect>(); });
        QCOMPARE(audioEffects().size(), count);
        QVERIFY(makeAudioEffect(QStringLiteral("test.scale")));
    }

    // What each effect does to known signals.
    void effects() {
        const std::vector<float> tone = sine(1000, 0.5, kRate);
        // Trim: gain and polarity.
        std::vector<float> t = run(QStringLiteral("builtin.trim"), {{QStringLiteral("gain"), -6.0206}}, tone);
        QVERIFY(std::abs(static_cast<double>(t[100] - tone[100] * 0.5f)) < 1e-4);
        t = run(QStringLiteral("builtin.trim"), {{QStringLiteral("polarity"), 1}}, tone);
        QCOMPARE(t[100], -tone[100]);
        t = run(QStringLiteral("builtin.trim"), {{QStringLiteral("gain"), -96}}, tone);
        QCOMPARE(t[100], 0.0f);
        // Compressor: a -6 dBFS tone over a -20 dB threshold at 4:1 settles near -20 + 14/4 = -16.5 dB.
        t = run(QStringLiteral("builtin.compressor"),
                {{QStringLiteral("threshold"), -20}, {QStringLiteral("ratio"), 4}, {QStringLiteral("attack"), 1},
                 {QStringLiteral("release"), 50}},
                tone);
        QVERIFY2(std::abs(levelDb(t, 4800) + 16.5) < 1.5, qPrintable(QString::number(levelDb(t, 4800))));
        // Make-up gain adds on top.
        const double plain = levelDb(t, 4800);
        t = run(QStringLiteral("builtin.compressor"),
                {{QStringLiteral("threshold"), -20}, {QStringLiteral("ratio"), 4}, {QStringLiteral("attack"), 1},
                 {QStringLiteral("release"), 50}, {QStringLiteral("gain"), 6}},
                tone);
        QVERIFY(std::abs(levelDb(t, 4800) - plain - 6) < 0.2);
        // Gate: a tone under the threshold falls by the range; one over it passes.
        const std::vector<float> quiet = sine(1000, 0.001, kRate); // -60 dBFS
        t = run(QStringLiteral("builtin.gate"), {{QStringLiteral("threshold"), -40}, {QStringLiteral("range"), -40}}, quiet);
        QVERIFY(levelDb(t, 4800) < -95);
        t = run(QStringLiteral("builtin.gate"), {{QStringLiteral("threshold"), -40}}, tone);
        QVERIFY(std::abs(levelDb(t, 4800) - levelDb(tone, 4800)) < 0.1);
        // De-Esser: an 8 kHz tone over the threshold is cut by the range; a 200 Hz one is not.
        const std::vector<float> ess = sine(8000, 0.5, kRate);
        t = run(QStringLiteral("builtin.deesser"), {{QStringLiteral("frequency"), 5000}, {QStringLiteral("range"), -10}}, ess);
        QVERIFY(std::abs(levelDb(t, 4800) - (levelDb(ess, 4800) - 10)) < 0.5);
        const std::vector<float> low = sine(200, 0.5, kRate);
        t = run(QStringLiteral("builtin.deesser"), {{QStringLiteral("frequency"), 5000}, {QStringLiteral("range"), -10}}, low);
        QVERIFY(std::abs(levelDb(t, 4800) - levelDb(low, 4800)) < 0.5);
        // EQ: +6 dB low shelf lifts 50 Hz by about 6 dB and leaves 5 kHz.
        const std::vector<float> bass = sine(50, 0.25, kRate);
        t = run(QStringLiteral("builtin.eq"), {{QStringLiteral("lowGain"), 6}, {QStringLiteral("lowFreq"), 200}}, bass);
        QVERIFY(std::abs(levelDb(t, 9600) - levelDb(bass, 9600) - 6) < 0.6);
        const std::vector<float> treble = sine(5000, 0.25, kRate);
        t = run(QStringLiteral("builtin.eq"), {{QStringLiteral("lowGain"), 6}, {QStringLiteral("lowFreq"), 200}}, treble);
        QVERIFY(std::abs(levelDb(t, 4800) - levelDb(treble, 4800)) < 0.1);
        // Delay: an impulse comes back after the time, at the mix times each feedback.
        std::vector<float> impulse(kRate, 0.0f);
        impulse[0] = 1;
        t = run(QStringLiteral("builtin.delay"),
                {{QStringLiteral("time"), 100}, {QStringLiteral("feedback"), 50}, {QStringLiteral("mix"), 50}}, impulse);
        QCOMPARE(t[0], 0.5f);
        QVERIFY(std::abs(static_cast<double>(t[4800]) - 0.5) < 1e-6);
        QVERIFY(std::abs(static_cast<double>(t[9600]) - 0.25) < 1e-6);
        // Reverb: an impulse gives a tail that dies away.
        auto reverb = makeAudioEffect(QStringLiteral("builtin.reverb"));
        QVERIFY(reverb->tail() > kRate / 2);
        std::vector<float> long_(4 * kRate, 0.0f);
        long_[0] = 1;
        reverb->set(QStringLiteral("mix"), 100);
        reverb->process(long_.data(), static_cast<int>(long_.size()));
        double early = 0, late = 0;
        for (int i = 0; i < kRate / 2; ++i) early += std::abs(static_cast<double>(long_[static_cast<std::size_t>(i)]));
        for (int i = 3 * kRate; i < 3 * kRate + kRate / 2; ++i) late += std::abs(static_cast<double>(long_[static_cast<std::size_t>(i)]));
        QVERIFY(early > 0.01);
        QVERIFY(late < early / 100);
        // Lo-Fi: 2 bits leave two levels a side.
        t = run(QStringLiteral("builtin.lofi"), {{QStringLiteral("bits"), 2}, {QStringLiteral("rate"), 48000}}, tone);
        for (float v : t) QVERIFY(v == 0.0f || std::abs(std::abs(v) - 0.5f) < 1e-6f || std::abs(v) == 1.0f);
        // Chorus: changes the sound, stays bounded.
        t = run(QStringLiteral("builtin.chorus"), {}, tone);
        QVERIFY(t != tone);
        for (float v : t) QVERIFY(std::abs(v) < 1.0f);
        // Reset forgets the sound so far.
        auto delay = makeAudioEffect(QStringLiteral("builtin.delay"));
        std::vector<float> one(4800, 0.0f);
        one[0] = 1;
        delay->process(one.data(), 4800);
        delay->reset();
        std::vector<float> zeros(kRate, 0.0f);
        delay->process(zeros.data(), kRate);
        for (float z : zeros) QCOMPARE(z, 0.0f);
    }

    // Inserts in the mix: a 1 kHz tone on A1 through Trim -6 dB then Delay; whole, in chunks and from
    // the middle; bypassed and unavailable inserts; Audio Mixdown.
    void inserts() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("tone.wav"));
        {
            WriterSettings ws;
            ws.format = QStringLiteral("wav");
            ws.audioCodec = QStringLiteral("pcm_f32le");
            ws.channels = 1;
            MediaWriter w;
            QVERIFY(w.open(path, ws));
            const std::vector<float> v = sine(1000, 0.5, 2 * kRate);
            QVERIFY(w.writeAudio(v.data(), 2 * kRate) && w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        const QList<model::BinItem> linked = linkFiles({path}, o);
        QCOMPARE(linked.size(), 1);
        const model::BinItem clip = linked.first();
        auto lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip.id) return clip;
            return std::nullopt;
        };
        // A1: the clip, 25 frames from frame 0, then 10 frames of filler (the delay's tail plays there).
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment s;
        s.clipId = clip.id;
        s.sourceTrack = QStringLiteral("A1");
        s.length = 25;
        s.pan = -100;
        seq.splice(QStringLiteral("A1"), 0, {s});
        seq.splice(QStringLiteral("A2"), 0, {model::Segment::filler(35)});
        model::BinItem item;
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        seq.storeIn(item);
        const std::int64_t count = 35 * kRate / 25;
        auto render = [&](const model::BinItem& it, std::int64_t from, std::int64_t length, int chunk) {
            AudioMix mix(audioPieces(it, lookup), trackInserts(it));
            std::vector<float> stereo(2 * static_cast<std::size_t>(length));
            for (std::int64_t at = 0; at < length; at += chunk) {
                mix.render(from + at, static_cast<int>(std::min<std::int64_t>(chunk, length - at)), stereo.data() + 2 * at);
            }
            return stereo;
        };
        const std::vector<float> dry = render(item, 0, count, static_cast<int>(count));

        model::Track* a1 = seq.track(QStringLiteral("A1"));
        model::TrackInsert trim{QStringLiteral("builtin.trim"), QStringLiteral("Trim"), {{QStringLiteral("gain"), -6.0206}}, false};
        a1->inserts[0] = trim;
        seq.storeIn(item);
        QCOMPARE(model::Sequence::fromItem(item).track(QStringLiteral("A1"))->inserts[0], std::optional(trim));
        QCOMPARE(trackInserts(item).at(QStringLiteral("A1")).size(), 1);
        QVERIFY(trackInserts(item, true).empty());
        std::vector<float> wet = render(item, 0, count, static_cast<int>(count));
        // Panned hard left: the left is the tone at half the level, the right silent.
        for (std::size_t i = 0; i < 2 * static_cast<std::size_t>(count); i += 2) {
            QVERIFY(std::abs(static_cast<double>(wet[i] - dry[i] * 0.5f)) < 1e-5);
            QVERIFY(std::abs(static_cast<double>(wet[i + 1])) < 1e-6);
        }
        // Bypassed: as without.
        a1->inserts[0]->bypass = true;
        seq.storeIn(item);
        QVERIFY(trackInserts(item).empty());
        QCOMPARE(render(item, 0, count, static_cast<int>(count)), dry);
        // Not installed: plays without it (p.790).
        a1->inserts[0] = model::TrackInsert{QStringLiteral("gone.plugin"), QStringLiteral("Gone"), {}, false};
        seq.storeIn(item);
        QCOMPARE(render(item, 0, count, static_cast<int>(count)), dry);

        // Trim then a delay: a tail sounds in the filler after the clip, on the left (the last pan).
        a1->inserts[0] = trim;
        a1->inserts[2] = model::TrackInsert{QStringLiteral("builtin.delay"), QStringLiteral("Delay"),
                                            {{QStringLiteral("time"), 100}, {QStringLiteral("feedback"), 40}, {QStringLiteral("mix"), 50}},
                                            false};
        seq.storeIn(item);
        wet = render(item, 0, count, static_cast<int>(count));
        // 50 ms after the clip ends, over 5 ms (a whole number of the tone's cycles).
        const std::size_t after = 2 * static_cast<std::size_t>(25 * kRate / 25 + 2400);
        double left = 0, right = 0;
        for (std::size_t i = after; i < after + 2 * 240; i += 2) {
            left = std::max(left, std::abs(static_cast<double>(wet[i])));
            right = std::max(right, std::abs(static_cast<double>(wet[i + 1])));
        }
        QVERIFY(left > 0.05);
        QVERIFY(right < 1e-6);
        // In chunks: the same.
        QCOMPARE(render(item, 0, count, 1001), wet);
        // From the middle (a seek): settled on the tail first, so the same as from the start.
        const std::int64_t from = 15 * kRate / 25;
        const std::vector<float> seek = render(item, from, count - from, 777);
        double worst = 0;
        for (std::size_t i = 0; i < seek.size(); ++i) {
            worst = std::max(worst, std::abs(static_cast<double>(seek[i] - wet[2 * static_cast<std::size_t>(from) + i])));
        }
        QVERIFY2(worst < 1e-5, qPrintable(QString::number(worst)));

        // Audio Mixdown hears them too.
        MixdownRequest r;
        r.sequence = item;
        r.tracks = {QStringLiteral("A1")};
        r.start = 0;
        r.end = 35;
        r.format = MixdownFormat::DualMono;
        r.clips = lookup;
        MixdownRenderer mixdown(r);
        std::vector<std::vector<float>> out(2);
        mixdown.render(0, static_cast<int>(count), out);
        for (std::size_t i = after; i < after + 2 * 240; i += 2) {
            QVERIFY(std::abs(static_cast<double>(out[0][i / 2] - wet[i])) < 1e-5);
        }
    }
};

QTEST_GUILESS_MAIN(TestAudioFx)
#include "tst_audio_fx.moc"
