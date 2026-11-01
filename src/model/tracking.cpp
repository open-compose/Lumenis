// Motion tracking's data (FX Guide ch.10, p.248-275; docs/spec/tracking.md): trackers kept with the effect, their
// paths (offset tracking, SteadyGlide and Smoothing, the reference frame), the parameters they drive, and the
// change they make at a frame. See tracking.h.
#include "lumenis/model/tracking.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace lumenis::model {

namespace {

const QString kKey = QStringLiteral("Trackers");

// Ours: the trackers' colours in turn.
const char* const kColors[] = {"#c040ff", "#00d0ff", "#ffd000", "#40ff60", "#ff6040", "#ff40b0", "#a0a0ff", "#ffffff"};

QJsonArray rectJson(const QRectF& r) {
    return {r.x(), r.y(), r.width(), r.height()};
}

QRectF rectFrom(const QJsonValue& v) {
    const QJsonArray a = v.toArray();
    if (a.size() != 4) return {};
    return {a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble(), a.at(3).toDouble()};
}

QString processName(TrackProcess p) {
    switch (p) {
    case TrackProcess::SteadyGlide: return QStringLiteral("steadyglide");
    case TrackProcess::Smoothing: return QStringLiteral("smoothing");
    case TrackProcess::None: break;
    }
    return QStringLiteral("none");
}

TrackProcess processFrom(const QString& s) {
    if (s == QLatin1String("steadyglide")) return TrackProcess::SteadyGlide;
    if (s == QLatin1String("smoothing")) return TrackProcess::Smoothing;
    return TrackProcess::None;
}

QJsonObject trackerJson(const Tracker& t) {
    QJsonArray regions;
    for (const TrackerRegion& r : t.regions) {
        QJsonArray points;
        for (const TrackPoint& p : r.points) points.append(QJsonArray{static_cast<qint64>(p.frame), p.x, p.y});
        regions.append(QJsonObject{{QStringLiteral("start"), static_cast<qint64>(r.start)},
                                   {QStringLiteral("target"), rectJson(r.target)},
                                   {QStringLiteral("search"), rectJson(r.search)},
                                   {QStringLiteral("points"), points}});
    }
    return {{QStringLiteral("name"), t.name},
            {QStringLiteral("color"), t.color},
            {QStringLiteral("enabled"), t.enabled},
            {QStringLiteral("offset"), t.offset},
            {QStringLiteral("process"), processName(t.process)},
            {QStringLiteral("smoothing"), t.smoothing},
            {QStringLiteral("reference"), static_cast<qint64>(t.reference)},
            {QStringLiteral("regions"), regions}};
}

Tracker trackerFrom(const QJsonObject& o) {
    Tracker t;
    t.name = o.value(QStringLiteral("name")).toString();
    t.color = o.value(QStringLiteral("color")).toString(QStringLiteral("#c040ff"));
    t.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    t.offset = o.value(QStringLiteral("offset")).toBool(true);
    t.process = processFrom(o.value(QStringLiteral("process")).toString());
    t.smoothing = std::clamp(o.value(QStringLiteral("smoothing")).toInt(50), 1, 100);
    t.reference = o.value(QStringLiteral("reference")).toInteger();
    for (const QJsonValue& rv : o.value(QStringLiteral("regions")).toArray()) {
        const QJsonObject ro = rv.toObject();
        TrackerRegion r;
        r.start = ro.value(QStringLiteral("start")).toInteger();
        r.target = rectFrom(ro.value(QStringLiteral("target")));
        r.search = rectFrom(ro.value(QStringLiteral("search")));
        for (const QJsonValue& pv : ro.value(QStringLiteral("points")).toArray()) {
            const QJsonArray a = pv.toArray();
            if (a.size() == 3) r.points << TrackPoint{a.at(0).toInteger(), a.at(1).toDouble(), a.at(2).toDouble()};
        }
        t.regions << r;
    }
    std::sort(t.regions.begin(), t.regions.end(), [](const TrackerRegion& a, const TrackerRegion& b) { return a.start < b.start; });
    return t;
}

const char* mappingKey(TrackedParameter p) {
    switch (p) {
    case TrackedParameter::Position: return "Position";
    case TrackedParameter::Scaling: return "Scaling";
    case TrackedParameter::Rotation: return "Rotation";
    case TrackedParameter::Tracking: return "Tracking";
    }
    return "Position";
}

// A Gaussian low-pass of the samples' coordinates over frames, sigma in frames.
QList<TrackPoint> lowPass(const QList<TrackPoint>& in, double sigma) {
    if (sigma <= 0.0 || in.size() < 3) return in;
    QList<TrackPoint> out = in;
    const double reach = 3.0 * sigma;
    for (qsizetype i = 0; i < in.size(); ++i) {
        double sx = 0.0, sy = 0.0, sw = 0.0;
        for (qsizetype j = 0; j < in.size(); ++j) {
            const double d = static_cast<double>(in.at(j).frame - in.at(i).frame);
            if (std::abs(d) > reach) continue;
            const double w = std::exp(-d * d / (2.0 * sigma * sigma));
            sx += w * in.at(j).x;
            sy += w * in.at(j).y;
            sw += w;
        }
        out[i].x = sx / sw;
        out[i].y = sy / sw;
    }
    return out;
}

// The tracker's points joined across regions by offset tracking, before processing.
QList<TrackPoint> joined(const Tracker& t) {
    QList<TrackPoint> out;
    for (qsizetype r = 0; r < t.regions.size(); ++r) {
        const TrackerRegion& region = t.regions.at(r);
        const std::int64_t end = r + 1 < t.regions.size() ? t.regions.at(r + 1).start : std::numeric_limits<std::int64_t>::max();
        QPointF offset;
        if (r > 0 && t.offset && !out.isEmpty() && !region.points.isEmpty()) {
            // At the frame before the region the new target's point and the path so far meet; elsewhere the
            // region's first point continues from the path's last.
            QPointF from(out.last().x, out.last().y);
            QPointF to(region.points.first().x, region.points.first().y);
            for (const TrackPoint& p : region.points) {
                if (p.frame == region.start - 1) {
                    to = QPointF(p.x, p.y);
                    const auto previous = std::find_if(out.begin(), out.end(), [&](const TrackPoint& q) { return q.frame == p.frame; });
                    if (previous != out.end()) from = QPointF(previous->x, previous->y);
                    break;
                }
            }
            offset = from - to;
        }
        for (const TrackPoint& p : region.points) {
            if (p.frame < region.start || p.frame >= end) continue;
            out << TrackPoint{p.frame, p.x + offset.x(), p.y + offset.y()};
        }
    }
    std::sort(out.begin(), out.end(), [](const TrackPoint& a, const TrackPoint& b) { return a.frame < b.frame; });
    return out;
}

// Ours: Smoothing's value 1-100 is a Gaussian of up to 10 frames; SteadyGlide's of up to 20 (it keeps only slow
// motion, as a pan's).
double sigmaOf(const Tracker& t) {
    if (t.process == TrackProcess::Smoothing) return t.smoothing / 100.0 * 10.0;
    if (t.process == TrackProcess::SteadyGlide) return t.smoothing / 100.0 * 20.0;
    return 0.0;
}

std::optional<QPointF> sampled(const QList<TrackPoint>& points, std::int64_t frame) {
    if (points.isEmpty()) return std::nullopt;
    if (frame <= points.first().frame) return QPointF(points.first().x, points.first().y);
    if (frame >= points.last().frame) return QPointF(points.last().x, points.last().y);
    const auto after = std::lower_bound(points.begin(), points.end(), frame, [](const TrackPoint& p, std::int64_t f) { return p.frame < f; });
    if (after->frame == frame) return QPointF(after->x, after->y);
    const TrackPoint& b = *after;
    const TrackPoint& a = *(after - 1);
    const double u = static_cast<double>(frame - a.frame) / static_cast<double>(b.frame - a.frame);
    return QPointF(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
}

// Solves the n x n system m x = b in place (Gaussian elimination with pivoting); false when singular.
bool solve(std::vector<std::vector<double>>& m, std::vector<double>& b) {
    const std::size_t n = b.size();
    for (std::size_t c = 0; c < n; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r) {
            if (std::abs(m[r][c]) > std::abs(m[pivot][c])) pivot = r;
        }
        if (std::abs(m[pivot][c]) < 1e-12) return false;
        std::swap(m[c], m[pivot]);
        std::swap(b[c], b[pivot]);
        for (std::size_t r = 0; r < n; ++r) {
            if (r == c) continue;
            const double f = m[r][c] / m[c][c];
            for (std::size_t k = c; k < n; ++k) m[r][k] -= f * m[c][k];
            b[r] -= f * b[c];
        }
    }
    for (std::size_t c = 0; c < n; ++c) b[c] /= m[c][c];
    return true;
}

using Mat3 = std::array<double, 9>;

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 out{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            for (int k = 0; k < 3; ++k) out[static_cast<std::size_t>(r * 3 + c)] += a[static_cast<std::size_t>(r * 3 + k)] * b[static_cast<std::size_t>(k * 3 + c)];
        }
    }
    return out;
}

// The map taking `from` to `to` (one to four point pairs): a move, a move with turn and size (similarity), an
// affine map, or a perspective map.
std::optional<Mat3> pointMap(const QList<QPointF>& from, const QList<QPointF>& to) {
    const qsizetype n = std::min(from.size(), to.size());
    if (n == 1) {
        const QPointF d = to.first() - from.first();
        return Mat3{1, 0, d.x(), 0, 1, d.y(), 0, 0, 1};
    }
    if (n == 2) {
        const QPointF a = from.at(1) - from.at(0);
        const QPointF b = to.at(1) - to.at(0);
        const double la = std::hypot(a.x(), a.y());
        if (la < 1e-12) return std::nullopt;
        const double s = std::hypot(b.x(), b.y()) / la;
        const double turn = std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x());
        const double c = s * std::cos(turn), sn = s * std::sin(turn);
        // to = R from + t, with R = [c -sn; sn c].
        const double tx = to.at(0).x() - (c * from.at(0).x() - sn * from.at(0).y());
        const double ty = to.at(0).y() - (sn * from.at(0).x() + c * from.at(0).y());
        return Mat3{c, -sn, tx, sn, c, ty, 0, 0, 1};
    }
    if (n == 3) {
        std::vector<std::vector<double>> m(6, std::vector<double>(6, 0.0));
        std::vector<double> b(6, 0.0);
        for (qsizetype i = 0; i < 3; ++i) {
            const auto r = static_cast<std::size_t>(i * 2);
            m[r] = {from.at(i).x(), from.at(i).y(), 1, 0, 0, 0};
            m[r + 1] = {0, 0, 0, from.at(i).x(), from.at(i).y(), 1};
            b[r] = to.at(i).x();
            b[r + 1] = to.at(i).y();
        }
        if (!solve(m, b)) return std::nullopt;
        return Mat3{b[0], b[1], b[2], b[3], b[4], b[5], 0, 0, 1};
    }
    std::vector<std::vector<double>> m(8, std::vector<double>(8, 0.0));
    std::vector<double> b(8, 0.0);
    for (qsizetype i = 0; i < 4; ++i) {
        const double x = from.at(i).x(), y = from.at(i).y(), u = to.at(i).x(), v = to.at(i).y();
        const auto r = static_cast<std::size_t>(i * 2);
        m[r] = {x, y, 1, 0, 0, 0, -u * x, -u * y};
        m[r + 1] = {0, 0, 0, x, y, 1, -v * x, -v * y};
        b[r] = u;
        b[r + 1] = v;
    }
    if (!solve(m, b)) return std::nullopt;
    return Mat3{b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], 1};
}

} // namespace

int Tracker::regionAt(std::int64_t frame) const {
    int found = -1;
    for (qsizetype i = 0; i < regions.size(); ++i) {
        if (regions.at(i).start <= frame) found = static_cast<int>(i);
    }
    return found;
}

std::optional<QPointF> Tracker::pointAt(std::int64_t frame) const {
    const int r = regionAt(frame);
    if (r < 0) return std::nullopt;
    for (const TrackPoint& p : regions.at(r).points) {
        if (p.frame == frame) return QPointF(p.x, p.y);
    }
    return std::nullopt;
}

void Tracker::setPoint(std::int64_t frame, const QPointF& at, int region) {
    if (region < 0) region = regionAt(frame);
    if (region < 0 || region >= regions.size()) return;
    QList<TrackPoint>& points = regions[region].points;
    const auto it = std::lower_bound(points.begin(), points.end(), frame, [](const TrackPoint& p, std::int64_t f) { return p.frame < f; });
    if (it != points.end() && it->frame == frame) {
        it->x = at.x();
        it->y = at.y();
    } else {
        points.insert(it, TrackPoint{frame, at.x(), at.y()});
    }
}

bool Tracker::hasData() const {
    return std::any_of(regions.begin(), regions.end(), [](const TrackerRegion& r) { return !r.points.isEmpty(); });
}

const Tracker* TrackingSetup::tracker(const QString& name) const {
    for (const Tracker& t : trackers) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

Tracker* TrackingSetup::tracker(const QString& name) {
    for (Tracker& t : trackers) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

TrackingSetup trackingOf(const Effect& effect) {
    const QJsonObject o = effect.parameters.value(kKey).toObject();
    TrackingSetup s;
    if (o.contains(QStringLiteral("engine"))) s.engine = o.value(QStringLiteral("engine")).toString();
    s.foreground = o.value(QStringLiteral("foreground")).toBool();
    for (const QJsonValue& v : o.value(QStringLiteral("list")).toArray()) s.trackers << trackerFrom(v.toObject());
    return s;
}

void setTracking(Effect& effect, const TrackingSetup& setup) {
    QJsonObject o = effect.parameters.value(kKey).toObject();
    QJsonArray list;
    for (const Tracker& t : setup.trackers) list.append(trackerJson(t));
    o.insert(QStringLiteral("list"), list);
    o.insert(QStringLiteral("engine"), setup.engine);
    o.insert(QStringLiteral("foreground"), setup.foreground);
    effect.parameters.insert(kKey, o);
}

QString nextTrackerName(const TrackingSetup& setup) {
    for (char c = 'A'; c <= 'D'; ++c) {
        const QString name = QStringLiteral("Point %1").arg(QLatin1Char(c));
        if (!setup.tracker(name)) return name;
    }
    return QStringLiteral("Untitled Tracker");
}

std::optional<Tracker> newTracker(const TrackingSetup& setup) {
    if (setup.trackers.size() >= kMaxTrackers) return std::nullopt;
    Tracker t;
    t.name = nextTrackerName(setup);
    t.color = QLatin1String(kColors[static_cast<std::size_t>(setup.trackers.size()) % std::size(kColors)]);
    TrackerRegion r;
    r.target = QRectF(0.45, 0.45, 0.1, 0.1);
    r.search = QRectF(0.4, 0.4, 0.2, 0.2);
    t.regions << r;
    return t;
}

QString trackedParameterKey(TrackedParameter parameter) {
    return QLatin1String(mappingKey(parameter));
}

int selectorCount(TrackedParameter parameter) {
    switch (parameter) {
    case TrackedParameter::Position: return 1;
    case TrackedParameter::Scaling:
    case TrackedParameter::Rotation: return 2;
    case TrackedParameter::Tracking: return 4;
    }
    return 1;
}

TrackerMapping trackerMapping(const Effect& effect, TrackedParameter parameter) {
    const QJsonObject o = effect.parameters.value(kKey).toObject().value(QStringLiteral("map")).toObject().value(QLatin1String(mappingKey(parameter))).toObject();
    TrackerMapping m;
    m.on = o.value(QStringLiteral("on")).toBool();
    for (const QJsonValue& v : o.value(QStringLiteral("selectors")).toArray()) m.selectors << v.toString();
    for (const QJsonValue& v : o.value(QStringLiteral("selectorOn")).toArray()) m.selectorOn << v.toBool();
    while (m.selectors.size() < selectorCount(parameter)) m.selectors << QString();
    while (m.selectorOn.size() < m.selectors.size()) m.selectorOn << false;
    if (o.contains(QStringLiteral("axis"))) m.axis = o.value(QStringLiteral("axis")).toString();
    m.stabilize = o.value(QStringLiteral("stabilize")).toBool();
    return m;
}

void setTrackerMapping(Effect& effect, TrackedParameter parameter, const TrackerMapping& m) {
    QJsonObject o = effect.parameters.value(kKey).toObject();
    QJsonObject map = o.value(QStringLiteral("map")).toObject();
    QJsonArray selectors, on;
    for (const QString& s : m.selectors) selectors.append(s);
    for (bool b : m.selectorOn) on.append(b);
    map.insert(QLatin1String(mappingKey(parameter)), QJsonObject{{QStringLiteral("on"), m.on},
                                                                 {QStringLiteral("selectors"), selectors},
                                                                 {QStringLiteral("selectorOn"), on},
                                                                 {QStringLiteral("axis"), m.axis},
                                                                 {QStringLiteral("stabilize"), m.stabilize}});
    o.insert(QStringLiteral("map"), map);
    effect.parameters.insert(kKey, o);
}

bool enableTracking(Effect& effect, TrackedParameter parameter, int selector) {
    const Effect before = effect;
    TrackingSetup setup = trackingOf(effect);
    TrackerMapping m = trackerMapping(effect, parameter);
    const int count = selectorCount(parameter);
    // The selectors to map: one (Tracking), or all the parameter has.
    QList<int> wanted;
    if (parameter == TrackedParameter::Tracking) {
        if (selector >= 0 && selector < count) wanted << selector;
    } else {
        for (int i = 0; i < count; ++i) wanted << i;
    }
    for (int i : wanted) {
        if (m.selectors.at(i).isEmpty() || !setup.tracker(m.selectors.at(i))) {
            // "Media Composer looks for existing trackers and maps them to the parameter's tracker selectors. If
            // trackers do not yet exist, Media Composer creates them" (p.259): the i-th tracker, else a new one.
            if (i < setup.trackers.size()) {
                m.selectors[i] = setup.trackers.at(i).name;
            } else if (const auto t = newTracker(setup)) {
                setup.trackers << *t;
                m.selectors[i] = t->name;
            }
        }
        m.selectorOn[i] = !m.selectors.at(i).isEmpty();
    }
    m.on = true;
    setTracking(effect, setup);
    setTrackerMapping(effect, parameter, m);
    // The two groups are mutually exclusive (p.251).
    const QList<TrackedParameter> others = parameter == TrackedParameter::Tracking
                                               ? QList<TrackedParameter>{TrackedParameter::Position, TrackedParameter::Scaling, TrackedParameter::Rotation}
                                               : QList<TrackedParameter>{TrackedParameter::Tracking};
    for (TrackedParameter p : others) {
        TrackerMapping other = trackerMapping(effect, p);
        if (!other.on) continue;
        other.on = false;
        setTrackerMapping(effect, p, other);
    }
    return effect.parameters != before.parameters;
}

bool disableTracking(Effect& effect, TrackedParameter parameter) {
    TrackerMapping m = trackerMapping(effect, parameter);
    if (!m.on) return false;
    // "clicking the Enable button that controls the whole parameter disables all the trackers at the same time
    // but does not change the enable status or the mappings of the individual trackers" (p.260).
    m.on = false;
    setTrackerMapping(effect, parameter, m);
    return true;
}

std::optional<QPointF> trackerPath(const Tracker& tracker, std::int64_t frame) {
    return sampled(processedPoints(tracker), frame);
}

QList<TrackPoint> processedPoints(const Tracker& tracker) {
    return lowPass(joined(tracker), sigmaOf(tracker));
}

std::optional<QPointF> trackerMovement(const Tracker& tracker, std::int64_t frame) {
    const QList<TrackPoint> points = processedPoints(tracker);
    const auto now = sampled(points, frame);
    const auto ref = sampled(points, tracker.reference);
    if (!now || !ref) return std::nullopt;
    return *now - *ref;
}

std::optional<QPointF> trackerJitter(const Tracker& tracker, std::int64_t frame) {
    const QList<TrackPoint> raw = joined(tracker);
    if (raw.isEmpty()) return std::nullopt;
    if (tracker.process != TrackProcess::SteadyGlide) return QPointF();
    const auto a = sampled(raw, frame);
    const auto b = sampled(lowPass(raw, sigmaOf(tracker)), frame);
    return *a - *b;
}

std::int64_t trackingFrame(double time, const EffectSpan& span) {
    return std::clamp<std::int64_t>(std::llround(span.frameOf(time)), 0, std::max<std::int64_t>(span.length - 1, 0));
}

TrackedChange trackedChange(const Effect& effect, std::int64_t frame, double aspect) {
    TrackedChange change;
    const TrackingSetup setup = trackingOf(effect);
    if (setup.trackers.isEmpty()) return change;
    // The trackers a mapping uses that have data, in selector order.
    auto mapped = [&](TrackedParameter p) {
        QList<const Tracker*> out;
        const TrackerMapping m = trackerMapping(effect, p);
        if (!m.on) return out;
        for (qsizetype i = 0; i < m.selectors.size(); ++i) {
            if (!m.selectorOn.value(i)) continue;
            const Tracker* t = setup.tracker(m.selectors.at(i));
            if (t && t->hasData()) out << t;
        }
        return out;
    };
    // Distances and angles are measured with the frame's own proportions (x scaled by its aspect).
    auto unit = [&](const QPointF& p) { return QPointF(p.x() * aspect, p.y()); };
    if (const QList<const Tracker*> ts = mapped(TrackedParameter::Position); !ts.isEmpty()) {
        if (const auto m = trackerMovement(*ts.first(), frame)) {
            change.position = *m;
            const QString axis = trackerMapping(effect, TrackedParameter::Position).axis;
            if (axis == QLatin1String("X Tracking Only")) change.position.setY(0.0);
            if (axis == QLatin1String("Y Tracking Only")) change.position.setX(0.0);
        }
    }
    auto pair = [&](TrackedParameter p) -> std::optional<std::pair<QPointF, QPointF>> {
        const QList<const Tracker*> ts = mapped(p);
        if (ts.size() < 2) return std::nullopt;
        const auto a = trackerPath(*ts.at(0), frame), b = trackerPath(*ts.at(1), frame);
        const auto a0 = trackerPath(*ts.at(0), ts.at(0)->reference), b0 = trackerPath(*ts.at(1), ts.at(0)->reference);
        if (!a || !b || !a0 || !b0) return std::nullopt;
        return std::pair{unit(*b - *a), unit(*b0 - *a0)};
    };
    if (const auto ab = pair(TrackedParameter::Scaling)) {
        const double was = std::hypot(ab->second.x(), ab->second.y());
        if (was > 1e-12) change.scale = std::hypot(ab->first.x(), ab->first.y()) / was;
    }
    if (const auto ab = pair(TrackedParameter::Rotation)) {
        double turn = (std::atan2(ab->first.y(), ab->first.x()) - std::atan2(ab->second.y(), ab->second.x())) * 180.0 / std::numbers::pi;
        while (turn > 180.0) turn -= 360.0;
        while (turn <= -180.0) turn += 360.0;
        change.rotation = turn; // y down: a growing angle turns clockwise
    }
    if (const QList<const Tracker*> ts = mapped(TrackedParameter::Tracking); !ts.isEmpty()) {
        QList<QPointF> from, to;
        for (const Tracker* t : ts) {
            const auto now = trackerPath(*t, frame), ref = trackerPath(*t, ts.first()->reference);
            if (!now || !ref) continue;
            from << unit(*ref);
            to << unit(*now);
        }
        if (const auto map = pointMap(from, to)) {
            // Back to fractions of the frame: S^-1 M S with S scaling x by the aspect.
            const Mat3 s{aspect, 0, 0, 0, 1, 0, 0, 0, 1};
            const Mat3 sInverse{1.0 / aspect, 0, 0, 0, 1, 0, 0, 0, 1};
            change.transform = multiply(sInverse, multiply(*map, s));
        }
    }
    return change;
}

} // namespace lumenis::model
