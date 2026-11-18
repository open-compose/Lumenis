#include "effect_drawing.h"

#include <algorithm>
#include <cmath>

// The drawing shared by the 2D effects (effect_drawing.h).
namespace lumenis::media::detail {

double boxDistance(double x, double y, double x0, double y0, double x1, double y1) {
    const double dx = std::max(x0 - x, x - x1);
    const double dy = std::max(y0 - y, y - y1);
    if (dx <= 0 && dy <= 0) return std::max(dx, dy);
    return std::hypot(std::max(dx, 0.0), std::max(dy, 0.0));
}

Border Border::of(const model::Effect& e, double time, const model::EffectSpan& span, int width) {
    const QString border = QStringLiteral("Border");
    const double unit = width / 16.0;
    Border b;
    b.width = std::max(model::numberAt(e, border, QStringLiteral("Width"), time, span).value_or(0.0), 0.0) / 100.0 * unit;
    const double soft = std::max(model::numberAt(e, border, QStringLiteral("Soft"), time, span).value_or(0.0), 0.0) / 255.0 * unit;
    b.edge = std::max(soft, 1.0);
    b.colour = model::colorAt(e, border, QStringLiteral("Color"), time, span).value_or(model::Rgb{1.0, 1.0, 1.0});
    const bool fixed = e.parameters.value(border).toObject().value(QStringLiteral("Fixed Border")).toBool();
    b.blend = fixed ? b.colour : model::colorAt(e, border, QStringLiteral("Blend Color"), time, span).value_or(b.colour);
    return b;
}

void Border::draw(float* o, double distance, const float in[4], float level) const {
    // The band, outside the foreground's region and under its soft edge.
    if (width > 0 && distance < width + edge) {
        const float a = level * static_cast<float>(std::clamp(0.5 - (distance - width) / edge, 0.0, 1.0));
        const double t = std::clamp(distance / width, 0.0, 1.0);
        const float rgb[3] = {static_cast<float>(colour.red + (blend.red - colour.red) * t),
                              static_cast<float>(colour.green + (blend.green - colour.green) * t),
                              static_cast<float>(colour.blue + (blend.blue - colour.blue) * t)};
        for (int c = 0; c < 3; ++c) o[c] = rgb[c] * a + o[c] * (1.0f - a);
        o[3] = a + o[3] * (1.0f - a);
    }
    const float a = level * static_cast<float>(std::clamp(0.5 - distance / edge, 0.0, 1.0));
    if (a > 0.0f) {
        for (int c = 0; c < 4; ++c) o[c] = in[c] * a + o[c] * (1.0f - a * in[3]);
    }
}

} // namespace lumenis::media::detail
