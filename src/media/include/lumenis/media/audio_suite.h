#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/audio_fx.h"
#include "lumenis/media/managed.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QMap>
#include <QString>

#include <optional>
#include <vector>

// AudioSuite (Editing Guide p.796-805; docs/spec/audiosuite.md): processes that work on whole
// stretches of sound at once rather than as it plays, and rendering a segment's AudioSuite effect
// (model::AudioSuiteEffect) into managed media.
//
// Avid's AudioSuite plug-ins come from its own plug-in format, which Cutthulhu does not host (no
// public SDK; docs/EXTENSIONS.md). The processes here are our own: the simple ones the guide
// describes in its plug-in reference (Gain, Normalize, Invert, Reverse, DC Offset Removal, Signal
// Generator; p.836-855), then every audio effect (built-in and CLAP, lumenis/media/audio_fx.h) run
// over the sound offline, as the guide's "Audio Track Effect and AudioSuite" plug-ins are.
namespace lumenis::media {

// One process the Plug-In Selection menu offers (p.797).
struct AudioSuiteInfo {
    QString id;       // "suite.normalize"; an audio effect's own id ("builtin.reverb", "clap:...")
    QString name;     // "Normalize"
    QString category; // the menu's group: "Process" for the built-in processes, else the effect's
    QString vendor;   // "Cutthulhu" for ours
    QList<AudioEffectParam> params;
    // An audio effect run offline (its sound past the end is cut, as a segment keeps its length).
    bool effect = false;
    // Makes sound of its own (Signal Generator): what comes in is replaced.
    bool generator = false;
    QMap<QString, double> defaults() const;
    const AudioEffectParam* param(const QString& paramId) const;
};

// The built-in processes, then every audio effect (audioEffects()), in order. Worked out again on
// each call, so effects registered later (a plug-in host) are included.
QList<AudioSuiteInfo> audioSuitePlugins();
std::optional<AudioSuiteInfo> findAudioSuitePlugin(const QString& id);

// Runs process `id` with `params` (missing ones at their defaults) over `channels` (each one channel
// of the same length, 48 kHz), in place. Channels are processed each by itself, except where a
// process says otherwise (Normalize's "Levels On All Chans-Tracks", p.803). False, with `error`, for
// an id nothing provides.
bool processAudioSuite(const QString& id, const QMap<QString, double>& params, std::vector<std::vector<float>>& channels,
                       QString* error = nullptr);

// The sound a segment plays before its AudioSuite effect, at 48 kHz: its source clip's track from its
// source frame for its length in sequence frames at `sequenceRate`, plus one frame, at its clip gain
// (the first in the render order, p.700). Silence where the clip has no media.
std::vector<float> segmentSource(const model::Segment& segment, const model::BinItem& clip, const Rational& sequenceRate);

// Render Effect (p.797, p.800): the segment's AudioSuite effect run over segmentSource() and written
// as managed media ("<clip name> <effect>", one mono PCM file on the creation's audio drive). Returns
// the effect with its render set (model::isRendered), or nothing with `error`.
std::optional<model::AudioSuiteEffect> renderAudioSuite(const model::Segment& segment, const model::BinItem& clip,
                                                        const Rational& sequenceRate, const MediaCreation& creation,
                                                        QString* error = nullptr);

} // namespace lumenis::media
