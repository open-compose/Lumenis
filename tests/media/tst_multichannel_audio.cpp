// Multichannel tracks as they play (media/playback.h; Editing Guide p.627, p.687-688;
// docs/spec/multichannel.md): a stereo track's channels left and right, a 5.1 track's channels placed
// for stereo speakers as the guide's table has them, and each channel of a multichannel track through
// its own copy of the track's effects.
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/multichannel.h"
#include "lumenis/model/sequence.h"

#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::media;

namespace {

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

} // namespace

class TestMultichannelAudio : public QObject {
    Q_OBJECT

private:
    model::BinItem clip_;
    QTemporaryDir dir_;

    model::BinItem sequenceOf(model::AudioFormat format, const QString& first, bool invert = false) {
        model::Sequence seq;
        model::Track* t = seq.ensureTrack(QStringLiteral("A1"), format);
        model::Segment s;
        s.clipId = clip_.id;
        s.name = clip_.name;
        s.sourceTrack = first;
        s.length = 10;
        t->segments = {s};
        if (invert) t->inserts[0] = model::TrackInsert{QStringLiteral("builtin.trim"), QStringLiteral("Trim"), {{QStringLiteral("polarity"), 1}}, false};
        model::BinItem item;
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        seq.storeIn(item);
        return item;
    }

    std::pair<double, double> play(const model::BinItem& item) {
        auto lookup = [this](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip_.id) return clip_;
            return std::nullopt;
        };
        AudioMix mix(audioPieces(item, lookup), trackInserts(item));
        std::vector<float> out(2 * 2000);
        mix.render(0, 2000, out.data());
        return {static_cast<double>(out[2 * 1000]), static_cast<double>(out[2 * 1000 + 1])};
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

    // "Stereo tracks include channels for the left and right speakers" (p.627).
    void stereo() {
        const auto [l, r] = play(sequenceOf(model::AudioFormat::Stereo, QStringLiteral("A3")));
        QVERIFY(std::abs(l - value(2)) < 1e-6);
        QVERIFY(std::abs(r - value(3)) < 1e-6);
        // Two pieces, the second under its own key.
        const auto pieces = audioPieces(sequenceOf(model::AudioFormat::Stereo, QStringLiteral("A1")),
                                        [this](const QString&, const QString&) { return std::optional(clip_); });
        QCOMPARE(pieces.size(), std::size_t{2});
        QCOMPARE(pieces.at(1).track, channelTrackKey(QStringLiteral("A1"), 1));
    }

    // A 5.1 track on stereo speakers (p.687-688): L and R to their sides, C and LFE in the centre, the
    // surrounds to their sides 3 dB down (Pro Tools order: L C R Ls Rs Lfe).
    void surroundOnStereo() {
        const auto [l, r] = play(sequenceOf(model::AudioFormat::Surround51, QStringLiteral("A1")));
        const double centre = std::cos(3.14159265358979 / 4);
        const double down = centre; // 3 dB down, as an equal-power pan to the middle has it
        const double wantL = value(0) + centre * value(1) + down * value(3) + centre * value(5);
        const double wantR = value(2) + centre * value(1) + down * value(4) + centre * value(5);
        QVERIFY2(std::abs(l - wantL) < 1e-5, qPrintable(QStringLiteral("%1 %2").arg(l).arg(wantL)));
        QVERIFY(std::abs(r - wantR) < 1e-5);
        // 7.1 (L C R Lss Rss Lsr Rsr Lfe): both surround pairs to their sides, 3 dB down.
        const auto [l7, r7] = play(sequenceOf(model::AudioFormat::Surround71, QStringLiteral("A1")));
        QVERIFY(std::abs(l7 - (value(0) + centre * value(1) + down * (value(3) + value(5)) + centre * value(7))) < 1e-5);
        QVERIFY(std::abs(r7 - (value(2) + centre * value(1) + down * (value(4) + value(6)) + centre * value(7))) < 1e-5);
    }

    // Each channel through its own effects: inverting a stereo track keeps left and right apart.
    void insertsPerChannel() {
        const model::BinItem item = sequenceOf(model::AudioFormat::Stereo, QStringLiteral("A1"), true);
        const TrackInserts inserts = trackInserts(item);
        QCOMPARE(inserts.size(), std::size_t{2});
        QVERIFY(inserts.count(QStringLiteral("A1")) && inserts.count(channelTrackKey(QStringLiteral("A1"), 1)));
        const auto [l, r] = play(item);
        QVERIFY2(std::abs(l + value(0)) < 1e-5, qPrintable(QString::number(l)));
        QVERIFY(std::abs(r + value(1)) < 1e-5);
    }
};

QTEST_GUILESS_MAIN(TestMultichannelAudio)
#include "tst_multichannel_audio.moc"
