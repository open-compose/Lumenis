#include "lumenis/media/audio_fx.h"

#include "lumenis/model/audio_eq.h"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <numbers>
#include <vector>

// The audio effect interface's shared parts and the built-in effects (audio_fx.h). Each effect keeps
// its parameter values in a map, works out what it needs from them (filter coefficients, envelope
// times) when they change, and processes one mono channel at 48 kHz in place.
namespace lumenis::media {

namespace {

constexpr double kRate = 48000.0;

double dbToGain(double db) {
    return std::pow(10.0, db / 20.0);
}

double gainToDb(double g) {
    return 20.0 * std::log10(std::max(g, 1e-9));
}

// A one-pole smoothing coefficient for a time constant in ms.
double coefficient(double ms) {
    return ms <= 0 ? 0.0 : std::exp(-1.0 / (ms * 0.001 * kRate));
}

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

// A biquad section in transposed direct form II.
struct Section {
    model::Biquad f;
    double z1 = 0, z2 = 0;
    double run(double x) {
        const double y = f.b0 * x + z1;
        z1 = f.b1 * x - f.a1 * y + z2;
        z2 = f.b2 * x - f.a2 * y;
        return y;
    }
    void clear() { z1 = z2 = 0; }
};

// What every built-in effect shares: its info, values and the dirty flag.
class Builtin : public AudioEffect {
public:
    explicit Builtin(const AudioEffectInfo& info) : info_(info), values_(info.defaults()) {}
    const AudioEffectInfo& info() const override { return info_; }
    void set(const QString& id, double value) override {
        const AudioEffectParam* p = info_.param(id);
        if (!p) return;
        value = std::clamp(value, p->min, p->max);
        if (p->steps > 0) value = std::round(value);
        if (values_.value(id) == value) return;
        values_.insert(id, value);
        dirty_ = true;
    }
    void process(float* samples, int count) override {
        if (dirty_) {
            update();
            dirty_ = false;
        }
        run(samples, count);
    }

protected:
    double v(const char* id) const { return values_.value(QString::fromLatin1(id)); }
    // Works out what the values need; called before processing after a change.
    virtual void update() = 0;
    virtual void run(float* samples, int count) = 0;

private:
    const AudioEffectInfo& info_;
    QMap<QString, double> values_;
    bool dirty_ = true;
};

// ---- Trim: gain and polarity (cf. Trim and Invert, p.845, p.860)

class Trim : public Builtin {
public:
    using Builtin::Builtin;
    void reset() override {}
    int tail() const override { return 0; }

protected:
    void update() override {
        gain_ = v("gain") <= -96 ? 0.0 : dbToGain(v("gain"));
        if (v("polarity") > 0.5) gain_ = -gain_;
    }
    void run(float* s, int n) override {
        for (int i = 0; i < n; ++i) s[i] = static_cast<float>(static_cast<double>(s[i]) * gain_);
    }

private:
    double gain_ = 1;
};

// ---- Compressor/Limiter: feed-forward, peak detection in dB with attack and release, a soft knee
// and make-up gain (cf. Compressor/Limiter, p.833).

class Compressor : public Builtin {
public:
    using Builtin::Builtin;
    void reset() override { env_ = -120; }
    int tail() const override { return static_cast<int>(v("release") * 0.001 * kRate * 5); }

protected:
    void update() override {
        attack_ = coefficient(v("attack"));
        release_ = coefficient(v("release"));
        makeup_ = v("gain");
    }
    // The gain change in dB for a level, with a knee `knee` dB wide around the threshold.
    double computer(double level) const {
        const double t = v("threshold");
        const double r = v("ratio");
        const double k = v("knee");
        const double over = level - t;
        if (k > 0 && std::abs(over) <= k / 2) {
            const double x = over + k / 2;
            return (1 / r - 1) * x * x / (2 * k);
        }
        return over > 0 ? over * (1 / r - 1) : 0.0;
    }
    void run(float* s, int n) override {
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(s[i]);
            const double level = gainToDb(std::abs(x));
            const double c = level > env_ ? attack_ : release_;
            env_ = c * env_ + (1 - c) * level;
            s[i] = static_cast<float>(x * dbToGain(computer(env_) + makeup_));
        }
    }

private:
    double attack_ = 0, release_ = 0, makeup_ = 0;
    double env_ = -120;
};

// ---- Expander/Gate: below the threshold the level falls `ratio` times faster, at most `range` dB
// (cf. Expander/Gate, p.842).

class Gate : public Builtin {
public:
    using Builtin::Builtin;
    void reset() override {
        env_ = -120;
        gain_ = v("range");
    }
    int tail() const override { return static_cast<int>(v("release") * 0.001 * kRate * 5); }

protected:
    void update() override {
        attack_ = coefficient(v("attack"));
        release_ = coefficient(v("release"));
    }
    void run(float* s, int n) override {
        const double t = v("threshold");
        const double r = v("ratio");
        const double range = v("range");
        const double detect = coefficient(1.0);
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(s[i]);
            const double level = gainToDb(std::abs(x));
            env_ = level > env_ ? level : detect * env_ + (1 - detect) * level;
            const double target = env_ >= t ? 0.0 : std::max((env_ - t) * (r - 1), range);
            // Opening takes the attack time, closing the release time.
            const double c = target > gain_ ? attack_ : release_;
            gain_ = c * gain_ + (1 - c) * target;
            s[i] = static_cast<float>(x * dbToGain(gain_));
        }
    }

private:
    double attack_ = 0, release_ = 0;
    double env_ = -120;
    double gain_ = 0;
};

// ---- De-Esser: the level above `frequency` drives a broadband reduction of at most `range` dB
// (cf. De-Esser, p.837).

class DeEsser : public Builtin {
public:
    using Builtin::Builtin;
    void reset() override {
        side_.clear();
        env_ = -120;
    }
    int tail() const override { return static_cast<int>(0.05 * kRate); }

protected:
    void update() override { side_.f = model::highPass(v("frequency"), 0.7071, kRate); }
    void run(float* s, int n) override {
        const double t = v("threshold");
        const double range = v("range");
        const double attack = coefficient(1.0);
        const double release = coefficient(50.0);
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(s[i]);
            const double level = gainToDb(std::abs(side_.run(x)));
            const double c = level > env_ ? attack : release;
            env_ = c * env_ + (1 - c) * level;
            const double cut = std::max(t - env_, range); // negative while over the threshold
            s[i] = static_cast<float>(x * dbToGain(std::min(cut, 0.0)));
        }
    }

private:
    Section side_;
    double env_ = -120;
};

// ---- EQ: low shelf, two peaks and a high shelf (cf. the EQ plug-ins, p.841).

class Equalizer : public Builtin {
public:
    using Builtin::Builtin;
    void reset() override {
        for (Section& b : bands_) b.clear();
    }
    int tail() const override { return static_cast<int>(0.05 * kRate); }

protected:
    void update() override {
        bands_[0].f = model::lowShelf(v("lowGain"), v("lowFreq"), kRate);
        bands_[1].f = model::peaking(v("mid1Gain"), v("mid1Freq"), v("mid1Width"), kRate);
        bands_[2].f = model::peaking(v("mid2Gain"), v("mid2Freq"), v("mid2Width"), kRate);
        bands_[3].f = model::highShelf(v("highGain"), v("highFreq"), kRate);
    }
    void run(float* s, int n) override {
        for (int i = 0; i < n; ++i) {
            double x = static_cast<double>(s[i]);
            for (Section& b : bands_) x = b.run(x);
            s[i] = static_cast<float>(x);
        }
    }

private:
    std::array<Section, 4> bands_;
};

// A delay line with linear interpolation between samples.
class DelayLine {
public:
    explicit DelayLine(std::size_t length) : buffer_(length, 0.0f) {}
    void clear() { std::fill(buffer_.begin(), buffer_.end(), 0.0f); }
    void write(double x) {
        buffer_[at_] = static_cast<float>(x);
        at_ = (at_ + 1) % buffer_.size();
    }
    // The sample written `delay` samples ago (1 or more).
    double read(double delay) const {
        const double d = std::clamp(delay, 1.0, static_cast<double>(buffer_.size() - 2));
        const auto whole = static_cast<std::size_t>(d);
        const double frac = d - static_cast<double>(whole);
        const std::size_t size = buffer_.size();
        const double a = static_cast<double>(buffer_[(at_ + size - whole) % size]);
        const double b = static_cast<double>(buffer_[(at_ + size - whole - 1) % size]);
        return a + (b - a) * frac;
    }

private:
    std::vector<float> buffer_;
    std::size_t at_ = 0;
};

// ---- Delay: one echo line with feedback (cf. the delay plug-ins, p.809, p.819).

class Delay : public Builtin {
public:
    explicit Delay(const AudioEffectInfo& info) : Builtin(info), line_(static_cast<std::size_t>(2.1 * kRate)) {}
    void reset() override { line_.clear(); }
    int tail() const override {
        // Until the echoes are 60 dB down.
        const double fb = v("feedback") / 100;
        const double repeats = fb <= 0.001 ? 1 : std::min(-3.0 / std::log10(fb), 50.0);
        return static_cast<int>(std::min(repeats * v("time") * 0.001 * kRate, 10 * kRate));
    }

protected:
    void update() override {}
    void run(float* s, int n) override {
        const double delay = v("time") * 0.001 * kRate;
        const double fb = v("feedback") / 100;
        const double mix = v("mix") / 100;
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(s[i]);
            const double d = line_.read(delay);
            line_.write(x + d * fb);
            s[i] = static_cast<float>(x * (1 - mix) + d * mix);
        }
    }

private:
    DelayLine line_;
};

// ---- Chorus: a short delay swept by a sine (cf. Chorus, p.808).

class Chorus : public Builtin {
public:
    explicit Chorus(const AudioEffectInfo& info) : Builtin(info), line_(static_cast<std::size_t>(0.1 * kRate)) {}
    void reset() override {
        line_.clear();
        phase_ = 0;
    }
    int tail() const override { return static_cast<int>(0.1 * kRate); }

protected:
    void update() override {}
    void run(float* s, int n) override {
        const double step = 2 * std::numbers::pi * v("rate") / kRate;
        const double pre = v("predelay") * 0.001 * kRate;
        const double depth = v("depth") * 0.001 * kRate;
        const double fb = v("feedback") / 100;
        const double mix = v("mix") / 100;
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(s[i]);
            const double d = line_.read(pre + depth * (0.5 + 0.5 * std::sin(phase_)));
            line_.write(x + d * fb);
            phase_ = std::fmod(phase_ + step, 2 * std::numbers::pi);
            s[i] = static_cast<float>(x * (1 - mix) + d * mix);
        }
    }

private:
    DelayLine line_;
    double phase_ = 0;
};

// ---- Reverb: Freeverb's design (Jezar at Dreampoint, public domain): eight damped combs in
// parallel, then four all-passes, after a pre-delay (cf. Reverb, p.821, p.835).

class Reverb : public Builtin {
public:
    explicit Reverb(const AudioEffectInfo& info) : Builtin(info), pre_(static_cast<std::size_t>(0.25 * kRate)) {
        // Freeverb's tunings, at 44.1 kHz, scaled to 48 kHz.
        constexpr int combs[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        constexpr int allpasses[] = {556, 441, 341, 225};
        for (int c : combs) combs_.push_back({std::vector<float>(static_cast<std::size_t>(c * kRate / 44100.0), 0.0f)});
        for (int a : allpasses) {
            allpasses_.push_back({std::vector<float>(static_cast<std::size_t>(a * kRate / 44100.0), 0.0f)});
        }
    }
    void reset() override {
        pre_.clear();
        for (Comb& c : combs_) {
            std::fill(c.buffer.begin(), c.buffer.end(), 0.0f);
            c.store = 0;
        }
        for (Allpass& a : allpasses_) std::fill(a.buffer.begin(), a.buffer.end(), 0.0f);
    }
    int tail() const override { return static_cast<int>((0.5 + 4.0 * v("size") / 100) * kRate); }

protected:
    void update() override {
        feedback_ = 0.7 + 0.28 * v("size") / 100;
        damp_ = 0.4 * v("damping") / 100;
    }
    void run(float* s, int n) override {
        const double pre = std::max(v("predelay") * 0.001 * kRate, 1.0);
        const double mix = v("mix") / 100;
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(s[i]);
            pre_.write(x);
            const double input = (v("predelay") > 0 ? pre_.read(pre) : x) * 0.015;
            double out = 0;
            for (Comb& c : combs_) {
                const double y = static_cast<double>(c.buffer[c.at]);
                c.store = y * (1 - damp_) + c.store * damp_;
                c.buffer[c.at] = static_cast<float>(input + c.store * feedback_);
                c.at = (c.at + 1) % c.buffer.size();
                out += y;
            }
            for (Allpass& a : allpasses_) {
                const double b = static_cast<double>(a.buffer[a.at]);
                a.buffer[a.at] = static_cast<float>(out + b * 0.5);
                a.at = (a.at + 1) % a.buffer.size();
                out = b - out;
            }
            s[i] = static_cast<float>(x * (1 - mix) + out * 3 * mix);
        }
    }

private:
    struct Comb {
        std::vector<float> buffer;
        std::size_t at = 0;
        double store = 0;
    };
    struct Allpass {
        std::vector<float> buffer;
        std::size_t at = 0;
    };
    DelayLine pre_;
    std::vector<Comb> combs_;
    std::vector<Allpass> allpasses_;
    double feedback_ = 0.84, damp_ = 0.2;
};

// ---- Lo-Fi: fewer bits and a lower sample rate (cf. Lo-Fi, p.845).

class LoFi : public Builtin {
public:
    using Builtin::Builtin;
    void reset() override {
        held_ = 0;
        count_ = 0;
    }
    int tail() const override { return 0; }

protected:
    void update() override {}
    void run(float* s, int n) override {
        const double levels = std::pow(2.0, v("bits") - 1);
        const double every = kRate / v("rate");
        const double mix = v("mix") / 100;
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(s[i]);
            count_ += 1;
            if (count_ >= every) {
                count_ -= every;
                held_ = std::round(x * levels) / levels;
            }
            s[i] = static_cast<float>(x * (1 - mix) + held_ * mix);
        }
    }

private:
    double held_ = 0;
    double count_ = 1e9; // takes the first sample at once
};

// The built-in effects: their infos (in the Effect Palette's order) and makers.
struct Entry {
    AudioEffectInfo info;
    std::function<std::unique_ptr<AudioEffect>(const AudioEffectInfo&)> make;
};

template <typename T>
Entry entry(const char* id, const char* name, const char* category, QList<AudioEffectParam> params) {
    Entry e;
    e.info.id = QStringLiteral("builtin.") + QString::fromLatin1(id);
    e.info.name = QObject::tr(name);
    e.info.category = QObject::tr(category);
    e.info.vendor = QStringLiteral("Cutthulhu");
    e.info.params = std::move(params);
    e.make = [](const AudioEffectInfo& info) { return std::make_unique<T>(info); };
    return e;
}

std::vector<Entry>& builtins() {
    static std::vector<Entry> list = [] {
        std::vector<Entry> l;
        l.push_back(entry<Compressor>("compressor", "Compressor/Limiter", "Dynamics",
                                      {param("threshold", "Threshold", "dB", -60, 0, -24),
                                       param("ratio", "Ratio", ":1", 1, 100, 3),
                                       param("attack", "Attack", "ms", 0.1, 300, 10),
                                       param("release", "Release", "ms", 5, 4000, 100),
                                       param("knee", "Knee", "dB", 0, 30, 0),
                                       param("gain", "Gain", "dB", -20, 40, 0)}));
        l.push_back(entry<Gate>("gate", "Expander/Gate", "Dynamics",
                                {param("threshold", "Threshold", "dB", -80, 0, -40),
                                 param("ratio", "Ratio", ":1", 1, 100, 30),
                                 param("range", "Range", "dB", -80, 0, -40),
                                 param("attack", "Attack", "ms", 0.1, 300, 1),
                                 param("release", "Release", "ms", 5, 4000, 100)}));
        l.push_back(entry<DeEsser>("deesser", "De-Esser", "Dynamics",
                                   {param("frequency", "Frequency", "Hz", 500, 16000, 6000),
                                    param("threshold", "Threshold", "dB", -60, 0, -30),
                                    param("range", "Range", "dB", -40, 0, -6)}));
        l.push_back(entry<Equalizer>("eq", "4-Band EQ", "EQ",
                                     {param("lowFreq", "Low Frequency", "Hz", 20, 1000, 100),
                                      param("lowGain", "Low Gain", "dB", -20, 15, 0),
                                      param("mid1Freq", "Low-Mid Frequency", "Hz", 50, 10000, 500),
                                      param("mid1Gain", "Low-Mid Gain", "dB", -20, 15, 0),
                                      param("mid1Width", "Low-Mid Width", "oct", 0.1, 4, 1),
                                      param("mid2Freq", "High-Mid Frequency", "Hz", 200, 16000, 3000),
                                      param("mid2Gain", "High-Mid Gain", "dB", -20, 15, 0),
                                      param("mid2Width", "High-Mid Width", "oct", 0.1, 4, 1),
                                      param("highFreq", "High Frequency", "Hz", 1000, 20000, 8000),
                                      param("highGain", "High Gain", "dB", -20, 15, 0)}));
        l.push_back(entry<Delay>("delay", "Delay", "Delay",
                                 {param("time", "Time", "ms", 1, 2000, 250),
                                  param("feedback", "Feedback", "%", 0, 95, 30),
                                  param("mix", "Mix", "%", 0, 100, 30)}));
        l.push_back(entry<Chorus>("chorus", "Chorus", "Modulation",
                                  {param("rate", "Rate", "Hz", 0.05, 10, 0.8),
                                   param("depth", "Depth", "ms", 0, 10, 3),
                                   param("predelay", "Pre Delay", "ms", 1, 30, 10),
                                   param("feedback", "Feedback", "%", -90, 90, 0),
                                   param("mix", "Mix", "%", 0, 100, 50)}));
        l.push_back(entry<Reverb>("reverb", "Reverb", "Reverb",
                                  {param("size", "Room Size", "%", 0, 100, 50),
                                   param("damping", "Damping", "%", 0, 100, 50),
                                   param("predelay", "Pre Delay", "ms", 0, 200, 10),
                                   param("mix", "Mix", "%", 0, 100, 25)}));
        l.push_back(entry<LoFi>("lofi", "Lo-Fi", "Harmonic",
                                {param("bits", "Bits", "", 2, 24, 8),
                                 param("rate", "Sample Rate", "Hz", 1000, 48000, 11025),
                                 param("mix", "Mix", "%", 0, 100, 100)}));
        l.push_back(entry<Trim>("trim", "Trim", "Other",
                                {param("gain", "Gain", "dB", -96, 12, 0),
                                 choice("polarity", "Polarity", {QObject::tr("Normal"), QObject::tr("Invert")}, 0)}));
        return l;
    }();
    return list;
}

struct Registry {
    std::mutex mutex;
    QList<AudioEffectInfo> infos;
    std::vector<AudioEffectMaker> makers;
    QHash<QString, std::size_t> makerFor; // id -> makers index
};

Registry& registry() {
    static Registry r;
    return r;
}

} // namespace

QMap<QString, double> AudioEffectInfo::defaults() const {
    QMap<QString, double> out;
    for (const AudioEffectParam& p : params) out.insert(p.id, p.defaultValue);
    return out;
}

const AudioEffectParam* AudioEffectInfo::param(const QString& paramId) const {
    for (const AudioEffectParam& p : params) {
        if (p.id == paramId) return &p;
    }
    return nullptr;
}

void AudioEffect::setAll(const QMap<QString, double>& values) {
    for (auto it = values.begin(); it != values.end(); ++it) set(it.key(), it.value());
}

const QList<AudioEffectInfo>& audioEffects() {
    static QList<AudioEffectInfo> all;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    all.clear();
    for (const Entry& e : builtins()) all << e.info;
    all += r.infos;
    return all;
}

const AudioEffectInfo* findAudioEffect(const QString& id) {
    for (const Entry& e : builtins()) {
        if (e.info.id == id) return &e.info;
    }
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    for (const AudioEffectInfo& i : r.infos) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

std::unique_ptr<AudioEffect> makeAudioEffect(const QString& id) {
    for (const Entry& e : builtins()) {
        if (e.info.id == id) {
            auto fx = e.make(e.info);
            fx->reset();
            return fx;
        }
    }
    Registry& r = registry();
    AudioEffectMaker maker;
    {
        std::lock_guard lock(r.mutex);
        const auto it = r.makerFor.find(id);
        if (it == r.makerFor.end()) return nullptr;
        maker = r.makers[*it];
    }
    return maker(id);
}

void registerAudioEffects(const QList<AudioEffectInfo>& effects, AudioEffectMaker maker) {
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    r.makers.push_back(std::move(maker));
    for (const AudioEffectInfo& info : effects) {
        bool known = r.makerFor.contains(info.id);
        for (const Entry& e : builtins()) known = known || e.info.id == info.id;
        if (known) continue;
        r.infos << info;
        r.makerFor.insert(info.id, r.makers.size() - 1);
    }
}

QString formatParam(const AudioEffectParam& p, double value) {
    if (p.steps > 0) {
        const auto i = static_cast<qsizetype>(std::lround(value - p.min));
        return i >= 0 && i < p.choices.size() ? p.choices.at(i) : QString::number(value);
    }
    if (p.unit == QLatin1String("dB")) {
        if (value <= -96 && p.min <= -96) return QStringLiteral("-∞ dB");
        return QStringLiteral("%1 dB").arg(value, 0, 'f', 1);
    }
    if (p.unit == QLatin1String("Hz") && value >= 1000) return QStringLiteral("%1 kHz").arg(value / 1000, 0, 'f', 2);
    if (p.unit == QLatin1String("Hz") || p.unit == QLatin1String("ms")) {
        return QStringLiteral("%1 %2").arg(value, 0, 'f', value < 10 ? 1 : 0).arg(p.unit);
    }
    if (p.unit == QLatin1String("%")) return QStringLiteral("%1 %").arg(value, 0, 'f', 0);
    if (p.unit.isEmpty()) return QString::number(value, 'g', 3);
    return QStringLiteral("%1 %2").arg(QString::number(value, 'g', 3), p.unit);
}

} // namespace lumenis::media
