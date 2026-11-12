// AudioSuite (media/audio_suite.h, model/audio_suite.h; Editing Guide p.796-805;
// docs/spec/audiosuite.md): the built-in processes, audio effects run offline, the effect on
// segments (what keeps a render and what loses it, its form in sequences), and Render Effect: the
// rendered managed media and playback hearing it in place of the source, at the clip gain the render
// already holds (the render order, p.700); templates and their names, and new master clips made from
// a clip's tracks (p.800-804).
#include "lumenis/media/audio_suite.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/sequence.h"

#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <numbers>

using namespace lumenis;
using namespace lumenis::media;

namespace {

constexpr int kRate = 48000;

double tone(std::int64_t n) {
    return 0.25 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(n) / kRate);
}

std::vector<float> sine(std::size_t count, double amplitude = 0.25, double offset = 0.0) {
    std::vector<float> v(count);
    for (std::size_t i = 0; i < count; ++i) {
        v[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(i) / kRate) + offset);
    }
    return v;
}

double peak(const std::vector<float>& v) {
    double p = 0;
    for (const float s : v) p = std::max(p, std::abs(static_cast<double>(s)));
    return p;
}

double rms(const std::vector<float>& v) {
    double sum = 0;
    for (const float s : v) sum += static_cast<double>(s) * static_cast<double>(s);
    return std::sqrt(sum / static_cast<double>(v.size()));
}

double mean(const std::vector<float>& v) {
    double sum = 0;
    for (const float s : v) sum += static_cast<double>(s);
    return sum / static_cast<double>(v.size());
}

bool writeTone(const QString& path, std::int64_t samples) {
    WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_f32le");
    s.channels = 1;
    MediaWriter w;
    if (!w.open(path, s)) return false;
    std::vector<float> v(static_cast<std::size_t>(samples));
    for (std::int64_t n = 0; n < samples; ++n) v[static_cast<std::size_t>(n)] = static_cast<float>(tone(n));
    return w.writeAudio(v.data(), static_cast<int>(samples)) && w.finish();
}

model::AudioSuiteEffect suite(const char* id, const char* name, const QMap<QString, double>& params = {}) {
    model::AudioSuiteEffect e;
    e.effect = QString::fromLatin1(id);
    e.name = QString::fromLatin1(name);
    e.params = params;
    return e;
}

double dbToGainOf(double db) {
    return std::pow(10.0, db / 20.0);
}

double db(double gain) {
    return 20.0 * std::log10(gain);
}

} // namespace

class TestAudioSuite : public QObject {
    Q_OBJECT

private slots:
    void plugins() {
        const QList<AudioSuiteInfo> all = audioSuitePlugins();
        QStringList ids;
        for (const AudioSuiteInfo& i : all) ids << i.id;
        for (const char* id : {"suite.gain", "suite.normalize", "suite.invert", "suite.reverse", "suite.dc", "suite.signal"}) {
            QVERIFY2(ids.contains(QLatin1String(id)), id);
        }
        // Every audio effect is there too, run offline.
        for (const AudioEffectInfo& e : audioEffects()) {
            const auto i = findAudioSuitePlugin(e.id);
            QVERIFY(i);
            QVERIFY(i->effect);
            QCOMPARE(i->name, e.name);
        }
        // Signal Generator's ranges are the guide's (p.855).
        const auto gen = findAudioSuitePlugin(QStringLiteral("suite.signal"));
        QVERIFY(gen && gen->generator);
        QCOMPARE(gen->param(QStringLiteral("frequency"))->min, 20.0);
        QCOMPARE(gen->param(QStringLiteral("frequency"))->max, 20000.0);
        QCOMPARE(gen->param(QStringLiteral("level"))->min, -95.0);
        QCOMPARE(gen->param(QStringLiteral("signal"))->choices.size(), 6);
        // Normalize's modes are named as on p.803.
        const auto norm = findAudioSuitePlugin(QStringLiteral("suite.normalize"));
        QCOMPARE(norm->param(QStringLiteral("mode"))->choices,
                 (QStringList{"Levels On All Chans-Tracks", "Level On Each Chan-Track"}));
        QVERIFY(!findAudioSuitePlugin(QStringLiteral("suite.nothing")));
        std::vector<std::vector<float>> c{sine(100)};
        QString error;
        QVERIFY(!processAudioSuite(QStringLiteral("suite.nothing"), {}, c, &error));
        QVERIFY(!error.isEmpty());
    }

    void simpleProcesses() {
        const std::vector<float> in = sine(4800);
        std::vector<std::vector<float>> c{in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.gain"), {{QStringLiteral("gain"), -6.0206}}, c));
        QVERIFY(std::abs(peak(c[0]) - 0.125) < 1e-4);
        c = {in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.gain"), {{QStringLiteral("gain"), -96}}, c));
        QCOMPARE(peak(c[0]), 0.0);
        c = {in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.invert"), {}, c));
        QCOMPARE(c[0][100], -in[100]);
        c = {in};
        QVERIFY(processAudioSuite(QStringLiteral("suite.reverse"), {}, c));
        QCOMPARE(c[0].front(), in.back());
        QCOMPARE(c[0][10], in[in.size() - 11]);
        c = {sine(48000, 0.25, 0.2)};
        QVERIFY(std::abs(mean(c[0]) - 0.2) < 1e-3);
        QVERIFY(processAudioSuite(QStringLiteral("suite.dc"), {}, c));
        QVERIFY(std::abs(mean(c[0])) < 1e-6);
    }

    void normalize() {
        // Two channels, one louder: All Chans keeps their balance, Each Chan levels both.
        std::vector<std::vector<float>> c{sine(48000, 0.5), sine(48000, 0.1)};
        QVERIFY(processAudioSuite(QStringLiteral("suite.normalize"), {{QStringLiteral("level"), -1}}, c));
        QVERIFY(std::abs(db(peak(c[0])) - -1) < 1e-3);
        QVERIFY(std::abs(peak(c[1]) / peak(c[0]) - 0.2) < 1e-4);
        c = {sine(48000, 0.5), sine(48000, 0.1)};
        QVERIFY(processAudioSuite(QStringLiteral("suite.normalize"), {{QStringLiteral("level"), -1}, {QStringLiteral("mode"), 1}}, c));
        QVERIFY(std::abs(db(peak(c[1])) - -1) < 1e-3);
        // RMS: the average level reaches the target.
        c = {sine(48000, 0.1)};
        QVERIFY(processAudioSuite(QStringLiteral("suite.normalize"),
                                  {{QStringLiteral("level"), -12}, {QStringLiteral("calibration"), 1}}, c));
        QVERIFY(std::abs(db(rms(c[0])) - -12) < 1e-3);
        // Silence stays silent.
        c = {std::vector<float>(100, 0.0f)};
        QVERIFY(processAudioSuite(QStringLiteral("suite.normalize"), {}, c));
        QCOMPARE(peak(c[0]), 0.0);
    }

    void signalGenerator() {
        for (int signal = 0; signal < 6; ++signal) {
            std::vector<std::vector<float>> c{std::vector<float>(48000, 0.7f)};
            QVERIFY(processAudioSuite(QStringLiteral("suite.signal"),
                                      {{QStringLiteral("signal"), signal}, {QStringLiteral("level"), -6}}, c));
            QVERIFY2(std::abs(db(peak(c[0])) - -6) < 1e-3, qPrintable(QString::number(signal)));
            c = {std::vector<float>(48000, 0.0f)};
            QVERIFY(processAudioSuite(QStringLiteral("suite.signal"),
                                      {{QStringLiteral("signal"), signal}, {QStringLiteral("level"), -20}, {QStringLiteral("calibration"), 1}}, c));
            QVERIFY(std::abs(db(rms(c[0])) - -20) < 1e-3);
        }
        // A 1 kHz sine at 48 kHz repeats every 48 samples, and the noise is the same each time.
        std::vector<std::vector<float>> c{std::vector<float>(480)};
        QVERIFY(processAudioSuite(QStringLiteral("suite.signal"), {}, c));
        QVERIFY(std::abs(static_cast<double>(c[0][5] - c[0][53])) < 1e-5);
        std::vector<std::vector<float>> n1{std::vector<float>(480)}, n2{std::vector<float>(480)};
        QVERIFY(processAudioSuite(QStringLiteral("suite.signal"), {{QStringLiteral("signal"), 4}}, n1));
        QVERIFY(processAudioSuite(QStringLiteral("suite.signal"), {{QStringLiteral("signal"), 4}}, n2));
        QCOMPARE(n1, n2);
    }

    // An audio effect offline is the effect as it plays, sample for sample.
    void effectOffline() {
        const std::vector<float> in = sine(10000, 0.8);
        const QMap<QString, double> params{{QStringLiteral("threshold"), -20}};
        std::vector<std::vector<float>> c{in};
        QVERIFY(processAudioSuite(QStringLiteral("builtin.compressor"), params, c));
        auto fx = makeAudioEffect(QStringLiteral("builtin.compressor"));
        QVERIFY(fx);
        fx->setAll(params);
        std::vector<float> live = in;
        for (std::size_t at = 0; at < live.size(); at += 1024) {
            fx->process(live.data() + at, static_cast<int>(std::min<std::size_t>(1024, live.size() - at)));
        }
        QCOMPARE(c[0], live);
        QVERIFY(c[0] != in); // and it did something
    }

    void onSegments() {
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment s;
        s.clipId = QStringLiteral("c1");
        s.name = QStringLiteral("Tone");
        s.sourceTrack = QStringLiteral("A1");
        s.length = 25;
        seq.splice(QStringLiteral("A1"), 0, {model::Segment::filler(10), s, s});
        model::AudioSuiteEffect e = suite("suite.gain", "Gain", {{QStringLiteral("gain"), -6}});
        // Frames 20 to 40 touch both segments; filler is left alone.
        QCOMPARE(model::setAudioSuite(seq, QStringLiteral("A1"), 0, 40, e), 2);
        const model::Track* t = seq.track(QStringLiteral("A1"));
        QVERIFY(!t->segments.at(0).audioSuite);
        QVERIFY(model::needsRender(t->segments.at(1)));
        QVERIFY(!model::isRendered(t->segments.at(1)));
        QCOMPARE(model::setAudioSuite(seq, QStringLiteral("A1"), 0, 40, e), 0);

        // A render holds while the segment and the values stay; the clip gain, a trim or new values
        // lose it; automation and EQ do not (p.700).
        model::Segment r = t->segments.at(1);
        r.audioSuite->rendered = QStringLiteral("/media/r.mxf");
        r.audioSuite->renderedKey = model::audioSuiteKey(r);
        QVERIFY(model::isRendered(r));
        model::Segment changed = r;
        changed.volumeKeys = {{0, -3}};
        changed.eq = model::AudioEq{};
        changed.pan = 50;
        QVERIFY(model::isRendered(changed));
        changed = r;
        changed.gain = -3;
        QVERIFY(!model::isRendered(changed));
        changed = r;
        changed.length = 20;
        QVERIFY(!model::isRendered(changed));
        changed = r;
        changed.audioSuite->params[QStringLiteral("gain")] = -5;
        QVERIFY(model::needsRender(changed));

        // Setting the same values again keeps a render; other values drop it.
        seq.track(QStringLiteral("A1"))->segments[1] = r;
        QCOMPARE(model::setAudioSuite(seq, QStringLiteral("A1"), 10, 11, e), 0);
        QVERIFY(model::isRendered(seq.track(QStringLiteral("A1"))->segments.at(1)));
        e.params[QStringLiteral("gain")] = -9;
        QCOMPARE(model::setAudioSuite(seq, QStringLiteral("A1"), 10, 11, e), 1);
        QVERIFY(model::needsRender(seq.track(QStringLiteral("A1"))->segments.at(1)));
        QCOMPARE(model::setAudioSuite(seq, QStringLiteral("A1"), 0, 100, std::nullopt), 2);

        // In sequences, render and all.
        seq.track(QStringLiteral("A1"))->segments[1] = r;
        model::BinItem item;
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        seq.storeIn(item);
        const model::Sequence back = model::Sequence::fromItem(item);
        QCOMPARE(back.track(QStringLiteral("A1"))->segments.at(1).audioSuite, r.audioSuite);
        QVERIFY(model::isRendered(back.track(QStringLiteral("A1"))->segments.at(1)));
        // A locked track is left alone.
        seq.track(QStringLiteral("A1"))->locked = true;
        QCOMPARE(model::setAudioSuite(seq, QStringLiteral("A1"), 0, 100, std::nullopt), 0);
    }

    // Render Effect: the processed sound becomes managed media that plays in the segment's place.
    void render() {
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("tone.wav"));
        QVERIFY(writeTone(wav, 4 * kRate));
        LinkOptions o;
        o.editRate = Rational(25);
        const model::BinItem clip = linkFiles({wav}, o).first();
        auto lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip.id) return clip;
            return std::nullopt;
        };
        model::Segment s;
        s.clipId = clip.id;
        s.name = clip.name;
        s.sourceTrack = QStringLiteral("A1");
        s.sourceIn = 10;
        s.length = 20;
        s.gain = -6;
        s.pan = -100; // hard left: the left channel is the piece itself, at the pan's full gain
        s.audioSuite = suite("suite.invert", "Invert");

        // The source as the render sees it: from the source frame, at the clip gain.
        const std::vector<float> source = segmentSource(s, clip, Rational(25));
        QCOMPARE(source.size(), std::size_t{21 * 1920});
        const double g = std::pow(10.0, -6.0 / 20.0);
        for (std::size_t i = 0; i < source.size(); i += 333) {
            QVERIFY(std::abs(static_cast<double>(source[i]) - tone(static_cast<std::int64_t>(i) + 10 * 1920) * g) < 1e-6);
        }

        const QString drive = dir.filePath(QStringLiteral("Drive"));
        QVERIFY(QDir().mkpath(drive));
        MediaCreation creation;
        creation.videoDrive = drive;
        creation.bitDepth = 24;
        QString error;
        const auto e = renderAudioSuite(s, clip, Rational(25), creation, &error);
        QVERIFY2(e, qPrintable(error));
        QVERIFY(QFileInfo::exists(e->rendered));
        QVERIFY(e->rendered.startsWith(drive));
        s.audioSuite = e;
        QVERIFY(model::isRendered(s));

        // Played in a sequence at frame 5: the inverted, attenuated tone, with the clip gain not
        // applied again.
        model::Sequence seq = model::Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("A1"), 0, {model::Segment::filler(5), s});
        model::BinItem item;
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        seq.storeIn(item);
        auto renderMix = [&](const model::BinItem& it) {
            AudioMix mix(audioPieces(it, lookup));
            std::vector<float> out(2 * 25 * 1920);
            mix.render(0, 25 * 1920, out.data());
            return out;
        };
        std::vector<float> out = renderMix(item);
        for (std::int64_t i = 5 * 1920; i < 25 * 1920; i += 211) {
            const double want = -tone(i - 5 * 1920 + 10 * 1920) * g;
            QVERIFY2(std::abs(static_cast<double>(out[static_cast<std::size_t>(2 * i)]) - want) < 1e-5,
                     qPrintable(QString::number(i)));
            QCOMPARE(out[static_cast<std::size_t>(2 * i + 1)], 0.0f);
        }

        // Out of date (the clip gain changed): the source plays, unprocessed, at the new gain.
        seq.track(QStringLiteral("A1"))->segments[1].gain = 0;
        seq.storeIn(item);
        out = renderMix(item);
        const std::int64_t i = 7 * 1920 + 17;
        QVERIFY(std::abs(static_cast<double>(out[static_cast<std::size_t>(2 * i)]) - tone(i - 5 * 1920 + 10 * 1920)) < 1e-5);

        // Without the effect's process installed, rendering fails and says so.
        s.audioSuite->effect = QStringLiteral("suite.nothing");
        QVERIFY(!renderAudioSuite(s, clip, Rational(25), creation, &error));
        QVERIFY(error.contains(QLatin1String("suite.nothing")));
    }

    // Templates (p.804): as bin items and JSON, and the names Media Composer gives (p.803-804).
    void templates() {
        model::AudioSuiteTemplate t;
        t.effect = suite("suite.normalize", "Normalize", {{QStringLiteral("level"), -3}});
        t.effect.rendered = QStringLiteral("/left/out");
        t.sourceBin = QStringLiteral("bin1");
        t.sourceClip = QStringLiteral("clip1");
        t.tracks = {0, 2};
        t.in = 10;
        t.out = 40;
        t.handleSeconds = 1.5;
        const QDateTime when = QDateTime::currentDateTimeUtc();
        const model::BinItem item = model::audioSuiteTemplateItem(t, QStringLiteral("Tone - Audio Processing Effect: Normalize"), when);
        QCOMPARE(item.kind, model::BinItemKind::Effect);
        const auto back = model::audioSuiteTemplateOf(item);
        QVERIFY(back);
        QVERIFY(back->effect.rendered.isEmpty()); // the render is not part of a template
        model::AudioSuiteTemplate want = t;
        want.effect.rendered.clear();
        QCOMPARE(*back, want);
        // A template from the Timeline has no source.
        model::AudioSuiteTemplate bare;
        bare.effect = suite("suite.invert", "Invert");
        const auto b = model::audioSuiteTemplateFromJson(model::audioSuiteTemplateToJson(bare));
        QVERIFY(b && !b->hasSource());
        QVERIFY(!model::audioSuiteTemplateOf(model::BinItem{}));
        QVERIFY(!model::audioSuiteTemplateFromJson({}));
        QCOMPARE(model::audioSuiteClipName(QStringLiteral("Test Audio clip"), QStringLiteral("Normalize"), false),
                 QStringLiteral("Test Audio clip_Normalize"));
        QCOMPARE(model::audioSuiteClipName(QStringLiteral("QuietClip"), QStringLiteral("Normalize"), true), QStringLiteral("QuietClip.Normalize"));
        QCOMPARE(model::audioSuiteTemplateName(QStringLiteral("Test Audio clip"), QStringLiteral("Normalize"), false),
                 QStringLiteral("Test Audio clip - Audio Processing Effect: Normalize"));
        QCOMPARE(model::audioSuiteTemplateName(QStringLiteral("QuietClip"), QStringLiteral("Normalize"), true),
                 QStringLiteral("QuietClip.Normalize.effect"));
    }

    // Creating new master clips (p.800-804): tracks chosen, the marks, a handle, several tracks
    // processed together; Duplicate.
    void newMasterClip() {
        QTemporaryDir dir;
        // Two channels: 0.5 and 0.1 sines.
        const QString wav = dir.filePath(QStringLiteral("two.wav"));
        {
            WriterSettings ws;
            ws.format = QStringLiteral("wav");
            ws.audioCodec = QStringLiteral("pcm_f32le");
            ws.channels = 2;
            MediaWriter w;
            QVERIFY(w.open(wav, ws));
            std::vector<float> v(2 * 2 * kRate);
            const std::vector<float> a = sine(2 * kRate, 0.5);
            const std::vector<float> b = sine(2 * kRate, 0.1);
            for (std::size_t i = 0; i < a.size(); ++i) {
                v[2 * i] = a[i];
                v[2 * i + 1] = b[i];
            }
            QVERIFY(w.writeAudio(v.data(), 2 * kRate) && w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        model::BinItem clip = linkFiles({wav}, o).first();
        clip.fields.insert(QStringLiteral("Start"), QStringLiteral("01:00:00:00"));
        QCOMPARE(clip.durationFrames, std::int64_t{50});
        const QString drive = dir.filePath(QStringLiteral("Drive"));
        QVERIFY(QDir().mkpath(drive));
        MediaCreation creation;
        creation.videoDrive = drive;
        creation.bitDepth = 24;

        // Both tracks, frames 10-29, Normalize on all tracks together, half a second of handle.
        AudioSuiteClipRequest r;
        r.clip = clip;
        r.tracks = {0, 1};
        r.in = 10;
        r.out = 29;
        r.handleSeconds = 0.5;
        r.effect = QStringLiteral("suite.normalize");
        r.params = {{QStringLiteral("level"), -1}};
        r.name = QStringLiteral("two_Normalize");
        QString error;
        const auto made = audioSuiteClip(r, creation, &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(made->name, QStringLiteral("two_Normalize"));
        QCOMPARE(made->durationFrames, std::int64_t{20 + 13}); // 0.5 s at 25 fps rounds up to 13 frames
        QCOMPARE(made->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:10"));
        const auto link = linkOf(*made);
        QVERIFY(link);
        QCOMPARE(link->audioPaths.size(), 2);
        AudioReader first, second;
        QVERIFY(first.open(link->audioPaths.at(0)) && second.open(link->audioPaths.at(1)));
        const std::vector<float> a = first.samples(0, 20 * 1920);
        const std::vector<float> b = second.samples(0, 20 * 1920);
        // The louder track reaches the level and the other keeps its share (p.803).
        QVERIFY(std::abs(db(peak(a)) - -1) < 0.01);
        QVERIFY(std::abs(peak(b) / peak(a) - 0.2) < 0.01);
        // From the clip's frame 10: its sample 19200.
        QVERIFY(std::abs(static_cast<double>(a[7]) - 0.5 * std::sin(2 * std::numbers::pi * 440 * (19200 + 7) / kRate) * dbToGainOf(-1) / 0.5) < 1e-3);
        // The handle is silent.
        const std::vector<float> tail = first.samples(25 * 1920, 1920);
        QCOMPARE(peak(tail), 0.0);

        // One track alone, the whole clip, Duplicate: the same sound.
        r.tracks = {1};
        r.in = 0;
        r.out = -1;
        r.handleSeconds = 0;
        r.effect = QStringLiteral("suite.duplicate");
        r.params = {};
        r.name = QStringLiteral("two_Duplicate");
        const auto dup = audioSuiteClip(r, creation, &error);
        QVERIFY2(dup, qPrintable(error));
        QCOMPARE(dup->durationFrames, std::int64_t{50});
        QCOMPARE(linkOf(*dup)->audioPaths.size(), 1);
        QVERIFY(first.open(linkOf(*dup)->audioPaths.first()));
        const std::vector<float> d = first.samples(1000, 10);
        for (std::size_t i = 0; i < d.size(); ++i) {
            QVERIFY(std::abs(static_cast<double>(d[i]) - 0.1 * std::sin(2 * std::numbers::pi * 440 * static_cast<double>(1000 + i) / kRate)) < 1e-4);
        }
        QVERIFY(findAudioSuitePlugin(QStringLiteral("suite.duplicate"))->masterClipOnly);

        // No tracks: refused.
        r.tracks = {};
        QVERIFY(!audioSuiteClip(r, creation, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAudioSuite)
#include "tst_audio_suite.moc"
