#include "lumenis/media/audio_suite.h"

#include "lumenis/core/timecode.h"

#include "lumenis/media/link.h"
#include "lumenis/media/playback.h"
#include "lumenis/media/time_stretch.h"
#include "lumenis/model/audio_suite.h"

#include <QObject>
#include <QtGlobal>

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
    return choice("calibration", QT_TRANSLATE_NOOP("QObject", "Calibration"), {QObject::tr("Peak"), QObject::tr("RMS")}, 0);
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

// Our ranges and defaults: the guide gives Signal Generator's (p.855); the others are ours. The names are marked for
// lupdate (QT_TRANSLATE_NOOP) and translated where param() and process() make them.
const QList<AudioSuiteInfo>& builtIn() {
    static const QList<AudioSuiteInfo> list = [] {
        QList<AudioSuiteInfo> l;
        l << process("suite.gain", QT_TRANSLATE_NOOP("QObject", "Gain"), {param("gain", QT_TRANSLATE_NOOP("QObject", "Gain"), "dB", -96, 24, 0)});
        l << process("suite.normalize", QT_TRANSLATE_NOOP("QObject", "Normalize"),
                     {param("level", QT_TRANSLATE_NOOP("QObject", "Level"), "dB", -40, 0, 0), calibration(),
                      // Named as the Processing Mode Selection menu names them (p.803).
                      choice("mode", QT_TRANSLATE_NOOP("QObject", "Processing"), {QObject::tr("Levels On All Chans-Tracks"), QObject::tr("Level On Each Chan-Track")}, 0)});
        l << process("suite.invert", QT_TRANSLATE_NOOP("QObject", "Invert"));
        l << process("suite.reverse", QT_TRANSLATE_NOOP("QObject", "Reverse"));
        l << process("suite.dc", QT_TRANSLATE_NOOP("QObject", "DC Offset Removal"));
        // "Creates a new master clip from a selected audio master clip" between its marks (p.839).
        AudioSuiteInfo dup = process("suite.duplicate", QT_TRANSLATE_NOOP("QObject", "Duplicate"));
        dup.masterClipOnly = true;
        l << dup;
        AudioSuiteInfo gen = process("suite.signal", QT_TRANSLATE_NOOP("QObject", "Signal Generator"),
                                     {param("frequency", QT_TRANSLATE_NOOP("QObject", "Frequency"), "Hz", 20, 20000, 1000),
                                      param("level", QT_TRANSLATE_NOOP("QObject", "Level"), "dB", -95, 0, -20),
                                      choice("signal", QT_TRANSLATE_NOOP("QObject", "Signal"),
                                             {QObject::tr("Sine"), QObject::tr("Square"), QObject::tr("Sawtooth"), QObject::tr("Triangle"),
                                              QObject::tr("White Noise"), QObject::tr("Pink Noise")},
                                             0),
                                      calibration()});
        gen.generator = true;
        l << gen;
        // The time processes (p.850-860), on our WSOLA engine (time_stretch.h). Their finer controls, shared by Pitch
        // Shift and Time Compression Expansion (p.850, p.857): Crossfade (0 is Auto), Min Pitch and Accuracy.
        const QList<AudioEffectParam> fine{param("crossfade", QT_TRANSLATE_NOOP("QObject", "Crossfade"), "ms", 0, 200, 0),
                                           param("minPitch", QT_TRANSLATE_NOOP("QObject", "Min Pitch"), "Hz", 40, 1000, 40),
                                           param("accuracy", QT_TRANSLATE_NOOP("QObject", "Accuracy"), "% sound", 0, 100, 50)};
        AudioEffectParam coarse = param("coarse", QT_TRANSLATE_NOOP("QObject", "Coarse"), "semitones", -12, 12, 0);
        coarse.steps = 25;
        AudioSuiteInfo pitch = process("suite.pitch_shift", QT_TRANSLATE_NOOP("QObject", "Pitch Shift"),
                                       QList<AudioEffectParam>{param("gain", QT_TRANSLATE_NOOP("QObject", "Gain"), "dB", -24, 6, 0), coarse,
                                                               param("fine", QT_TRANSLATE_NOOP("QObject", "Fine"), "cents", -100, 100, 0)} +
                                           fine +
                                           QList<AudioEffectParam>{choice("timeCorrection", QT_TRANSLATE_NOOP("QObject", "Time Correction"),
                                                                          {QObject::tr("Off"), QObject::tr("On")}, 1)});
        pitch.masterClipOnly = true;
        pitch.changesLength = true; // without Time Correction it varispeeds (p.851)
        l << pitch;
        AudioSuiteInfo tce = process("suite.time_compression", QT_TRANSLATE_NOOP("QObject", "Time Compression Expansion"),
                                     QList<AudioEffectParam>{param("ratio", QT_TRANSLATE_NOOP("QObject", "Ratio"), "x", 0.25, 4.0, 1.0)} + fine);
        tce.masterClipOnly = true;
        tce.changesLength = true;
        l << tce;
        AudioEffectParam transpose = param("transpose", QT_TRANSLATE_NOOP("QObject", "Transpose"), "semitones", -24, 24, 0);
        AudioSuiteInfo shift = process(
            "suite.time_shift", "Time Shift",
            {choice("mode", QT_TRANSLATE_NOOP("QObject", "Mode"),
                    {QObject::tr("Monophonic"), QObject::tr("Polyphonic"), QObject::tr("Rhythmic"), QObject::tr("Varispeed")}, 1),
             choice("range", QT_TRANSLATE_NOOP("QObject", "Range"), {QObject::tr("Low"), QObject::tr("Mid"), QObject::tr("High"), QObject::tr("Wide")}, 3),
             param("gain", QT_TRANSLATE_NOOP("QObject", "Gain"), "dB", -6, 0, 0),
             param("shift", QT_TRANSLATE_NOOP("QObject", "Time Shift"), "%", 25, 400, 100),
             param("window", QT_TRANSLATE_NOOP("QObject", "Window"), "ms", 6, 185, 18),
             param("threshold", QT_TRANSLATE_NOOP("QObject", "Threshold"), "dB", -40, 0, -6),
             param("decay", QT_TRANSLATE_NOOP("QObject", "Decay Rate"), "ms", 10, 1000, 100),
             transpose});
        shift.masterClipOnly = true;
        shift.changesLength = true;
        l << shift;
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

// Pitch Shift, Time Compression Expansion and Time Shift (p.850-860) on the time-stretch engine.
std::vector<std::vector<float>> timeProcess(const QString& id, const QMap<QString, double>& params, const AudioSuiteInfo& info,
                                            std::vector<std::vector<float>> channels) {
    auto v = [&](const char* key) { return value(params, info, key); };
    StretchOptions o;
    o.sampleRate = kPlaybackRate;
    if (id != QLatin1String("suite.time_shift")) {
        o.crossfadeMs = v("crossfade");
        o.minPitchHz = v("minPitch");
        o.accuracy = v("accuracy") / 100.0;
    }
    if (id == QLatin1String("suite.pitch_shift")) {
        // The input level first (p.850), then the transposition in semitones and cents.
        for (auto& c : channels) scale(c, dbToGain(v("gain")));
        return pitchShift(channels, v("coarse") + v("fine") / 100.0, v("timeCorrection") >= 0.5, o);
    }
    if (id == QLatin1String("suite.time_compression")) return timeStretch(channels, v("ratio"), o);
    // Time Shift: its Gain attenuates the input (p.858); 25% of the speed is four times the length (p.859).
    for (auto& c : channels) scale(c, dbToGain(v("gain")));
    const double speed = v("shift") / 100.0;
    const double length = 1.0 / speed;
    const double semitones = v("transpose");
    const int mode = static_cast<int>(std::lround(v("mode")));
    if (mode == 3) return varispeed(channels, speed, kPlaybackRate); // time and pitch linked, as tape (p.858)
    if (mode == 2) {
        // Rhythmic: cut at the transients, then any transposition with the length kept.
        o.accuracy = 0.0;
        auto s = rhythmicStretch(channels, length, v("threshold"), v("decay"), o);
        return std::abs(semitones) < 1e-9 ? s : pitchShift(s, semitones, true, o);
    }
    // Monophonic: joins found for the range's lowest pitch, with windows three of its periods long and the search
    // towards sound. Polyphonic: the Window control's length (p.859-860). Ours: the ranges' lowest pitches.
    static constexpr double kRangeLow[] = {40.0, 80.0, 160.0, 40.0};
    o.minPitchHz = kRangeLow[std::clamp(static_cast<int>(std::lround(v("range"))), 0, 3)];
    if (mode == 0) {
        o.crossfadeMs = 1500.0 / o.minPitchHz; // half the window: one and a half periods
        o.accuracy = 1.0;
    } else {
        o.crossfadeMs = v("window") / 2.0;
        o.accuracy = 0.5;
    }
    return pitchShift(channels, semitones, true, o, length);
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
        if (error) *error = QObject::tr("The Audio Processing plug-in %1 is not installed.").arg(id);
        return false;
    }
    if (info->effect) {
        // Each channel through an effect of its own, in blocks as playback would.
        constexpr int kBlock = 1024;
        for (auto& c : channels) {
            auto fx = makeAudioEffect(id);
            if (!fx) {
                if (error) *error = QObject::tr("The Audio Processing plug-in %1 is not installed.").arg(info->name);
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
    } else if (id == QLatin1String("suite.duplicate")) {
        // The sound as it is.
    } else if (id == QLatin1String("suite.pitch_shift") || id == QLatin1String("suite.time_compression") ||
               id == QLatin1String("suite.time_shift")) {
        channels = timeProcess(id, params, *info, channels);
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

std::vector<float> clipTrackSound(const model::BinItem& clip, int trackIndex, std::int64_t from, std::int64_t count) {
    std::vector<float> out(static_cast<std::size_t>(std::max<std::int64_t>(count, 0)), 0.0f);
    if (out.empty()) return out;
    // The clip's track as playback reads it, alone and unpanned: full on the left, nothing right.
    const QString track = QStringLiteral("A%1").arg(trackIndex + 1);
    std::vector<AudioPiece> pieces = audioPieces(clip, {}, [&](const QString& t) { return t == track; });
    if (pieces.empty()) return out;
    AudioPiece p = pieces.front();
    p.at = 0;
    p.sourceStart += framesToSamples(from, clip.editRate);
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
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = stereo[2 * i];
    return out;
}

std::vector<float> segmentSource(const model::Segment& s, const model::BinItem& clip, const Rational& sequenceRate) {
    const std::int64_t count = framesToSamples(s.length + 1, sequenceRate);
    const auto ref = model::parseTrackName(s.sourceTrack);
    if (!ref || ref->kind != model::TrackKind::Audio) {
        return std::vector<float>(static_cast<std::size_t>(std::max<std::int64_t>(count, 0)), 0.0f);
    }
    std::vector<float> out = clipTrackSound(clip, ref->number - 1, s.sourceIn, count);
    const double gain = s.gain <= model::kGainOff ? 0.0 : dbToGain(s.gain);
    for (float& v : out) v = static_cast<float>(static_cast<double>(v) * gain);
    return out;
}

std::optional<model::AudioSuiteEffect> renderAudioSuite(const model::Segment& s, const model::BinItem& clip,
                                                        const Rational& sequenceRate, const MediaCreation& creation,
                                                        QString* error) {
    if (!s.audioSuite || s.isFiller()) {
        if (error) *error = QObject::tr("The segment has no Audio Processing effect.");
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

std::optional<model::BinItem> audioSuiteClip(const AudioSuiteClipRequest& r, const MediaCreation& creation, QString* error) {
    auto failed = [&](const QString& text) -> std::optional<model::BinItem> {
        if (error) *error = text;
        return std::nullopt;
    };
    if (r.tracks.isEmpty()) return failed(QObject::tr("Select the input tracks."));
    const std::int64_t last = r.out < 0 ? r.clip.durationFrames - 1 : std::min(r.out, r.clip.durationFrames - 1);
    const std::int64_t in = std::clamp<std::int64_t>(r.in, 0, std::max<std::int64_t>(last, 0));
    const std::int64_t frames = last - in + 1;
    if (frames <= 0) return failed(QObject::tr("The clip has no frames to process."));
    // The handle in whole frames, so the new clip ends on a frame.
    const double fps = static_cast<double>(r.clip.editRate.num()) / static_cast<double>(r.clip.editRate.den());
    const auto handle = static_cast<std::int64_t>(std::ceil(std::max(r.handleSeconds, 0.0) * fps - 1e-9));
    const std::int64_t total = frames + handle;
    const std::int64_t samples = framesToSamples(total, r.clip.editRate);
    const std::int64_t sound = framesToSamples(frames, r.clip.editRate);
    std::vector<std::vector<float>> channels;
    for (const int t : r.tracks) {
        std::vector<float> c = clipTrackSound(r.clip, t, in, sound);
        c.resize(static_cast<std::size_t>(samples), 0.0f); // the handle: silence for the effect to ring into
        channels.push_back(std::move(c));
    }
    if (!processAudioSuite(r.effect, r.params, channels, error)) return std::nullopt;
    // A time process made the sound longer or shorter: the clip is as long as it, in whole frames, the rest silence.
    std::int64_t made = total;
    if (!channels.empty() && static_cast<std::int64_t>(channels.front().size()) != samples) {
        made = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(std::ceil(samplesToFrames(static_cast<std::int64_t>(channels.front().size()), r.clip.editRate) - 1e-9)));
    }
    QString start;
    if (const auto first = model::timecodeFrames(r.clip.fields.value(QStringLiteral("Start")), r.clip.editRate)) {
        const TimecodeFormat format = TimecodeFormat::forRate(r.clip.editRate, model::isDropFrame(r.clip));
        start = QString::fromStdString(Timecode(*first + in, format).toString());
    }
    auto clip = renderedAudioClip(
        r.name, r.clip.editRate, made, static_cast<int>(channels.size()),
        [&](std::int64_t at, int count, std::vector<std::vector<float>>& out) {
            for (std::size_t c = 0; c < channels.size(); ++c) {
                out[c].assign(static_cast<std::size_t>(count), 0.0f);
                for (int i = 0; i < count; ++i) {
                    const auto k = static_cast<std::size_t>(at + i);
                    if (k < channels[c].size()) out[c][static_cast<std::size_t>(i)] = channels[c][k];
                }
            }
        },
        start, creation, error);
    return clip;
}

} // namespace lumenis::media
