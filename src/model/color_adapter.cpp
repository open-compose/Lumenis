// Colour encoding and colour adapters as data (Editing Guide p.536-541, p.544; 8.5c, docs/spec/color-adapters.md). The
// transformations themselves are media::applyColorTransformations'.
#include "lumenis/model/color_adapter.h"

#include "lumenis/model/sequence.h"

#include <QJsonArray>

namespace lumenis::model {

namespace {
const QString kEncodingKey = QStringLiteral("colorEncoding");
} // namespace

QStringList sourceColorSpaces() {
    return {QStringLiteral("Unknown"),     QStringLiteral("Rec.709"),         QStringLiteral("Rec.601"),
            QStringLiteral("Rec.2020"),    QStringLiteral("sRGB"),            QStringLiteral("Linear"),
            QStringLiteral("Gamma 2.6"),   QStringLiteral("Printing Density (Cineon)"), QStringLiteral("Arri Log-C"),
            QStringLiteral("Canon C-Log"), QStringLiteral("Sony S-Log2 S-Gamut")};
}

QJsonObject ColorEncoding::toJson() const {
    QJsonObject o{{QStringLiteral("sourceSpace"), sourceSpace},
                  {QStringLiteral("transformations"), QJsonArray::fromStringList(transformations)}};
    if (bypass) o.insert(QStringLiteral("bypass"), true);
    return o;
}

ColorEncoding ColorEncoding::fromJson(const QJsonObject& json) {
    ColorEncoding e;
    e.sourceSpace = json.value(QStringLiteral("sourceSpace")).toString(e.sourceSpace);
    if (e.sourceSpace.isEmpty()) e.sourceSpace = QStringLiteral("Unknown");
    for (const QJsonValue& v : json.value(QStringLiteral("transformations")).toArray()) {
        if (!v.toString().isEmpty()) e.transformations << v.toString();
    }
    e.bypass = json.value(QStringLiteral("bypass")).toBool();
    return e;
}

std::optional<ColorEncoding> colorEncodingOf(const BinItem& item) {
    const QJsonValue v = item.sourceSettings.value(kEncodingKey);
    if (!v.isObject()) return std::nullopt;
    return ColorEncoding::fromJson(v.toObject());
}

QString colorTransformationsText(const ColorEncoding& encoding) {
    const QString list = encoding.transformations.join(QStringLiteral(", "));
    if (encoding.bypass && !list.isEmpty()) return QStringLiteral("Bypassed: ") + list;
    return list;
}

void setColorEncoding(BinItem& item, const std::optional<ColorEncoding>& encoding) {
    if (encoding && !encoding->isEmpty()) {
        item.sourceSettings.insert(kEncodingKey, encoding->toJson());
        const QString text = colorTransformationsText(*encoding);
        if (text.isEmpty()) {
            item.fields.remove(QLatin1String(kColorTransformationsHeading));
        } else {
            item.fields.insert(QLatin1String(kColorTransformationsHeading), text);
        }
    } else {
        item.sourceSettings.remove(kEncodingKey);
        item.fields.remove(QLatin1String(kColorTransformationsHeading));
    }
}

QStringList autoTransformations(const QString& space) {
    // The guide's transformations (p.539-540) that take each space to Rec. 709 video levels.
    const QString toRec709 = QStringLiteral("Linear full range to REC709");
    if (space == QLatin1String("Linear")) return {toRec709};
    if (space == QLatin1String("Gamma 2.6")) return {QStringLiteral("Gamma 2.6 to Linear"), toRec709};
    if (space == QLatin1String("sRGB")) return {QStringLiteral("SRGB (IEC 61966-2.1) to linear"), toRec709};
    if (space == QLatin1String("Printing Density (Cineon)")) return {QStringLiteral("Printing Density (Cineon) to linear"), toRec709};
    if (space == QLatin1String("Arri Log-C")) return {QStringLiteral("Arri ALEXA Log-C SUP 3.x to ITU 709")};
    if (space == QLatin1String("Canon C-Log")) return {QStringLiteral("Canon C-Log to REC709")};
    // Rec. 601 and 2020 differ from 709 in their primaries, which no listed transformation converts; Sony's looks come
    // through a provider (ours).
    return {};
}

std::optional<ColorEncoding> colorAdapterOf(const BinItem& item) {
    if (item.kind != BinItemKind::MasterClip && item.kind != BinItemKind::Subclip) return std::nullopt;
    const auto e = colorEncodingOf(item);
    if (!e || e->applied().isEmpty()) return std::nullopt;
    return e;
}

int refreshColorAdapters(Sequence& sequence, const std::function<std::optional<BinItem>(const QString& bin, const QString& clip)>& resolve) {
    if (!resolve) return 0;
    int changed = 0;
    for (const QString& name : sequence.trackNames()) {
        Track* track = sequence.track(name);
        if (!track || track->kind != TrackKind::Video) continue;
        for (Segment& s : track->segments) {
            if (s.isFiller() || s.clipId.isEmpty()) continue;
            const auto clip = resolve(s.binId, s.clipId);
            if (!clip) continue;
            const std::optional<ColorEncoding> now = colorAdapterOf(*clip);
            if (now == s.color) continue;
            s.color = now;
            ++changed;
        }
    }
    return changed;
}

int removeColorAdapters(Sequence& sequence) {
    int removed = 0;
    for (const QString& name : sequence.trackNames()) {
        Track* track = sequence.track(name);
        if (!track) continue;
        for (Segment& s : track->segments) {
            if (!s.color) continue;
            s.color.reset();
            ++removed;
        }
    }
    return removed;
}

} // namespace lumenis::model
