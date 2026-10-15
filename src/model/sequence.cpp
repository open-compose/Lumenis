#include "lumenis/model/sequence.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>

namespace lumenis::model {

namespace {

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

void effectsToJson(const Segment& s, QJsonObject& o) {
    if (s.effect) o.insert(QStringLiteral("effect"), s.effect->toJson());
    if (s.transition) o.insert(QStringLiteral("transition"), transitionToJson(*s.transition));
}

QJsonObject segmentToJson(const Segment& s) {
    QJsonObject o{{QStringLiteral("length"), static_cast<qint64>(s.length)}};
    effectsToJson(s, o);
    if (s.isFiller()) {
        if (s.addEdit) o.insert(QStringLiteral("addEdit"), true);
        return o;
    }
    o.insert(QStringLiteral("clip"), s.clipId);
    if (!s.binId.isEmpty()) o.insert(QStringLiteral("bin"), s.binId);
    o.insert(QStringLiteral("name"), s.name);
    o.insert(QStringLiteral("track"), s.sourceTrack);
    o.insert(QStringLiteral("in"), static_cast<qint64>(s.sourceIn));
    return o;
}

Segment segmentFromJson(const QJsonObject& o) {
    Segment s;
    s.clipId = o.value(QStringLiteral("clip")).toString();
    s.binId = o.value(QStringLiteral("bin")).toString();
    s.name = o.value(QStringLiteral("name")).toString();
    s.sourceTrack = o.value(QStringLiteral("track")).toString();
    s.sourceIn = o.value(QStringLiteral("in")).toInteger();
    s.length = std::max<std::int64_t>(o.value(QStringLiteral("length")).toInteger(), 0);
    s.addEdit = s.isFiller() && o.value(QStringLiteral("addEdit")).toBool();
    if (o.contains(QStringLiteral("effect"))) s.effect = Effect::fromJson(o.value(QStringLiteral("effect")).toObject());
    if (o.contains(QStringLiteral("transition"))) {
        s.transition = transitionFromJson(o.value(QStringLiteral("transition")).toObject());
    }
    return s;
}

} // namespace

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

std::int64_t Track::length() const {
    std::int64_t total = 0;
    for (const Segment& s : segments) total += s.length;
    return total;
}

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

Sequence Sequence::withDefaultTracks() {
    Sequence s;
    s.tracks_ = {Track{TrackKind::Video, 1, AudioFormat::Mono, {}}, Track{TrackKind::Audio, 1, AudioFormat::Mono, {}},
                 Track{TrackKind::Audio, 2, AudioFormat::Mono, {}}};
    return s;
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
        if (t.kind == TrackKind::Audio && t.format != AudioFormat::Mono) {
            for (const auto& [format, text] : kFormats) {
                if (format == t.format) o.insert(QStringLiteral("format"), QString::fromLatin1(text));
            }
        }
        tracks.append(o);
    }
    return QJsonObject{{QStringLiteral("tracks"), tracks}, {QStringLiteral("timecode"), timecode_}};
}

Sequence Sequence::fromJson(const QJsonObject& o) {
    Sequence s;
    s.timecode_ = o.value(QStringLiteral("timecode")).toBool(true);
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
        for (const QJsonValue& sv : to.value(QStringLiteral("segments")).toArray()) {
            t.segments << segmentFromJson(sv.toObject());
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

Track* Sequence::ensureTrack(const QString& name) {
    if (Track* t = track(name)) return t;
    const auto ref = parseTrackName(name);
    if (!ref || ref->number > (ref->kind == TrackKind::Data ? 1 : kMaxTracks)) return nullptr;
    tracks_ << Track{ref->kind, ref->number, AudioFormat::Mono, {}};
    sortTracks();
    return track(name);
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
    if (!tail.isFiller()) tail.sourceIn += head;
    tail.length -= head;
    t.segments[i].length = head;
    t.segments.insert(i + 1, tail);
    return i + 1;
}

void Sequence::tidy(Track& t) {
    QList<Segment> out;
    for (const Segment& s : t.segments) {
        if (s.length <= 0) continue;
        // Filler joins the filler before it, unless effects tell them apart.
        if (s.isFiller() && !s.addEdit && !s.hasEffects() && !out.isEmpty() && out.last().isFiller() &&
            !out.last().effect) {
            out.last().length += s.length;
        } else {
            Segment kept = s;
            if (s.isFiller()) {
                kept = Segment::filler(s.length);
                kept.effect = s.effect;
                kept.transition = s.transition;
            }
            kept.addEdit = s.isFiller() && s.addEdit && !out.isEmpty() && out.last().isFiller();
            out << kept;
        }
    }
    t.segments = out;
}

void Sequence::splice(const QString& name, std::int64_t at, const QList<Segment>& material) {
    Track* t = track(name);
    if (!t || at < 0) return;
    const int i = cut(*t, at);
    for (int k = 0; k < material.size(); ++k) t->segments.insert(i + k, material.at(k));
    tidy(*t);
}

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

QStringList sourceTracksOf(const BinItem& item) {
    if (item.kind == BinItemKind::Sequence) return Sequence::fromItem(item).trackNames();
    QStringList tracks = parseTrackList(item.fields.value(QStringLiteral("Tracks")));
    if (!tracks.isEmpty()) return tracks;
    if (item.kind == BinItemKind::AudioClip) return {QStringLiteral("A1"), QStringLiteral("A2")};
    return {QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")};
}

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

EditPlan planEdit(const EditMarks& source, const EditMarks& record) {
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
        // Single-mark editing: the source position is the IN (p.468).
        plan.sourceStart = source.position;
        plan.length = *sOut - source.position + 1;
        plan.recordStart = recordStartFor(plan.length);
    } else if (rOut) {
        plan.sourceStart = sIn.value_or(source.position);
        plan.recordStart = record.position;
        plan.length = *rOut - record.position + 1;
    } else {
        // From the source IN (or position) to the end of the clip, at the
        // record IN or position (p.466, p.501).
        plan.sourceStart = sIn.value_or(source.position);
        plan.length = source.duration - plan.sourceStart;
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

QStringList applyEdit(Sequence& sequence, EditKind kind, const EditPlan& plan, const QString& binId,
                      const BinItem& source, const TrackPatch& patch) {
    QStringList changed;
    if (!plan.ok()) return changed;
    const std::optional<Sequence> nested =
        source.kind == BinItemKind::Sequence ? std::optional<Sequence>(Sequence::fromItem(source)) : std::nullopt;
    for (const auto& [from, to] : patch.editPairs(sequence)) {
        if (const Track* existing = sequence.track(to); existing && existing->locked) continue;
        if (!sequence.ensureTrack(to)) continue;
        QList<Segment> material;
        if (nested) {
            material = nested->material(from, plan.sourceStart, plan.length);
        } else {
            Segment s;
            s.clipId = source.id;
            s.binId = binId;
            s.name = source.name;
            s.sourceTrack = from;
            s.sourceIn = plan.sourceStart;
            s.length = plan.length;
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
    if (before.isFiller() || after.isFiller()) return before.isFiller() && after.isFiller() && after.addEdit;
    return before.clipId == after.clipId && before.binId == after.binId && before.sourceTrack == after.sourceTrack &&
           before.sourceIn + before.length == after.sourceIn;
}

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

Sequence Sequence::excerpt(const QStringList& names, std::int64_t start, std::int64_t length) const {
    Sequence out;
    out.timecode_ = timecode_;
    for (const Track& t : tracks_) {
        if (!names.contains(t.name())) continue;
        Track copy = t;
        copy.locked = false;
        copy.syncLocked = false;
        copy.segments = material(t.name(), start, length);
        tidy(copy);
        bool empty = true;
        for (const Segment& s : copy.segments) empty = empty && s.isFiller();
        if (!empty) out.tracks_ << copy;
    }
    return out;
}

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

std::optional<QString> shiftedTrack(const QString& name, int by) {
    const auto ref = parseTrackName(name);
    if (!ref) return std::nullopt;
    const int number = ref->number + by;
    if (number < 1 || number > (ref->kind == TrackKind::Data ? 1 : kMaxTracks)) return std::nullopt;
    return trackName(ref->kind, number);
}

} // namespace

QList<SegmentRef> moveSegments(Sequence& sequence, const QList<SegmentRef>& segments, std::int64_t frames, int tracks,
                               EditKind kind, bool copy, QString* error) {
    auto failed = [error](const QString& text) {
        if (error) *error = text;
        return QList<SegmentRef>();
    };
    struct Piece {
        SegmentRef from;
        Segment segment;
        QString to;
        std::int64_t at = 0;
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
        if (ref.start + frames < 0) return failed(QStringLiteral("The segment would start before the sequence."));
        pieces << Piece{ref, *seg, *to, ref.start + frames};
    }
    if (pieces.isEmpty()) return failed(QStringLiteral("Select segments to move."));
    // Remove the originals, the latest first so earlier ones keep their
    // place; Extract shifts later destinations on the same track.
    if (!copy) {
        QList<Piece> order = pieces;
        std::sort(order.begin(), order.end(), [](const Piece& a, const Piece& b) { return a.from.start > b.from.start; });
        for (const Piece& p : order) {
            if (kind == EditKind::SpliceIn) {
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
        if (!sequence.ensureTrack(p.to)) continue;
        if (kind == EditKind::SpliceIn) {
            sequence.splice(p.to, p.at, {p.segment});
            for (SegmentRef& m : moved) {
                if (m.track == p.to && m.start >= p.at) m.start += p.segment.length;
            }
        } else {
            sequence.overwrite(p.to, p.at, {p.segment});
        }
        moved << SegmentRef{p.to, p.at};
    }
    std::reverse(moved.begin(), moved.end());
    return moved;
}

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
        return MatchedFrame{s, s.sourceIn + position - start};
    }
    return std::nullopt;
}

void Sequence::normalize(const QString& name) {
    if (Track* t = track(name)) tidy(*t);
}

namespace {

constexpr std::int64_t kNoLimit = std::int64_t(1) << 40;

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
int rippleSign(TrimSide side) {
    return side == TrimSide::A ? 1 : side == TrimSide::B ? -1 : 0;
}

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
            // Takes `by` frames off the start (negative: adds them).
            if (!s->isFiller()) s->sourceIn += by;
            s->length -= by;
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
        if (!next.isFiller()) next.sourceIn += delta;
        next.length -= delta;
        ref.start += delta;
        sequence.normalize(ref.track);
    }
    return delta;
}

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

bool applySegmentEffect(Sequence& sequence, const QString& name, std::int64_t frame, const Effect& effect) {
    Track* t = sequence.track(name);
    // Video effects go on video tracks; never on data tracks (FX guide p.44).
    if (!t || t->kind != TrackKind::Video || t->locked) return false;
    const int i = t->segmentAt(frame);
    if (i < 0) return false;
    t->segments[i].effect = effect;
    return true;
}

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

bool transitionFits(const Transition& transition, const TransitionRoom& room, QString* which) {
    QString shortSide;
    if (transition.after() > room.after) shortSide = QStringLiteral("Media A");
    if (transition.before() > room.before) shortSide = QStringLiteral("Media B");
    if (which) *which = shortSide;
    return transition.duration > 0 && shortSide.isEmpty();
}

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

bool applyTransition(Sequence& sequence, const QString& name, std::int64_t frame, const Transition& transition,
                     const SourceLength& sourceLength) {
    const auto room = transitionRoom(sequence, name, frame, sourceLength);
    if (!room || !transitionFits(transition, *room)) return false;
    Track* t = sequence.track(name);
    if (t->locked) return false;
    t->segments[sidesAt(*t, frame).b].transition = transition;
    return true;
}

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

} // namespace lumenis::model
