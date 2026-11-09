// Mask margins and Target Mask values (8.5e3); see the header. The presets are those of the Target Settings dialog's
// picture (Editing Guide p.551).
#include "lumenis/model/mask_margins.h"

#include <algorithm>

namespace lumenis::model {

QJsonObject MaskMargins::toJson() const {
    return {{QStringLiteral("preset"), preset},
            {QStringLiteral("top"), top},
            {QStringLiteral("bottom"), bottom},
            {QStringLiteral("left"), left},
            {QStringLiteral("right"), right}};
}

MaskMargins MaskMargins::fromJson(const QJsonObject& o) {
    auto percent = [&](const char* key) { return std::clamp(o.value(QLatin1String(key)).toDouble(), 0.0, 100.0); };
    MaskMargins m;
    m.preset = o.value(QStringLiteral("preset")).toString();
    m.top = percent("top");
    m.bottom = percent("bottom");
    m.left = percent("left");
    m.right = percent("right");
    return m;
}

QStringList maskPresets() {
    return {QStringLiteral("4:3"),    QStringLiteral("9:16"),   QStringLiteral("16:9"),   QStringLiteral("1:1"),
            QStringLiteral("1.32:1"), QStringLiteral("1.37:1"), QStringLiteral("1.66:1"), QStringLiteral("1.75:1"),
            QStringLiteral("1.85:1"), QStringLiteral("1.90:1"), QStringLiteral("2:1"),    QStringLiteral("2.33:1"),
            QStringLiteral("2.35:1"), QStringLiteral("2.37:1"), QStringLiteral("2.39:1"), customMaskPreset()};
}

std::optional<double> maskPresetRatio(const QString& preset) {
    if (!maskPresets().contains(preset) || preset == customMaskPreset()) return std::nullopt;
    const QStringList parts = preset.split(QLatin1Char(':'));
    if (parts.size() != 2) return std::nullopt;
    bool okA = false, okB = false;
    const double a = parts.at(0).toDouble(&okA), b = parts.at(1).toDouble(&okB);
    if (!okA || !okB || a <= 0.0 || b <= 0.0) return std::nullopt;
    return a / b;
}

MaskMargins maskForPreset(const QString& preset, double pictureRatio) {
    MaskMargins m;
    m.preset = preset;
    const auto ratio = maskPresetRatio(preset);
    if (!ratio || pictureRatio <= 0.0) return m;
    if (*ratio > pictureRatio) {
        // Wider than the picture: bands at the top and bottom.
        m.top = m.bottom = (1.0 - pictureRatio / *ratio) * 50.0;
    } else if (*ratio < pictureRatio) {
        m.left = m.right = (1.0 - *ratio / pictureRatio) * 50.0;
    }
    return m;
}

QRectF maskedArea(const QRectF& picture, const MaskMargins& margins) {
    const double left = picture.left() + picture.width() * margins.left / 100.0;
    const double right = picture.right() - picture.width() * margins.right / 100.0;
    const double top = picture.top() + picture.height() * margins.top / 100.0;
    const double bottom = picture.bottom() - picture.height() * margins.bottom / 100.0;
    return QRectF(QPointF(left, top), QPointF(std::max(left, right), std::max(top, bottom)));
}

QString targetMaskValue(TargetMask mask) {
    switch (mask) {
    case TargetMask::None: break;
    case TargetMask::MixToWhite: return QStringLiteral("white");
    case TargetMask::MixToBlack: return QStringLiteral("black");
    case TargetMask::BlackMask: return QStringLiteral("blackMask");
    }
    return QStringLiteral("none");
}

TargetMask targetMaskFrom(const QString& value) {
    if (value == QLatin1String("white")) return TargetMask::MixToWhite;
    if (value == QLatin1String("black")) return TargetMask::MixToBlack;
    if (value == QLatin1String("blackMask")) return TargetMask::BlackMask;
    return TargetMask::None;
}

QStringList targetMaskLabels() {
    return {QStringLiteral("No Mask"), QStringLiteral("Mix to White"), QStringLiteral("Mix to Black"), QStringLiteral("Black Mask")};
}

} // namespace lumenis::model
