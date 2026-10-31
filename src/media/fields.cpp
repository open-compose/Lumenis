// Fields, pulldown and the format conversions of Timewarp effects and Motion Adapters (fields.h).
#include "lumenis/media/fields.h"

#include "lumenis/media/optical_flow.h"

#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace lumenis::media {

namespace {

QImage asArgb(const QImage& image) {
    return image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_ARGB32);
}

// Whether line y holds field `field`'s lines.
bool fieldLine(int y, int field, bool topFirst) {
    const int first = topFirst ? 0 : 1; // the line parity of the first field
    return ((y & 1) == first) == (field == 0);
}

// The mean difference of two lines' channels, 0-255.
double lineDifference(const QRgb* a, const QRgb* b, int width) {
    double sum = 0;
    for (int x = 0; x < width; ++x) {
        sum += std::abs(qRed(a[x]) - qRed(b[x])) + std::abs(qGreen(a[x]) - qGreen(b[x])) + std::abs(qBlue(a[x]) - qBlue(b[x]));
    }
    return width > 0 ? sum / (3.0 * width) : 0.0;
}

// How unlike its two fields are: the mean difference of each line from the average of the lines around it.
double combing(const QImage& frame) {
    const QImage f = asArgb(frame);
    if (f.height() < 3) return 0;
    double sum = 0;
    for (int y = 1; y + 1 < f.height(); ++y) {
        const auto* up = reinterpret_cast<const QRgb*>(f.constScanLine(y - 1));
        const auto* line = reinterpret_cast<const QRgb*>(f.constScanLine(y));
        const auto* down = reinterpret_cast<const QRgb*>(f.constScanLine(y + 1));
        for (int x = 0; x < f.width(); ++x) {
            sum += std::abs(2 * qGray(line[x]) - qGray(up[x]) - qGray(down[x])) / 2.0;
        }
    }
    return sum / (static_cast<double>(f.width()) * (f.height() - 2));
}

double frameDifference(const QImage& a, const QImage& b) {
    if (a.isNull() || b.isNull() || a.size() != b.size()) return 1e9;
    const QImage x = asArgb(a);
    const QImage y = asArgb(b);
    double sum = 0;
    for (int line = 0; line < x.height(); ++line) {
        sum += lineDifference(reinterpret_cast<const QRgb*>(x.constScanLine(line)), reinterpret_cast<const QRgb*>(y.constScanLine(line)), x.width());
    }
    return x.height() > 0 ? sum / x.height() : 0.0;
}

// The 2:3 cycle's ten fields (AA BB BC CD DD) by film frame, and each film frame's first field in it.
constexpr std::array<int, 10> kFilmOfField{0, 0, 1, 1, 1, 2, 2, 3, 3, 3};
constexpr std::array<int, 4> kFirstFieldOf{0, 2, 5, 7};

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int phaseOf(int phase) {
    return ((phase % 5) + 5) % 5;
}

} // namespace

FieldRendering fieldRendering(model::TimewarpType type) {
    switch (type) {
    case model::TimewarpType::DuplicatedField: return FieldRendering::DuplicatedField;
    case model::TimewarpType::BothFields: return FieldRendering::BothFields;
    case model::TimewarpType::InterpolatedField: return FieldRendering::InterpolatedField;
    case model::TimewarpType::VtrStyle: return FieldRendering::VtrStyle;
    case model::TimewarpType::BlendedInterpolated: return FieldRendering::BlendedInterpolated;
    case model::TimewarpType::BlendedVtr: return FieldRendering::BlendedVtr;
    case model::TimewarpType::FluidMotionDraft: return FieldRendering::FluidMotionDraft;
    case model::TimewarpType::FluidMotion: return FieldRendering::FluidMotion;
    }
    return FieldRendering::BothFields;
}

QImage fieldPicture(const QImage& frame, int field, bool topFirst, FieldFill fill) {
    if (frame.isNull()) return {};
    const QImage in = asArgb(frame);
    QImage out(in.size(), QImage::Format_ARGB32);
    const int h = in.height();
    const int w = in.width();
    // The field's own line nearest y, above (dir -1) or below (+1), staying inside the picture.
    auto own = [&](int y, int dir) {
        int at = y + dir;
        if (at < 0 || at >= h || !fieldLine(at, field, topFirst)) at = y - dir;
        return std::clamp(at, 0, h - 1);
    };
    for (int y = 0; y < h; ++y) {
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        if (fill == FieldFill::ShiftLine) {
            // Each line pair (0 and 1, 2 and 3, ...) shows the field's line of the pair, whichever field it is:
            // the second field is moved a line to sit where the first does, so the picture does not bob.
            const int from = fieldLine(y, field, topFirst) ? y : std::clamp(y ^ 1, 0, h - 1);
            std::copy_n(reinterpret_cast<const QRgb*>(in.constScanLine(fieldLine(from, field, topFirst) ? from : own(y, -1))), w, dst);
            continue;
        }
        if (fieldLine(y, field, topFirst)) {
            std::copy_n(reinterpret_cast<const QRgb*>(in.constScanLine(y)), w, dst);
            continue;
        }
        const auto* up = reinterpret_cast<const QRgb*>(in.constScanLine(own(y, -1)));
        if (fill == FieldFill::Duplicate) {
            std::copy_n(up, w, dst);
            continue;
        }
        const auto* down = reinterpret_cast<const QRgb*>(in.constScanLine(own(y, 1)));
        for (int x = 0; x < w; ++x) {
            dst[x] = qRgba((qRed(up[x]) + qRed(down[x]) + 1) / 2, (qGreen(up[x]) + qGreen(down[x]) + 1) / 2,
                           (qBlue(up[x]) + qBlue(down[x]) + 1) / 2, (qAlpha(up[x]) + qAlpha(down[x]) + 1) / 2);
        }
    }
    return out;
}

QImage weaveFields(const QImage& first, const QImage& second, bool topFirst) {
    if (first.isNull()) return second;
    if (second.isNull()) return first;
    const QImage a = asArgb(first);
    const QImage b = asArgb(second.size() == first.size() ? second : second.scaled(first.size()));
    QImage out(a.size(), QImage::Format_ARGB32);
    for (int y = 0; y < a.height(); ++y) {
        const QImage& from = fieldLine(y, 0, topFirst) ? a : b;
        std::copy_n(reinterpret_cast<const QRgb*>(from.constScanLine(y)), a.width(), reinterpret_cast<QRgb*>(out.scanLine(y)));
    }
    return out;
}

QImage adaptiveDeinterlace(const QImage& previous, const QImage& frame, const QImage& next, int field, bool topFirst) {
    if (frame.isNull()) return {};
    const QImage in = asArgb(frame);
    const QImage interpolated = fieldPicture(in, field, topFirst, FieldFill::Interpolate);
    const QImage before = previous.size() == in.size() ? asArgb(previous) : QImage();
    const QImage after = next.size() == in.size() ? asArgb(next) : QImage();
    QImage out = interpolated;
    // Ours: a pixel of the other field stays where it, and the frames around it, show the picture still (its
    // change across them, or its unlikeness to the lines around it with no frames to compare, under 12 levels
    // of 255); elsewhere it is interpolated, as moving parts would comb.
    constexpr int kStill = 12;
    for (int y = 0; y < in.height(); ++y) {
        if (fieldLine(y, field, topFirst)) continue;
        const auto* own = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        const auto* filled = reinterpret_cast<const QRgb*>(interpolated.constScanLine(y));
        const auto* b = before.isNull() ? nullptr : reinterpret_cast<const QRgb*>(before.constScanLine(y));
        const auto* a = after.isNull() ? nullptr : reinterpret_cast<const QRgb*>(after.constScanLine(y));
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            int motion = 0;
            if (b) motion = std::max(motion, std::abs(qGray(own[x]) - qGray(b[x])));
            if (a) motion = std::max(motion, std::abs(qGray(own[x]) - qGray(a[x])));
            if (!a && !b) motion = std::abs(qGray(own[x]) - qGray(filled[x]));
            if (motion < kStill) dst[x] = own[x];
        }
    }
    return out;
}

QImage blendImages(const QImage& a, const QImage& b, double t) {
    if (a.isNull()) return b;
    if (b.isNull() || t <= 0.0) return a;
    QImage out = asArgb(a);
    if (t >= 1.0) return asArgb(b.size() == a.size() ? b : b.scaled(a.size()));
    QPainter p(&out);
    p.setOpacity(t);
    p.drawImage(QRect(QPoint(0, 0), out.size()), b);
    return out;
}

std::int64_t pulldownFilmFrame(std::int64_t field, int phase) {
    const int p = phaseOf(phase);
    const std::int64_t c = 2 * p + field;
    const std::int64_t cycle = floorDiv(c, 10);
    const auto within = static_cast<std::size_t>(c - cycle * 10);
    return 4 * cycle + kFilmOfField[within] - kFilmOfField[static_cast<std::size_t>(2 * p)];
}

std::int64_t pulldownFirstField(std::int64_t film, int phase) {
    const int p = phaseOf(phase);
    const std::int64_t absolute = film + kFilmOfField[static_cast<std::size_t>(2 * p)];
    const std::int64_t cycle = floorDiv(absolute, 4);
    const auto within = static_cast<std::size_t>(absolute - cycle * 4);
    return 10 * cycle + kFirstFieldOf[within] - 2 * p;
}

std::optional<std::int64_t> pulldownWholeFrame(std::int64_t frame, int phase) {
    const std::int64_t a = pulldownFilmFrame(2 * frame, phase);
    if (a != pulldownFilmFrame(2 * frame + 1, phase)) return std::nullopt;
    return a;
}

int detectPulldownPhase(const FrameGetter& frameAt, std::int64_t frames, bool topFirst) {
    (void)topFirst; // the combing measure is the same for either field order
    const std::int64_t n = std::min<std::int64_t>(frames, 15);
    std::vector<double> comb;
    for (std::int64_t i = 0; i < n; ++i) comb.push_back(combing(frameAt(i)));
    int best = 0;
    double bestScore = 0;
    for (int p = 0; p < 5; ++p) {
        double score = 0;
        for (std::int64_t i = 0; i < n; ++i) {
            const bool split = !pulldownWholeFrame(i, p).has_value();
            score += split ? comb[static_cast<std::size_t>(i)] : -comb[static_cast<std::size_t>(i)];
        }
        if (score > bestScore + 1e-6) {
            bestScore = score;
            best = p;
        }
    }
    return best;
}

int detectRepeatCount(const FrameGetter& frameAt, std::int64_t frames) {
    const std::int64_t n = std::min<std::int64_t>(frames, 30);
    std::map<int, int> runs;
    QImage last = frameAt(0);
    int run = 1;
    for (std::int64_t i = 1; i < n; ++i) {
        const QImage f = frameAt(i);
        if (frameDifference(f, last) < 0.5) {
            ++run;
            continue;
        }
        ++runs[run];
        run = 1;
        last = f;
    }
    // A run cut short by the frames looked at is not counted unless it is the only one.
    if (runs.empty()) return run > 1 && n > 1 ? run : 1;
    int best = 1;
    int count = 0;
    for (const auto& [length, times] : runs) {
        if (times > count) {
            best = length;
            count = times;
        }
    }
    return best;
}

namespace {

bool fluid(FieldRendering r) {
    return r == FieldRendering::FluidMotion || r == FieldRendering::FluidMotionDraft;
}

bool blended(FieldRendering r) {
    return r == FieldRendering::BlendedInterpolated || r == FieldRendering::BlendedVtr || fluid(r);
}

// Two pictures `t` of the way from one to the next: mixed, or along their motion.
QImage between(const QImage& a, const QImage& b, double t, const FieldConversion& c) {
    if (t < 1e-3 || b.isNull()) return a;
    if (fluid(c.rendering)) return interpolateFrames(a, b, t, c.rendering == FieldRendering::FluidMotionDraft);
    return blendImages(a, b, t);
}

// The picture of a source of whole pictures (picture(k) for k = 0, 1, ...) at position u in them.
QImage wholePictures(const std::function<QImage(std::int64_t)>& picture, double u, const FieldConversion& c) {
    const auto k = static_cast<std::int64_t>(std::floor(u + 1e-6));
    const QImage a = picture(k);
    if (!blended(c.rendering)) return a;
    return between(a, picture(k + 1), u - static_cast<double>(k), c);
}

// A film frame of a source with 2:3 pulldown, from its first two fields.
QImage filmPicture(const FrameGetter& frameAt, std::int64_t film, const FieldConversion& c) {
    const std::int64_t g = pulldownFirstField(film, c.phase);
    const std::int64_t frame = floorDiv(g, 2);
    if (g - 2 * frame == 0) return frameAt(frame); // both fields of one frame
    // The film frame's first field is a frame's second: its second field is the next frame's first.
    return weaveFields(frameAt(frame + 1), frameAt(frame), c.topFirst);
}

// The source's picture at position p (clip frames).
QImage sourcePicture(const FrameGetter& frameAt, double p, const FieldConversion& c) {
    switch (c.source) {
    case SourceFormat::Progressive: return wholePictures(frameAt, p, c);
    case SourceFormat::ProgressiveRepeated: {
        const int r = std::max(c.repeat, 1);
        return wholePictures([&](std::int64_t k) { return frameAt(k * r); }, p / r, c);
    }
    case SourceFormat::FilmPulldown: {
        // The film position: the film frame of the field at p, and how far through its fields p is.
        const double q = 2.0 * p;
        const auto g = static_cast<std::int64_t>(std::floor(q + 1e-6));
        const std::int64_t k = pulldownFilmFrame(g, c.phase);
        const auto first = static_cast<double>(pulldownFirstField(k, c.phase));
        const auto end = static_cast<double>(pulldownFirstField(k + 1, c.phase));
        const double film = static_cast<double>(k) + std::clamp((q - first) / std::max(end - first, 1.0), 0.0, 1.0);
        return wholePictures([&](std::int64_t f) { return filmPicture(frameAt, f, c); }, film, c);
    }
    case SourceFormat::Interlaced: break;
    }
    // An interlaced source: its fields, each at its own time.
    const auto frameIndex = static_cast<std::int64_t>(std::floor(p + 1e-6));
    if (c.rendering == FieldRendering::BothFields) {
        if (!c.adaptive) return frameAt(frameIndex);
        return adaptiveDeinterlace(frameAt(frameIndex - 1), frameAt(frameIndex), frameAt(frameIndex + 1), 0, c.topFirst);
    }
    if (c.rendering == FieldRendering::DuplicatedField) return fieldPicture(frameAt(frameIndex), 0, c.topFirst, FieldFill::Duplicate);
    const FieldFill fill = c.rendering == FieldRendering::VtrStyle || c.rendering == FieldRendering::BlendedVtr ? FieldFill::ShiftLine
                                                                                                                : FieldFill::Interpolate;
    auto field = [&](std::int64_t g) {
        const std::int64_t f = floorDiv(g, 2);
        const int which = static_cast<int>(g - 2 * f);
        if (c.adaptive && fill == FieldFill::Interpolate) return adaptiveDeinterlace(frameAt(f - 1), frameAt(f), frameAt(f + 1), which, c.topFirst);
        return fieldPicture(frameAt(f), which, c.topFirst, fill);
    };
    return wholePictures(field, 2.0 * p, c);
}

} // namespace

QImage convertedFrame(const FrameGetter& frameAt, const std::function<double(double)>& positionAt, double outputFrame,
                      const FieldConversion& conversion) {
    const FieldConversion& c = conversion;
    switch (c.output) {
    case OutputFormat::Progressive: return sourcePicture(frameAt, positionAt(outputFrame), c);
    case OutputFormat::Interlaced:
        return weaveFields(sourcePicture(frameAt, positionAt(outputFrame), c), sourcePicture(frameAt, positionAt(outputFrame + 0.5), c),
                           c.topFirst);
    case OutputFormat::FilmPulldown: {
        // Film frames sampled at 24 to every 30 output frames (each 5/4 frames apart), in 2:3 cadence from
        // the effect's first frame (an A frame).
        const auto frame = static_cast<std::int64_t>(std::floor(outputFrame + 1e-6));
        const std::int64_t a = pulldownFilmFrame(2 * frame, 0);
        const std::int64_t b = pulldownFilmFrame(2 * frame + 1, 0);
        const QImage first = sourcePicture(frameAt, positionAt(static_cast<double>(a) * 1.25), c);
        if (a == b) return first;
        return weaveFields(first, sourcePicture(frameAt, positionAt(static_cast<double>(b) * 1.25), c), c.topFirst);
    }
    }
    return {};
}

} // namespace lumenis::media
