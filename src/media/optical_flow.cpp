#include "lumenis/media/optical_flow.h"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <cstdint>

// FluidMotion's optical flow (optical_flow.h).
namespace lumenis::media {

namespace {

// A brightness plane.
struct Plane {
    int width = 0;
    int height = 0;
    std::vector<float> v;
    float at(int x, int y) const {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return v[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
};

// The brightness of `image` at `width` × `height` (smooth scaling averages as it shrinks).
Plane brightness(const QImage& image, int width, int height) {
    const QImage small = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    Plane p;
    p.width = small.width();
    p.height = small.height();
    p.v.resize(static_cast<std::size_t>(p.width) * static_cast<std::size_t>(p.height));
    for (int y = 0; y < p.height; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(small.constScanLine(y));
        for (int x = 0; x < p.width; ++x) {
            const QRgb c = line[x];
            p.v[static_cast<std::size_t>(y) * static_cast<std::size_t>(p.width) + static_cast<std::size_t>(x)] =
                0.299f * static_cast<float>(qRed(c)) + 0.587f * static_cast<float>(qGreen(c)) + 0.114f * static_cast<float>(qBlue(c));
        }
    }
    return p;
}

// Half the size, each pixel the average of four.
Plane halve(const Plane& p) {
    Plane h;
    h.width = std::max(1, p.width / 2);
    h.height = std::max(1, p.height / 2);
    h.v.resize(static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.height));
    for (int y = 0; y < h.height; ++y) {
        for (int x = 0; x < h.width; ++x) {
            h.v[static_cast<std::size_t>(y) * static_cast<std::size_t>(h.width) + static_cast<std::size_t>(x)] =
                0.25f * (p.at(2 * x, 2 * y) + p.at(2 * x + 1, 2 * y) + p.at(2 * x, 2 * y + 1) + p.at(2 * x + 1, 2 * y + 1));
        }
    }
    return h;
}

// The sum of absolute differences between the block at (bx, by) of `a` and the one moved by (dx, dy) in `b`.
float blockCost(const Plane& a, const Plane& b, int bx, int by, int size, int dx, int dy) {
    float sum = 0.0f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) sum += std::abs(a.at(bx + x, by + y) - b.at(bx + x + dx, by + y + dy));
    }
    // A slight preference for small motions, so that flat areas stay still.
    return sum + 0.05f * static_cast<float>(size * size) * static_cast<float>(std::abs(dx) + std::abs(dy));
}

// Block vectors at one level: each block searched within `radius` of its guess.
void matchLevel(const Plane& a, const Plane& b, int size, int columns, int rows, std::vector<int>& vx, std::vector<int>& vy, int radius) {
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const std::size_t i = static_cast<std::size_t>(r) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(c);
            const int gx = vx[i];
            const int gy = vy[i];
            int bestX = gx;
            int bestY = gy;
            float best = blockCost(a, b, c * size, r * size, size, gx, gy);
            for (int dy = gy - radius; dy <= gy + radius; ++dy) {
                for (int dx = gx - radius; dx <= gx + radius; ++dx) {
                    const float cost = blockCost(a, b, c * size, r * size, size, dx, dy);
                    if (cost < best) {
                        best = cost;
                        bestX = dx;
                        bestY = dy;
                    }
                }
            }
            vx[i] = bestX;
            vy[i] = bestY;
        }
    }
}

// A block whose vector no neighbour shares (within a pixel and a half) takes the median of the 3 × 3
// blocks around it, which drops stray vectors without wiping out small moving things.
void medianClean(int columns, int rows, std::vector<float>& vx, std::vector<float>& vy) {
    const std::vector<float> x0 = vx;
    const std::vector<float> y0 = vy;
    float xs[9];
    float ys[9];
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const std::size_t i = static_cast<std::size_t>(r) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(c);
            int n = 0;
            int agreeing = 0;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    const int rr = std::clamp(r + dr, 0, rows - 1);
                    const int cc = std::clamp(c + dc, 0, columns - 1);
                    const std::size_t j = static_cast<std::size_t>(rr) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(cc);
                    xs[n] = x0[j];
                    ys[n] = y0[j];
                    ++n;
                    if (j != i && std::abs(x0[j] - x0[i]) <= 1.5f && std::abs(y0[j] - y0[i]) <= 1.5f) ++agreeing;
                }
            }
            if (agreeing > 0) continue;
            std::nth_element(xs, xs + 4, xs + 9);
            std::nth_element(ys, ys + 4, ys + 9);
            vx[i] = xs[4];
            vy[i] = ys[4];
        }
    }
}

// The pixel of `image` (RGB32) at a point, from the four around it.
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
    const auto* l0 = reinterpret_cast<const QRgb*>(image.constScanLine(y0));
    const auto* l1 = reinterpret_cast<const QRgb*>(image.constScanLine(y1));
    auto mixChannel = [&](int (*channel)(QRgb)) {
        const double top = channel(l0[x0]) * (1 - fx) + channel(l0[x1]) * fx;
        const double bottom = channel(l1[x0]) * (1 - fx) + channel(l1[x1]) * fx;
        return top * (1 - fy) + bottom * fy;
    };
    return qRgb(static_cast<int>(std::lround(mixChannel(qRed))), static_cast<int>(std::lround(mixChannel(qGreen))),
                static_cast<int>(std::lround(mixChannel(qBlue))));
}

} // namespace

void FlowField::at(double x, double y, double* outX, double* outY) const {
    *outX = 0.0;
    *outY = 0.0;
    if (isEmpty()) return;
    // Block centres; bilinear between the four around the point.
    const double gx = std::clamp(x / block - 0.5, 0.0, static_cast<double>(columns - 1));
    const double gy = std::clamp(y / block - 0.5, 0.0, static_cast<double>(rows - 1));
    const int c0 = static_cast<int>(gx);
    const int r0 = static_cast<int>(gy);
    const int c1 = std::min(c0 + 1, columns - 1);
    const int r1 = std::min(r0 + 1, rows - 1);
    const double fx = gx - c0;
    const double fy = gy - r0;
    auto idx = [this](int c, int r) { return static_cast<std::size_t>(r) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(c); };
    auto lerp = [&](const std::vector<float>& v) {
        const double top = static_cast<double>(v[idx(c0, r0)]) * (1 - fx) + static_cast<double>(v[idx(c1, r0)]) * fx;
        const double bottom = static_cast<double>(v[idx(c0, r1)]) * (1 - fx) + static_cast<double>(v[idx(c1, r1)]) * fx;
        return top * (1 - fy) + bottom * fy;
    };
    *outX = lerp(dx);
    *outY = lerp(dy);
}

FlowField estimateFlow(const QImage& from, const QImage& to, bool draft) {
    FlowField field;
    if (from.isNull() || to.isNull() || from.size() != to.size()) return field;
    // A working copy at most 480 (Draft 240) pixels wide; three levels of halving above it.
    const int limit = draft ? 240 : 480;
    const double scale = std::min(1.0, static_cast<double>(limit) / std::max(from.width(), 1));
    const int width = std::max(8, static_cast<int>(std::lround(from.width() * scale)));
    const int height = std::max(8, static_cast<int>(std::lround(from.height() * scale)));
    constexpr int kLevels = 3;
    Plane a[kLevels];
    Plane b[kLevels];
    a[0] = brightness(from, width, height);
    b[0] = brightness(to, width, height);
    for (int l = 1; l < kLevels; ++l) {
        a[l] = halve(a[l - 1]);
        b[l] = halve(b[l - 1]);
    }
    // Eight-pixel blocks at the finest level halve cleanly to four and two above it.
    constexpr int size = 8;
    const int columns = std::max(1, width / size);
    const int rows = std::max(1, height / size);
    std::vector<int> vx(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows), 0);
    std::vector<int> vy(vx.size(), 0);
    // Coarsest first: the same blocks (half as many pixels each level up), each level's vectors doubled as
    // the next one's guesses and searched again close by.
    for (int l = kLevels - 1; l >= 0; --l) {
        const int levelSize = size >> l;
        if (l != kLevels - 1) {
            for (std::size_t i = 0; i < vx.size(); ++i) {
                vx[i] *= 2;
                vy[i] *= 2;
            }
        }
        matchLevel(a[l], b[l], levelSize, columns, rows, vx, vy, l == kLevels - 1 ? (draft ? 3 : 5) : 2);
    }
    field.columns = columns;
    field.rows = rows;
    field.block = static_cast<double>(size) / scale;
    field.dx.resize(vx.size());
    field.dy.resize(vy.size());
    for (std::size_t i = 0; i < vx.size(); ++i) {
        field.dx[i] = static_cast<float>(vx[i] / scale);
        field.dy[i] = static_cast<float>(vy[i] / scale);
    }
    medianClean(columns, rows, field.dx, field.dy);
    return field;
}

QImage interpolateFrames(const QImage& a, const QImage& b, double t, bool draft) {
    t = std::clamp(t, 0.0, 1.0);
    if (a.isNull()) return b;
    if (b.isNull()) return a;
    const QImage ia = a.convertToFormat(QImage::Format_RGB32);
    const QImage ib = b.convertToFormat(QImage::Format_RGB32);
    if (ia.size() != ib.size()) {
        // Not the same picture size: a plain mix.
        QImage out = ia;
        const QImage scaled = ib.scaled(ia.size());
        for (int y = 0; y < out.height(); ++y) {
            auto* o = reinterpret_cast<QRgb*>(out.scanLine(y));
            const auto* s = reinterpret_cast<const QRgb*>(scaled.constScanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                o[x] = qRgb(static_cast<int>(std::lround(qRed(o[x]) * (1 - t) + qRed(s[x]) * t)),
                            static_cast<int>(std::lround(qGreen(o[x]) * (1 - t) + qGreen(s[x]) * t)),
                            static_cast<int>(std::lround(qBlue(o[x]) * (1 - t) + qBlue(s[x]) * t)));
            }
        }
        return out;
    }
    // The motion from a to b. A point of the in-between frame came from `a` a fraction t back along its
    // motion and goes on to `b` the rest of the way. Of the vectors of the blocks around it (and no
    // motion), the one whose two ends look most alike is taken, so that a moving edge keeps its own
    // motion rather than an average of it and its background's.
    const FlowField flow = estimateFlow(ia, ib, draft);
    QImage out(ia.size(), QImage::Format_RGB32);
    auto difference = [](QRgb p, QRgb q) {
        return std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) + std::abs(qBlue(p) - qBlue(q));
    };
    for (int y = 0; y < out.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            double cx[5] = {0, 0, 0, 0, 0};
            double cy[5] = {0, 0, 0, 0, 0};
            int count = 1; // no motion first
            if (!flow.isEmpty()) {
                const double gx = std::clamp(x / flow.block - 0.5, 0.0, static_cast<double>(flow.columns - 1));
                const double gy = std::clamp(y / flow.block - 0.5, 0.0, static_cast<double>(flow.rows - 1));
                const int c0 = static_cast<int>(gx);
                const int r0 = static_cast<int>(gy);
                const int cs[2] = {c0, std::min(c0 + 1, flow.columns - 1)};
                const int rs[2] = {r0, std::min(r0 + 1, flow.rows - 1)};
                for (const int r : rs) {
                    for (const int c : cs) {
                        const std::size_t i = static_cast<std::size_t>(r) * static_cast<std::size_t>(flow.columns) + static_cast<std::size_t>(c);
                        cx[count] = static_cast<double>(flow.dx[i]);
                        cy[count] = static_cast<double>(flow.dy[i]);
                        ++count;
                    }
                }
            }
            QRgb bestA = 0, bestB = 0;
            int best = -1;
            for (int k = 0; k < count; ++k) {
                const QRgb fromA = sample(ia, x - t * cx[k], y - t * cy[k]);
                const QRgb fromB = sample(ib, x + (1 - t) * cx[k], y + (1 - t) * cy[k]);
                const int d = difference(fromA, fromB);
                if (best < 0 || d < best) {
                    best = d;
                    bestA = fromA;
                    bestB = fromB;
                }
            }
            line[x] = qRgb(static_cast<int>(std::lround(qRed(bestA) * (1 - t) + qRed(bestB) * t)),
                           static_cast<int>(std::lround(qGreen(bestA) * (1 - t) + qGreen(bestB) * t)),
                           static_cast<int>(std::lround(qBlue(bestA) * (1 - t) + qBlue(bestB) * t)));
        }
    }
    return out;
}

} // namespace lumenis::media
