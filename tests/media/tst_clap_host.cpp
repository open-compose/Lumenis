// The CLAP host (lumenis/media/clap_host.h; docs/spec/clap-host.md), with a CLAP file of our own
// (clap_test_plugin.cpp, built as cutthulhu-test.clap): finding files and bundles, reading the
// plug-ins, leaving out what does not fit, registering them as audio effects, and their sound alone
// and as Audio Track Effect inserts.
#include "lumenis/media/audio_fx.h"
#include "lumenis/media/clap_host.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <numbers>

using namespace lumenis;
using namespace lumenis::media;

namespace {

const QString kPlugin = QStringLiteral(LUMENIS_TEST_CLAP);

const ClapPluginInfo* byId(const QList<ClapPluginInfo>& list, const char* id) {
    for (const ClapPluginInfo& p : list) {
        if (p.id == QLatin1String(id)) return &p;
    }
    return nullptr;
}

std::vector<float> tone(int count) {
    std::vector<float> v(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) v[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(2 * std::numbers::pi * 440 * i / 48000.0));
    return v;
}

} // namespace

class TestClapHost : public QObject {
    Q_OBJECT

private slots:
    void findAndScan() {
        QTemporaryDir dir;
        // A plain file, a macOS-style bundle (Bundle.clap/Contents/MacOS/Bundle), a file that is not a
        // library, and a folder without plug-ins.
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("one"))));
        QVERIFY(QFile::copy(kPlugin, dir.filePath(QStringLiteral("one/Plain.clap"))));
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("two/Bundle.clap/Contents/MacOS"))));
        QVERIFY(QFile::copy(kPlugin, dir.filePath(QStringLiteral("two/Bundle.clap/Contents/MacOS/Bundle"))));
        {
            QFile bad(dir.filePath(QStringLiteral("two/Broken.clap")));
            QVERIFY(bad.open(QIODevice::WriteOnly));
            bad.write("not a library");
        }
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("three"))));
        const QStringList files = findClapFiles({dir.path()});
        QCOMPARE(files.size(), 3);
        QVERIFY(files.at(0).endsWith(QLatin1String("one/Plain.clap")));
        QVERIFY(files.at(1).endsWith(QLatin1String("two/Broken.clap")));
        QVERIFY(files.at(2).endsWith(QLatin1String("two/Bundle.clap")));

        QStringList errors;
        const QList<ClapPluginInfo> plugins = scanClapPlugins(files, &errors);
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.first().contains(QLatin1String("Broken.clap")));
        QCOMPARE(plugins.size(), 6); // three from each library
        const ClapPluginInfo* gain = byId(plugins, "org.cutthulhu.test.gain");
        QVERIFY(gain);
        QCOMPARE(gain->name, QStringLiteral("Test Gain"));
        QCOMPARE(gain->vendor, QStringLiteral("Cutthulhu Tests"));
        QCOMPARE(gain->version, QStringLiteral("1.0"));
        QCOMPARE(gain->features, (QStringList{"audio-effect", "utility"}));
        QCOMPARE(gain->inputChannels, 1);
        QCOMPARE(gain->outputChannels, 1);
        QVERIFY(gain->usable());
        QCOMPARE(gain->category(), QStringLiteral("CLAP"));
        QCOMPARE(gain->params.size(), 2);
        QCOMPARE(gain->params.at(0).id, QStringLiteral("7"));
        QCOMPARE(gain->params.at(0).name, QStringLiteral("Gain"));
        QCOMPARE(gain->params.at(0).min, -60.0);
        QCOMPARE(gain->params.at(1).steps, 2);
        QCOMPARE(gain->params.at(1).choices, (QStringList{"Off", "On"}));
        const ClapPluginInfo* stereo = byId(plugins, "org.cutthulhu.test.stereo");
        QCOMPARE(stereo->inputChannels, 2);
        QCOMPARE(stereo->category(), QStringLiteral("Delay"));
        QVERIFY(stereo->usable());
        const ClapPluginInfo* synth = byId(plugins, "org.cutthulhu.test.synth");
        QVERIFY(!synth->usable());
        QCOMPARE(synth->whyNot(), QStringLiteral("not an audio effect"));

        // CLAP_PATH adds folders to the search.
        qputenv("CLAP_PATH", QFile::encodeName(dir.filePath(QStringLiteral("two"))));
        QVERIFY(clapSearchPaths().contains(QDir::cleanPath(dir.filePath(QStringLiteral("two")))));
        qunsetenv("CLAP_PATH");
    }

    // Registered, they are audio effects; the instrument is left out, and a second registration of
    // the same plug-in is ignored.
    void registerAndProcess() {
        const QList<ClapPluginInfo> plugins = scanClapPlugins({kPlugin});
        QCOMPARE(registerClapPlugins(plugins), 2);
        QCOMPARE(registerClapPlugins(plugins), 0);
        QVERIFY(!clapPlugins().isEmpty());
        const AudioEffectInfo* info = findAudioEffect(QStringLiteral("clap:org.cutthulhu.test.gain"));
        QVERIFY(info);
        QCOMPARE(info->name, QStringLiteral("Test Gain"));
        QCOMPARE(info->vendor, QStringLiteral("Cutthulhu Tests"));
        QVERIFY(!findAudioEffect(QStringLiteral("clap:org.cutthulhu.test.synth")));

        const std::vector<float> in = tone(5000); // more than one of the host's blocks
        auto gain = makeAudioEffect(QStringLiteral("clap:org.cutthulhu.test.gain"));
        QVERIFY(gain);
        QCOMPARE(gain->tail(), 4800);
        std::vector<float> out = in;
        gain->process(out.data(), static_cast<int>(out.size()));
        QCOMPARE(out, in);
        gain->set(QStringLiteral("7"), -6.0206);
        out = in;
        gain->process(out.data(), static_cast<int>(out.size()));
        for (std::size_t i = 0; i < in.size(); i += 97) QVERIFY(std::abs(static_cast<double>(out[i] - in[i] * 0.5f)) < 1e-5);
        gain->setAll({{QStringLiteral("7"), 0}, {QStringLiteral("9"), 1}});
        out = in;
        gain->process(out.data(), static_cast<int>(out.size()));
        QCOMPARE(out[100], -in[100]);
        // Out of range values are clamped by the host.
        gain->set(QStringLiteral("7"), 100);
        gain->set(QStringLiteral("9"), 0);
        out = in;
        gain->process(out.data(), static_cast<int>(out.size()));
        QVERIFY(std::abs(static_cast<double>(out[100]) - static_cast<double>(in[100]) * std::pow(10.0, 12 / 20.0)) < 1e-5);

        // Stereo: the track on both inputs, the outputs averaged.
        auto stereo = makeAudioEffect(QStringLiteral("clap:org.cutthulhu.test.stereo"));
        QVERIFY(stereo);
        QCOMPARE(stereo->tail(), 48000); // ours when a plug-in does not say
        stereo->set(QStringLiteral("1"), 0.5);
        out = in;
        stereo->process(out.data(), static_cast<int>(out.size()));
        QVERIFY(std::abs(static_cast<double>(out[100] - in[100] * 0.5f)) < 1e-6);
        stereo->reset();
    }

    // As an Audio Track Effect insert in the mix.
    void insert() {
        registerClapPlugins(scanClapPlugins({kPlugin}));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("tone.wav"));
        {
            WriterSettings ws;
            ws.format = QStringLiteral("wav");
            ws.audioCodec = QStringLiteral("pcm_f32le");
            ws.channels = 1;
            MediaWriter w;
            QVERIFY(w.open(path, ws));
            const std::vector<float> v = tone(48000);
            QVERIFY(w.writeAudio(v.data(), 48000) && w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        const model::BinItem clip = linkFiles({path}, o).first();
        auto lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip.id) return clip;
            return std::nullopt;
        };
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment s;
        s.clipId = clip.id;
        s.sourceTrack = QStringLiteral("A1");
        s.length = 25;
        s.pan = -100;
        seq.splice(QStringLiteral("A1"), 0, {s});
        model::BinItem item;
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        seq.storeIn(item);
        auto render = [&] {
            AudioMix mix(audioPieces(item, lookup), trackInserts(item));
            std::vector<float> stereo(2 * 48000);
            mix.render(0, 48000, stereo.data());
            return stereo;
        };
        const std::vector<float> dry = render();
        seq.track(QStringLiteral("A1"))->inserts[0] =
            model::TrackInsert{QStringLiteral("clap:org.cutthulhu.test.gain"), QStringLiteral("Test Gain"), {{QStringLiteral("9"), 1}}, false};
        seq.storeIn(item);
        const std::vector<float> wet = render();
        for (std::size_t i = 0; i < dry.size(); i += 2 * 101) QVERIFY(std::abs(static_cast<double>(wet[i] + dry[i])) < 1e-6);
    }

    void provider() {
        ClapHostProvider p;
        QCOMPARE(p.info().id, std::string("org.cleveraudio.clap-host"));
        QCOMPARE(p.standard(), ext::PluginStandard::CLAP);
        const std::vector<ext::PluginDescriptor> found = p.scan({std::filesystem::path(QFileInfo(kPlugin).absolutePath().toStdString())});
        QVERIFY(found.size() >= 3);
        bool gain = false;
        for (const auto& d : found) gain = gain || d.id == "org.cutthulhu.test.gain";
        QVERIFY(gain);
    }
};

QTEST_GUILESS_MAIN(TestClapHost)
#include "tst_clap_host.moc"
