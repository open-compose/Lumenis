#include "lumenis/model/marker.h"

#include <QJsonObject>

#include <algorithm>

namespace lumenis::model {

const QList<MarkerColor>& markerColors() {
    static const QList<MarkerColor> colors{
        {QStringLiteral("Red"), 0xd23a32u},     {QStringLiteral("Orange"), 0xe8802au},
        {QStringLiteral("Yellow"), 0xe6c619u},  {QStringLiteral("Lime"), 0x9cd22cu},
        {QStringLiteral("Green"), 0x3cb04au},   {QStringLiteral("Teal"), 0x2aa096u},
        {QStringLiteral("Cyan"), 0x3cc8e6u},    {QStringLiteral("Blue"), 0x3a6cd8u},
        {QStringLiteral("Navy"), 0x2c3a8cu},    {QStringLiteral("Purple"), 0x864cc8u},
        {QStringLiteral("Magenta"), 0xd03cb4u}, {QStringLiteral("Pink"), 0xec8cb4u},
        {QStringLiteral("Brown"), 0x8c5c34u},   {QStringLiteral("Gray"), 0x8c8c8cu},
        {QStringLiteral("White"), 0xf0f0f0u},   {QStringLiteral("Black"), 0x202020u},
    };
    return colors;
}

std::uint32_t markerColor(const QString& name) {
    for (const MarkerColor& c : markerColors()) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) return c.rgb;
    }
    return markerColors().first().rgb;
}

QList<Marker> markersOf(const BinItem& item) {
    QList<Marker> out;
    for (const QJsonValue& v : item.markers) {
        const QJsonObject o = v.toObject();
        Marker m;
        m.id = o.value(QStringLiteral("id")).toString();
        m.user = o.value(QStringLiteral("user")).toString();
        m.name = o.value(QStringLiteral("name")).toString();
        m.color = o.value(QStringLiteral("color")).toString(QStringLiteral("Red"));
        m.comment = o.value(QStringLiteral("comment")).toString();
        m.track = o.value(QStringLiteral("track")).toString();
        m.frame = std::max<std::int64_t>(o.value(QStringLiteral("frame")).toInteger(), 0);
        m.length = std::max<std::int64_t>(o.value(QStringLiteral("length")).toInteger(1), 1);
        if (m.id.isEmpty()) m.id = newItemId();
        out << m;
    }
    std::stable_sort(out.begin(), out.end(), [](const Marker& a, const Marker& b) { return a.frame < b.frame; });
    return out;
}

void setMarkers(BinItem& item, QList<Marker> markers) {
    std::stable_sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.frame < b.frame; });
    QJsonArray a;
    for (const Marker& m : markers) {
        QJsonObject o{{QStringLiteral("id"), m.id},
                      {QStringLiteral("color"), m.color},
                      {QStringLiteral("track"), m.track},
                      {QStringLiteral("frame"), static_cast<qint64>(m.frame)}};
        if (!m.user.isEmpty()) o.insert(QStringLiteral("user"), m.user);
        if (!m.name.isEmpty()) o.insert(QStringLiteral("name"), m.name);
        if (!m.comment.isEmpty()) o.insert(QStringLiteral("comment"), m.comment);
        if (m.length > 1) o.insert(QStringLiteral("length"), static_cast<qint64>(m.length));
        a.append(o);
    }
    item.markers = a;
}

bool addMarker(BinItem& item, Marker marker) {
    QList<Marker> markers = markersOf(item);
    if (marker.id.isEmpty()) marker.id = newItemId();
    marker.length = std::max<std::int64_t>(marker.length, 1);
    if (marker.isSpanned()) {
        for (const Marker& m : markers) {
            if (m.isSpanned() && m.frame < marker.end() && marker.frame < m.end()) return false;
        }
    }
    markers.removeIf([&](const Marker& m) {
        return !marker.isSpanned() && !m.isSpanned() && m.frame == marker.frame && m.track == marker.track;
    });
    markers << marker;
    setMarkers(item, markers);
    return true;
}

bool removeMarker(BinItem& item, const QString& id) {
    QList<Marker> markers = markersOf(item);
    if (markers.removeIf([&](const Marker& m) { return m.id == id; }) == 0) return false;
    setMarkers(item, markers);
    return true;
}

bool updateMarker(BinItem& item, const Marker& marker) {
    QList<Marker> markers = markersOf(item);
    for (Marker& m : markers) {
        if (m.id == marker.id) {
            m = marker;
            setMarkers(item, markers);
            return true;
        }
    }
    return false;
}

std::optional<std::int64_t> previousMarker(const BinItem& item, std::int64_t frame) {
    std::optional<std::int64_t> best;
    for (const Marker& m : markersOf(item)) {
        if (m.frame < frame) best = m.frame;
    }
    return best;
}

std::optional<std::int64_t> nextMarker(const BinItem& item, std::int64_t frame) {
    for (const Marker& m : markersOf(item)) {
        if (m.frame > frame) return m.frame;
    }
    return std::nullopt;
}

std::optional<std::pair<std::int64_t, std::int64_t>> markMarkers(const BinItem& item, std::int64_t frame) {
    std::optional<std::int64_t> before;
    for (const Marker& m : markersOf(item)) {
        if (m.frame <= frame) before = m.frame;
    }
    const auto after = nextMarker(item, frame);
    if (!before || !after) return std::nullopt;
    return std::make_pair(*before, *after - 1);
}

} // namespace lumenis::model
