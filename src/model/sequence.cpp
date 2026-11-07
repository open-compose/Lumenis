#include "lumenis/model/sequence.h"

#include "lumenis/model/group_clip.h"

#include "lumenis/model/timewarp.h"

#include "lumenis/model/audio_eq.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/clip_audio.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/multichannel.h"
#include "lumenis/model/surround_pan.h"
#include "lumenis/model/title_clip.h"
#include "lumenis/model/tracking.h"
#include "lumenis/model/timewarp.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

// Sequences (sequence.h): tracks of segments stored as JSON in a sequence's BinItem::composition,
// and the editing operations on them: splice, overwrite, lift and extract, three-point edits and
// track patching, segment moves, trims, slip and slide, effects and transitions, clip gain and
// pan, and undo. Everything here is pure data with no widgets; the Timeline and Composer
// (src/ui/timeline.*, composer.*) call it, AAF import and export (src/interop/aaf_*) read and
// build sequences, and structure dumps (structure.*) print them. Frames are always counted from
// the sequence's or clip's first frame, at the item's edit rate. docs/spec/native-model.md
// describes how segments refer to clips.
namespace lumenis::model {

namespace {

// "num/den" or a whole number, positive; nothing otherwise.
std::optional<Rational> rateFromText(const QString& text) {
    const QStringList parts = text.split(QLatin1Char('/'));
    bool okNum = false;
    bool okDen = true;
    const qint64 num = parts.value(0).toLongLong(&okNum);
    const qint64 den = parts.size() > 1 ? parts.value(1).toLongLong(&okDen) : 1;
    if (!okNum || !okDen || num <= 0 || den <= 0 || parts.size() > 2) return std::nullopt;
    return Rational(num, den);
}

// The letter of a track name, and the order of the kinds in a sequence.
QChar kindLetter(TrackKind kind) {
    switch (kind) {
    case TrackKind::Video: return QLatin1Char('V');
    case TrackKind::Audio: return QLatin1Char('A');
    case TrackKind::Data: return QLatin1Char('D');
    }
    return QLatin1Char('V');
}

int kindOrder(TrackKind kind) {
    switch (kind) {
    case TrackKind::Video: return 0;
    case TrackKind::Audio: return 1;
    case TrackKind::Data: return 2;
    }
    return 0;
}

// The names used in composition JSON for audio formats and transition alignments.
const std::pair<AudioFormat, const char*> kFormats[] = {
    {AudioFormat::Mono, "mono"},
    {AudioFormat::Stereo, "stereo"},
    {AudioFormat::Surround51, "5.1"},
    {AudioFormat::Surround71, "7.1"},
};

const std::pair<TransitionAlignment, const char*> kAlignments[] = {
    {TransitionAlignment::EndingAtCut, "ending"},
    {TransitionAlignment::CenteredOnCut, "centered"},
    {TransitionAlignment::StartingAtCut, "starting"},
    {TransitionAlignment::CustomStart, "custom"},
};

// A render as {clips: [...], key}; nothing for JSON without a clip. (6.4a's single {clip} is read too.)
QJsonObject renderToJson(const EffectRender& r) {
    QJsonArray clips;
    for (const QJsonObject& c : r.clips) clips.append(c);
    return {{QStringLiteral("clips"), clips}, {QStringLiteral("key"), r.key}};
}
std::optional<EffectRender> renderFromJson(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    EffectRender r;
    for (const QJsonValue& c : o.value(QStringLiteral("clips")).toArray()) {
        if (!c.toObject().isEmpty()) r.clips << c.toObject();
    }
    if (o.value(QStringLiteral("clip")).isObject() && !o.value(QStringLiteral("clip")).toObject().isEmpty()) {
        r.clips << o.value(QStringLiteral("clip")).toObject();
    }
    if (r.clips.isEmpty()) return std::nullopt;
    r.key = o.value(QStringLiteral("key")).toString();
    return r;
}

// {effect, duration, alignment} plus start for CustomStart.
QJsonObject transitionToJson(const Transition& t) {
    QJsonObject o{{QStringLiteral("effect"), t.effect.toJson()},
                  {QStringLiteral("duration"), static_cast<qint64>(t.duration)}};
    for (const auto& [a, text] : kAlignments) {
        if (a == t.alignment) o.insert(QStringLiteral("alignment"), QString::fromLatin1(text));
    }
    if (t.alignment == TransitionAlignment::CustomStart) o.insert(QStringLiteral("start"), static_cast<qint64>(t.start));
    return o;
}

Transition transitionFromJson(const QJsonObject& o) {
    Transition t;
    t.effect = Effect::fromJson(o.value(QStringLiteral("effect")).toObject());
    t.duration = std::max<std::int64_t>(o.value(QStringLiteral("duration")).toInteger(), 0);
    const QString alignment = o.value(QStringLiteral("alignment")).toString();
    for (const auto& [a, text] : kAlignments) {
        if (alignment == QLatin1String(text)) t.alignment = a;
    }
    t.start = o.value(QStringLiteral("start")).toInteger();
    return t;
}

// Keyframes as [[frame, value], ...]; read back sorted, one a frame, values clamped.
QJsonArray keysToJson(const QList<AudioKey>& keys) {
    QJsonArray a;
    for (const AudioKey& k : keys) a.append(QJsonArray{k.frame, k.value});
    return a;
}

QList<AudioKey> keysFromJson(const QJsonValue& v, bool pan) {
    QList<AudioKey> keys;
    for (const QJsonValue& k : v.toArray()) {
        const QJsonArray a = k.toArray();
        if (a.size() < 2) continue;
        const double value = a.at(1).toDouble();
        keys << AudioKey{a.at(0).toDouble(), pan ? std::clamp(value, -double(kPanMax), double(kPanMax)) : clampGain(value)};
    }
    std::stable_sort(keys.begin(), keys.end(), [](const AudioKey& x, const AudioKey& y) { return x.frame < y.frame; });
    QList<AudioKey> out;
    for (const AudioKey& k : keys) {
        if (!out.isEmpty() && out.last().frame == k.frame) {
            out.last() = k;
        } else {
            out << k;
        }
    }
    return out;
}

// {low, mid, high, lowTurnover, highTurnover, midFrequency, wideMid, in}; fromJson() puts
// out-of-range values back in range (clampEq).
QJsonObject eqToJson(const AudioEq& e) {
    QJsonObject o{{QStringLiteral("low"), e.low},
                  {QStringLiteral("mid"), e.mid},
                  {QStringLiteral("high"), e.high},
                  {QStringLiteral("lowTurnover"), e.lowTurnover},
                  {QStringLiteral("highTurnover"), e.highTurnover},
                  {QStringLiteral("midFrequency"), e.midFrequency},
                  {QStringLiteral("wideMid"), e.wideMid},
                  {QStringLiteral("in"), e.in}};
    if (!e.preset.isEmpty()) o.insert(QStringLiteral("preset"), e.preset);
    if (e.midOctaves > 0) o.insert(QStringLiteral("midOctaves"), e.midOctaves);
    return o;
}

AudioEq eqFromJson(const QJsonObject& o) {
    AudioEq e;
    e.low = o.value(QStringLiteral("low")).toDouble();
    e.mid = o.value(QStringLiteral("mid")).toDouble();
    e.high = o.value(QStringLiteral("high")).toDouble();
    e.lowTurnover = o.value(QStringLiteral("lowTurnover")).toInt(e.lowTurnover);
    e.highTurnover = o.value(QStringLiteral("highTurnover")).toInt(e.highTurnover);
    e.midFrequency = o.value(QStringLiteral("midFrequency")).toDouble(e.midFrequency);
    e.wideMid = o.value(QStringLiteral("wideMid")).toBool(true);
    e.in = o.value(QStringLiteral("in")).toBool(true);
    e.preset = o.value(QStringLiteral("preset")).toString();
    e.midOctaves = o.value(QStringLiteral("midOctaves")).toDouble();
    return clampEq(e);
}

void effectsToJson(const Segment& s, QJsonObject& o) {
    if (s.gain != 0.0) o.insert(QStringLiteral("gain"), s.gain);
    if (s.ownPan()) o.insert(QStringLiteral("pan"), s.pan);
    if (!s.volumeKeys.isEmpty()) o.insert(QStringLiteral("volumeKeys"), keysToJson(s.volumeKeys));
    if (!s.panKeys.isEmpty()) o.insert(QStringLiteral("panKeys"), keysToJson(s.panKeys));
    if (s.eq) o.insert(QStringLiteral("eq"), eqToJson(*s.eq));
    if (s.audioSuite) o.insert(QStringLiteral("audioSuite"), audioSuiteToJson(*s.audioSuite));
    if (s.effect) o.insert(QStringLiteral("effect"), s.effect->toJson());
    if (s.effect && s.render) o.insert(QStringLiteral("render"), renderToJson(*s.render));
    if (s.transition) o.insert(QStringLiteral("transition"), transitionToJson(*s.transition));
    if (s.transition && s.transitionRender) o.insert(QStringLiteral("transitionRender"), renderToJson(*s.transitionRender));
    if (s.hasNest()) {
        o.insert(QStringLiteral("nest"), s.nest);
        if (s.nestIn != 0) o.insert(QStringLiteral("nestIn"), static_cast<qint64>(s.nestIn));
    }
}

// See Sequence::toJson() in sequence.h for the layout. Defaults (0 gain and pan, no MobID of its
// own) are left out.
QJsonObject segmentToJson(const Segment& s) {
    QJsonObject o{{QStringLiteral("length"), static_cast<qint64>(s.length)}};
    effectsToJson(s, o);
    if (s.isFiller()) {
        if (s.addEdit) o.insert(QStringLiteral("addEdit"), true);
        return o;
    }
    o.insert(QStringLiteral("clip"), s.clipId);
    if (!s.sourceMob.isNull()) o.insert(QStringLiteral("mob"), QString::fromStdString(s.sourceMob.toString()));
    if (!s.binId.isEmpty()) o.insert(QStringLiteral("bin"), s.binId);
    o.insert(QStringLiteral("name"), s.name);
    o.insert(QStringLiteral("track"), s.sourceTrack);
    o.insert(QStringLiteral("in"), static_cast<qint64>(s.sourceIn));
    if (!s.localColor.isEmpty()) o.insert(QStringLiteral("localColor"), s.localColor);
    if (!s.note.isEmpty()) o.insert(QStringLiteral("note"), s.note);
    if (s.groupAngle) o.insert(QStringLiteral("groupAngle"), *s.groupAngle);
    if (!s.groupOffsets.isEmpty()) {
        QJsonArray offsets;
        for (const std::int64_t v : s.groupOffsets) offsets.append(static_cast<qint64>(v));
        o.insert(QStringLiteral("groupOffsets"), offsets);
    }
    if (!s.correctionComment.isEmpty()) o.insert(QStringLiteral("correctionComment"), s.correctionComment);
    if (!s.sourceCorrection.isEmpty()) o.insert(QStringLiteral("sourceCorrection"), s.sourceCorrection.toJson());
    if (!s.programCorrection.isEmpty()) o.insert(QStringLiteral("programCorrection"), s.programCorrection.toJson());
    if (!s.sourceCorrection.isEmpty() && s.sourceRelationship != CorrectionRelationship::SourceSegment)
        o.insert(QStringLiteral("sourceRelationship"), relationshipKey(s.sourceRelationship));
    if (!s.programCorrection.isEmpty() && s.programRelationship != CorrectionRelationship::ProgramSegment)
        o.insert(QStringLiteral("programRelationship"), relationshipKey(s.programRelationship));
    if (!s.surround.isEmpty()) {
        QJsonArray pans;
        for (const SurroundPan& p : s.surround) pans.append(surroundPanToJson(p));
        o.insert(QStringLiteral("surround"), pans);
    }
    if (s.adapter) {
        o.insert(QStringLiteral("adapter"), QJsonObject{{QStringLiteral("rate"), QString::fromStdString(s.adapter->clipRate.toString())},
                                                       {QStringLiteral("sequenceRate"), QString::fromStdString(s.adapter->sequenceRate.toString())},
                                                       {QStringLiteral("fieldMotion"), s.adapter->fieldMotion},
                                                       {QStringLiteral("type"), s.adapter->type}});
        if (s.adapter->interlaced) {
            QJsonObject a = o.value(QStringLiteral("adapter")).toObject();
            a.insert(QStringLiteral("interlaced"), true);
            o.insert(QStringLiteral("adapter"), a);
        }
    }
    return o;
}

// A stored "mob" equal to the one derived from the clip id is dropped, so segments that never had
// a MobID of their own stay that way. Gain and pan are clamped to their ranges.
Segment segmentFromJson(const QJsonObject& o) {
    Segment s;
    s.clipId = o.value(QStringLiteral("clip")).toString();
    if (const auto mob = MobId::fromString(o.value(QStringLiteral("mob")).toString().toStdString());
        mob && !mob->isNull() && *mob != derivedMobId(s.clipId)) {
        s.sourceMob = *mob;
    }
    s.binId = o.value(QStringLiteral("bin")).toString();
    s.name = o.value(QStringLiteral("name")).toString();
    s.sourceTrack = o.value(QStringLiteral("track")).toString();
    s.sourceIn = o.value(QStringLiteral("in")).toInteger();
    s.nest = o.value(QStringLiteral("nest")).toObject();
    s.nestIn = s.nest.isEmpty() ? 0 : o.value(QStringLiteral("nestIn")).toInteger();
    if (!s.isFiller()) s.localColor = o.value(QStringLiteral("localColor")).toString();
    if (!s.isFiller()) s.note = o.value(QStringLiteral("note")).toString();
    if (!s.isFiller() && o.contains(QStringLiteral("groupAngle"))) s.groupAngle = o.value(QStringLiteral("groupAngle")).toInt();
    if (!s.isFiller()) {
        for (const QJsonValue& v : o.value(QStringLiteral("groupOffsets")).toArray()) s.groupOffsets << v.toInteger();
    }
    if (!s.isFiller()) s.correctionComment = o.value(QStringLiteral("correctionComment")).toString();
    if (!s.isFiller()) {
        s.sourceCorrection = ColorCorrection::fromJson(o.value(QStringLiteral("sourceCorrection")).toObject());
        s.programCorrection = ColorCorrection::fromJson(o.value(QStringLiteral("programCorrection")).toObject());
        // A relationship of the other side, or an unknown one, is the side's segment relationship.
        const auto source = relationshipFromKey(o.value(QStringLiteral("sourceRelationship")).toString());
        if (source && sideOf(*source) == CorrectionSide::Source) s.sourceRelationship = *source;
        const auto program = relationshipFromKey(o.value(QStringLiteral("programRelationship")).toString());
        if (program && sideOf(*program) == CorrectionSide::Program) s.programRelationship = *program;
    }
    for (const QJsonValue& v : o.value(QStringLiteral("surround")).toArray()) {
        if (!s.isFiller() && s.surround.size() < 2) s.surround << surroundPanFromJson(v.toObject());
    }
    if (const QJsonObject a = o.value(QStringLiteral("adapter")).toObject(); !s.isFiller() && !a.isEmpty()) {
        const auto clipRate = rateFromText(a.value(QStringLiteral("rate")).toString());
        const auto sequenceRate = rateFromText(a.value(QStringLiteral("sequenceRate")).toString());
        if (clipRate && sequenceRate) {
            s.adapter = MotionAdapter{*clipRate, *sequenceRate, a.value(QStringLiteral("fieldMotion")).toString(),
                                      a.value(QStringLiteral("type")).toString(), a.value(QStringLiteral("interlaced")).toBool()};
        }
    }
    s.length = std::max<std::int64_t>(o.value(QStringLiteral("length")).toInteger(), 0);
    s.addEdit = s.isFiller() && o.value(QStringLiteral("addEdit")).toBool();
    s.gain = clampGain(o.value(QStringLiteral("gain")).toDouble());
    s.pan = std::clamp(o.value(QStringLiteral("pan")).toInt(), -kPanMax, kPanMax);
    s.hasPan = o.contains(QStringLiteral("pan"));
    s.volumeKeys = keysFromJson(o.value(QStringLiteral("volumeKeys")), false);
    s.panKeys = keysFromJson(o.value(QStringLiteral("panKeys")), true);
    if (o.contains(QStringLiteral("eq"))) s.eq = eqFromJson(o.value(QStringLiteral("eq")).toObject());
    if (o.contains(QStringLiteral("audioSuite"))) s.audioSuite = audioSuiteFromJson(o.value(QStringLiteral("audioSuite")).toObject());
    if (o.contains(QStringLiteral("effect"))) s.effect = Effect::fromJson(o.value(QStringLiteral("effect")).toObject());
    if (s.effect) s.render = renderFromJson(o.value(QStringLiteral("render")));
    if (o.contains(QStringLiteral("transition"))) {
        s.transition = transitionFromJson(o.value(QStringLiteral("transition")).toObject());
        s.transitionRender = renderFromJson(o.value(QStringLiteral("transitionRender")));
    }
    return s;
}

} // namespace

QJsonObject audioEqToJson(const AudioEq& eq) {
    return eqToJson(eq);
}

AudioEq audioEqFromJson(const QJsonObject& o) {
    return eqFromJson(o);
}

MobId Segment::sourceMobId() const {
    if (isFiller()) return {};
    return sourceMob.isNull() ? derivedMobId(clipId) : sourceMob;
}

QJsonObject Effect::toJson() const {
    QJsonObject o{{QStringLiteral("category"), category}, {QStringLiteral("name"), name}};
    if (!parameters.isEmpty()) o.insert(QStringLiteral("parameters"), parameters);
    return o;
}

Effect Effect::fromJson(const QJsonObject& o) {
    return Effect{o.value(QStringLiteral("category")).toString(), o.value(QStringLiteral("name")).toString(),
                  o.value(QStringLiteral("parameters")).toObject()};
}

std::int64_t Transition::before() const {
    switch (alignment) {
    case TransitionAlignment::EndingAtCut: return duration;
    case TransitionAlignment::CenteredOnCut: return duration / 2;
    case TransitionAlignment::StartingAtCut: return 0;
    case TransitionAlignment::CustomStart: return std::clamp<std::int64_t>(start, 0, duration);
    }
    return 0;
}

QString trackName(TrackKind kind, int number) {
    return kindLetter(kind) + QString::number(number);
}

std::optional<TrackRef> parseTrackName(const QString& name) {
    static const QRegularExpression re(QStringLiteral("^([VAD])(\\d{1,2})$"));
    const auto m = re.match(name.trimmed().toUpper());
    if (!m.hasMatch()) return std::nullopt;
    const int number = m.captured(2).toInt();
    if (number < 1) return std::nullopt;
    const QChar k = m.captured(1).at(0);
    TrackRef ref;
    ref.kind = k == QLatin1Char('V') ? TrackKind::Video : k == QLatin1Char('A') ? TrackKind::Audio : TrackKind::Data;
    ref.number = number;
    return ref;
}

QStringList parseTrackList(const QString& text) {
    // "V1 A1-2", "V1-2 A1 A2 TC1": a letter and a number or a range.
    static const QRegularExpression re(QStringLiteral("^([VAD])(\\d{1,2})(?:-(\\d{1,2}))?$"));
    QStringList out;
    for (const QString& token : text.toUpper().split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
        const auto m = re.match(token);
        if (!m.hasMatch()) continue;
        const int first = m.captured(2).toInt();
        const int last = m.captured(3).isEmpty() ? first : m.captured(3).toInt();
        for (int n = std::max(first, 1); n <= std::min(last, kMaxTracks); ++n) {
            const QString name = m.captured(1) + QString::number(n);
            if (!out.contains(name)) out << name;
        }
    }
    return out;
}

Segment Segment::filler(std::int64_t length) {
    Segment s;
    s.length = length;
    return s;
}

bool Track::hasInserts() const {
    return std::any_of(inserts.begin(), inserts.end(), [](const auto& in) { return in.has_value(); });
}

std::int64_t Track::length() const {
    std::int64_t total = 0;
    for (const Segment& s : segments) total += s.length;
    return total;
}

// A linear walk; frames before 0 or at or past the end give -1.
int Track::segmentAt(std::int64_t frame, std::int64_t* start) const {
    std::int64_t at = 0;
    for (int i = 0; i < segments.size(); ++i) {
        const std::int64_t end = at + segments.at(i).length;
        if (frame >= at && frame < end) {
            if (start) *start = at;
            return i;
        }
        at = end;
    }
    return -1;
}

QList<std::int64_t> Track::editPoints() const {
    QList<std::int64_t> points{0};
    std::int64_t at = 0;
    for (const Segment& s : segments) {
        at += s.length;
        points << at;
    }
    return points;
}

// TC1 is not a track here: timecode_ defaults to true.
Sequence Sequence::withDefaultTracks() {
    Sequence s;
    s.tracks_ = {Track{TrackKind::Video, 1, AudioFormat::Mono, {}}, Track{TrackKind::Audio, 1, AudioFormat::Mono, {}},
                 Track{TrackKind::Audio, 2, AudioFormat::Mono, {}}};
    return s;
}

void Sequence::setMixFormat(AudioFormat format) {
    mixFormat_ = format == AudioFormat::Mono ? AudioFormat::Stereo : format;
}

Sequence Sequence::fromItem(const BinItem& item) {
    if (!item.composition.isEmpty()) return fromJson(item.composition);
    Sequence s = withDefaultTracks();
    // A sequence made before it had tracks keeps its length as filler.
    if (item.durationFrames > 0) s.tracks_.first().segments << Segment::filler(item.durationFrames);
    return s;
}

void Sequence::storeIn(BinItem& item) const {
    item.composition = toJson();
    item.durationFrames = duration();
}

QJsonObject Sequence::toJson() const {
    QJsonArray tracks;
    for (const Track& t : tracks_) {
        QJsonArray segments;
        for (const Segment& s : t.segments) segments.append(segmentToJson(s));
        QJsonObject o{{QStringLiteral("name"), t.name()}, {QStringLiteral("segments"), segments}};
        if (t.locked) o.insert(QStringLiteral("locked"), true);
        if (t.syncLocked) o.insert(QStringLiteral("syncLocked"), true);
        if (t.lfeOnly) o.insert(QStringLiteral("lfeOnly"), true);
        // The per track display settings (p.677), written only when set.
        QJsonObject display;
        if (t.showWaveform) display.insert(QStringLiteral("waveform"), true);
        if (t.showClipVolume) display.insert(QStringLiteral("clipVolume"), true);
        if (t.showVolume) display.insert(QStringLiteral("volume"), true);
        if (t.showPan) display.insert(QStringLiteral("pan"), true);
        if (t.heightStep != 0) display.insert(QStringLiteral("height"), t.heightStep);
        if (!t.color.isEmpty()) display.insert(QStringLiteral("color"), t.color);
        if (!display.isEmpty()) o.insert(QStringLiteral("display"), display);
        if (t.inactive) o.insert(QStringLiteral("inactive"), true);
        if (!t.customName.isEmpty()) o.insert(QStringLiteral("customName"), t.customName);
        if (t.kind == TrackKind::Audio && t.format != AudioFormat::Mono) {
            for (const auto& [format, text] : kFormats) {
                if (format == t.format) o.insert(QStringLiteral("format"), QString::fromLatin1(text));
            }
        }
        if (t.hasInserts()) {
            QJsonArray inserts;
            for (int slot = 0; slot < kTrackInserts; ++slot) {
                const auto& in = t.inserts[static_cast<std::size_t>(slot)];
                if (!in) continue;
                QJsonObject params;
                for (auto it = in->params.begin(); it != in->params.end(); ++it) params.insert(it.key(), it.value());
                QJsonObject io{{QStringLiteral("slot"), slot},
                               {QStringLiteral("effect"), in->effect},
                               {QStringLiteral("name"), in->name},
                               {QStringLiteral("params"), params}};
                if (in->bypass) io.insert(QStringLiteral("bypass"), true);
                inserts.append(io);
            }
            o.insert(QStringLiteral("inserts"), inserts);
        }
        tracks.append(o);
    }
    QJsonObject out{{QStringLiteral("tracks"), tracks}, {QStringLiteral("timecode"), timecode_}};
    if (editedGroup_.size() == 2) out.insert(QStringLiteral("editsGroup"), QJsonArray::fromStringList(editedGroup_));
    if (mixFormat_ != AudioFormat::Stereo) {
        for (const auto& [format, text] : kFormats) {
            if (format == mixFormat_) out.insert(QStringLiteral("mix"), QString::fromLatin1(text));
        }
    }
    return out;
}

Sequence Sequence::fromJson(const QJsonObject& o) {
    Sequence s;
    s.timecode_ = o.value(QStringLiteral("timecode")).toBool(true);
    if (const QJsonArray g = o.value(QStringLiteral("editsGroup")).toArray(); g.size() == 2) {
        s.editedGroup_ = {g.at(0).toString(), g.at(1).toString()};
    }
    const QString mix = o.value(QStringLiteral("mix")).toString();
    for (const auto& [f, text] : kFormats) {
        if (mix == QLatin1String(text)) s.setMixFormat(f);
    }
    for (const QJsonValue& v : o.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject to = v.toObject();
        const auto ref = parseTrackName(to.value(QStringLiteral("name")).toString());
        if (!ref || s.track(trackName(ref->kind, ref->number))) continue;
        Track t;
        t.kind = ref->kind;
        t.number = ref->number;
        t.locked = to.value(QStringLiteral("locked")).toBool();
        t.syncLocked = to.value(QStringLiteral("syncLocked")).toBool();
        const QString format = to.value(QStringLiteral("format")).toString();
        for (const auto& [f, text] : kFormats) {
            if (format == QLatin1String(text)) t.format = f;
        }
        t.lfeOnly = t.kind == TrackKind::Audio && t.format == AudioFormat::Mono && to.value(QStringLiteral("lfeOnly")).toBool();
        const QJsonObject display = to.value(QStringLiteral("display")).toObject();
        const bool audio = t.kind == TrackKind::Audio;
        t.showWaveform = audio && display.value(QStringLiteral("waveform")).toBool();
        t.showClipVolume = audio && display.value(QStringLiteral("clipVolume")).toBool();
        t.showVolume = audio && display.value(QStringLiteral("volume")).toBool();
        t.showPan = audio && !t.showVolume && display.value(QStringLiteral("pan")).toBool();
        t.heightStep = std::clamp(display.value(QStringLiteral("height")).toInt(), -kTrackHeightSteps / 2, kTrackHeightSteps);
        t.color = display.value(QStringLiteral("color")).toString();
        static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
        if (!hex.match(t.color).hasMatch()) t.color.clear();
        t.inactive = audio && to.value(QStringLiteral("inactive")).toBool();
        t.customName = to.value(QStringLiteral("customName")).toString().trimmed();
        for (const QJsonValue& sv : to.value(QStringLiteral("segments")).toArray()) {
            t.segments << segmentFromJson(sv.toObject());
        }
        for (const QJsonValue& iv : to.value(QStringLiteral("inserts")).toArray()) {
            const QJsonObject io = iv.toObject();
            const int slot = io.value(QStringLiteral("slot")).toInt(-1);
            if (t.kind != TrackKind::Audio || slot < 0 || slot >= kTrackInserts) continue;
            TrackInsert in;
            in.effect = io.value(QStringLiteral("effect")).toString();
            in.name = io.value(QStringLiteral("name")).toString();
            const QJsonObject params = io.value(QStringLiteral("params")).toObject();
            for (auto it = params.begin(); it != params.end(); ++it) in.params.insert(it.key(), it.value().toDouble());
            in.bypass = io.value(QStringLiteral("bypass")).toBool();
            if (!in.effect.isEmpty()) t.inserts[static_cast<std::size_t>(slot)] = in;
        }
        tidy(t);
        s.tracks_ << t;
    }
    s.sortTracks();
    return s;
}

const Track* Sequence::track(const QString& name) const {
    for (const Track& t : tracks_) {
        if (t.name() == name) return &t;
    }
    return nullptr;
}

Track* Sequence::track(const QString& name) {
    for (Track& t : tracks_) {
        if (t.name() == name) return &t;
    }
    return nullptr;
}

QStringList Sequence::trackNames() const {
    QStringList names;
    for (const Track& t : tracks_) names << t.name();
    return names;
}

void Sequence::sortTracks() {
    std::stable_sort(tracks_.begin(), tracks_.end(), [](const Track& a, const Track& b) {
        if (a.kind != b.kind) return kindOrder(a.kind) < kindOrder(b.kind);
        return a.number < b.number;
    });
}

std::optional<QString> Sequence::addTrack(TrackKind kind, AudioFormat format) {
    const int limit = kind == TrackKind::Data ? 1 : kMaxTracks;
    for (int n = 1; n <= limit; ++n) {
        if (track(trackName(kind, n))) continue;
        tracks_ << Track{kind, n, kind == TrackKind::Audio ? format : AudioFormat::Mono, {}};
        sortTracks();
        return trackName(kind, n);
    }
    return std::nullopt;
}

Track* Sequence::ensureTrack(const QString& name, AudioFormat format) {
    if (Track* t = track(name)) return t;
    const auto ref = parseTrackName(name);
    if (!ref || ref->number > (ref->kind == TrackKind::Data ? 1 : kMaxTracks)) return nullptr;
    tracks_ << Track{ref->kind, ref->number, ref->kind == TrackKind::Audio ? format : AudioFormat::Mono, {}};
    sortTracks();
    return track(name);
}

void Sequence::restoreTrack(const Track& t) {
    removeTrack(t.name());
    tracks_ << t;
    sortTracks();
}

std::int64_t Sequence::duration() const {
    std::int64_t d = 0;
    for (const Track& t : tracks_) d = std::max(d, t.length());
    return d;
}

QList<std::int64_t> Sequence::editPoints(const QStringList& names) const {
    QList<std::int64_t> points{0};
    for (const Track& t : tracks_) {
        if (!names.isEmpty() && !names.contains(t.name())) continue;
        points << t.editPoints();
    }
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

// Pieces of the segments overlapping the range, trimmed to it (sourceIn moved on for a piece that
// starts inside its segment), then filler for whatever lies past the track's end.
QList<Segment> Sequence::material(const QString& name, std::int64_t start, std::int64_t length) const {
    QList<Segment> out;
    if (length <= 0) return out;
    const std::int64_t end = start + length;
    std::int64_t at = 0;
    std::int64_t covered = start;
    if (const Track* t = track(name)) {
        for (const Segment& s : t->segments) {
            const std::int64_t sEnd = at + s.length;
            const std::int64_t from = std::max(at, start);
            const std::int64_t to = std::min(sEnd, end);
            if (from < to) {
                Segment piece = s;
                piece.sourceIn += from - at;
                if (piece.hasNest()) piece.nestIn += from - at;
                piece.length = to - from;
                out << piece;
                covered = to;
            }
            at = sEnd;
            if (at >= end) break;
        }
    }
    if (covered < end) out << Segment::filler(end - covered);
    return out;
}

// Returns the index of the segment that now starts at `frame`.
int Sequence::cut(Track& t, std::int64_t frame) {
    const std::int64_t length = t.length();
    if (frame >= length) {
        if (frame > length) t.segments << Segment::filler(frame - length);
        return static_cast<int>(t.segments.size());
    }
    std::int64_t start = 0;
    const int i = t.segmentAt(std::max<std::int64_t>(frame, 0), &start);
    if (i < 0 || start == frame) return std::max(i, 0);
    Segment tail = t.segments.at(i);
    tail.addEdit = false;
    tail.transition.reset(); // the cut's transition stays with the head
    const std::int64_t head = frame - start;
    tail.shiftStart(head);
    tail.length -= head;
    // The second part's tracking data keeps following its material (FX Guide p.281-282), and so do its Fixed
    // keyframes: "Keyframes are now preserved more reliably when performing operations such as trimming, splitting,
    // and duplicating clips" (What's New 2026.8 p.9).
    if (tail.effect) {
        shiftTrackingData(*tail.effect, -head);
        shiftFixedKeyframes(*tail.effect, -head);
    }
    t.segments[i].length = head;
    t.segments.insert(i + 1, tail);
    return i + 1;
}

void Sequence::tidy(Track& t) {
    QList<Segment> out;
    for (const Segment& s : t.segments) {
        if (s.length <= 0) continue;
        // Filler joins the filler before it, unless effects tell them apart.
        if (s.isFiller() && !s.addEdit && !s.hasEffects() && !s.hasNest() && !out.isEmpty() && out.last().isFiller() &&
            !out.last().effect && !out.last().hasNest()) {
            out.last().length += s.length;
        } else {
            Segment kept = s;
            if (s.isFiller()) {
                // Filler keeps only what filler can have: its effects, their renders and a nest.
                kept = Segment::filler(s.length);
                kept.effect = s.effect;
                kept.render = s.render;
                kept.transition = s.transition;
                kept.transitionRender = s.transitionRender;
                kept.nest = s.nest;
                kept.nestIn = s.nestIn;
            }
            kept.addEdit = s.isFiller() && s.addEdit && !out.isEmpty() && out.last().isFiller();
            out << kept;
        }
    }
    t.segments = out;
}

// cut() pads with filler when `at` is past the end, so a splice there leaves a gap of filler.
void Sequence::splice(const QString& name, std::int64_t at, const QList<Segment>& material) {
    Track* t = track(name);
    if (!t || at < 0) return;
    const int i = cut(*t, at);
    for (int k = 0; k < material.size(); ++k) t->segments.insert(i + k, material.at(k));
    tidy(*t);
}

// Cuts at `at` and at the end of the overwritten range, removes the segments between, then
// inserts the material in their place.
void Sequence::overwrite(const QString& name, std::int64_t at, const QList<Segment>& material) {
    Track* t = track(name);
    if (!t || at < 0) return;
    std::int64_t length = 0;
    for (const Segment& s : material) length += s.length;
    const int i = cut(*t, at);
    // Only what the track has past `at` is replaced.
    const int j = cut(*t, std::min(at + length, std::max(t->length(), at)));
    t->segments.remove(i, j - i);
    for (int k = 0; k < material.size(); ++k) t->segments.insert(i + k, material.at(k));
    tidy(*t);
}

QList<Segment> Sequence::lift(const QString& name, std::int64_t start, std::int64_t length) {
    Track* t = track(name);
    if (!t) return {};
    start = std::max<std::int64_t>(start, 0);
    const std::int64_t end = std::min(start + length, t->length());
    if (start >= end) return {};
    const int i = cut(*t, start);
    const int j = cut(*t, end);
    const QList<Segment> removed = t->segments.mid(i, j - i);
    t->segments.remove(i, j - i);
    t->segments.insert(i, Segment::filler(end - start));
    tidy(*t);
    return removed;
}

QList<Segment> Sequence::extract(const QString& name, std::int64_t start, std::int64_t length) {
    Track* t = track(name);
    if (!t) return {};
    start = std::max<std::int64_t>(start, 0);
    const std::int64_t end = std::min(start + length, t->length());
    if (start >= end) return {};
    const int i = cut(*t, start);
    const int j = cut(*t, end);
    const QList<Segment> removed = t->segments.mid(i, j - i);
    t->segments.remove(i, j - i);
    tidy(*t);
    return removed;
}

// Audio clips without a Tracks column offer A1 and A2.
QStringList sourceTracksOf(const BinItem& item) {
    // A Title Effect clip is a one-track composition (title_clip.h): V1.
    if (item.kind == BinItemKind::Sequence || item.kind == BinItemKind::Title) return Sequence::fromItem(item).trackNames();
    QStringList tracks = parseTrackList(item.fields.value(QStringLiteral("Tracks")));
    if (!tracks.isEmpty() && !trackFormatsOf(item).isEmpty()) {
        // The audio channels as grouped by the Track Formats.
        QStringList grouped;
        for (const QString& t : tracks) {
            const auto ref = parseTrackName(t);
            if (ref && ref->kind != TrackKind::Audio) grouped << t;
        }
        for (const SourceAudioTrack& a : sourceAudioTracks(item)) grouped << a.name;
        return grouped;
    }
    if (!tracks.isEmpty()) return tracks;
    if (item.kind == BinItemKind::AudioClip) return {QStringLiteral("A1"), QStringLiteral("A2")};
    return {QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")};
}

// Patches of tracks the new source does not have are dropped; selections are kept by name.
void TrackPatch::setSourceTracks(const QStringList& tracks) {
    source_ = tracks;
    for (auto it = patch_.begin(); it != patch_.end();) {
        it = tracks.contains(it.key()) ? std::next(it) : patch_.erase(it);
    }
}

bool TrackPatch::isSourceSelected(const QString& track) const {
    return source_.contains(track) && !sourceOff_.contains(track);
}

void TrackPatch::setSourceSelected(const QString& track, bool on) {
    if (on) {
        sourceOff_.remove(track);
    } else {
        sourceOff_.insert(track);
    }
}

void TrackPatch::setRecordSelected(const QString& track, bool on) {
    if (on) {
        recordOff_.remove(track);
    } else {
        recordOff_.insert(track);
    }
}

QStringList TrackPatch::selectedRecordTracks(const Sequence& sequence) const {
    QStringList out;
    for (const QString& name : sequence.trackNames()) {
        if (isRecordSelected(name)) out << name;
    }
    return out;
}

bool TrackPatch::canPatch(const QString& source, const QString& record) {
    const auto s = parseTrackName(source);
    const auto r = parseTrackName(record);
    return s && r && s->kind == r->kind && s->kind != TrackKind::Data;
}

bool TrackPatch::patch(const QString& source, const QString& record) {
    if (!source_.contains(source) || !canPatch(source, record)) return false;
    // A source track already going to `record` takes this one's old place.
    const QString previous = recordFor(source);
    for (const QString& other : source_) {
        if (other != source && recordFor(other) == record) {
            if (other == previous) {
                patch_.remove(other);
            } else {
                patch_.insert(other, previous);
            }
        }
    }
    if (source == record) {
        patch_.remove(source);
    } else {
        patch_.insert(source, record);
    }
    return true;
}

QString TrackPatch::recordFor(const QString& source) const {
    return patch_.value(source, source);
}

QString TrackPatch::sourceFor(const QString& record) const {
    for (const QString& s : source_) {
        if (recordFor(s) == record) return s;
    }
    return {};
}

// Each record track receives at most one source track: the first one in source order.
QList<std::pair<QString, QString>> TrackPatch::editPairs(const Sequence& sequence) const {
    QList<std::pair<QString, QString>> pairs;
    QSet<QString> used;
    for (const QString& s : source_) {
        if (!isSourceSelected(s)) continue;
        const QString r = recordFor(s);
        if (used.contains(r)) continue;
        if (sequence.track(r) && !isRecordSelected(r)) continue;
        if (!parseTrackName(r)) continue;
        used.insert(r);
        pairs.append({s, r});
    }
    return pairs;
}

Effect motionEffect(double speed) {
    Effect e;
    e.category = QStringLiteral("EDL");
    e.name = QStringLiteral("Motion Effect");
    e.parameters = QJsonObject{{QStringLiteral("M2"), QJsonObject{{QStringLiteral("Speed"), speed}}}};
    return e;
}

std::optional<double> motionSpeed(const Effect& e) {
    if (e.category != QLatin1String("EDL") || e.name != QLatin1String("Motion Effect")) return std::nullopt;
    const QJsonValue v = e.parameters.value(QStringLiteral("M2")).toObject().value(QStringLiteral("Speed"));
    return v.isDouble() ? std::optional<double>(v.toDouble()) : std::nullopt;
}

double segmentSourcePosition(const Segment& segment, std::int64_t offset) {
    if (segment.effect && isTimewarp(*segment.effect)) {
        return static_cast<double>(segment.sourceIn) + timewarpPosition(*segment.effect, offset, segment.length);
    }
    return static_cast<double>(segmentSourceFrame(segment, offset));
}

std::int64_t segmentSourceFrame(const Segment& segment, std::int64_t offset) {
    if (segment.effect && isTimewarp(*segment.effect)) {
        // The nudge keeps whole positions (2.0 computed as 1.9999...) on their frame.
        return static_cast<std::int64_t>(std::floor(segmentSourcePosition(segment, offset) + 1e-6));
    }
    if (segment.effect) {
        if (const auto speed = motionSpeed(*segment.effect)) {
            // The nudge keeps exact products (0.5 x 4 = 2) from falling a frame short.
            const double moved = static_cast<double>(offset) * *speed;
            return segment.sourceIn + static_cast<std::int64_t>(std::trunc(moved + (moved >= 0 ? 1e-6 : -1e-6)));
        }
    }
    return segment.sourceIn + offset;
}

// The record marks give the length and place; the source marks only the start and the speed.
EditPlan planFitToFill(const EditMarks& source, const EditMarks& record, double* speed) {
    EditPlan plan;
    if (!source.in || !source.out || !record.in || !record.out) {
        plan.error = QStringLiteral("Mark an IN and an OUT in both monitors for Fit to Fill.");
        return plan;
    }
    const std::int64_t sourceLength = *source.out - *source.in + 1;
    const std::int64_t recordLength = *record.out - *record.in + 1;
    if (sourceLength <= 0 || recordLength <= 0) {
        plan.error = QStringLiteral("Each OUT must come after its IN.");
        return plan;
    }
    plan.sourceStart = *source.in;
    plan.recordStart = *record.in;
    plan.length = recordLength;
    if (speed) *speed = static_cast<double>(sourceLength) / static_cast<double>(recordLength);
    return plan;
}

// Lengths are inclusive of both marks (OUT - IN + 1). recordStartFor() backtimes from a record OUT
// when there is no record IN.
EditPlan planEdit(const EditMarks& source, const EditMarks& record, bool singleMark) {
    EditPlan plan;
    if (source.duration <= 0) {
        plan.error = QStringLiteral("There is no source material to edit.");
        return plan;
    }
    const std::optional<std::int64_t>& sIn = source.in;
    const std::optional<std::int64_t>& sOut = source.out;
    const std::optional<std::int64_t>& rIn = record.in;
    const std::optional<std::int64_t>& rOut = record.out;
    auto recordStartFor = [&](std::int64_t length) {
        if (rIn) return *rIn;
        if (rOut) return *rOut - length + 1;
        return record.position;
    };
    if (sIn && sOut) {
        // The source marks give the duration.
        plan.length = *sOut - *sIn + 1;
        plan.sourceStart = *sIn;
        plan.recordStart = recordStartFor(plan.length);
    } else if (rIn && rOut) {
        // The record marks give the duration; a lone source OUT backtimes.
        plan.length = *rOut - *rIn + 1;
        plan.recordStart = *rIn;
        plan.sourceStart = sIn ? *sIn : sOut ? *sOut - plan.length + 1 : source.position;
    } else if (sOut) {
        // Single-mark editing: the source position is the IN (p.468); without it, the clip's start.
        plan.sourceStart = singleMark ? source.position : 0;
        plan.length = *sOut - plan.sourceStart + 1;
        plan.recordStart = recordStartFor(plan.length);
    } else if (rOut) {
        plan.sourceStart = sIn.value_or(source.position);
        plan.recordStart = record.position;
        plan.length = *rOut - record.position + 1;
    } else {
        // From the source IN (or position) to the end of the clip, at the
        // record IN or position (p.466, p.501). Single-mark editing: a source IN with the position
        // after it ends at the position (p.468: "mark an In point ... play ... then press the
        // Splice-in, Overwrite, or Replace Edit button").
        plan.sourceStart = sIn.value_or(source.position);
        plan.length = singleMark && sIn && source.position > *sIn ? source.position - *sIn + 1
                                                                  : source.duration - plan.sourceStart;
        plan.recordStart = rIn.value_or(record.position);
    }
    if (plan.length <= 0) {
        plan.error = QStringLiteral("The OUT mark is before the IN mark.");
    } else if (plan.sourceStart < 0 || plan.sourceStart + plan.length > source.duration) {
        plan.error = QStringLiteral("Insufficient source material to make this edit.");
    } else if (plan.recordStart < 0) {
        plan.error = QStringLiteral("The edit would start before the beginning of the sequence.");
    }
    return plan;
}

EditPlan planSyncPoint(const EditMarks& source, const EditMarks& record) {
    EditPlan plan;
    const std::int64_t offset = source.position - record.position; // source frame - record frame
    const auto& sIn = source.in;
    const auto& sOut = source.out;
    const auto& rIn = record.in;
    const auto& rOut = record.out;
    const int marks = (sIn ? 1 : 0) + (sOut ? 1 : 0) + (rIn ? 1 : 0) + (rOut ? 1 : 0);
    if (marks == 2 && sIn && sOut) {
        plan.sourceStart = *sIn;
        plan.length = *sOut - *sIn + 1;
    } else if (marks == 2 && rIn && rOut) {
        plan.sourceStart = *rIn + offset;
        plan.length = *rOut - *rIn + 1;
    } else if (marks == 2 && sIn && rOut) {
        plan.sourceStart = *sIn;
        plan.length = *rOut + offset - *sIn + 1;
    } else if (marks == 2 && rIn && sOut) {
        plan.sourceStart = *rIn + offset;
        plan.length = *sOut - plan.sourceStart + 1;
    } else {
        plan.error = QStringLiteral("Sync Point editing needs an IN and an OUT: both in one monitor, or one in each.");
        return plan;
    }
    plan.recordStart = plan.sourceStart - offset;
    if (plan.length <= 0) {
        plan.error = QStringLiteral("The OUT mark is before the IN mark.");
    } else if (plan.sourceStart < 0 || plan.sourceStart + plan.length > source.duration) {
        plan.error = QStringLiteral("Insufficient source material to make this edit.");
    } else if (plan.recordStart < 0) {
        plan.error = QStringLiteral("The edit would start before the beginning of the sequence.");
    }
    return plan;
}

QStringList applyEdit(Sequence& sequence, EditKind kind, const EditPlan& plan, const QString& binId,
                      const BinItem& source, const TrackPatch& patch, bool createTracks) {
    QStringList changed;
    if (!plan.ok()) return changed;
    const bool anyTracks = !sequence.tracks().isEmpty();
    // A multigroup edits as the sequence of group segments it holds (group_clip.h, p.1125).
    const auto multigroup = multigroupSequence(source);
    const std::optional<Sequence> nested =
        multigroup ? std::optional<Sequence>(Sequence::fromItem(*multigroup))
        : source.kind == BinItemKind::Sequence || source.kind == BinItemKind::Title ? std::optional<Sequence>(Sequence::fromItem(source)) : std::nullopt;
    for (const auto& [from, to] : patch.editPairs(sequence)) {
        const AudioFormat format = sourceFormat(source, from);
        if (const Track* existing = sequence.track(to); existing && (existing->locked || (existing->kind == TrackKind::Audio && existing->format != format))) {
            continue;
        }
        if (!createTracks && anyTracks && !sequence.track(to)) continue;
        if (!sequence.ensureTrack(to, format)) continue;
        QList<Segment> material;
        if (nested) {
            material = nested->material(from, plan.sourceStart, plan.length);
            // A rolling or crawling title starts where the source IN is in its roll (title_clip.h, FX Guide p.370).
            if (source.kind == BinItemKind::Title) {
                for (Segment& s : material) {
                    if (s.effect && isClassicTitleEffect(*s.effect) && source.durationFrames > 0) {
                        setTitleRollStart(*s.effect, static_cast<double>(plan.sourceStart) / static_cast<double>(source.durationFrames));
                        // Which clip it came from, for revising it from the sequence (p.372).
                        setTitleSource(*s.effect, {binId, source.id});
                    }
                }
            }
        } else {
            Segment s;
            s.clipId = source.id;
            s.sourceMob = source.mobId;
            s.binId = binId;
            s.name = source.name;
            // A group clip's segments show its selected angle under the angle's name (p.1131).
            if (const auto group = groupOf(source); group && !group->angles.isEmpty()) {
                s.groupAngle = group->selected;
                s.name = group->angles[group->selected].clip.name;
                for (const GroupAngle& a : group->angles) s.groupOffsets << a.offset;
            }
            // A multichannel source track is named by its first channel (multichannel.h).
            s.sourceTrack = sourceChannel(source, from);
            s.sourceIn = plan.sourceStart;
            s.length = plan.length;
            // A mixed rate clip as the monitor shows it, at the sequence's rate (motion_adapter.h).
            s.adapter = adapterOf(source);
            // "Levels set in master clips carry across to the sequence after you edit the clips"
            // (p.711): Apply Gain and Center Pan (clip_audio.h).
            if (s.sourceTrack.startsWith(QLatin1Char('A'))) {
                s.gain = clipGain(source);
                if (const auto pan = clipPan(source)) {
                    s.pan = *pan;
                    s.hasPan = true;
                }
            }
            material << s;
        }
        if (kind == EditKind::SpliceIn) {
            sequence.splice(to, plan.recordStart, material);
        } else {
            sequence.overwrite(to, plan.recordStart, material);
        }
        changed << to;
    }
    // A splice into a sync-locked track puts filler into the other
    // sync-locked tracks, so they stay in sync (p.493).
    if (kind == EditKind::SpliceIn) {
        bool synced = false;
        for (const QString& name : changed) synced = synced || sequence.track(name)->syncLocked;
        if (synced) {
            for (const QString& name : sequence.trackNames()) {
                const Track* t = sequence.track(name);
                if (changed.contains(name) || !t->syncLocked || t->locked || plan.recordStart >= t->length()) continue;
                sequence.splice(name, plan.recordStart, {Segment::filler(plan.length)});
            }
        }
    }
    return changed;
}

QString editFormatMismatch(const Sequence& sequence, const BinItem& source, const TrackPatch& patch) {
    for (const auto& [from, to] : patch.editPairs(sequence)) {
        const Track* existing = sequence.track(to);
        if (!existing || existing->kind != TrackKind::Audio) continue;
        const AudioFormat format = sourceFormat(source, from);
        if (existing->format != format) {
            return QObject::tr("%1 is %2 but %3 is %4.").arg(from, formatName(format).toLower(), to, formatName(existing->format).toLower());
        }
    }
    return {};
}

// Tracks that had nothing in the range are not reported as changed.
QStringList removeMaterial(Sequence& sequence, bool extract, std::int64_t start, std::int64_t length,
                           const QStringList& tracks) {
    QStringList changed;
    QStringList targets;
    bool synced = false;
    for (const QString& name : tracks) {
        const Track* t = sequence.track(name);
        if (!t || t->locked) continue;
        targets << name;
        synced = synced || t->syncLocked;
    }
    if (extract && synced) {
        for (const Track& t : sequence.tracks()) {
            if (t.syncLocked && !t.locked && !targets.contains(t.name())) targets << t.name();
        }
    }
    for (const QString& name : targets) {
        const QList<Segment> removed =
            extract ? sequence.extract(name, start, length) : sequence.lift(name, start, length);
        if (!removed.isEmpty()) changed << name;
    }
    return changed;
}

bool Sequence::removeTrack(const QString& name) {
    for (qsizetype i = 0; i < tracks_.size(); ++i) {
        if (tracks_.at(i).name() == name) {
            tracks_.removeAt(i);
            return true;
        }
    }
    return false;
}

// An Add Edit in filler is flagged on the second part, or tidy() would merge it straight back.
bool Sequence::addFillerAtStart(std::int64_t length) {
    if (length <= 0 || duration() == 0) return false;
    for (const Track& t : tracks_) {
        if (t.locked) return false;
    }
    for (Track& t : tracks_) {
        if (t.segments.isEmpty()) continue; // an empty track stays empty
        splice(t.name(), 0, {Segment::filler(length)});
    }
    return true;
}

std::optional<std::int64_t> Sequence::findBlackHole(const QStringList& tracks, std::int64_t after) const {
    std::optional<std::int64_t> best;
    const std::int64_t end = duration();
    for (const QString& name : tracks) {
        const Track* t = track(name);
        if (!t) continue;
        std::int64_t at = 0;
        for (const Segment& s : t->segments) {
            if (s.isFiller() && at > after && at < end && (!best || at < *best)) best = at;
            at += s.length;
        }
        // A track that ends before the sequence does leaves a black hole after its end.
        if (at > after && at < end && (!best || at < *best)) best = at;
    }
    return best;
}

std::optional<std::int64_t> Sequence::findFlashFrame(const QStringList& tracks, std::int64_t after, std::int64_t shorterThan) const {
    std::optional<std::int64_t> best;
    for (const QString& name : tracks) {
        const Track* t = track(name);
        if (!t) continue;
        std::int64_t at = 0;
        for (const Segment& s : t->segments) {
            if (!s.isFiller() && s.length < shorterThan && at > after && (!best || at < *best)) best = at;
            at += s.length;
        }
    }
    return best;
}

Sequence Sequence::selectionCopy(const QList<SegmentRef>& segments) const {
    Sequence out = *this;
    out.tracks_.clear();
    for (const Track& t : tracks_) {
        Track copy = t;
        copy.segments.clear();
        bool any = false;
        std::int64_t at = 0;
        for (const Segment& s : t.segments) {
            const bool chosen = !s.isFiller() && segments.contains(SegmentRef{t.name(), at});
            copy.segments << (chosen ? s : Segment::filler(s.length));
            any = any || chosen;
            at += s.length;
        }
        if (!any) continue;
        while (!copy.segments.isEmpty() && copy.segments.constLast().isFiller()) copy.segments.removeLast();
        tidy(copy);
        out.tracks_ << copy;
    }
    return out;
}

QList<std::pair<std::int64_t, std::int64_t>> Sequence::headsSpans(const QStringList& tracks) const {
    QList<std::pair<std::int64_t, std::int64_t>> out;
    const QList<std::int64_t> points = editPoints(tracks);
    for (qsizetype i = 1; i < points.size(); ++i) {
        if (points.at(i) > points.at(i - 1)) out.append({points.at(i - 1), points.at(i)});
    }
    return out;
}

// Each track gives up the span and takes it back at `to`, which moves left by the span's length when
// it lies after it. A track shorter than the span's end is first padded with filler so that every
// track shifts by the same amount.
bool Sequence::moveSpan(const QStringList& tracks, std::int64_t start, std::int64_t length, std::int64_t to) {
    if (length <= 0 || start < 0 || (to >= start && to <= start + length)) return false;
    for (const QString& name : tracks) {
        const Track* t = track(name);
        if (!t || t->locked) return false;
    }
    const std::int64_t at = to > start ? to - length : to;
    for (const QString& name : tracks) {
        const QList<Segment> moved = material(name, start, length);
        if (track(name)->length() < start + length) overwrite(name, start, {Segment::filler(length)});
        extract(name, start, length);
        splice(name, at, moved);
    }
    return true;
}

bool Sequence::addEdit(const QString& name, std::int64_t frame) {
    Track* t = track(name);
    if (!t || frame <= 0 || frame >= t->length()) return false;
    std::int64_t start = 0;
    t->segmentAt(frame, &start);
    if (start == frame) return false;
    const int i = cut(*t, frame);
    if (t->segments.at(i).isFiller()) t->segments[i].addEdit = true;
    tidy(*t);
    return true;
}

bool Sequence::isMatchFrame(const Segment& before, const Segment& after) {
    // An edit with a transition, or between different effects, is kept.
    if (after.transition || before.effect != after.effect) return false;
    if (before.gain != after.gain || before.pan != after.pan) return false;
    // Halves of one segment keep the same keyframes (counted in the clip's frames), so an Add Edit
    // between them is still a match frame; other keyframes keep the edit.
    if (before.volumeKeys != after.volumeKeys || before.panKeys != after.panKeys) return false;
    if (before.eq != after.eq || before.localColor != after.localColor || before.surround != after.surround) return false;
    if (before.sourceCorrection != after.sourceCorrection || before.programCorrection != after.programCorrection) return false;
    if (before.sourceRelationship != after.sourceRelationship || before.programRelationship != after.programRelationship) return false;
    if (before.correctionComment != after.correctionComment) return false;
    if (before.isFiller() || after.isFiller()) return before.isFiller() && after.isFiller() && after.addEdit;
    return before.clipId == after.clipId && before.binId == after.binId && before.sourceTrack == after.sourceTrack &&
           before.sourceIn + before.length == after.sourceIn;
}

// Joins each segment into the one before it when the edit between them is a match frame.
int Sequence::removeMatchFrameEdits(const QStringList& names, std::int64_t start, std::int64_t length) {
    int removed = 0;
    const std::int64_t end = start + length;
    for (Track& t : tracks_) {
        if (!names.isEmpty() && !names.contains(t.name())) continue;
        QList<Segment> out;
        std::int64_t at = 0;
        for (const Segment& s : t.segments) {
            // The edit at `at` is inside the range: strictly between its ends.
            if (!out.isEmpty() && at > start && at < end && isMatchFrame(out.last(), s)) {
                out.last().length += s.length;
                ++removed;
            } else {
                out << s;
            }
            at += s.length;
        }
        t.segments = out;
    }
    return removed;
}

// The excerpt's tracks are unlocked and not sync-locked; tracks with only filler are left out.
Sequence Sequence::excerpt(const QStringList& names, std::int64_t start, std::int64_t length) const {
    Sequence out;
    out.timecode_ = timecode_;
    out.mixFormat_ = mixFormat_;
    for (const Track& t : tracks_) {
        if (!names.contains(t.name())) continue;
        Track copy = t;
        copy.locked = false;
        copy.syncLocked = false;
        copy.inserts = {}; // the Clipboard holds material, not the track's effects or display
        copy.inactive = false;
        copy.showWaveform = copy.showClipVolume = copy.showVolume = copy.showPan = false;
        copy.color.clear();
        copy.heightStep = 0;
        copy.customName.clear();
        copy.segments = material(t.name(), start, length);
        tidy(copy);
        bool empty = true;
        for (const Segment& s : copy.segments) empty = empty && s.isFiller();
        if (!empty) out.tracks_ << copy;
    }
    return out;
}

// The record range is IN to OUT, or else the segment under the record position; the source start
// is chosen so that the source position lands on the record position.
EditPlan planReplace(const Sequence& sequence, const QString& track, const EditMarks& source, const EditMarks& record) {
    EditPlan plan;
    if (source.duration <= 0) {
        plan.error = QStringLiteral("There is no source material to edit.");
        return plan;
    }
    if (record.in && record.out) {
        // "Replace edits always fill the In to Out portion" (p.468).
        plan.recordStart = *record.in;
        plan.length = *record.out - *record.in + 1;
    } else {
        const Track* t = sequence.track(track);
        std::int64_t start = 0;
        const int i = t ? t->segmentAt(record.position, &start) : -1;
        if (i < 0) {
            plan.error = QStringLiteral("There is no clip under the position indicator to replace.");
            return plan;
        }
        plan.recordStart = start;
        plan.length = t->segments.at(i).length;
    }
    // The two positions are the sync frames (p.468).
    plan.sourceStart = source.position - (record.position - plan.recordStart);
    if (plan.length <= 0) {
        plan.error = QStringLiteral("The OUT mark is before the IN mark.");
    } else if (plan.sourceStart < 0 || plan.sourceStart + plan.length > source.duration) {
        plan.error = QStringLiteral("Insufficient source material to make this edit.");
    }
    return plan;
}

QStringList paste(Sequence& sequence, const Sequence& clip, std::int64_t at, EditKind kind) {
    QStringList changed;
    const std::int64_t length = clip.duration();
    if (length <= 0) return changed;
    for (const Track& t : clip.tracks()) {
        if (const Track* existing = sequence.track(t.name()); existing && existing->locked) continue;
        if (!sequence.ensureTrack(t.name())) continue;
        const QList<Segment> material = clip.material(t.name(), 0, length);
        if (kind == EditKind::SpliceIn) {
            sequence.splice(t.name(), at, material);
        } else {
            sequence.overwrite(t.name(), at, material);
        }
        changed << t.name();
    }
    return changed;
}

std::optional<Segment> segmentFor(const Sequence& sequence, const SegmentRef& ref) {
    const Track* t = sequence.track(ref.track);
    std::int64_t start = 0;
    const int i = t ? t->segmentAt(ref.start, &start) : -1;
    if (i < 0 || start != ref.start) return std::nullopt;
    return t->segments.at(i);
}

namespace {

// The audio segment `ref` names, for changing; nullptr if there is none.
Segment* audioSegment(Sequence& sequence, const SegmentRef& ref, const QString& sourceTrack) {
    const auto to = parseTrackName(sourceTrack);
    if (!to || to->kind != TrackKind::Audio) return nullptr;
    Track* t = sequence.track(ref.track);
    std::int64_t start = 0;
    const int i = t && t->kind == TrackKind::Audio ? t->segmentAt(ref.start, &start) : -1;
    if (i < 0 || start != ref.start || t->segments.at(i).isFiller()) return nullptr;
    return &t->segments[i];
}

} // namespace

bool changeSourceTrack(Sequence& sequence, const SegmentRef& ref, const QString& sourceTrack) {
    Segment* seg = audioSegment(sequence, ref, sourceTrack);
    if (!seg) return false;
    seg->sourceTrack = sourceTrack;
    return true;
}

bool changeSourceTrack(Sequence& sequence, const SegmentRef& ref, const BinItem& master, const QString& masterBin,
                       std::int64_t offset, const QString& sourceTrack) {
    Segment* seg = audioSegment(sequence, ref, sourceTrack);
    if (!seg) return false;
    seg->clipId = master.id;
    seg->binId = masterBin;
    seg->sourceMob = mobIdOf(master);
    seg->name = master.name;
    seg->sourceIn += offset;
    seg->sourceTrack = sourceTrack;
    return true;
}

namespace {

// The track `by` numbers up (negative: down) of the same kind, or nullopt past 1 or the limit.
std::optional<QString> shiftedTrack(const QString& name, int by) {
    const auto ref = parseTrackName(name);
    if (!ref) return std::nullopt;
    const int number = ref->number + by;
    if (number < 1 || number > (ref->kind == TrackKind::Data ? 1 : kMaxTracks)) return std::nullopt;
    return trackName(ref->kind, number);
}

} // namespace

// All checks are made before anything changes. Then the originals are taken out (unless copying)
// and the pieces put in at their destinations. Destinations are adjusted for extracts on the same
// track, and the references already returned for splices that push them.
void syncLockAllTracks(BinItem& sequence) {
    Sequence s = Sequence::fromItem(sequence);
    for (const QString& name : s.trackNames()) {
        Track* t = s.track(name);
        if (t && t->kind != TrackKind::Data) t->syncLocked = true;
    }
    s.storeIn(sequence);
}

QList<SegmentRef> moveSegments(Sequence& sequence, const QList<SegmentRef>& segments, std::int64_t frames, int tracks,
                               EditKind kind, bool copy, QString* error, bool keepSync) {
    auto failed = [error](const QString& text) {
        if (error) *error = text;
        return QList<SegmentRef>();
    };
    struct Piece {
        SegmentRef from;
        Segment segment;
        QString to;
        std::int64_t at = 0;
        AudioFormat format = AudioFormat::Mono; // a track added for it takes the source's format
    };
    QList<Piece> pieces;
    for (const SegmentRef& ref : segments) {
        const auto seg = segmentFor(sequence, ref);
        if (!seg) continue;
        const auto to = shiftedTrack(ref.track, tracks);
        if (!to) return failed(QStringLiteral("There is no track to move to."));
        if (const Track* t = sequence.track(*to); t && t->locked) {
            // "You cannot move segments to locked tracks" (p.619).
            return failed(QStringLiteral("%1 is locked.").arg(*to));
        }
        if (const Track* t = sequence.track(ref.track); !copy && t->locked) {
            return failed(QStringLiteral("%1 is locked.").arg(ref.track));
        }
        // "You can move mono audio tracks only to other mono audio tracks, and you can move stereo
        // audio tracks only to stereo tracks" (p.610-611); so too for surround tracks.
        if (const Track* from = sequence.track(ref.track), *dest = sequence.track(*to);
            from && dest && from->kind == TrackKind::Audio && dest->format != from->format) {
            return failed(QStringLiteral("%1 and %2 have different audio formats.").arg(ref.track, *to));
        }
        if (ref.start + frames < 0) return failed(QStringLiteral("The segment would start before the sequence."));
        const Track* from = sequence.track(ref.track);
        pieces << Piece{ref, *seg, *to, ref.start + frames, from ? from->format : AudioFormat::Mono};
    }
    if (pieces.isEmpty()) return failed(QStringLiteral("Select segments to move."));
    // Segment Drag Sync Locks (p.618): a splice into a sync-locked track keeps sync. The pieces going to
    // sync-locked tracks leave filler behind instead of closing the gap.
    auto syncLocked = [&sequence](const QString& name) {
        const Track* t = sequence.track(name);
        return t && t->syncLocked;
    };
    QSet<QString> destinations;
    for (const Piece& p : pieces) destinations.insert(p.to);
    const bool syncing = keepSync && kind == EditKind::SpliceIn;
    // Remove the originals, the latest first so earlier ones keep their
    // place; Extract shifts later destinations on the same track.
    if (!copy) {
        QList<Piece> order = pieces;
        std::sort(order.begin(), order.end(), [](const Piece& a, const Piece& b) { return a.from.start > b.from.start; });
        for (const Piece& p : order) {
            if (kind == EditKind::SpliceIn && !(syncing && syncLocked(p.to))) {
                sequence.extract(p.from.track, p.from.start, p.segment.length);
                for (Piece& q : pieces) {
                    if (q.to != p.from.track) continue;
                    if (q.at >= p.from.start + p.segment.length) {
                        q.at -= p.segment.length;
                    } else if (q.at > p.from.start) {
                        q.at = p.from.start;
                    }
                }
            } else {
                sequence.lift(p.from.track, p.from.start, p.segment.length);
            }
        }
    }
    // Put them in, the latest first; a splice pushes the ones already put
    // in after it on its track.
    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return a.at > b.at; });
    QList<SegmentRef> moved;
    for (const Piece& p : pieces) {
        if (!sequence.ensureTrack(p.to, p.format)) continue;
        if (kind == EditKind::SpliceIn) {
            sequence.splice(p.to, p.at, {p.segment});
            for (SegmentRef& m : moved) {
                if (m.track == p.to && m.start >= p.at) m.start += p.segment.length;
            }
            if (syncing && syncLocked(p.to)) {
                // "On all other sync-locked tracks that correspond to the new location of the segment
                // you moved" (p.618): tracks that get no moved segment of their own.
                QStringList others;
                for (const Track& t : sequence.tracks()) {
                    if (t.syncLocked && !t.locked && !destinations.contains(t.name()) && p.at < t.length()) others << t.name();
                }
                for (const QString& name : others) sequence.splice(name, p.at, {Segment::filler(p.segment.length)});
            }
        } else {
            sequence.overwrite(p.to, p.at, {p.segment});
        }
        moved << SegmentRef{p.to, p.at};
    }
    std::reverse(moved.begin(), moved.end());
    return moved;
}

// Latest first, so the references to earlier segments stay valid. Locked tracks are skipped;
// false when nothing was deleted.
bool deleteSegments(Sequence& sequence, const QList<SegmentRef>& segments, bool extract) {
    QList<std::pair<SegmentRef, std::int64_t>> found;
    for (const SegmentRef& ref : segments) {
        const auto seg = segmentFor(sequence, ref);
        const Track* t = sequence.track(ref.track);
        if (seg && t && !t->locked) found.append({ref, seg->length});
    }
    if (found.isEmpty()) return false;
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first.start > b.first.start; });
    for (const auto& [ref, length] : found) {
        if (extract) {
            sequence.extract(ref.track, ref.start, length);
        } else {
            sequence.lift(ref.track, ref.start, length);
        }
    }
    return true;
}

// An excerpt of the span the segments cover, with every unselected segment in it lifted to filler.
// An empty sequence (and *start 0) when no reference is valid.
Sequence copySegments(const Sequence& sequence, const QList<SegmentRef>& segments, std::int64_t* start) {
    std::int64_t first = -1;
    std::int64_t end = 0;
    QStringList tracks;
    for (const SegmentRef& ref : segments) {
        const auto seg = segmentFor(sequence, ref);
        if (!seg) continue;
        first = first < 0 ? ref.start : std::min(first, ref.start);
        end = std::max(end, ref.start + seg->length);
        if (!tracks.contains(ref.track)) tracks << ref.track;
    }
    if (start) *start = std::max<std::int64_t>(first, 0);
    if (first < 0) return {};
    // Only the selected segments: the rest of each track is filler.
    Sequence out = sequence.excerpt(tracks, first, end - first);
    for (const QString& name : tracks) {
        const Track* t = sequence.track(name);
        std::int64_t at = 0;
        for (const Segment& s : t->segments) {
            const std::int64_t from = std::max(at, first);
            const std::int64_t to = std::min(at + s.length, end);
            if (from < to && !segments.contains(SegmentRef{name, at})) out.lift(name, from - first, to - from);
            at += s.length;
        }
    }
    return out;
}

std::optional<MatchedFrame> matchFrame(const Sequence& sequence, const QStringList& tracks, std::int64_t position) {
    // Video from the top track down, then audio from A1 (p.434).
    QStringList order;
    for (auto it = sequence.tracks().crbegin(); it != sequence.tracks().crend(); ++it) {
        if (it->kind == TrackKind::Video) order << it->name();
    }
    for (const Track& t : sequence.tracks()) {
        if (t.kind == TrackKind::Audio) order << t.name();
    }
    for (const QString& name : order) {
        if (!tracks.isEmpty() && !tracks.contains(name)) continue;
        const Track* t = sequence.track(name);
        std::int64_t start = 0;
        const int i = t->segmentAt(position, &start);
        if (i < 0 || t->segments.at(i).isFiller()) continue;
        const Segment& s = t->segments.at(i);
        return MatchedFrame{s, segmentSourceFrame(s, position - start)};
    }
    return std::nullopt;
}

QStringList emptyAudioTracks(const Sequence& sequence, std::int64_t start, std::int64_t end, const QStringList& exclude) {
    QStringList out;
    for (const Track& t : sequence.tracks()) {
        if (t.kind != TrackKind::Audio || exclude.contains(t.name())) continue;
        bool empty = true;
        std::int64_t at = 0;
        for (const Segment& s : t.segments) {
            if (!s.isFiller() && at < end && at + s.length > start) {
                empty = false;
                break;
            }
            at += s.length;
        }
        if (empty) out << t.name();
    }
    return out;
}

// Each segment of the clip whose source range covers the frame gives one sequence frame; a frame
// shown on several tracks at once is listed once.
QList<std::int64_t> reverseMatchFrames(const Sequence& sequence, const QStringList& tracks, const QString& clipId,
                                       const QStringList& sourceTracks, std::int64_t frame) {
    QList<std::int64_t> out;
    for (const Track& t : sequence.tracks()) {
        if (!tracks.isEmpty() && !tracks.contains(t.name())) continue;
        std::int64_t start = 0;
        for (const Segment& s : t.segments) {
            if (!s.isFiller() && s.clipId == clipId && (sourceTracks.isEmpty() || sourceTracks.contains(s.sourceTrack)) &&
                frame >= s.sourceIn && frame < s.sourceIn + s.length) {
                out << start + frame - s.sourceIn;
            }
            start += s.length;
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void Sequence::normalize(const QString& name) {
    if (Track* t = track(name)) tidy(*t);
}

namespace {

// "No limit" in trim range calculations: far more frames than any sequence has, but small enough
// not to overflow when added to frame counts.
constexpr std::int64_t kNoLimit = std::int64_t(1) << 40;

// The two segments at an edit point, as indices into the track's segments (-1: none).
struct EditSides {
    int a = -1; // the segment ending at the edit
    int b = -1; // the segment starting there
};

EditSides sidesAt(const Track& t, std::int64_t frame) {
    EditSides e;
    std::int64_t at = 0;
    for (int i = 0; i < t.segments.size(); ++i) {
        const std::int64_t end = at + t.segments.at(i).length;
        if (end == frame) e.a = i;
        if (at == frame) e.b = i;
        at = end;
    }
    return e;
}

// Clips keep at least one frame (p.647); filler can be used up.
std::int64_t minLength(const Segment& s) {
    return s.isFiller() ? 0 : 1;
}

// Frames the clip has after the segment's last frame, and before its first.
std::int64_t tailRoom(const Segment& s, const SourceLength& length) {
    if (s.isFiller() || !length) return kNoLimit;
    const auto total = length(s);
    if (!total) return kNoLimit;
    return std::max<std::int64_t>(*total - (s.sourceIn + s.length), 0);
}

// For the head, the clip's frames before sourceIn; filler has no limit.
std::int64_t headRoom(const Segment& s) {
    return s.isFiller() ? kNoLimit : std::max<std::int64_t>(s.sourceIn, 0);
}

// Filler from `frame` on: what a sync-locked track can give up there.
std::int64_t fillerRun(const Track& t, std::int64_t frame) {
    if (frame >= t.length()) return kNoLimit;
    std::int64_t start = 0;
    int i = t.segmentAt(frame, &start);
    if (i < 0 || !t.segments.at(i).isFiller()) return 0;
    std::int64_t run = start + t.segments.at(i).length - frame;
    for (++i; i < t.segments.size() && t.segments.at(i).isFiller(); ++i) run += t.segments.at(i).length;
    return i >= t.segments.size() ? kNoLimit : run;
}

// Filler just before `frame`: what a sync-locked track can give up when an
// A-side is trimmed back to there.
std::int64_t fillerRunBefore(const Track& t, std::int64_t frame) {
    if (frame > t.length()) return kNoLimit;
    std::int64_t run = 0;
    std::int64_t at = 0;
    for (const Segment& s : t.segments) {
        const std::int64_t end = at + s.length;
        if (end > frame) break;
        run = s.isFiller() ? run + s.length : 0;
        at = end;
    }
    return at == frame ? run : 0;
}

// How a single-roller ripple changes the length of the track after its
// edit: +frames on the A-side, -frames on the B-side.
// 0 for a dual-roller trim, which does not change the track's length.
int rippleSign(TrimSide side) {
    return side == TrimSide::A ? 1 : side == TrimSide::B ? -1 : 0;
}

// The rollers on tracks that exist and are not locked.
QList<TrimRoller> unlocked(const Sequence& seq, const QList<TrimRoller>& rollers) {
    QList<TrimRoller> out;
    for (const TrimRoller& r : rollers) {
        if (const Track* t = seq.track(r.track); t && !t->locked) out << r;
    }
    return out;
}

// The other sync-locked tracks a ripple keeps in sync: only when a trimmed
// track is sync-locked and all rollers change the length the same way.
QStringList syncTracks(const Sequence& seq, const QList<TrimRoller>& rollers, TrimKind kind) {
    if (kind != TrimKind::Ripple || rollers.isEmpty()) return {};
    bool synced = false;
    const int sign = rippleSign(rollers.first().side);
    QSet<QString> trimmed;
    for (const TrimRoller& r : rollers) {
        if (rippleSign(r.side) != sign || sign == 0) return {};
        synced = synced || seq.track(r.track)->syncLocked;
        trimmed.insert(r.track);
    }
    if (!synced) return {};
    QStringList out;
    for (const Track& t : seq.tracks()) {
        if (t.syncLocked && !t.locked && !trimmed.contains(t.name())) out << t.name();
    }
    return out;
}

} // namespace

QList<std::int64_t> trimPoints(const Sequence& sequence, const QString& name) {
    QList<std::int64_t> out;
    const Track* t = sequence.track(name);
    if (!t) return out;
    std::int64_t at = 0;
    for (const Segment& s : t->segments) {
        at += s.length;
        out << at;
    }
    return out;
}

// Each roller limits the range by its segments: a dual roller by both sides; an A-side by what
// the A segment can lose or gain (and, for Overwrite, what the B segment can lose); a B-side
// likewise. The range is the intersection, then narrowed so that sync-locked tracks give up only
// filler.
TrimRange trimRange(const Sequence& sequence, const QList<TrimRoller>& all, TrimKind kind,
                    const SourceLength& sourceLength) {
    const QList<TrimRoller> rollers = unlocked(sequence, all);
    if (rollers.isEmpty()) return {};
    TrimRange range{-kNoLimit, kNoLimit};
    for (const TrimRoller& r : rollers) {
        const Track* t = sequence.track(r.track);
        const EditSides e = sidesAt(*t, r.frame);
        const Segment* a = e.a >= 0 ? &t->segments.at(e.a) : nullptr;
        const Segment* b = e.b >= 0 ? &t->segments.at(e.b) : nullptr;
        std::int64_t lo = 0;
        std::int64_t hi = 0;
        if (r.side == TrimSide::Both) {
            if (a && b) {
                hi = std::min(tailRoom(*a, sourceLength), b->length - minLength(*b));
                lo = -std::min(a->length - minLength(*a), headRoom(*b));
            }
        } else if (r.side == TrimSide::A) {
            if (a) {
                lo = -(a->length - minLength(*a));
                hi = tailRoom(*a, sourceLength);
                if (kind == TrimKind::Overwrite && b) hi = std::min(hi, b->length - minLength(*b));
            }
        } else if (b) {
            hi = b->length - minLength(*b);
            lo = -headRoom(*b);
            if (kind == TrimKind::Overwrite) lo = -std::min(headRoom(*b), a ? a->length - minLength(*a) : 0);
        }
        range.min = std::max(range.min, lo);
        range.max = std::min(range.max, hi);
    }
    // Sync-locked tracks can only give up filler (p.662).
    const QStringList synced = syncTracks(sequence, rollers, kind);
    for (const TrimRoller& r : rollers) {
        for (const QString& name : synced) {
            const Track& t = *sequence.track(name);
            if (rippleSign(r.side) > 0) {
                // A-side back: the frames before the edit go.
                range.min = std::max(range.min, -fillerRunBefore(t, r.frame));
            } else {
                // B-side on: the frames after it go.
                range.max = std::min(range.max, fillerRun(t, r.frame));
            }
        }
    }
    if (range.min > range.max) return {};
    return range;
}

// One delta for every roller. Dual rollers move the edit (A longer, B shorter, or the reverse).
// Ripple trims change one side and so the track's length. Overwrite trims keep the length: the
// other side is overwritten or filler is put in. `shift` records how much each ripple moved the
// rest of its track, used to move the sync-locked tracks and the rollers that come after.
QList<TrimRoller> trim(Sequence& sequence, const QList<TrimRoller>& all, TrimKind kind, std::int64_t frames,
                       const SourceLength& sourceLength, std::int64_t* applied) {
    const TrimRange range = trimRange(sequence, all, kind, sourceLength);
    const std::int64_t delta = std::clamp(frames, range.min, range.max);
    if (applied) *applied = delta;
    if (delta == 0) return all;
    const QList<TrimRoller> rollers = unlocked(sequence, all);
    const QStringList synced = syncTracks(sequence, rollers, kind);
    // Later edits first, so earlier ones keep their frames.
    QList<int> order;
    for (int i = 0; i < rollers.size(); ++i) order << i;
    std::sort(order.begin(), order.end(), [&](int x, int y) { return rollers.at(x).frame > rollers.at(y).frame; });
    QList<std::int64_t> moved(rollers.size());
    QList<std::int64_t> shift(rollers.size(), 0); // how much the track grew at the roller
    for (int k : order) {
        const TrimRoller& r = rollers.at(k);
        Track* t = sequence.track(r.track);
        const EditSides e = sidesAt(*t, r.frame);
        Segment* a = e.a >= 0 ? &t->segments[e.a] : nullptr;
        Segment* b = e.b >= 0 ? &t->segments[e.b] : nullptr;
        auto trimHead = [](Segment* s, std::int64_t by) {
            // Takes `by` frames off the start (negative: adds them). Fixed effect keyframes stay with
            // the material (FX Guide p.124).
            s->shiftStart(by);
            s->length -= by;
            if (s->effect) {
                shiftFixedKeyframes(*s->effect, -by);
                shiftTrackingData(*s->effect, -by); // "tracking data is preserved" (p.282)
            }
        };
        if (r.side == TrimSide::Both) {
            a->length += delta;
            trimHead(b, delta);
            moved[k] = r.frame + delta;
        } else if (kind == TrimKind::Ripple) {
            if (r.side == TrimSide::A) {
                a->length += delta;
                moved[k] = r.frame + delta;
                shift[k] = delta;
            } else {
                trimHead(b, delta);
                moved[k] = r.frame;
                shift[k] = -delta;
            }
        } else if (r.side == TrimSide::A) {
            a->length += delta;
            if (delta > 0 && b) trimHead(b, delta);
            if (delta < 0) t->segments.insert(e.a + 1, Segment::filler(-delta));
            moved[k] = r.frame + delta;
        } else {
            trimHead(b, delta);
            if (delta > 0) t->segments.insert(e.b, Segment::filler(delta));
            if (delta < 0 && a) a->length += delta;
            moved[k] = r.frame + delta;
        }
        // The other sync-locked tracks get or lose filler at the edit.
        for (const QString& name : synced) {
            if (shift[k] > 0 && r.frame < sequence.track(name)->length()) {
                sequence.splice(name, r.frame, {Segment::filler(shift[k])});
            } else if (shift[k] < 0) {
                const std::int64_t at = r.side == TrimSide::A ? r.frame + shift[k] : r.frame;
                sequence.extract(name, at, -shift[k]);
            }
        }
        sequence.normalize(r.track);
    }
    // The rollers' new frames, moved on by the ripples at earlier edits on the same track. Rollers
    // on locked tracks keep their frames.
    QList<TrimRoller> out = all;
    for (int i = 0; i < rollers.size(); ++i) {
        std::int64_t frame = moved.at(i);
        for (int j = 0; j < rollers.size(); ++j) {
            if (rollers.at(j).track == rollers.at(i).track && rollers.at(j).frame < rollers.at(i).frame) frame += shift.at(j);
        }
        for (TrimRoller& o : out) {
            if (o == rollers.at(i)) {
                o.frame = frame;
                break;
            }
        }
    }
    return out;
}

// Only sourceIn changes, so nothing moves in the sequence. The delta is limited so that every
// slipped clip stays within its media; filler and locked tracks are skipped.
std::int64_t slipSegments(Sequence& sequence, const QList<SegmentRef>& segments, std::int64_t frames,
                          const SourceLength& sourceLength) {
    std::int64_t lo = -kNoLimit;
    std::int64_t hi = kNoLimit;
    QList<SegmentRef> clips;
    for (const SegmentRef& ref : segments) {
        const auto seg = segmentFor(sequence, ref);
        if (!seg || seg->isFiller() || sequence.track(ref.track)->locked) continue;
        lo = std::max(lo, -seg->sourceIn);
        hi = std::min(hi, tailRoom(*seg, sourceLength));
        clips << ref;
    }
    if (clips.isEmpty() || lo > hi) return 0;
    const std::int64_t delta = std::clamp(frames, lo, hi);
    for (const SegmentRef& ref : clips) {
        Track* t = sequence.track(ref.track);
        t->segments[t->segmentAt(ref.start)].sourceIn += delta;
    }
    return delta;
}

// A segment slides only if it has neighbours on both sides: the one before grows (or shrinks)
// and the one after shrinks (or grows) by the same amount. The references are updated in place.
std::int64_t slideSegments(Sequence& sequence, QList<SegmentRef>& segments, std::int64_t frames,
                           const SourceLength& sourceLength) {
    std::int64_t lo = -kNoLimit;
    std::int64_t hi = kNoLimit;
    QList<int> usable;
    for (int k = 0; k < segments.size(); ++k) {
        const SegmentRef& ref = segments.at(k);
        const Track* t = sequence.track(ref.track);
        std::int64_t start = 0;
        const int i = t ? t->segmentAt(ref.start, &start) : -1;
        if (i <= 0 || start != ref.start || i + 1 >= t->segments.size() || t->locked) continue;
        const Segment& prev = t->segments.at(i - 1);
        const Segment& next = t->segments.at(i + 1);
        hi = std::min({hi, tailRoom(prev, sourceLength), next.length - minLength(next)});
        lo = std::max({lo, -(prev.length - minLength(prev)), -headRoom(next)});
        usable << k;
    }
    if (usable.isEmpty() || lo > hi) return 0;
    const std::int64_t delta = std::clamp(frames, lo, hi);
    for (int k : usable) {
        SegmentRef& ref = segments[k];
        Track* t = sequence.track(ref.track);
        const int i = t->segmentAt(ref.start);
        t->segments[i - 1].length += delta;
        Segment& next = t->segments[i + 1];
        next.shiftStart(delta);
        next.length -= delta;
        if (next.effect) {
            shiftFixedKeyframes(*next.effect, -delta);
            shiftTrackingData(*next.effect, -delta);
        }
        ref.start += delta;
        sequence.normalize(ref.track);
    }
    return delta;
}

// The track end is not an edit that can be extended. A track is skipped if the move does not fit
// in its trim range.
QStringList extendEdits(Sequence& sequence, const QStringList& tracks, std::int64_t mark, bool toIn,
                        const SourceLength& sourceLength) {
    QStringList changed;
    for (const QString& name : tracks) {
        const Track* t = sequence.track(name);
        if (!t || t->locked) continue;
        // Edits with a segment on both sides.
        QList<std::int64_t> edits = trimPoints(sequence, name);
        if (!edits.isEmpty()) edits.removeLast();
        std::optional<std::int64_t> edit;
        std::int64_t target = toIn ? mark : mark + 1;
        for (std::int64_t e : edits) {
            if (toIn && e > mark && (!edit || e < *edit)) edit = e;
            if (!toIn && e <= mark && (!edit || e > *edit)) edit = e;
        }
        if (!edit || *edit == target) continue;
        const QList<TrimRoller> roller{{name, *edit, TrimSide::Both}};
        const TrimRange range = trimRange(sequence, roller, TrimKind::Ripple, sourceLength);
        const std::int64_t delta = target - *edit;
        if (delta < range.min || delta > range.max) continue;
        trim(sequence, roller, TrimKind::Ripple, delta, sourceLength);
        changed << name;
    }
    return changed;
}

void EditHistory::push(EditStep step) {
    steps_.resize(next_);
    steps_ << std::move(step);
    if (steps_.size() > kLevels) steps_.removeFirst();
    next_ = steps_.size();
}

const EditStep* EditHistory::undo() {
    if (!canUndo()) return nullptr;
    --next_;
    return &steps_.at(next_);
}

const EditStep* EditHistory::redo() {
    if (!canRedo()) return nullptr;
    ++next_;
    return &steps_.at(next_ - 1);
}

void EditHistory::clear() {
    steps_.clear();
    next_ = 0;
}

QStringList EditHistory::descriptions() const {
    QStringList out;
    for (qsizetype i = steps_.size() - 1; i >= 0; --i) out << steps_.at(i).description;
    return out;
}

// ---------------------------------------------------------------- effects

// The segment effect replaces any there; filler can take one.
bool applySegmentEffect(Sequence& sequence, const QString& name, std::int64_t frame, const Effect& effect, bool nest) {
    Track* t = sequence.track(name);
    // Video effects go on video tracks; never on data tracks (FX guide p.44).
    if (!t || t->kind != TrackKind::Video || t->locked) return false;
    const int i = t->segmentAt(frame);
    if (i < 0) return false;
    // A Timewarp gets its template's graphs for the segment's length (timewarpForSegment).
    const Effect applied = timewarpForSegment(effect, t->segments.at(i).length);
    if (nest && t->segments.at(i).effect) {
        // "If you apply the Timewarp effect as part of a nested effect, it must be the innermost effect"
        // (FX Guide p.177).
        if (isTimewarp(applied)) return false;
        nestEffect(t->segments[i], applied);
        return true;
    }
    t->segments[i].effect = applied;
    return true;
}

// Nullopt when there is no segment starting at `frame` (or on a data track).
std::optional<TransitionRoom> transitionRoom(const Sequence& sequence, const QString& name, std::int64_t frame,
                                             const SourceLength& sourceLength) {
    const Track* t = sequence.track(name);
    if (!t || t->kind == TrackKind::Data) return std::nullopt;
    const EditSides sides = sidesAt(*t, frame);
    if (sides.b < 0) return std::nullopt;
    const Segment& b = t->segments.at(sides.b);
    TransitionRoom room;
    if (sides.a < 0) {
        // The start of the track: from black or silence, after the cut only.
        room.after = b.length;
        return room;
    }
    const Segment& a = t->segments.at(sides.a);
    // Before the cut the incoming clip shows frames before its start; after
    // it the outgoing clip shows frames past its end (p.48).
    room.before = std::min(a.length, headRoom(b));
    room.after = std::min(b.length, tailRoom(a, sourceLength));
    return room;
}

// A zero-length transition never fits. When both sides are short, both are named.
bool transitionFits(const Transition& transition, const TransitionRoom& room, QString* which) {
    QStringList shortSides;
    if (transition.after() > room.after) shortSides << QStringLiteral("Media A");
    if (transition.before() > room.before) shortSides << QStringLiteral("Media B");
    const QString shortSide = shortSides.join(QStringLiteral(" and "));
    if (which) *which = shortSide;
    return transition.duration > 0 && shortSide.isEmpty();
}

// Ending and Starting keep their alignment while there is room on their side; Centered keeps it
// with the smaller of the two sides on both. Otherwise the transition takes all the room there
// is, realigned to fit.
Transition sizeToFit(const Transition& transition, const TransitionRoom& room) {
    Transition t = transition;
    const std::int64_t before = std::min(transition.before(), room.before);
    const std::int64_t after = std::min(transition.after(), room.after);
    switch (transition.alignment) {
    case TransitionAlignment::EndingAtCut:
        if (before > 0) {
            t.duration = before;
            return t;
        }
        break;
    case TransitionAlignment::StartingAtCut:
        if (after > 0) {
            t.duration = after;
            return t;
        }
        break;
    case TransitionAlignment::CenteredOnCut: {
        const std::int64_t half = std::min(before, after);
        if (half > 0) {
            t.duration = 2 * half;
            return t;
        }
        break;
    }
    case TransitionAlignment::CustomStart: break;
    }
    // The alignment cannot be kept: whatever room there is (p.48).
    t.duration = before + after;
    if (before == 0) {
        t.alignment = TransitionAlignment::StartingAtCut;
    } else if (after == 0) {
        t.alignment = TransitionAlignment::EndingAtCut;
    } else {
        t.alignment = TransitionAlignment::CustomStart;
        t.start = before;
    }
    return t;
}

// The transition is stored on the B-side segment, replacing any there.
bool applyTransition(Sequence& sequence, const QString& name, std::int64_t frame, const Transition& transition,
                     const SourceLength& sourceLength) {
    const auto room = transitionRoom(sequence, name, frame, sourceLength);
    if (!room || !transitionFits(transition, *room)) return false;
    Track* t = sequence.track(name);
    if (t->locked) return false;
    t->segments[sidesAt(*t, frame).b].transition = transition;
    return true;
}

// Returns the removed effect's name, or empty when there was none (or the track is locked).
QString removeEffect(Sequence& sequence, const QString& name, std::int64_t frame, EffectTarget target) {
    Track* t = sequence.track(name);
    if (!t || t->locked) return {};
    if (target == EffectTarget::Transition) {
        const int b = sidesAt(*t, frame).b;
        if (b < 0 || !t->segments.at(b).transition) return {};
        const QString removed = t->segments.at(b).transition->effect.name;
        t->segments[b].transition.reset();
        return removed;
    }
    const int i = t->segmentAt(frame);
    if (i < 0 || !t->segments.at(i).effect) return {};
    const QString removed = t->segments.at(i).effect->name;
    t->segments[i].effect.reset();
    sequence.normalize(name);
    return removed;
}

// On each track, transitions are looked at before segment effects.
std::optional<EffectRef> effectAt(const Sequence& sequence, const QStringList& tracks, std::int64_t position) {
    for (const QString& name : tracks) {
        const Track* t = sequence.track(name);
        if (!t) continue;
        std::int64_t at = 0;
        for (const Segment& s : t->segments) {
            if (s.transition && position >= at - s.transition->before() && position < at + s.transition->after()) {
                return EffectRef{name, at, EffectTarget::Transition};
            }
            at += s.length;
        }
        std::int64_t start = 0;
        const int i = t->segmentAt(position, &start);
        if (i >= 0 && t->segments.at(i).effect) return EffectRef{name, start, EffectTarget::Segment};
    }
    return std::nullopt;
}

namespace {

// The segment starting exactly at ref.frame. Non-const so that the non-const effectFor() and
// transitionFor() can share the lookup.
Segment* segmentForEffect(const Sequence& sequence, const EffectRef& ref) {
    const Track* t = sequence.track(ref.track);
    if (!t) return nullptr;
    std::int64_t start = 0;
    const int i = t->segmentAt(ref.frame, &start);
    if (i < 0 || start != ref.frame) return nullptr;
    return const_cast<Segment*>(&t->segments.at(i));
}

} // namespace

const Effect* effectFor(const Sequence& sequence, const EffectRef& ref) {
    const Segment* s = segmentForEffect(sequence, ref);
    if (!s) return nullptr;
    if (ref.target == EffectTarget::Transition) return s->transition ? &s->transition->effect : nullptr;
    return s->effect ? &*s->effect : nullptr;
}

Effect* effectFor(Sequence& sequence, const EffectRef& ref) {
    return const_cast<Effect*>(effectFor(std::as_const(sequence), ref));
}

const Transition* transitionFor(const Sequence& sequence, const EffectRef& ref) {
    if (ref.target != EffectTarget::Transition) return nullptr;
    const Segment* s = segmentForEffect(sequence, ref);
    return s && s->transition ? &*s->transition : nullptr;
}

Transition* transitionFor(Sequence& sequence, const EffectRef& ref) {
    return const_cast<Transition*>(transitionFor(std::as_const(sequence), ref));
}

// ---------------------------------------------------------------- audio

double clampGain(double db) {
    if (db <= kGainOff + 1) return kGainOff;
    return std::min(std::round(db * 10) / 10, kGainMax);
}

// Audio tracks only. A segment counts if any of its frames is in the range.
int setClipAudio(Sequence& sequence, const QString& name, std::int64_t start, std::int64_t length,
                 std::optional<double> gain, std::optional<int> pan) {
    Track* t = sequence.track(name);
    if (!t || t->locked || t->kind != TrackKind::Audio) return 0;
    int changed = 0;
    std::int64_t at = 0;
    for (Segment& s : t->segments) {
        const std::int64_t end = at + s.length;
        if (!s.isFiller() && end > start && at < start + length) {
            if (gain) s.gain = clampGain(*gain);
            if (pan && *pan == kDefaultPan) {
                s.pan = 0;
                s.hasPan = false;
            } else if (pan) {
                s.pan = std::clamp(*pan, -kPanMax, kPanMax);
                s.hasPan = true;
            }
            ++changed;
        }
        at = end;
    }
    return changed;
}

namespace {

// Calls `f` with each material segment of an unlocked audio track that overlaps [start, start + length).
template <typename F>
int eachClip(Sequence& sequence, const QString& name, std::int64_t start, std::int64_t length, F f) {
    Track* t = sequence.track(name);
    if (!t || t->locked || t->kind != TrackKind::Audio) return 0;
    int changed = 0;
    std::int64_t at = 0;
    for (Segment& s : t->segments) {
        const std::int64_t end = at + s.length;
        if (!s.isFiller() && end > start && at < start + length && f(s)) ++changed;
        at = end;
    }
    return changed;
}

} // namespace

int adjustClipAudio(Sequence& sequence, const QString& track, std::int64_t start, std::int64_t length, double byDb,
                    int byPan, int side) {
    return eachClip(sequence, track, start, length, [&](Segment& s) {
        const Segment before = s;
        if (byDb != 0.0 && s.gain > kGainOff) s.gain = clampGain(s.gain + byDb);
        if (byPan != 0) {
            s.pan = std::clamp(s.panOr(side) + byPan, -kPanMax, kPanMax);
            s.hasPan = true;
        }
        return !(s == before);
    });
}

int setDefaultClipPan(Sequence& sequence, const QString& track, std::int64_t start, std::int64_t length, int side,
                      bool commit) {
    return eachClip(sequence, track, start, length, [&](Segment& s) {
        if (commit) {
            if (s.ownPan()) return false;
            s.pan = side;
            s.hasPan = true;
            return true;
        }
        if (!s.ownPan() || s.pan != side) return false;
        s.pan = 0;
        s.hasPan = false;
        return true;
    });
}

} // namespace lumenis::model
