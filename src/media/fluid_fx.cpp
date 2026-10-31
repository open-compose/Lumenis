// FluidBlur, FluidColorMap and FluidMorph on our optical flow (fluid_fx.h).
#include "lumenis/media/fluid_fx.h"

#include "lumenis/media/optical_flow.h"

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

QImage asArgb(const QImage& image) {
    return image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_ARGB32);
}

// The picture at (x, y), between pixels, held at the edges.
QRgb sample(const QImage& image, double x, double y) {
    const int w = image.width();
    const int h = image.height();
    x = std::clamp(x, 0.0, static_cast<double>(w - 1));
    y = std::clamp(y, 0.0, static_cast<double>(h - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const auto* r0 = reinterpret_cast<const QRgb*>(image.constScanLine(y0));
    const auto* r1 = reinterpret_cast<const QRgb*>(image.constScanLine(y1));
    auto mixChannel = [&](int (*channel)(QRgb)) {
        const double top = channel(r0[x0]) * (1 - fx) + channel(r0[x1]) * fx;
        const double bottom = channel(r1[x0]) * (1 - fx) + channel(r1[x1]) * fx;
        return static_cast<int>(std::lround(top * (1 - fy) + bottom * fy));
    };
    return qRgba(mixChannel(qRed), mixChannel(qGreen), mixChannel(qBlue), mixChannel(qAlpha));
}

// Pixels of this picture in pixels of a 1920-wide one, as the thresholds count them.
double toHd(const QImage& image) {
    return image.width() > 0 ? 1920.0 / image.width() : 1.0;
}

} // namespace

QImage fluidBlur(const QImage& previous, const QImage& frame, const QImage& next, const FluidBlurOptions& o) {
    if (frame.isNull()) return {};
    const QImage in = asArgb(frame);
    if (next.isNull() || next.size() != frame.size()) return in;
    const FlowField flow = estimateFlow(in, next);
    const FlowField before = previous.size() == frame.size() && o.trail > 0 ? estimateFlow(previous, in) : FlowField();
    const double hd = toHd(in);
    const double lower = std::max(o.lower, 0.0);
    const double upper = std::max(o.upper, lower + 1e-6);
    QImage out(in.size(), QImage::Format_ARGB32);
    for (int y = 0; y < in.height(); ++y) {
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            double dx = 0;
            double dy = 0;
            flow.at(x + 0.5, y + 0.5, &dx, &dy);
            const double motion = std::hypot(dx, dy) * hd;
            double strength = 0;
            if (motion >= lower && motion <= upper) strength = std::clamp(o.amount, 0.0, 100.0) / 100.0 * (motion - lower) / (upper - lower);
            // The blur's reach along the motion: centred on the pixel, or only behind it with Sharpen
            // Leading Edge; and with Trail, back along the previous frame's motion too.
            double back = strength * (o.sharpenLeadingEdge ? 1.0 : 0.5);
            double ahead = o.sharpenLeadingEdge ? 0.0 : strength * 0.5;
            double px = 0;
            double py = 0;
            if (!before.isEmpty()) {
                before.at(x + 0.5, y + 0.5, &px, &py);
                back += std::clamp(o.trail, 0.0, 100.0) / 100.0 * std::hypot(px, py) / std::max(std::hypot(dx, dy), 1e-6) * 0.5;
            }
            const double reach = (back + ahead) * std::hypot(dx, dy);
            if (reach < 0.5) {
                dst[x] = reinterpret_cast<const QRgb*>(in.constScanLine(y))[x];
                continue;
            }
            const int samples = std::clamp(static_cast<int>(std::ceil(reach)) + 1, 2, 48);
            double r = 0, g = 0, b = 0, a = 0;
            for (int k = 0; k < samples; ++k) {
                const double s = -back + (back + ahead) * k / (samples - 1);
                const QRgb c = sample(in, x + s * dx, y + s * dy);
                r += qRed(c);
                g += qGreen(c);
                b += qBlue(c);
                a += qAlpha(c);
            }
            dst[x] = qRgba(static_cast<int>(std::lround(r / samples)), static_cast<int>(std::lround(g / samples)),
                           static_cast<int>(std::lround(b / samples)), static_cast<int>(std::lround(a / samples)));
        }
    }
    return out;
}

QImage fluidColorMap(const QImage& frame, const QImage& next, const FluidColorMapOptions& o) {
    if (frame.isNull()) return {};
    QImage out(frame.size(), QImage::Format_ARGB32);
    out.fill(qRgb(0, 0, 0));
    if (next.isNull() || next.size() != frame.size()) return out;
    const FlowField flow = estimateFlow(frame, next);
    const double hd = toHd(frame);
    const double low = std::max(o.minMotion, 0.0);
    const double high = std::max(o.maxMotion, low + 1e-6);
    const double constant = std::clamp(o.constantSaturation, 0.0, 100.0) / 100.0;
    for (int y = 0; y < frame.height(); ++y) {
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < frame.width(); ++x) {
            double dx = 0;
            double dy = 0;
            flow.at(x + 0.5, y + 0.5, &dx, &dy);
            const double t = std::clamp((std::hypot(dx, dy) * hd - low) / (high - low), 0.0, 1.0);
            // The vectorscope's compass: the direction (up the picture as up the scope) is the angle of the
            // colour difference signals, Cb across and Cr up; brightness and saturation rise with the motion,
            // saturation less the higher Constant Saturation is.
            const double angle = std::atan2(-dy, dx);
            // Ours: brightness up to 70%, so the fastest motion keeps its colour; saturation (colour for the
            // brightness) from the constant to full as the motion rises.
            const double saturation = constant + (1.0 - constant) * t;
            const double luma = 0.7 * t;
            const double chroma = 0.4 * saturation * t;
            const double cb = chroma * std::cos(angle);
            const double cr = chroma * std::sin(angle);
            // Rec. 709.
            const double r = luma + 1.5748 * cr;
            const double g = luma - 0.1873 * cb - 0.4681 * cr;
            const double b = luma + 1.8556 * cb;
            auto byte = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
            dst[x] = qRgb(byte(r), byte(g), byte(b));
        }
    }
    return out;
}

QImage fluidMorph(const QImage& outgoing, const QImage& incoming, double t, bool featureMatch) {
    if (outgoing.isNull()) return incoming;
    if (incoming.isNull()) return outgoing;
    const QImage a = asArgb(outgoing);
    const QImage b = asArgb(incoming.size() == outgoing.size() ? incoming : incoming.scaled(outgoing.size()));
    t = std::clamp(t, 0.0, 1.0);
    if (featureMatch) return interpolateFrames(a, b, t);
    QImage out(a.size(), QImage::Format_ARGB32);
    for (int y = 0; y < a.height(); ++y) {
        const auto* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const auto* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            // Each pixel changes over in a quarter of the transition, earlier the brighter the incoming
            // picture is there than the outgoing.
            const double lead = (qGray(pb[x]) - qGray(pa[x])) / 255.0; // -1 to 1
            const double centre = 0.5 - 0.375 * lead;
            const double w = std::clamp((t - centre) / 0.25 + 0.5, 0.0, 1.0);
            auto mixChannel = [&](int (*channel)(QRgb)) {
                return static_cast<int>(std::lround(channel(pa[x]) * (1 - w) + channel(pb[x]) * w));
            };
            dst[x] = qRgba(mixChannel(qRed), mixChannel(qGreen), mixChannel(qBlue), mixChannel(qAlpha));
        }
    }
    return out;
}

} // namespace lumenis::media
