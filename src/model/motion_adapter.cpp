#include "lumenis/model/motion_adapter.h"

#include "lumenis/model/group_clip.h"

#include "lumenis/core/project_format.h"
#include "lumenis/core/timecode.h"
#include "lumenis/model/marker.h"

#include <QJsonObject>

#include <algorithm>

namespace lumenis::model {

namespace {

constexpr char kAdapterKey[] = "adapter";
// The edit rate the clip had before it was adapted, kept with the copy.
constexpr char kAdaptedFromKey[] = "adaptedFrom";

QString rateString(const Rational& r) {
    return QString::fromStdString(r.toString());
}

// "num/den" or a whole number, positive; nothing otherwise.
std::optional<Rational> rateFrom(const QString& text) {
    const QStringList parts = text.split(QLatin1Char('/'));
    bool okNum = false;
    bool okDen = true;
    const qint64 num = parts.value(0).toLongLong(&okNum);
    const qint64 den = parts.size() > 1 ? parts.value(1).toLongLong(&okDen) : 1;
    if (!okNum || !okDen || num <= 0 || den <= 0 || parts.size() > 2) return std::nullopt;
    return Rational(num, den);
}

QJsonObject adapterToJson(const MotionAdapter& a) {
    QJsonObject o{{QStringLiteral("rate"), rateString(a.clipRate)},
                  {QStringLiteral("sequenceRate"), rateString(a.sequenceRate)},
                  {QStringLiteral("fieldMotion"), a.fieldMotion},
                  {QStringLiteral("type"), a.type}};
    if (a.interlaced) o.insert(QStringLiteral("interlaced"), true);
    return o;
}

std::optional<MotionAdapter> adapterFromJson(const QJsonObject& o) {
    const auto clipRate = rateFrom(o.value(QStringLiteral("rate")).toString());
    const auto sequenceRate = rateFrom(o.value(QStringLiteral("sequenceRate")).toString());
    if (!clipRate || !sequenceRate) return std::nullopt;
    return MotionAdapter{*clipRate, *sequenceRate, o.value(QStringLiteral("fieldMotion")).toString(),
                         o.value(QStringLiteral("type")).toString(), o.value(QStringLiteral("interlaced")).toBool()};
}

// frames × to / from, rounded down: the frame of rate `to` showing at the time of frame `frames` of
// rate `from`. Exact in rationals; frames is at most a day's worth, so the products fit.
std::int64_t convertDown(std::int64_t frames, const Rational& from, const Rational& to) {
    if (from == to) return frames;
    return (Rational(frames) * to / from).floor();
}

// The first frame of rate `to` at or after the time of frame `frames` of rate `from`.
std::int64_t convertUp(std::int64_t frames, const Rational& from, const Rational& to) {
    if (from == to) return frames;
    return -(Rational(-frames) * to / from).floor();
}

// A timecode column ("Start", "End") counted at another rate: the same time, in frames of `to`.
void convertTimecode(BinItem& item, const QString& heading, const Rational& from, const Rational& to) {
    const QString text = item.fields.value(heading);
    if (text.isEmpty()) return;
    const auto frames = timecodeFrames(text, from);
    if (!frames) return;
    const bool drop = text.contains(QLatin1Char(';')) && TimecodeFormat::forRate(to, false).nominalFps % 30 == 0;
    const std::int64_t converted = (Rational(*frames) * to / from).round();
    item.fields.insert(heading, QString::fromStdString(Timecode(converted, TimecodeFormat::forRate(to, drop)).toString()));
}

// The item counted at rate `to` instead of `from`: duration, Start and End, markers and a subclip's
// offset in its master. Markers and offsets keep the frame showing their time.
void convertFrames(BinItem& item, const Rational& from, const Rational& to, bool up) {
    if (from == to) return;
    item.durationFrames = std::max<std::int64_t>((Rational(item.durationFrames) * to / from).round(), 1);
    convertTimecode(item, QStringLiteral("Start"), from, to);
    convertTimecode(item, QStringLiteral("End"), from, to);
    QList<Marker> markers = markersOf(item);
    for (Marker& m : markers) {
        const std::int64_t end = m.end();
        m.frame = up ? convertUp(m.frame, from, to) : convertDown(m.frame, from, to);
        m.length = std::max<std::int64_t>((up ? convertUp(end, from, to) : convertDown(end, from, to)) - m.frame, 1);
    }
    if (!markers.isEmpty()) setMarkers(item, markers);
    for (ClipSource& s : item.sources) {
        if (s.kind == ClipSource::Kind::Clip) s.offset = up ? convertUp(s.offset, from, to) : convertDown(s.offset, from, to);
    }
    item.editRate = to;
}

} // namespace

QString fieldMotionText(FieldMotionAttribute f) {
    switch (f) {
    case FieldMotionAttribute::Interlaced: return QStringLiteral("Interlaced");
    case FieldMotionAttribute::Film23: return QStringLiteral("2:3 Film");
    case FieldMotionAttribute::Progressive: return QStringLiteral("Progressive");
    case FieldMotionAttribute::ProgressiveStrobe: return QStringLiteral("Progressive Strobe");
    }
    return QStringLiteral("Progressive");
}

std::optional<FieldMotionAttribute> fieldMotionFromText(const QString& text) {
    static const FieldMotionAttribute all[] = {FieldMotionAttribute::Interlaced, FieldMotionAttribute::Film23,
                                               FieldMotionAttribute::Progressive, FieldMotionAttribute::ProgressiveStrobe};
    for (const FieldMotionAttribute f : all) {
        if (fieldMotionText(f).compare(text.trimmed(), Qt::CaseInsensitive) == 0) return f;
    }
    return std::nullopt;
}

// Ours for the project type: the format the clip was made in, as its Format column names it ("1080i/59.94",
// "1080p/25"), which a clip linked or imported into the project has; the project's own for clips without one.
FieldMotionAttribute clipFieldMotion(const BinItem& clip, bool interlacedProject) {
    if (const auto f = fieldMotionFromText(clip.fields.value(QLatin1String(kFieldMotionHeading)))) return *f;
    const QString format = clip.fields.value(QStringLiteral("Format"));
    if (format.contains(QLatin1String("i/"))) return FieldMotionAttribute::Interlaced;
    if (format.contains(QLatin1String("p/"))) return FieldMotionAttribute::Progressive;
    return interlacedProject ? FieldMotionAttribute::Interlaced : FieldMotionAttribute::Progressive;
}

QList<FieldMotionAttribute> fieldMotionChoices(const BinItem& clip) {
    QList<FieldMotionAttribute> out{FieldMotionAttribute::Interlaced};
    if (TimecodeFormat::forRate(clip.editRate, false).nominalFps == 30) out << FieldMotionAttribute::Film23;
    out << FieldMotionAttribute::Progressive << FieldMotionAttribute::ProgressiveStrobe;
    return out;
}

bool hasFieldMotion(const BinItem& clip) {
    if (clip.kind != BinItemKind::MasterClip && clip.kind != BinItemKind::Subclip) return false;
    // A clip without media of its own (from AAF, a bin) counts as a picture clip; one linked to sound only
    // does not.
    return clip.link.isEmpty() || clip.link.value(QStringLiteral("videoStream")).toInt(-1) >= 0;
}

Rational clipFrameRate(const BinItem& clip) {
    if (const auto a = adapterOf(clip)) return a->clipRate;
    // A clip conformed to the project's rate (Source Settings' Playback Rate, p.545) plays its frames at its own rate.
    if (hasFieldMotion(clip) && !clip.link.isEmpty() && !clip.link.value(QStringLiteral("conformed")).toBool()) {
        const QString path = clip.link.value(QStringLiteral("path")).toString();
        const qint64 frames = clip.link.value(QStringLiteral("frames")).toInteger();
        // A still lasts one frame; a sequence of images runs at the clip's rate (its path holds the
        // files' number pattern, media::ImageSequence).
        if (frames > 1 && !path.contains(QLatin1Char('%'))) {
            if (const auto rate = rateFrom(clip.link.value(QStringLiteral("rate")).toString())) return *rate;
        }
    }
    return clip.editRate;
}

bool isMixedRate(const BinItem& clip, const Rational& projectRate) {
    return hasFieldMotion(clip) && !(clipFrameRate(clip) == projectRate);
}

TimewarpType defaultAdapterType(FieldMotionAttribute source, bool blending) {
    if (blending) return TimewarpType::BlendedInterpolated;
    return source == FieldMotionAttribute::Interlaced ? TimewarpType::InterpolatedField : TimewarpType::BothFields;
}

std::optional<MotionAdapter> adapterFor(const BinItem& clip, const Rational& projectRate, bool interlacedProject, bool blending) {
    if (const auto a = adapterOf(clip)) return a;
    // Clips without a picture at another rate are counted at the project's too, so their sound stays
    // where it is; they show no adapter (isMixedRate).
    const bool audioOnly = clip.kind == BinItemKind::AudioClip && !(clip.editRate == projectRate);
    const FieldMotionAttribute source = clipFieldMotion(clip, interlacedProject);
    // A clip at the project's rate whose field motion does not match it (6.5d3a): an interlaced one in a
    // progressive project, one with 2:3 pulldown or repeated frames.
    const bool fields = hasFieldMotion(clip) && ((source == FieldMotionAttribute::Interlaced && !interlacedProject) ||
                                                 source == FieldMotionAttribute::Film23 || source == FieldMotionAttribute::ProgressiveStrobe);
    if (!isMixedRate(clip, projectRate) && !audioOnly && !fields) return std::nullopt;
    return MotionAdapter{clipFrameRate(clip), projectRate, fieldMotionText(source), timewarpTypeName(defaultAdapterType(source, blending)),
                         interlacedProject};
}

TimewarpType adapterType(const MotionAdapter& adapter) {
    return timewarpTypeFromName(adapter.type);
}

bool adapterShown(const MotionAdapter& adapter) {
    return !(adapter.clipRate == adapter.sequenceRate) || fieldMotionFromText(adapter.fieldMotion) != FieldMotionAttribute::Progressive;
}

bool adapterBlends(const MotionAdapter& adapter) {
    switch (adapterType(adapter)) {
    case TimewarpType::BlendedInterpolated:
    case TimewarpType::BlendedVtr:
    case TimewarpType::FluidMotionDraft:
    case TimewarpType::FluidMotion: return true;
    default: return false;
    }
}

BinItem adaptedClip(const BinItem& clip, const MotionAdapter& adapter) {
    BinItem out = isAdapted(clip) ? unadaptedClip(clip) : clip;
    out.composition.insert(QLatin1String(kAdaptedFromKey), rateString(out.editRate));
    // A clip linked at the project's rate is already counted in it; a clip at its own rate is converted.
    convertFrames(out, out.editRate, adapter.sequenceRate, false);
    out.composition.insert(QLatin1String(kAdapterKey), adapterToJson(adapter));
    return out;
}

std::optional<MotionAdapter> adapterOf(const BinItem& item) {
    if (item.kind == BinItemKind::Sequence) return std::nullopt;
    const QJsonObject o = item.composition.value(QLatin1String(kAdapterKey)).toObject();
    if (o.isEmpty()) return std::nullopt;
    return adapterFromJson(o);
}

bool isAdapted(const BinItem& item) {
    return adapterOf(item).has_value();
}

BinItem clipForSegment(const BinItem& clip, const Segment& segment) {
    // A group clip's segment plays its angle (group_clip.h).
    if (clip.kind == BinItemKind::Group) {
        const BinItem angle = groupAngleClip(clip, segment.groupAngle.value_or(-1), segment.groupOffsets);
        return segment.adapter ? adaptedClip(angle, *segment.adapter) : angle;
    }
    return segment.adapter ? adaptedClip(clip, *segment.adapter) : clip;
}

BinItem unadaptedClip(const BinItem& item, const std::optional<BinItem>& original) {
    const auto adapter = adapterOf(item);
    if (!adapter) return item;
    BinItem out = item;
    const Rational rate = original ? original->editRate
                                   : rateFrom(out.composition.value(QLatin1String(kAdaptedFromKey)).toString()).value_or(adapter->clipRate);
    out.composition.remove(QLatin1String(kAdapterKey));
    out.composition.remove(QLatin1String(kAdaptedFromKey));
    convertFrames(out, out.editRate, rate, true);
    if (original) {
        out.durationFrames = original->durationFrames;
        // A plain array rather than a braced list in the loop, which MSVC has mishandled.
        static const char* const headings[] = {"Start", "End"};
        for (const char* h : headings) {
            const QString heading = QLatin1String(h);
            if (original->fields.contains(heading)) {
                out.fields.insert(heading, original->fields.value(heading));
            } else {
                out.fields.remove(heading);
            }
        }
        out.sources = original->sources;
        // Markers the copy left as they were keep their own frames, which the conversion may not
        // give back exactly; moved and new ones are converted.
        const QList<Marker> shown = markersOf(adaptedClip(*original, *adapter));
        const QList<Marker> own = markersOf(*original);
        QList<Marker> kept = markersOf(item);
        bool changed = false;
        for (Marker& m : kept) {
            const auto was = std::find_if(shown.begin(), shown.end(), [&](const Marker& s) { return s.id == m.id; });
            const auto mine = std::find_if(own.begin(), own.end(), [&](const Marker& o) { return o.id == m.id; });
            if (was != shown.end() && mine != own.end() && was->frame == m.frame && was->length == m.length) {
                m.frame = mine->frame;
                m.length = mine->length;
            } else {
                const std::int64_t end = m.end();
                m.frame = convertUp(m.frame, adapter->sequenceRate, rate);
                m.length = std::max<std::int64_t>(convertUp(end, adapter->sequenceRate, rate) - m.frame, 1);
            }
            changed = true;
        }
        if (changed) setMarkers(out, kept);
    }
    return out;
}

std::int64_t clipFrameAt(std::int64_t projectFrame, const Rational& clipRate, const Rational& projectRate) {
    return convertDown(projectFrame, projectRate, clipRate);
}

std::int64_t projectFrameAt(std::int64_t clipFrame, const Rational& clipRate, const Rational& projectRate) {
    return convertUp(clipFrame, clipRate, projectRate);
}

QString mixedRateName(const QString& name, const Rational& clipRate) {
    return QStringLiteral("%1 (%2 fps)").arg(name, QString::fromStdString(frameRateText(clipRate)));
}

bool promoteAdapter(Segment& segment) {
    if (!segment.adapter || segment.effect) return false;
    Effect e = promotedTimewarp(1.0);
    Timewarp t = timewarpOf(e);
    t.type = adapterType(*segment.adapter);
    setTimewarp(e, t);
    segment.effect = e;
    return true;
}

double adapterSpeed(const MotionAdapter&) {
    return 100.0;
}

int refreshMotionAdapters(Sequence& sequence, const std::function<std::optional<BinItem>(const QString& bin, const QString& clip)>& resolve,
                          bool interlacedProject, bool blending) {
    if (!resolve) return 0;
    int changed = 0;
    for (const QString& name : sequence.trackNames()) {
        Track* track = sequence.track(name);
        for (Segment& s : track->segments) {
            if (!s.adapter) continue;
            const auto clip = resolve(s.binId, s.clipId);
            if (!clip) continue;
            // A clip set to play at the project's rate in its Source Settings (p.545) needs no adapter for its rate:
            // "select Clip > Refresh Sequence > Motion Adapters/Timewarps". Ours: the segment keeps its length.
            if (clip->link.value(QStringLiteral("conformed")).toBool() && !(s.adapter->clipRate == s.adapter->sequenceRate)) {
                s.adapter.reset();
                ++changed;
                continue;
            }
            const FieldMotionAttribute now = clipFieldMotion(*clip, interlacedProject);
            if (s.adapter->fieldMotion == fieldMotionText(now)) continue;
            const auto before = fieldMotionFromText(s.adapter->fieldMotion);
            if (before && s.adapter->type == timewarpTypeName(defaultAdapterType(*before, blending))) {
                s.adapter->type = timewarpTypeName(defaultAdapterType(now, blending));
            }
            s.adapter->fieldMotion = fieldMotionText(now);
            ++changed;
        }
    }
    return changed;
}

} // namespace lumenis::model
