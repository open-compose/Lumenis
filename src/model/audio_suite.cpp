#include "lumenis/model/audio_suite.h"

#include <QJsonObject>

// AudioSuite effects on segments (audio_suite.h).
namespace lumenis::model {

QString audioSuiteKey(const Segment& s) {
    if (!s.audioSuite || s.isFiller()) return {};
    // Everything the rendered sound depends on, in a fixed order. Parameters print with all their
    // digits so any change shows.
    QStringList parts{s.clipId, s.binId, s.sourceTrack, QString::number(s.sourceIn), QString::number(s.length),
                      QString::number(s.gain, 'g', 17), s.audioSuite->effect};
    for (auto it = s.audioSuite->params.cbegin(); it != s.audioSuite->params.cend(); ++it) {
        parts << it.key() + QLatin1Char('=') + QString::number(it.value(), 'g', 17);
    }
    return parts.join(QLatin1Char('|'));
}

bool isRendered(const Segment& s) {
    return s.audioSuite && !s.audioSuite->rendered.isEmpty() && s.audioSuite->renderedKey == audioSuiteKey(s);
}

bool needsRender(const Segment& s) {
    return s.audioSuite && !s.isFiller() && !isRendered(s);
}

int setAudioSuite(Sequence& sequence, const QString& name, std::int64_t from, std::int64_t to,
                  const std::optional<AudioSuiteEffect>& effect) {
    Track* t = sequence.track(name);
    if (!t || t->locked || t->kind != TrackKind::Audio) return 0;
    int changed = 0;
    std::int64_t at = 0;
    for (Segment& s : t->segments) {
        const std::int64_t end = at + s.length;
        if (!s.isFiller() && end > from && at < to) {
            std::optional<AudioSuiteEffect> next;
            if (effect) {
                next = *effect;
                next->rendered.clear();
                next->renderedStream = 0;
                next->renderedChannel = 0;
                next->renderedKey.clear();
                // The same effect with the same values keeps its render.
                if (s.audioSuite && s.audioSuite->effect == effect->effect && s.audioSuite->params == effect->params) {
                    next = s.audioSuite;
                    next->name = effect->name;
                }
            }
            if (s.audioSuite != next) {
                s.audioSuite = next;
                ++changed;
            }
        }
        at = end;
    }
    return changed;
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
