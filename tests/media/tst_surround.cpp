// Surround mixing (media/surround.h, media/playback.h; Editing Guide p.686-692, p.732;
// docs/spec/surround.md): the folds from a sequence's mix format to the speakers listening, a
// multichannel track's channels on their speakers in a surround sequence, every output order of the
// Mix Mode Selection button, and Stereo, 5.1 and 7.1 Audio Mixdowns.
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/mixdown.h"
#include "lumenis/media/playback.h"
#include "lumenis/media/surround.h"
#include "lumenis/model/multichannel.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::media;
using model::AudioFormat;

namespace {

constexpr double kHalf = 0.70710678;

// Channel k holds the constant (k + 1) / 20.
bool writeChannels(const QString& path, int channels) {
    WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_f32le");
    s.channels = channels;
    MediaWriter w;
    if (!w.open(path, s)) return false;
    std::vector<float> v(static_cast<std::size_t>(channels * 48000));
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<float>(static_cast<int>(i % static_cast<std::size_t>(channels)) + 1) / 20.0f;
    return w.writeAudio(v.data(), 48000) && w.finish();
}

double value(int channel) {
    return (channel + 1) / 20.0;
}

double gain(const std::vector<SpeakerGains>& m, int out, Speaker s) {
    return static_cast<double>(m.at(static_cast<std::size_t>(out))[static_cast<std::size_t>(s)]);
}

// The output channels' speakers that take anything at all.
int heard(const std::vector<SpeakerGains>& m, int out) {
    int n = 0;
    for (float g : m.at(static_cast<std::size_t>(out))) n += g != 0.0f ? 1 : 0;
    return n;
}

} // namespace

class TestSurround : public QObject {
    Q_OBJECT

private:
    model::BinItem clip_;
    QTemporaryDir dir_;

    // A sequence mixed in `mix` with one track of `format` from channel `first` of the clip; a mono
    // track gets `pan`.
    model::BinItem sequenceOf(AudioFormat mix, AudioFormat format, const QString& first, int pan = 0) {
        model::Sequence seq;
        seq.setMixFormat(mix);
        model::Track* t = seq.ensureTrack(QStringLiteral("A1"), format);
        model::Segment s;
        s.clipId = clip_.id;
        s.name = clip_.name;
        s.sourceTrack = first;
        s.length = 10;
        s.pan = pan;
        t->segments = {s};
        model::BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Surround");
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        seq.storeIn(item);
        return item;
    }

    ClipLookup lookup() {
        return [this](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip_.id) return clip_;
            return std::nullopt;
        };
    }

    // One sample of each output channel, from the middle of the first segment.
    std::vector<double> play(const model::BinItem& item, MonitorMix monitor) {
        AudioMix mix(audioPieces(item, lookup()), trackInserts(item));
        mix.setOutput(model::Sequence::fromItem(item).mixFormat(), monitor);
        const int ch = mix.channels();
        std::vector<float> out(static_cast<std::size_t>(ch) * 2000);
        mix.render(0, 2000, out.data());
        std::vector<double> got;
        for (int c = 0; c < ch; ++c) got.push_back(static_cast<double>(out[static_cast<std::size_t>(ch * 1000 + c)]));
        return got;
    }

    static void near(const std::vector<double>& got, const std::vector<double>& want) {
        QCOMPARE(got.size(), want.size());
        for (std::size_t c = 0; c < got.size(); ++c) {
            QVERIFY2(std::abs(got[c] - want[c]) < 1e-5,
                     qPrintable(QStringLiteral("channel %1: %2, not %3").arg(c).arg(got[c]).arg(want[c])));
        }
    }

private slots:
    void initTestCase() {
        const QString wav = dir_.filePath(QStringLiteral("eight.wav"));
        QVERIFY(writeChannels(wav, 8));
        LinkOptions o;
        o.editRate = Rational(25);
        clip_ = linkFiles({wav}, o).first();
        QCOMPARE(model::audioChannelsOf(clip_), 8);
    }

    // The Sequence Mix Format is kept with the sequence; Mono is not one (p.686).
    void sequenceFormat() {
        model::Sequence s;
        QCOMPARE(s.mixFormat(), AudioFormat::Stereo);
        QVERIFY(!s.toJson().contains(QStringLiteral("mix")));
        s.setMixFormat(AudioFormat::Surround71);
        QCOMPARE(model::Sequence::fromJson(s.toJson()).mixFormat(), AudioFormat::Surround71);
        QCOMPARE(s.excerpt({}, 0, 0).mixFormat(), AudioFormat::Surround71);
        s.setMixFormat(AudioFormat::Mono);
        QCOMPARE(s.mixFormat(), AudioFormat::Stereo);
    }

    // The menu's mixes and their channel orders (p.689, p.691-692).
    void monitorMixes() {
        QCOMPARE(media::monitorMixes().size(), 10);
        for (MonitorMix m : media::monitorMixes()) {
            QCOMPARE(monitorMixFromId(monitorMixId(m)), std::optional(m));
            QCOMPARE(static_cast<int>(outputMatrix(AudioFormat::Surround71, m).size()), monitorChannels(m));
        }
        QVERIFY(!monitorMixFromId(QStringLiteral("direct")));
        QCOMPARE(monitorChannelNames(MonitorMix::ProTools51).join(QLatin1Char(' ')), QStringLiteral("L C R Ls Rs Lfe"));
        QCOMPARE(monitorChannelNames(MonitorMix::Smpte51).join(QLatin1Char(' ')), QStringLiteral("L R C Lfe Ls Rs"));
        QCOMPARE(monitorChannelNames(MonitorMix::ProTools71).join(QLatin1Char(' ')), QStringLiteral("L C R Lss Rss Lsr Rsr Lfe"));
        QCOMPARE(monitorChannelNames(MonitorMix::Ext71).join(QLatin1Char(' ')), QStringLiteral("L R C Lfe Lsr Rsr Lss Rss"));
        QCOMPARE(monitorChannelNames(MonitorMix::SmpteDs71).join(QLatin1Char(' ')), QStringLiteral("L R C Lfe Lss Rss Lsr Rsr"));
        QCOMPARE(monitorMixName(MonitorMix::Ext71), QStringLiteral("7.1 EXT: L R C Lfe Lsr Rsr Lss Rss"));
    }

    // The table on p.687-688, speaker by speaker.
    void downmixTable() {
        // A stereo sequence on stereo speakers: L and R alone.
        auto m = outputMatrix(AudioFormat::Stereo, MonitorMix::Stereo);
        QCOMPARE(gain(m, 0, Speaker::L), 1.0);
        QCOMPARE(gain(m, 1, Speaker::R), 1.0);
        QCOMPARE(gain(m, 0, Speaker::R), 0.0);
        // 5.1 on stereo: C and LFE in the middle, L and R to their sides, Lr and Rr to their sides
        // 3 dB down.
        m = outputMatrix(AudioFormat::Surround51, MonitorMix::Stereo);
        QCOMPARE(gain(m, 0, Speaker::L), 1.0);
        QVERIFY(std::abs(gain(m, 0, Speaker::C) - kHalf) < 1e-6 && std::abs(gain(m, 1, Speaker::C) - kHalf) < 1e-6);
        QVERIFY(std::abs(gain(m, 0, Speaker::Lfe) - kHalf) < 1e-6);
        QVERIFY(std::abs(gain(m, 0, Speaker::Lsr) - kHalf) < 1e-6 && gain(m, 1, Speaker::Lsr) == 0.0);
        // 7.1 on stereo: the side and rear surrounds to their sides 3 dB down.
        m = outputMatrix(AudioFormat::Surround71, MonitorMix::Stereo);
        QVERIFY(std::abs(gain(m, 0, Speaker::Lss) - kHalf) < 1e-6 && std::abs(gain(m, 1, Speaker::Rsr) - kHalf) < 1e-6);
        // 7.1 on 5.1: Lss and Rss in the middle of the L/Lr and R/Rr pairs.
        m = outputMatrix(AudioFormat::Surround71, MonitorMix::ProTools51); // L C R Ls Rs Lfe
        QVERIFY(std::abs(gain(m, 0, Speaker::Lss) - kHalf) < 1e-6 && std::abs(gain(m, 3, Speaker::Lss) - kHalf) < 1e-6);
        QVERIFY(std::abs(gain(m, 2, Speaker::Rss) - kHalf) < 1e-6 && std::abs(gain(m, 4, Speaker::Rss) - kHalf) < 1e-6);
        QCOMPARE(gain(m, 1, Speaker::C), 1.0);
        QCOMPARE(gain(m, 5, Speaker::Lfe), 1.0);
        // 5.1 on 7.1: Lss and Rss are not used.
        m = outputMatrix(AudioFormat::Surround51, MonitorMix::ProTools71); // L C R Lss Rss Lsr Rsr Lfe
        QCOMPARE(heard(m, 3), 0);
        QCOMPARE(heard(m, 4), 0);
        QCOMPARE(gain(m, 5, Speaker::Lsr), 1.0);
        // "Stereo sequences are output only to L and R" (p.691).
        m = outputMatrix(AudioFormat::Stereo, MonitorMix::Smpte51); // L R C Lfe Ls Rs
        for (int c = 2; c < 6; ++c) QCOMPARE(heard(m, c), 0);
        // Mono: every channel in the middle.
        m = outputMatrix(AudioFormat::Surround71, MonitorMix::Mono);
        QCOMPARE(m.size(), std::size_t{1});
        QCOMPARE(heard(m, 0), kSpeakers);
    }

    // The four-channel mixes (p.691).
    void fourChannels() {
        auto m = outputMatrix(AudioFormat::Surround71, MonitorMix::LrcLfe); // L R C Lfe
        QVERIFY(std::abs(gain(m, 0, Speaker::Lsr) - kHalf) < 1e-6 && std::abs(gain(m, 0, Speaker::Lss) - kHalf) < 1e-6);
        QCOMPARE(gain(m, 3, Speaker::Lfe), 1.0);
        QCOMPARE(gain(m, 0, Speaker::C), 0.0);
        m = outputMatrix(AudioFormat::Surround71, MonitorMix::Lrcs); // L R C S
        QVERIFY(std::abs(gain(m, 0, Speaker::Lfe) - kHalf) < 1e-6 && std::abs(gain(m, 1, Speaker::Lfe) - kHalf) < 1e-6);
        QVERIFY(gain(m, 3, Speaker::Lsr) > 0 && gain(m, 3, Speaker::Rsr) > 0);
        QVERIFY(gain(m, 0, Speaker::Lss) > 0 && gain(m, 3, Speaker::Lss) > 0);
        m = outputMatrix(AudioFormat::Surround71, MonitorMix::Quad); // L R Ls Rs
        QVERIFY(std::abs(gain(m, 0, Speaker::C) - kHalf) < 1e-6 && std::abs(gain(m, 1, Speaker::C) - kHalf) < 1e-6);
        QCOMPARE(gain(m, 2, Speaker::Lsr), 1.0);
        QVERIFY(gain(m, 0, Speaker::Lss) > 0 && gain(m, 2, Speaker::Lss) > 0);
    }

    // A 5.1 track in a 5.1 sequence: each channel on its speaker, in the order the monitor asks.
    void surroundTrackOnSurroundSpeakers() {
        const model::BinItem item = sequenceOf(AudioFormat::Surround51, AudioFormat::Surround51, QStringLiteral("A1"));
        // Pro Tools order is the track's own: L C R Ls Rs Lfe.
        near(play(item, MonitorMix::ProTools51), {value(0), value(1), value(2), value(3), value(4), value(5)});
        // SMPTE: L R C Lfe Ls Rs.
        near(play(item, MonitorMix::Smpte51), {value(0), value(2), value(1), value(5), value(3), value(4)});
        // On 7.1 speakers, Lss and Rss are silent: L C R Lss Rss Lsr Rsr Lfe.
        near(play(item, MonitorMix::ProTools71), {value(0), value(1), value(2), 0, 0, value(3), value(4), value(5)});
        // On stereo speakers the same as a stereo sequence plays it (p.687).
        const auto stereo = play(item, MonitorMix::Stereo);
        const auto folded = play(sequenceOf(AudioFormat::Stereo, AudioFormat::Surround51, QStringLiteral("A1")), MonitorMix::Stereo);
        near(stereo, folded);
    }

    // A 7.1 track in a 5.1 sequence has its side surrounds folded between the L/Lr and R/Rr pairs.
    void sevenOneInFiveOne() {
        const model::BinItem item = sequenceOf(AudioFormat::Surround51, AudioFormat::Surround71, QStringLiteral("A1"));
        // The track: L C R Lss Rss Lsr Rsr Lfe = channels 0-7.
        near(play(item, MonitorMix::ProTools51),
             {value(0) + kHalf * value(3), value(1), value(2) + kHalf * value(4), value(5) + kHalf * value(3),
              value(6) + kHalf * value(4), value(7)});
        // A 7.1 sequence keeps all eight (7.1 EXT: L R C Lfe Lsr Rsr Lss Rss).
        near(play(sequenceOf(AudioFormat::Surround71, AudioFormat::Surround71, QStringLiteral("A1")), MonitorMix::Ext71),
             {value(0), value(2), value(1), value(7), value(5), value(6), value(3), value(4)});
    }

    // Mono and stereo tracks still pan between L and R, whatever the speakers.
    void pannedTracks() {
        const model::BinItem item = sequenceOf(AudioFormat::Surround51, AudioFormat::Mono, QStringLiteral("A2"), -100);
        near(play(item, MonitorMix::ProTools51), {value(1), 0, 0, 0, 0, 0});
        const model::BinItem stereo = sequenceOf(AudioFormat::Surround71, AudioFormat::Stereo, QStringLiteral("A1"));
        near(play(stereo, MonitorMix::SmpteDs71), {value(0), value(1), 0, 0, 0, 0, 0, 0});
    }

    // Stereo, 5.1 and 7.1 mixdowns (p.732): one track of the format, its channels in Pro Tools order.
    void mixdowns() {
        const QString drive = dir_.filePath(QStringLiteral("Drive"));
        QVERIFY(QDir().mkpath(drive));
        MediaCreation creation;
        creation.videoDrive = drive;
        creation.bitDepth = 24;
        const model::BinItem seq71 = sequenceOf(AudioFormat::Surround71, AudioFormat::Surround71, QStringLiteral("A1"));
        struct Case {
            MixdownFormat format;
            AudioFormat track;
            std::vector<double> want;
        };
        const double l = value(0) + kHalf * (value(1) + value(3) + value(5) + value(7));
        const double r = value(2) + kHalf * (value(1) + value(4) + value(6) + value(7));
        const QList<Case> cases{
            {MixdownFormat::Stereo, AudioFormat::Stereo, {l, r}},
            {MixdownFormat::Surround51, AudioFormat::Surround51,
             {value(0) + kHalf * value(3), value(1), value(2) + kHalf * value(4), value(5) + kHalf * value(3),
              value(6) + kHalf * value(4), value(7)}},
            {MixdownFormat::Surround71, AudioFormat::Surround71,
             {value(0), value(1), value(2), value(3), value(4), value(5), value(6), value(7)}},
            {MixdownFormat::DualMono, AudioFormat::Mono, {l, r}},
        };
        for (const Case& c : cases) {
            MixdownRequest req;
            req.sequence = seq71;
            req.tracks = {QStringLiteral("A1")};
            req.start = 0;
            req.end = 10;
            req.format = c.format;
            req.clips = lookup();
            QCOMPARE(mixdownTrackFormat(c.format), c.track);
            QString error;
            const auto made = mixdownClip(req, QStringLiteral("Mix"), QStringLiteral("01:00:00:00"), creation, &error);
            QVERIFY2(made, qPrintable(error));
            const auto link = linkOf(*made);
            QVERIFY(link);
            QCOMPARE(static_cast<std::size_t>(link->audioPaths.size()), c.want.size());
            const auto groups = model::trackFormatsOf(*made);
            if (c.track == AudioFormat::Mono) {
                QVERIFY(groups.isEmpty());
            } else {
                QCOMPARE(groups, (QList<model::ChannelGroup>{{c.track, 1}}));
            }
            std::vector<double> got;
            for (const QString& path : link->audioPaths) {
                AudioReader reader;
                QVERIFY(reader.open(path));
                got.push_back(static_cast<double>(reader.samples(1000, 1).at(0)));
            }
            near(got, c.want);
        }
    }
};

QTEST_GUILESS_MAIN(TestSurround)
#include "tst_surround.moc"
