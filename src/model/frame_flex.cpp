// The Reformat attribute, FrameFlex boxes and spatial adapters as data (Editing Guide p.475-478, p.527-535; 8.5a,
// docs/spec/frameflex.md). Drawing them is media::spatialAdaptedImage's.
#include "lumenis/model/frame_flex.h"

#include "lumenis/model/sequence.h"

#include <cmath>

namespace lumenis::model {

namespace {
const QString kFrameFlexKey = QStringLiteral("frameFlex");
} // namespace

QString reformatText(Reformat r) {
    switch (r) {
    case Reformat::Stretch: return QStringLiteral("Stretch");
    case Reformat::Letterbox: return QStringLiteral("Pillarbox/Letterbox");
    case Reformat::CenterCrop: return QStringLiteral("Center Crop");
    case Reformat::CenterKeepSize: return QStringLiteral("Center Keep Size");
    }
    return {};
}

std::optional<Reformat> reformatFromText(const QString& text) {
    const QString t = text.trimmed();
    for (Reformat r : {Reformat::Stretch, Reformat::Letterbox, Reformat::CenterCrop, Reformat::CenterKeepSize}) {
        if (t.compare(reformatText(r), Qt::CaseInsensitive) == 0) return r;
    }
    if (t == QLatin1String("stretch")) return Reformat::Stretch;
    if (t == QLatin1String("letterbox")) return Reformat::Letterbox;
    if (t == QLatin1String("crop")) return Reformat::CenterCrop;
    if (t == QLatin1String("keepSize")) return Reformat::CenterKeepSize;
    return std::nullopt;
}

std::optional<Reformat> reformatOf(const BinItem& item) {
    return reformatFromText(item.fields.value(QLatin1String(kReformatHeading)));
}

bool FrameFlex::isFullImage() const {
    return std::abs(x) < 1e-9 && std::abs(y) < 1e-9 && std::abs(size - 1.0) < 1e-9 && std::abs(rotation) < 1e-9 &&
           frameAspect <= 0.0 && imageAspect <= 0.0 && std::abs(pixelAspect - 1.0) < 1e-9 && imageSize.isEmpty();
}

QJsonObject FrameFlex::toJson() const {
    QJsonObject o{{QStringLiteral("x"), x},
                  {QStringLiteral("y"), y},
                  {QStringLiteral("size"), size},
                  {QStringLiteral("rotation"), rotation},
                  {QStringLiteral("pixelAspect"), pixelAspect},
                  {QStringLiteral("imageAspect"), imageAspect},
                  {QStringLiteral("frameAspect"), frameAspect},
                  {QStringLiteral("color"), color}};
    if (!imageSize.isEmpty()) {
        o.insert(QStringLiteral("imageWidth"), imageSize.width());
        o.insert(QStringLiteral("imageHeight"), imageSize.height());
    }
    return o;
}

FrameFlex FrameFlex::fromJson(const QJsonObject& json) {
    FrameFlex f;
    f.x = json.value(QStringLiteral("x")).toDouble();
    f.y = json.value(QStringLiteral("y")).toDouble();
    f.size = json.value(QStringLiteral("size")).toDouble(1.0);
    if (f.size <= 0) f.size = 1.0;
    f.rotation = json.value(QStringLiteral("rotation")).toDouble();
    f.pixelAspect = json.value(QStringLiteral("pixelAspect")).toDouble(1.0);
    if (f.pixelAspect <= 0) f.pixelAspect = 1.0;
    f.imageAspect = std::max(0.0, json.value(QStringLiteral("imageAspect")).toDouble());
    f.frameAspect = std::max(0.0, json.value(QStringLiteral("frameAspect")).toDouble());
    f.color = json.value(QStringLiteral("color")).toString(f.color);
    const int w = json.value(QStringLiteral("imageWidth")).toInt();
    const int h = json.value(QStringLiteral("imageHeight")).toInt();
    if (w > 0 && h > 0) f.imageSize = QSize(w, h);
    return f;
}

std::optional<FrameFlex> frameFlexOf(const BinItem& item) {
    const QJsonValue v = item.sourceSettings.value(kFrameFlexKey);
    if (!v.isObject()) return std::nullopt;
    return FrameFlex::fromJson(v.toObject());
}

void setFrameFlex(BinItem& item, const std::optional<FrameFlex>& flex) {
    if (flex) {
        item.sourceSettings.insert(kFrameFlexKey, flex->toJson());
    } else {
        item.sourceSettings.remove(kFrameFlexKey);
    }
}

FrameFlex sizeMatchingRaster(FrameFlex flex, QSize imageRaster, QSize projectRaster) {
    const QSize image = flex.imageSize.isEmpty() ? imageRaster : flex.imageSize;
    if (image.width() <= 0 || projectRaster.width() <= 0 || projectRaster.height() <= 0) return flex;
    // The box is the project raster's width of the image's pixels, in the project's shape.
    flex.size = static_cast<double>(projectRaster.width()) / (static_cast<double>(image.width()) * flex.pixelAspect);
    flex.frameAspect = static_cast<double>(projectRaster.width()) / static_cast<double>(projectRaster.height());
    return flex;
}

QJsonObject SpatialAdapter::toJson() const {
    QJsonObject o{{QStringLiteral("reformat"), reformatText(reformat)}};
    if (frameFlex) o.insert(kFrameFlexKey, frameFlex->toJson());
    if (creative) o.insert(QStringLiteral("creative"), true);
    return o;
}

SpatialAdapter SpatialAdapter::fromJson(const QJsonObject& json) {
    SpatialAdapter a;
    a.reformat = reformatFromText(json.value(QStringLiteral("reformat")).toString()).value_or(Reformat::Letterbox);
    if (json.value(kFrameFlexKey).isObject()) a.frameFlex = FrameFlex::fromJson(json.value(kFrameFlexKey).toObject());
    a.creative = json.value(QStringLiteral("creative")).toBool();
    return a;
}

std::optional<SpatialAdapter> spatialAdapterOf(const BinItem& item) {
    if (item.kind != BinItemKind::MasterClip && item.kind != BinItemKind::Subclip) return std::nullopt;
    const auto reformat = reformatOf(item);
    auto flex = frameFlexOf(item);
    if (flex && flex->isFullImage()) flex.reset();
    if (!reformat && !flex) return std::nullopt;
    SpatialAdapter a;
    a.reformat = reformat.value_or(Reformat::Letterbox);
    a.frameFlex = flex;
    return a;
}

int refreshSpatialAdapters(Sequence& sequence, const std::function<std::optional<BinItem>(const QString& bin, const QString& clip)>& resolve,
                           bool creative) {
    int changed = 0;
    if (!resolve) return 0;
    for (const QString& name : sequence.trackNames()) {
        Track* track = sequence.track(name);
        if (!track || track->kind != TrackKind::Video) continue;
        for (Segment& s : track->segments) {
            if (s.isFiller() || s.clipId.isEmpty()) continue;
            const auto clip = resolve(s.binId, s.clipId);
            if (!clip) continue;
            // Framing changed in the sequence stays unless asked for (p.478).
            if (s.spatial && s.spatial->creative && !creative) continue;
            const std::optional<SpatialAdapter> now = spatialAdapterOf(*clip);
            if (now == s.spatial) continue;
            s.spatial = now;
            ++changed;
        }
    }
    return changed;
}

} // namespace lumenis::model
