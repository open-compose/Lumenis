#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

// Audio effects (Editing Guide ch. 24; docs/spec/track-effects.md): the interface every audio effect
// has, whoever made it, and the effects built in. Audio Track Effects (p.790-795) run a track's
// inserts through these as it plays (media::AudioMix); AudioSuite (p.796-805) runs them over
// stretches of sound offline (lumenis/media/audio_suite.h). A plug-in host (CLAP, lumenis/ext/plugin_host.h) adds its plug-ins with
// registerAudioEffects(), so the rest of Cutthulhu sees one list.
//
// The built-in effects are our own: the guide describes Avid's core plug-ins (p.805-860) by what they
// do, and these do the same kinds of thing under plain names. Their DSP is textbook (Audio EQ
// Cookbook filters, feed-forward dynamics, Freeverb's public-domain reverb design).
namespace lumenis::media {

// One parameter: its id (kept in sequences), name, unit ("dB", "ms", "Hz", "%" or empty), range and
// default. `steps` above 0 makes it a choice of that many values from `min` by 1 (`choices` names
// them).
struct AudioEffectParam {
    QString id;
    QString name;
    QString unit;
    double min = 0;
    double max = 1;
    double defaultValue = 0;
    int steps = 0;
    QStringList choices;
};

// What the Effect Palette and the Audio Track Effect tool show of an effect.
struct AudioEffectInfo {
    QString id;       // "builtin.compressor"; a plug-in's own id for hosted ones
    QString name;     // "Compressor/Limiter"
    QString category; // the Effect Palette's Audio Track category: "Dynamics", "EQ", ...
    QString vendor;   // "Cutthulhu" for the built-in ones
    QList<AudioEffectParam> params;
    // The parameter values a new insert starts with.
    QMap<QString, double> defaults() const;
    const AudioEffectParam* param(const QString& paramId) const;
};

// One running effect on one mono channel at 48 kHz.
class AudioEffect {
public:
    virtual ~AudioEffect() = default;
    virtual const AudioEffectInfo& info() const = 0;
    // Sets a parameter by id, clamped to its range; unknown ids are ignored.
    virtual void set(const QString& id, double value) = 0;
    // Forgets the sound so far (delays, envelopes, filters), as after a seek.
    virtual void reset() = 0;
    // Processes `count` samples in place.
    virtual void process(float* samples, int count) = 0;
    // How long, in samples, the effect goes on sounding after its input stops (a reverb's tail), so
    // a seek can play that much first and the effect is settled where play starts.
    virtual int tail() const = 0;
    // Sets every parameter from `values`; missing ones keep their value.
    void setAll(const QMap<QString, double>& values);
};

// Every effect that can be inserted: the built-in ones, then any registered, in order.
const QList<AudioEffectInfo>& audioEffects();
const AudioEffectInfo* findAudioEffect(const QString& id);
// A new effect with its default parameters; null for an id nothing provides (an "Unavailable
// Effect", p.790).
std::unique_ptr<AudioEffect> makeAudioEffect(const QString& id);
// Adds effects from elsewhere (a plug-in host): their infos and a maker for their ids. Later
// registrations of an id already known are ignored.
using AudioEffectMaker = std::function<std::unique_ptr<AudioEffect>(const QString& id)>;
void registerAudioEffects(const QList<AudioEffectInfo>& effects, AudioEffectMaker maker);

// A parameter value as the tool shows it: "-12.0 dB", "250 ms", "6.00 kHz", "30 %", a choice's name.
QString formatParam(const AudioEffectParam& param, double value);

} // namespace lumenis::media
