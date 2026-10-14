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

QJsonObject segmentToJson(const Segment& s) {
    QJsonObject o{{QStringLiteral("length"), static_cast<qint64>(s.length)}};
    if (s.isFiller()) return o;
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
    return s;
}

} // namespace

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
    const std::int64_t head = frame - start;
    tail.sourceIn += head;
    tail.length -= head;
    t.segments[i].length = head;
    t.segments.insert(i + 1, tail);
    return i + 1;
}

void Sequence::tidy(Track& t) {
    QList<Segment> out;
    for (const Segment& s : t.segments) {
        if (s.length <= 0) continue;
        if (s.isFiller() && !out.isEmpty() && out.last().isFiller()) {
            out.last().length += s.length;
        } else {
            out << (s.isFiller() ? Segment::filler(s.length) : s);
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
    return changed;
}

QStringList removeMaterial(Sequence& sequence, bool extract, std::int64_t start, std::int64_t length,
                           const QStringList& tracks) {
    QStringList changed;
    for (const QString& name : tracks) {
        const QList<Segment> removed =
            extract ? sequence.extract(name, start, length) : sequence.lift(name, start, length);
        if (!removed.isEmpty()) changed << name;
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

} // namespace lumenis::model
