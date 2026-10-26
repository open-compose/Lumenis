#include "lumenis/media/audio_suite.h"

#include "lumenis/media/link.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/audio_suite.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>

// AudioSuite processes and rendering (audio_suite.h). Every process sees whole channels at once, so
// the ones that measure first (Normalize) or run backwards (Reverse) need nothing special.
namespace lumenis::media {

namespace {

AudioEffectParam param(const char* id, const char* name, const char* unit, double min, double max, double def) {
    AudioEffectParam p;
    p.id = QString::fromLatin1(id);
    p.name = QObject::tr(name);
    p.unit = QString::fromLatin1(unit);
    p.min = min;
    p.max = max;
    p.defaultValue = def;
    return p;
}

AudioEffectParam choice(const char* id, const char* name, const QStringList& choices, int def) {
    AudioEffectParam p = param(id, name, "", 0, static_cast<double>(choices.size() - 1), def);
    p.steps = static_cast<int>(choices.size());
    p.choices = choices;
    return p;
}

// The rms and peak buttons of Gain, Normalize and Signal Generator (p.844, p.850, p.856).
AudioEffectParam calibration() {
    return choice("calibration", "Calibration", {QObject::tr("Peak"), QObject::tr("RMS")}, 0);
}

AudioSuiteInfo process(const char* id, const char* name, QList<AudioEffectParam> params = {}) {
    AudioSuiteInfo i;
    i.id = QString::fromLatin1(id);
    i.name = QObject::tr(name);
    i.category = QObject::tr("Process");
    i.vendor = QStringLiteral("Cutthulhu");
    i.params = std::move(params);
    return i;
}

// Our ranges and defaults: the guide gives Signal Generator's (p.855); the others are ours.
const QList<AudioSuiteInfo>& builtIn() {
    static const QList<AudioSuiteInfo> list = [] {
        QList<AudioSuiteInfo> l;
        l << process("suite.gain", "Gain", {param("gain", "Gain", "dB", -96, 24, 0)});
        l << process("suite.normalize", "Normalize",
                     {param("level", "Level", "dB", -40, 0, 0), calibration(),
                      // Named as the Processing Mode Selection menu names them (p.803).
                      choice("mode", "Processing", {QObject::tr("Levels On All Chans-Tracks"), QObject::tr("Level On Each Chan-Track")}, 0)});
        l << process("suite.invert", "Invert");
        l << process("suite.reverse", "Reverse");
        l << process("suite.dc", "DC Offset Removal");
        AudioSuiteInfo gen = process("suite.signal", "Signal Generator",
                                     {param("frequency", "Frequency", "Hz", 20, 20000, 1000),
                                      param("level", "Level", "dB", -95, 0, -20),
                                      choice("signal", "Signal",
                                             {QObject::tr("Sine"), QObject::tr("Square"), QObject::tr("Sawtooth"), QObject::tr("Triangle"),
                                              QObject::tr("White Noise"), QObject::tr("Pink Noise")},
                                             0),
                                      calibration()});
        gen.generator = true;
        l << gen;
        return l;
    }();
    return list;
}

double value(const QMap<QString, double>& params, const AudioSuiteInfo& info, const char* id) {
    const QString key = QString::fromLatin1(id);
    const AudioEffectParam* p = info.param(key);
    const double v = params.value(key, p ? p->defaultValue : 0.0);
    return p ? std::clamp(v, p->min, p->max) : v;
}

double dbToGain(double db) {
    return std::pow(10.0, db / 20.0);
}

// The peak or the RMS of the given channels together.
double measure(const std::vector<std::vector<float>>& channels, bool rms) {
    double peak = 0, sum = 0;
    std::size_t n = 0;
    for (const auto& c : channels) {
        for (const float s : c) {
            const double v = static_cast<double>(s);
            peak = std::max(peak, std::abs(v));
            sum += v * v;
        }
        n += c.size();
    }
    if (rms) return n == 0 ? 0.0 : std::sqrt(sum / static_cast<double>(n));
    return peak;
}

void scale(std::vector<float>& c, double gain) {
    for (float& s : c) s = static_cast<float>(static_cast<double>(s) * gain);
}

// The waveform at unit size; the caller scales it to the level.
void generate(std::vector<float>& c, int signal, double frequency) {
    std::mt19937 random(1); // the same noise each time, so a render can be made again exactly
    std::uniform_real_distribution<double> white(-1.0, 1.0);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    const double step = frequency / kPlaybackRate;
    for (std::size_t i = 0; i < c.size(); ++i) {
        const double phase = std::fmod(static_cast<double>(i) * step, 1.0);
        double v = 0;
        switch (signal) {
        case 0: v = std::sin(2 * std::numbers::pi * phase); break;
        case 1: v = phase < 0.5 ? 1.0 : -1.0; break;
        case 2: v = 2 * phase - 1; break;
        case 3: v = phase < 0.5 ? 4 * phase - 1 : 3 - 4 * phase; break;
        case 4: v = white(random); break;
        default: {
            // Pink noise: Paul Kellet's refined filter of white noise (published for anyone's use).
            const double w = white(random);
            b0 = 0.99886 * b0 + w * 0.0555179;
            b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522;
            b5 = -0.7616 * b5 - w * 0.0168980;
            v = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
            b6 = w * 0.115926;
            break;
        }
        }
        c[i] = static_cast<float>(v);
    }
}

} // namespace

QMap<QString, double> AudioSuiteInfo::defaults() const {
    QMap<QString, double> m;
    for (const AudioEffectParam& p : params) m.insert(p.id, p.defaultValue);
    return m;
}

const AudioEffectParam* AudioSuiteInfo::param(const QString& paramId) const {
    for (const AudioEffectParam& p : params) {
        if (p.id == paramId) return &p;
    }
    return nullptr;
}

QList<AudioSuiteInfo> audioSuitePlugins() {
    QList<AudioSuiteInfo> list = builtIn();
    for (const AudioEffectInfo& e : audioEffects()) {
        AudioSuiteInfo i;
        i.id = e.id;
        i.name = e.name;
        i.category = e.category;
        i.vendor = e.vendor;
        i.params = e.params;
        i.effect = true;
        list << i;
    }
    return list;
}

std::optional<AudioSuiteInfo> findAudioSuitePlugin(const QString& id) {
    for (const AudioSuiteInfo& i : audioSuitePlugins()) {
        if (i.id == id) return i;
    }
    return std::nullopt;
}

bool processAudioSuite(const QString& id, const QMap<QString, double>& params, std::vector<std::vector<float>>& channels,
                       QString* error) {
    const auto info = findAudioSuitePlugin(id);
    if (!info) {
        if (error) *error = QObject::tr("The AudioSuite plug-in %1 is not installed.").arg(id);
        return false;
    }
    if (info->effect) {
        // Each channel through an effect of its own, in blocks as playback would.
        constexpr int kBlock = 1024;
        for (auto& c : channels) {
            auto fx = makeAudioEffect(id);
            if (!fx) {
                if (error) *error = QObject::tr("The AudioSuite plug-in %1 is not installed.").arg(info->name);
                return false;
            }
            fx->setAll(params);
            for (std::size_t at = 0; at < c.size(); at += kBlock) {
                fx->process(c.data() + at, static_cast<int>(std::min<std::size_t>(kBlock, c.size() - at)));
            }
        }
        return true;
    }
    if (id == QLatin1String("suite.gain")) {
        const double g = value(params, *info, "gain");
        for (auto& c : channels) scale(c, g <= -96 ? 0.0 : dbToGain(g));
    } else if (id == QLatin1String("suite.normalize")) {
        const double target = dbToGain(value(params, *info, "level"));
        const bool rms = value(params, *info, "calibration") >= 0.5;
        if (value(params, *info, "mode") < 0.5) {
            // "Adjusted proportionally to the one with the highest signal" (p.799).
            const double level = measure(channels, rms);
            if (level > 0) {
                for (auto& c : channels) scale(c, target / level);
            }
        } else {
            for (auto& c : channels) {
                const double level = measure({c}, rms);
                if (level > 0) scale(c, target / level);
            }
        }
    } else if (id == QLatin1String("suite.invert")) {
        for (auto& c : channels) scale(c, -1.0);
    } else if (id == QLatin1String("suite.reverse")) {
        for (auto& c : channels) std::reverse(c.begin(), c.end());
    } else if (id == QLatin1String("suite.dc")) {
        // The steady offset is the mean; taking it away leaves the sound centred on zero.
        for (auto& c : channels) {
            if (c.empty()) continue;
            double sum = 0;
            for (const float s : c) sum += static_cast<double>(s);
            const double mean = sum / static_cast<double>(c.size());
            for (float& s : c) s = static_cast<float>(static_cast<double>(s) - mean);
        }
    } else if (id == QLatin1String("suite.signal")) {
        const double level = dbToGain(value(params, *info, "level"));
        const bool rms = value(params, *info, "calibration") >= 0.5;
        for (auto& c : channels) {
            generate(c, static_cast<int>(std::lround(value(params, *info, "signal"))), value(params, *info, "frequency"));
            const double unit = measure({c}, rms);
            if (unit > 0) scale(c, level / unit);
        }
    }
    return true;
}

std::vector<float> segmentSource(const model::Segment& s, const model::BinItem& clip, const Rational& sequenceRate) {
    const std::int64_t count = framesToSamples(s.length + 1, sequenceRate);
    std::vector<float> out(static_cast<std::size_t>(std::max<std::int64_t>(count, 0)), 0.0f);
    const auto ref = model::parseTrackName(s.sourceTrack);
    if (out.empty() || !ref || ref->kind != model::TrackKind::Audio) return out;
    // The clip's track as playback reads it, alone and unpanned: full on the left, nothing right.
    const QString track = QStringLiteral("A%1").arg(ref->number);
    std::vector<AudioPiece> pieces = audioPieces(clip, {}, [&](const QString& t) { return t == track; });
    if (pieces.empty()) return out;
    AudioPiece p = pieces.front();
    p.at = 0;
    p.sourceStart += framesToSamples(s.sourceIn, clip.editRate);
    p.length = count;
    p.left = 1.0f;
    p.right = 0.0f;
    AudioMix mix({p});
    std::vector<float> stereo(2 * static_cast<std::size_t>(count));
    constexpr int kChunk = 8192;
    for (std::int64_t at = 0; at < count; at += kChunk) {
        const int n = static_cast<int>(std::min<std::int64_t>(kChunk, count - at));
        mix.render(at, n, stereo.data() + 2 * at);
    }
    const double gain = s.gain <= model::kGainOff ? 0.0 : dbToGain(s.gain);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<float>(static_cast<double>(stereo[2 * i]) * gain);
    return out;
}

std::optional<model::AudioSuiteEffect> renderAudioSuite(const model::Segment& s, const model::BinItem& clip,
                                                        const Rational& sequenceRate, const MediaCreation& creation,
                                                        QString* error) {
    if (!s.audioSuite || s.isFiller()) {
        if (error) *error = QObject::tr("The segment has no AudioSuite effect.");
        return std::nullopt;
    }
    std::vector<std::vector<float>> sound{segmentSource(s, clip, sequenceRate)};
    if (!processAudioSuite(s.audioSuite->effect, s.audioSuite->params, sound, error)) return std::nullopt;
    const std::vector<float>& made = sound.front();
    const auto rendered = renderedAudioClip(
        QStringLiteral("%1 %2").arg(s.name, s.audioSuite->name), sequenceRate, s.length + 1, 1,
        [&](std::int64_t at, int count, std::vector<std::vector<float>>& out) {
            out[0].assign(static_cast<std::size_t>(count), 0.0f);
            for (int i = 0; i < count; ++i) {
                const auto k = static_cast<std::size_t>(at + i);
                if (k < made.size()) out[0][static_cast<std::size_t>(i)] = made[k];
            }
        },
        QString(), creation, error);
    if (!rendered) return std::nullopt;
    const auto link = linkOf(*rendered);
    const auto channel = link ? link->channelOf(0) : std::nullopt;
    if (!channel) {
        if (error) *error = QObject::tr("The rendered media could not be read.");
        return std::nullopt;
    }
    model::AudioSuiteEffect e = *s.audioSuite;
    e.rendered = link->audioFile(0);
    e.renderedStream = channel->first;
    e.renderedChannel = channel->second;
    e.renderedKey = model::audioSuiteKey(s);
    return e;
}

} // namespace lumenis::media
