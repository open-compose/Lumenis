#include "lumenis/model/audio_eq.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

// Audio EQ (audio_eq.h). The filter formulas are Robert Bristow-Johnson's "Cookbook formulae for
// audio EQ biquad filter coefficients", which are published for anyone to use.
namespace lumenis::model {

namespace {

template <std::size_t N>
int nearest(const std::array<int, N>& choices, int value) {
    return *std::min_element(choices.begin(), choices.end(),
                             [value](int a, int b) { return std::abs(a - value) < std::abs(b - value); });
}

double clampGainDb(double db) {
    return std::round(std::clamp(db, kEqMin, kEqMax) * 10) / 10;
}

Biquad normalised(double b0, double b1, double b2, double a0, double a1, double a2) {
    return Biquad{b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

// Shelves with a slope of 1: alpha = sin(w0) / 2 * sqrt(2).
Biquad shelf(bool low, double db, double frequency, double rate) {
    const double a = std::pow(10.0, db / 40.0);
    const double w0 = 2 * std::numbers::pi * frequency / rate;
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / 2 * std::numbers::sqrt2;
    const double r = 2 * std::sqrt(a) * alpha;
    if (low) {
        return normalised(a * ((a + 1) - (a - 1) * c + r), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - r),
                          (a + 1) + (a - 1) * c + r, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - r);
    }
    return normalised(a * ((a + 1) + (a - 1) * c + r), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - r),
                      (a + 1) - (a - 1) * c + r, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - r);
}

// A peaking filter `octaves` wide.
Biquad peak(double db, double frequency, double octaves, double rate) {
    const double a = std::pow(10.0, db / 40.0);
    const double w0 = 2 * std::numbers::pi * frequency / rate;
    const double alpha = std::sin(w0) * std::sinh(std::numbers::ln2 / 2 * octaves * w0 / std::sin(w0));
    const double c = std::cos(w0);
    return normalised(1 + alpha * a, -2 * c, 1 - alpha * a, 1 + alpha / a, -2 * c, 1 - alpha / a);
}

} // namespace

Biquad lowShelf(double db, double frequency, double sampleRate) {
    return shelf(true, db, std::min(frequency, sampleRate * 0.45), sampleRate);
}

Biquad highShelf(double db, double frequency, double sampleRate) {
    return shelf(false, db, std::min(frequency, sampleRate * 0.45), sampleRate);
}

Biquad peaking(double db, double frequency, double octaves, double sampleRate) {
    return peak(db, std::min(frequency, sampleRate * 0.45), octaves, sampleRate);
}

Biquad highPass(double frequency, double q, double sampleRate) {
    const double w0 = 2 * std::numbers::pi * std::min(frequency, sampleRate * 0.45) / sampleRate;
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / (2 * q);
    return normalised((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

Biquad lowPass(double frequency, double q, double sampleRate) {
    const double w0 = 2 * std::numbers::pi * std::min(frequency, sampleRate * 0.45) / sampleRate;
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / (2 * q);
    return normalised((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

AudioEq clampEq(const AudioEq& eq) {
    AudioEq e = eq;
    e.low = clampGainDb(e.low);
    e.mid = clampGainDb(e.mid);
    e.high = clampGainDb(e.high);
    if (e.preset.isEmpty()) {
        e.lowTurnover = nearest(kLowTurnovers, e.lowTurnover);
        e.highTurnover = nearest(kHighTurnovers, e.highTurnover);
        e.midFrequency = std::clamp(e.midFrequency, kEqRangeMin, kEqRangeMax);
        e.midOctaves = 0;
    } else {
        // A template's own values, kept within what can be heard.
        e.lowTurnover = std::clamp(e.lowTurnover, kPresetMinHz, kPresetMaxHz);
        e.highTurnover = std::clamp(e.highTurnover, kPresetMinHz, kPresetMaxHz);
        e.midFrequency = std::clamp(e.midFrequency, static_cast<double>(kPresetMinHz), static_cast<double>(kPresetMaxHz));
        e.midOctaves = e.midOctaves > 0 ? std::clamp(e.midOctaves, kPresetMinOctaves, kPresetMaxOctaves) : 0.0;
    }
    return e;
}

bool isFlat(const AudioEq& eq) {
    return eq.low == 0 && eq.mid == 0 && eq.high == 0;
}

std::array<Biquad, 3> eqFilters(const AudioEq& eq, double sampleRate) {
    // A band at 0 dB is no filter at all, so a flat EQ passes the sound unchanged.
    auto band = [](double db, auto make) { return db == 0 ? Biquad{} : make(); };
    return {band(eq.low, [&] { return shelf(true, eq.low, eq.lowTurnover, sampleRate); }),
            band(eq.mid, [&] { return peak(eq.mid, eq.midFrequency, eq.octaves(), sampleRate); }),
            band(eq.high, [&] { return shelf(false, eq.high, std::min<double>(eq.highTurnover, sampleRate * 0.45), sampleRate); })};
}

double eqResponse(const AudioEq& eq, double frequency, double sampleRate) {
    const std::complex<double> z1 = std::polar(1.0, -2 * std::numbers::pi * frequency / sampleRate);
    const std::complex<double> z2 = z1 * z1;
    double db = 0;
    for (const Biquad& f : eqFilters(eq, sampleRate)) {
        const std::complex<double> h = (f.b0 + f.b1 * z1 + f.b2 * z2) / (1.0 + f.a1 * z1 + f.a2 * z2);
        db += 20 * std::log10(std::max(std::abs(h), 1e-12));
    }
    return db;
}

QList<EqTemplate> parseEqTemplates(const QByteArray& json, QString* error) {
    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = e.errorString();
        return {};
    }
    QList<EqTemplate> out;
    for (const QJsonValue& v : doc.object().value(QStringLiteral("templates")).toArray()) {
        const QJsonObject o = v.toObject();
        EqTemplate t;
        t.eq.preset = o.value(QStringLiteral("name")).toString();
        if (t.eq.preset.isEmpty()) continue;
        t.eq.low = o.value(QStringLiteral("low")).toDouble();
        t.eq.lowTurnover = o.value(QStringLiteral("lowTurnover")).toInt(t.eq.lowTurnover);
        t.eq.mid = o.value(QStringLiteral("mid")).toDouble();
        t.eq.midFrequency = o.value(QStringLiteral("midFrequency")).toDouble(t.eq.midFrequency);
        t.eq.midOctaves = o.value(QStringLiteral("midOctaves")).toDouble(2.0);
        t.eq.high = o.value(QStringLiteral("high")).toDouble();
        t.eq.highTurnover = o.value(QStringLiteral("highTurnover")).toInt(t.eq.highTurnover);
        t.eq = clampEq(t.eq);
        t.source = o.value(QStringLiteral("source")).toString();
        t.note = o.value(QStringLiteral("note")).toString();
        out << t;
    }
    return out;
}

QString octavesText(double octaves) {
    // Quarters as fractions below 1; others as a number.
    for (int q = 1; q < 4; ++q) {
        if (std::abs(octaves - q / 4.0) < 1e-9) return q == 2 ? QStringLiteral("1/2") : QStringLiteral("%1/4").arg(q);
    }
    return QString::number(octaves, 'g', 3);
}

QString describeEq(const AudioEq& eq) {
    auto hz = [](double f) {
        return f < 1000 ? QStringLiteral("%1 Hz").arg(std::lround(f)) : QStringLiteral("%1 kHz").arg(QString::number(f / 1000, 'g', 3));
    };
    auto db = [](double v) { return (v > 0 ? QStringLiteral("+") : QString()) + QString::number(v, 'f', 1) + QStringLiteral(" dB"); };
    return QStringLiteral("low %1 at %2, mid %3 at %4 (%5 octave), high %6 at %7")
        .arg(db(eq.low), hz(eq.lowTurnover), db(eq.mid), hz(eq.midFrequency), octavesText(eq.octaves()), db(eq.high),
             hz(eq.highTurnover));
}

int setEq(Sequence& sequence, const QString& name, std::int64_t from, std::int64_t to, const std::optional<AudioEq>& eq) {
    Track* t = sequence.track(name);
    if (!t || t->locked || t->kind != TrackKind::Audio) return 0;
    int changed = 0;
    std::int64_t at = 0;
    for (Segment& s : t->segments) {
        const std::int64_t end = at + s.length;
        if (!s.isFiller() && end > from && at < to) {
            const std::optional<AudioEq> next = eq ? std::optional(clampEq(*eq)) : std::nullopt;
            if (s.eq != next) {
                s.eq = next;
                ++changed;
            }
        }
        at = end;
    }
    return changed;
}

BinItem eqTemplateItem(const AudioEq& eq, const QString& name, const QDateTime& when) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::Effect;
    item.name = name;
    item.created = when;
    item.modified = when;
    item.composition.insert(QStringLiteral("eq"), audioEqToJson(eq));
    return item;
}

std::optional<AudioEq> eqTemplateOf(const BinItem& item) {
    if (item.kind != BinItemKind::Effect || !item.composition.contains(QStringLiteral("eq"))) return std::nullopt;
    return audioEqFromJson(item.composition.value(QStringLiteral("eq")).toObject());
}

QString eqTemplateName(const AudioEq& eq) {
    return eq.preset.isEmpty() ? QStringLiteral("Audio EQ") : eq.preset;
}

} // namespace lumenis::model
