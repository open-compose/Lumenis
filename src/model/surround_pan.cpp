#include "lumenis/model/surround_pan.h"

#include "lumenis/model/multichannel.h"

#include <algorithm>
#include <cmath>

// Surround panning (surround_pan.h).
namespace lumenis::model {

double SurroundPan::x() const {
    const double t = (std::clamp(frontRear, -100, 100) + 100) / 200.0;
    return rear + (front - rear) * t;
}

SurroundPan SurroundPan::at(double x, double y, const SurroundPan& from) {
    SurroundPan p = from;
    p.front = p.rear = static_cast<int>(std::lround(std::clamp(x, -100.0, 100.0)));
    p.frontRear = static_cast<int>(std::lround(std::clamp(y, -100.0, 100.0)));
    return p;
}

SurroundPan SurroundPan::at(double x, double y) {
    return at(x, y, SurroundPan{});
}

QJsonObject surroundPanToJson(const SurroundPan& p) {
    QJsonObject o{{QStringLiteral("front"), p.front}, {QStringLiteral("rear"), p.rear}, {QStringLiteral("fr"), p.frontRear}};
    const SurroundPan d;
    if (p.center != d.center) o.insert(QStringLiteral("center"), p.center);
    if (p.side != d.side) o.insert(QStringLiteral("side"), p.side);
    if (p.lfe != d.lfe) o.insert(QStringLiteral("lfe"), p.lfe);
    return o;
}

SurroundPan surroundPanFromJson(const QJsonObject& o) {
    SurroundPan p;
    auto get = [&o](const char* key, int fallback, int low, int high) {
        return std::clamp(o.value(QLatin1String(key)).toInt(fallback), low, high);
    };
    p.front = get("front", p.front, -100, 100);
    p.rear = get("rear", p.rear, -100, 100);
    p.frontRear = get("fr", p.frontRear, -100, 100);
    p.center = get("center", p.center, 0, 100);
    p.side = get("side", p.side, 0, 100);
    p.lfe = get("lfe", p.lfe, 0, 100);
    return p;
}

QList<SurroundPan> surroundPansOf(const Segment& segment, AudioFormat format, int stereoPan) {
    const int channels = channelCount(format);
    if (channels > 2) return {};
    QList<SurroundPan> out;
    for (int k = 0; k < channels; ++k) {
        if (k < segment.surround.size()) {
            out << segment.surround.at(k);
            continue;
        }
        const int x = channels == 1 ? std::clamp(stereoPan, -100, 100) : (k == 0 ? -100 : 100);
        out << SurroundPan::at(x, 100);
    }
    return out;
}

QString surroundXText(int x) {
    if (x == 0) return QStringLiteral("MID");
    return (x < 0 ? QStringLiteral("L") : QStringLiteral("R")) + QString::number(std::abs(x));
}

QString surroundYText(int y) {
    if (y == 0) return QStringLiteral("MID");
    return (y > 0 ? QStringLiteral("F") : QStringLiteral("R")) + QString::number(std::abs(y));
}

SurroundPan linkedPan(const SurroundPan& other, const SurroundPan& before, const SurroundPan& after, StereoLink link) {
    if (link == StereoLink::Unlinked) return other;
    SurroundPan p = other;
    const int dir = link == StereoLink::Mirror ? -1 : 1;
    p.front = std::clamp(other.front + dir * (after.front - before.front), -100, 100);
    p.rear = std::clamp(other.rear + dir * (after.rear - before.rear), -100, 100);
    p.frontRear = std::clamp(other.frontRear + (after.frontRear - before.frontRear), -100, 100);
    p.center = after.center;
    p.side = after.side;
    p.lfe = after.lfe;
    return p;
}

// A segment's pans are made whole first (surroundPansOf with its track's side), so setting one
// channel keeps the other's default.
int setSurroundPan(Sequence& sequence, const QString& name, std::int64_t start, std::int64_t length, int channel,
                   const SurroundPan& pan, const std::optional<SurroundPan>& other) {
    Track* t = sequence.track(name);
    if (!t || t->locked || t->kind != TrackKind::Audio) return 0;
    const int channels = channelCount(t->format);
    if (channels > 2 || channel < 0 || channel >= channels) return 0;
    int changed = 0;
    std::int64_t at = 0;
    for (Segment& s : t->segments) {
        const std::int64_t end = at + s.length;
        if (!s.isFiller() && end > start && at < start + length) {
            QList<SurroundPan> pans = surroundPansOf(s, t->format, s.pan);
            pans[channel] = pan;
            if (other && channels == 2) pans[1 - channel] = *other;
            s.surround = pans;
            ++changed;
        }
        at = end;
    }
    return changed;
}

bool setLfeOnly(Sequence& sequence, const QString& name, bool on) {
    Track* t = sequence.track(name);
    if (!t || t->kind != TrackKind::Audio || t->format != AudioFormat::Mono) return false;
    t->lfeOnly = on;
    return true;
}

} // namespace lumenis::model
