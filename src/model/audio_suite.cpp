#include "lumenis/model/audio_suite.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

// AudioSuite effects on segments (audio_suite.h).
namespace lumenis::model {

QString audioSuiteKey(const Segment& s) {
    if (!s.audioSuite || s.isFiller()) return {};
    // Everything the rendered sound depends on, in a fixed order. Parameters print with all their
    // digits so any change shows.
    QStringList parts{s.clipId, s.binId, s.sourceTrack, QString::number(s.sourceIn), QString::number(s.length),
                      QString::number(s.gain, 'g', 17)};
    // Every level in turn (p.800): the render holds them all.
    for (const AudioSuiteEffect& e : audioSuiteLevels(s)) {
        parts << e.effect;
        for (auto it = e.params.cbegin(); it != e.params.cend(); ++it) {
            parts << it.key() + QLatin1Char('=') + QString::number(it.value(), 'g', 17);
        }
    }
    return parts.join(QLatin1Char('|'));
}

QList<AudioSuiteEffect> audioSuiteLevels(const Segment& s) {
    if (!s.audioSuite) return {};
    return QList<AudioSuiteEffect>{*s.audioSuite} + s.nestedAudioSuites;
}

bool isRendered(const Segment& s) {
    return s.audioSuite && !s.audioSuite->rendered.isEmpty() && s.audioSuite->renderedKey == audioSuiteKey(s);
}

bool needsRender(const Segment& s) {
    return s.audioSuite && !s.isFiller() && !isRendered(s);
}

int setAudioSuite(Sequence& sequence, const QString& name, std::int64_t from, std::int64_t to,
                  const std::optional<AudioSuiteEffect>& effect, int level) {
    Track* t = sequence.track(name);
    // Multichannel tracks take no AudioSuite effects yet (docs/spec/audiosuite.md).
    if (!t || t->locked || t->kind != TrackKind::Audio || t->format != AudioFormat::Mono || level < 1) return 0;
    int changed = 0;
    std::int64_t at = 0;
    for (Segment& s : t->segments) {
        const std::int64_t end = at + s.length;
        at = end;
        if (s.isFiller() || end - s.length >= to || end <= from) continue;
        QList<AudioSuiteEffect> levels = audioSuiteLevels(s);
        const auto index = static_cast<qsizetype>(level - 1);
        if (index > levels.size()) continue; // past the next free level
        const std::optional<AudioSuiteEffect> old = index < levels.size() ? std::optional(levels.at(index)) : std::nullopt;
        if (effect) {
            AudioSuiteEffect next = *effect;
            next.rendered.clear();
            next.renderedStream = 0;
            next.renderedChannel = 0;
            next.renderedKey.clear();
            // The same effect with the same values keeps itself, and level 1 its render.
            if (old && old->effect == effect->effect && old->params == effect->params) {
                next = *old;
                next.name = effect->name;
            }
            if (index < levels.size()) {
                levels[index] = next;
            } else {
                levels.append(next);
            }
        } else {
            if (!old) continue;
            levels.removeAt(index);
        }
        // Level 1 keeps the render of every level; the render's key tells when it is out of date (audioSuiteKey).
        std::optional<AudioSuiteEffect> first;
        if (!levels.isEmpty()) {
            first = levels.takeFirst();
            if (s.audioSuite && index > 0) {
                first->rendered = s.audioSuite->rendered;
                first->renderedStream = s.audioSuite->renderedStream;
                first->renderedChannel = s.audioSuite->renderedChannel;
                first->renderedKey = s.audioSuite->renderedKey;
            }
        }
        if (s.audioSuite != first || s.nestedAudioSuites != levels) {
            s.audioSuite = first;
            s.nestedAudioSuites = levels;
            ++changed;
        }
    }
    return changed;
}

QJsonObject audioSuiteTemplateToJson(const AudioSuiteTemplate& t) {
    AudioSuiteEffect e = t.effect;
    e.rendered.clear();
    e.renderedKey.clear();
    e.renderedStream = 0;
    e.renderedChannel = 0;
    QJsonObject o{{QStringLiteral("effect"), audioSuiteToJson(e)}};
    if (t.hasSource()) {
        o.insert(QStringLiteral("sourceBin"), t.sourceBin);
        o.insert(QStringLiteral("sourceClip"), t.sourceClip);
        QJsonArray tracks;
        for (const int k : t.tracks) tracks.append(k);
        o.insert(QStringLiteral("tracks"), tracks);
        if (t.in) o.insert(QStringLiteral("in"), static_cast<qint64>(*t.in));
        if (t.out) o.insert(QStringLiteral("out"), static_cast<qint64>(*t.out));
        if (t.handleSeconds > 0) o.insert(QStringLiteral("handle"), t.handleSeconds);
    }
    return o;
}

std::optional<AudioSuiteTemplate> audioSuiteTemplateFromJson(const QJsonObject& o) {
    AudioSuiteTemplate t;
    t.effect = audioSuiteFromJson(o.value(QStringLiteral("effect")).toObject());
    if (t.effect.effect.isEmpty()) return std::nullopt;
    t.sourceBin = o.value(QStringLiteral("sourceBin")).toString();
    t.sourceClip = o.value(QStringLiteral("sourceClip")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("tracks")).toArray()) t.tracks << v.toInt();
    if (o.contains(QStringLiteral("in"))) t.in = o.value(QStringLiteral("in")).toInteger();
    if (o.contains(QStringLiteral("out"))) t.out = o.value(QStringLiteral("out")).toInteger();
    t.handleSeconds = std::max(o.value(QStringLiteral("handle")).toDouble(), 0.0);
    return t;
}

BinItem audioSuiteTemplateItem(const AudioSuiteTemplate& t, const QString& name, const QDateTime& when) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::Effect;
    item.name = name;
    item.created = when;
    item.modified = when;
    item.composition.insert(QStringLiteral("audioSuite"), audioSuiteTemplateToJson(t));
    return item;
}

std::optional<AudioSuiteTemplate> audioSuiteTemplateOf(const BinItem& item) {
    if (item.kind != BinItemKind::Effect || !item.composition.contains(QStringLiteral("audioSuite"))) return std::nullopt;
    return audioSuiteTemplateFromJson(item.composition.value(QStringLiteral("audioSuite")).toObject());
}

QString audioSuiteClipName(const QString& clip, const QString& effect, bool mac) {
    return clip + (mac ? QLatin1Char('.') : QLatin1Char('_')) + effect;
}

QString audioSuiteTemplateName(const QString& clip, const QString& effect, bool mac) {
    return mac ? QStringLiteral("%1.%2.effect").arg(clip, effect) : QStringLiteral("%1 - Audio Processing Effect: %2").arg(clip, effect);
}

QJsonObject audioSuiteToJson(const AudioSuiteEffect& e) {
    QJsonObject params;
    for (auto it = e.params.cbegin(); it != e.params.cend(); ++it) params.insert(it.key(), it.value());
    QJsonObject o{{QStringLiteral("effect"), e.effect}, {QStringLiteral("name"), e.name}, {QStringLiteral("params"), params}};
    if (!e.rendered.isEmpty()) {
        o.insert(QStringLiteral("rendered"), e.rendered);
        o.insert(QStringLiteral("renderedStream"), e.renderedStream);
        o.insert(QStringLiteral("renderedChannel"), e.renderedChannel);
        o.insert(QStringLiteral("renderedKey"), e.renderedKey);
    }
    return o;
}

AudioSuiteEffect audioSuiteFromJson(const QJsonObject& o) {
    AudioSuiteEffect e;
    e.effect = o.value(QStringLiteral("effect")).toString();
    e.name = o.value(QStringLiteral("name")).toString();
    const QJsonObject params = o.value(QStringLiteral("params")).toObject();
    for (auto it = params.begin(); it != params.end(); ++it) e.params.insert(it.key(), it.value().toDouble());
    e.rendered = o.value(QStringLiteral("rendered")).toString();
    e.renderedStream = o.value(QStringLiteral("renderedStream")).toInt();
    e.renderedChannel = o.value(QStringLiteral("renderedChannel")).toInt();
    e.renderedKey = o.value(QStringLiteral("renderedKey")).toString();
    return e;
}

} // namespace lumenis::model
