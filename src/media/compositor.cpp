#include "lumenis/media/compositor.h"
#include "lumenis/media/plasma_wipe.h"
#include "lumenis/media/color_correction.h"
#include "lumenis/media/classic_title.h"
#include "lumenis/media/titler.h"
#include "lumenis/media/tracking.h"

#include "lumenis/media/fields.h"
#include "lumenis/media/fluid_fx.h"
#include "lumenis/media/optical_flow.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/effect_render.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/timewarp.h"
#include "lumenis/model/title_clip.h"

#include "effect_drawing.h"

#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cmath>

// The compositor (compositor.h). Tracks are walked from the top down to find what hides the rest,
// then composited from the bottom up. Each effect is a function of the pictures it takes and its
// parameters (model::Effect::parameters, by category and name, as the Effect Editor stores them).
namespace lumenis::media {

namespace {

constexpr float kDensityFloor = 1e-3f; // the darkest light the film blend works with

bool parameterFlag(const model::Effect& effect, const QString& category, const QString& name) {
    return effect.parameters.value(category).toObject().value(name).toBool();
}

// A number parameter of the Fluid effects at `time`, else its value until set.
double fluidNumber(const model::Effect& e, const QString& category, const QString& name, double time, std::int64_t length) {
    return model::numberAt(e, category, name, time, model::EffectSpan(std::max<std::int64_t>(length, 1)))
        .value_or(parameterDefault(e, category, name).toDouble());
}

bool isFluid(const model::Effect& e, const char* name) {
    return e.category == QLatin1String("Illusion FX") && e.name == QLatin1String(name);
}

} // namespace

// ---------------------------------------------------------------- Picture

Picture::Picture(int width, int height)
    : width_(std::max(width, 0)), height_(std::max(height, 0)),
      data_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4, 0.0f) {}

Picture Picture::solid(int width, int height, const QColor& color) {
    Picture p(width, height);
    const float a = static_cast<float>(color.alphaF());
    const float rgba[4] = {static_cast<float>(color.redF()) * a, static_cast<float>(color.greenF()) * a,
                           static_cast<float>(color.blueF()) * a, a};
    for (std::size_t i = 0; i < p.data_.size(); i += 4) std::copy(rgba, rgba + 4, &p.data_[i]);
    return p;
}

Picture Picture::fromImage(const QImage& image, int width, int height, ImageInterpolation interpolation) {
    Picture p(width, height);
    if (image.isNull() || p.isNull()) return p;
    QImage fitted = image;
    if (image.size() != QSize(width, height)) {
        fitted = image.scaled(width, height, Qt::KeepAspectRatio,
                              interpolation == ImageInterpolation::Draft ? Qt::FastTransformation : Qt::SmoothTransformation);
    }
    fitted = fitted.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const int ox = (width - fitted.width()) / 2;
    const int oy = (height - fitted.height()) / 2;
    for (int y = 0; y < fitted.height(); ++y) {
        const uchar* line = fitted.constScanLine(y);
        for (int x = 0; x < fitted.width(); ++x) {
            float* out = p.pixel(ox + x, oy + y);
            for (int c = 0; c < 4; ++c) out[c] = static_cast<float>(line[x * 4 + c]) / 255.0f;
        }
    }
    return p;
}

void Picture::quantize(EffectPrecision precision) {
    if (precision == EffectPrecision::Bits32) return;
    const float steps = precision == EffectPrecision::Bits8 ? 255.0f : 65535.0f;
    for (float& v : data_) v = std::round(std::clamp(v, 0.0f, 1.0f) * steps) / steps;
}

QImage Picture::toImage() const {
    QImage out(width_, height_, QImage::Format_RGB32);
    for (int y = 0; y < height_; ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < width_; ++x) {
            const float* p = pixel(x, y);
            // Premultiplied over black: the colour channels as they are.
            auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            line[x] = qRgb(channel(p[0]), channel(p[1]), channel(p[2]));
        }
    }
    return out;
}

// ---------------------------------------------------------------- blends

Picture mix(const Picture& a, const Picture& b, double t) {
    Picture out(a.width(), a.height());
    const float f = static_cast<float>(std::clamp(t, 0.0, 1.0));
    const auto& x = a.data();
    const auto& y = b.data();
    auto& o = out.data();
    for (std::size_t i = 0; i < o.size(); ++i) o[i] = x[i] + (y[i] - x[i]) * f;
    return out;
}

Picture over(const Picture& top, const Picture& bottom) {
    Picture out(top.width(), top.height());
    const auto& t = top.data();
    const auto& b = bottom.data();
    auto& o = out.data();
    for (std::size_t i = 0; i < o.size(); i += 4) {
        const float keep = 1.0f - t[i + 3];
        for (std::size_t c = 0; c < 4; ++c) o[i + c] = t[i + c] + b[i + c] * keep;
    }
    return out;
}

Picture faded(const Picture& picture, double level) {
    Picture out = picture;
    const float f = static_cast<float>(std::clamp(level, 0.0, 1.0));
    for (float& v : out.data()) v *= f;
    return out;
}

Picture filmMix(const Picture& a, const Picture& b, double t) {
    Picture out(a.width(), a.height());
    const float f = static_cast<float>(std::clamp(t, 0.0, 1.0));
    const auto& x = a.data();
    const auto& y = b.data();
    auto& o = out.data();
    for (std::size_t i = 0; i < o.size(); i += 4) {
        const float alpha = x[i + 3] + (y[i + 3] - x[i + 3]) * f;
        for (std::size_t c = 0; c < 3; ++c) {
            const float da = -std::log10(std::max(x[i + c], kDensityFloor));
            const float db = -std::log10(std::max(y[i + c], kDensityFloor));
            const float v = std::pow(10.0f, -(da + (db - da) * f));
            // The floor stands for black: black ends stay black.
            o[i + c] = v <= kDensityFloor * 1.0001f ? 0.0f : std::min(v, alpha);
        }
        o[i + 3] = alpha;
    }
    return out;
}

// ---------------------------------------------------------------- tracks

std::int64_t segmentClipFrame(const model::Segment& segment, std::int64_t offset) {
    return model::segmentSourceFrame(segment, offset);
}

double segmentBlend(const model::Segment& segment, std::int64_t offset) {
    if (!segment.effect || !model::blendsFrames(*segment.effect)) return 0.0;
    const double position = model::segmentSourcePosition(segment, offset);
    return std::clamp(position - static_cast<double>(model::segmentSourceFrame(segment, offset)), 0.0, 1.0);
}

std::optional<TrackFrame> trackFrame(const model::Sequence& sequence, const QString& name, std::int64_t frame) {
    const model::Track* track = sequence.track(name);
    if (!track || frame < 0) return std::nullopt;
    std::int64_t start = 0;
    const int i = track->segmentAt(frame, &start);
    if (i < 0) return std::nullopt;
    const auto& segs = track->segments;
    const model::Segment& seg = segs.at(i);
    TrackFrame out;
    out.segment = &seg;
    out.frame = segmentClipFrame(seg, frame - start);
    out.blend = segmentBlend(seg, frame - start);
    out.offset = frame - start;
    out.segmentOffset = frame - start;
    // A transition sits on the cut where its segment starts (model::Segment::transition), `before()`
    // frames on the outgoing side and `after()` on the incoming.
    auto inTransition = [&](int incoming, std::int64_t cut) {
        const model::Transition& t = *segs.at(incoming).transition;
        const std::int64_t begin = cut - t.before();
        if (t.duration <= 0 || frame < begin || frame >= cut + t.after()) return false;
        const model::Segment& a = segs.at(incoming - 1);
        const model::Segment& b = segs.at(incoming);
        const std::int64_t aStart = cut - a.length;
        out.outgoing = &a;
        out.outgoingFrame = segmentClipFrame(a, frame - aStart);
        out.outgoingBlend = segmentBlend(a, frame - aStart);
        out.outgoingOffset = frame - aStart;
        out.segment = &b;
        out.frame = segmentClipFrame(b, frame - cut);
        out.blend = segmentBlend(b, frame - cut);
        out.segmentOffset = frame - cut;
        out.transition = &t;
        out.offset = frame - begin;
        out.progress = static_cast<double>(frame - begin + 1) / static_cast<double>(t.duration + 1);
        return true;
    };
    if (i > 0 && seg.transition && inTransition(i, start)) return out;
    if (i + 1 < segs.size() && segs.at(i + 1).transition && inTransition(i + 1, start + seg.length)) return out;
    return out;
}

namespace {

// A segment's own picture: transparent for filler and for media that cannot be had.
// `blend` (0 to 1) mixes in the next source frame: a blended Timewarp's in-between frame ("Blends, or
// averages, pixels from the original frames", FX Guide p.186).
// A Timewarp whose Source or Output is not Progressive (FX Guide p.183, p.205): its frame made field by field
// from its source frames; nothing when the formats are both Progressive (the plain path below).
std::optional<Picture> convertedPicture(const Composition& c, const model::Segment& segment, std::int64_t offset, bool* missing) {
    if (!segment.effect || !model::isTimewarp(*segment.effect)) return std::nullopt;
    const model::Timewarp t = model::timewarpOf(*segment.effect);
    const model::TimewarpFormat source = model::sourceFormat(t, c.interlaced);
    const model::TimewarpFormat output = model::outputFormat(t, c.interlaced);
    if (source == model::TimewarpFormat::Progressive && output == model::TimewarpFormat::Progressive) return std::nullopt;
    FieldConversion conv;
    conv.source = source == model::TimewarpFormat::Interlaced ? SourceFormat::Interlaced
                  : source == model::TimewarpFormat::Film     ? SourceFormat::FilmPulldown
                  : source == model::TimewarpFormat::Repeated ? SourceFormat::ProgressiveRepeated
                                                              : SourceFormat::Progressive;
    conv.output = output == model::TimewarpFormat::Interlaced ? OutputFormat::Interlaced
                  : output == model::TimewarpFormat::Film     ? OutputFormat::FilmPulldown
                                                              : OutputFormat::Progressive;
    conv.rendering = fieldRendering(t.type);
    conv.adaptive = t.adaptiveDeinterlace && model::adaptiveAvailable(source, output, t.type);
    conv.topFirst = c.topFieldFirst ? c.topFieldFirst(segment) : true;
    conv.phase = t.pulldownPhase;
    conv.repeat = t.repeatCount;
    bool lost = false;
    const FrameGetter frameAt = [&](std::int64_t frame) -> QImage {
        if (frame < 0) return {};
        const std::optional<QImage> image = c.source(segment, frame, c.size);
        if (!image) lost = true;
        return image.value_or(QImage());
    };
    // The source position at an output time with a fraction: between the positions of the frames around it.
    const std::int64_t length = std::max<std::int64_t>(segment.length, 1);
    const auto positionAt = [&](double time) {
        const auto o = static_cast<std::int64_t>(std::floor(time));
        const double a = model::timewarpPosition(*segment.effect, o, length);
        const double b = model::timewarpPosition(*segment.effect, o + 1, length);
        return static_cast<double>(segment.sourceIn) + a + (b - a) * (time - static_cast<double>(o));
    };
    const QImage image = convertedFrame(frameAt, positionAt, static_cast<double>(offset), conv);
    if (lost && image.isNull()) {
        if (missing) *missing = true;
        return Picture(c.size.width(), c.size.height());
    }
    if (image.isNull()) return Picture(c.size.width(), c.size.height());
    return Picture::fromImage(image, c.size.width(), c.size.height(), c.interpolation);
}

Picture uncorrectedSegmentPicture(const Composition& c, const model::Segment* segment, std::int64_t frame, bool* missing, double blend,
                                  std::int64_t offset) {
    if (segment && segment->hasNest() && c.source) {
        // A nest (FX Guide p.219): its tracks composited at the segment's frame stand in for the clip.
        const model::Sequence nest = model::nestOf(*segment);
        Composition inner = c;
        inner.sequence = &nest;
        inner.tracks.clear();
        for (const model::Track& t : nest.tracks()) {
            if (t.kind == model::TrackKind::Video) inner.tracks << t.name();
        }
        std::sort(inner.tracks.begin(), inner.tracks.end(), [](const QString& a, const QString& b) {
            return model::parseTrackName(a)->number < model::parseTrackName(b)->number;
        });
        return composeFrame(inner, segment->nestIn + std::max<std::int64_t>(offset, 0), missing);
    }
    if (!segment || segment->isFiller() || !c.source) return Picture(c.size.width(), c.size.height());
    if (offset >= 0) {
        if (auto converted = convertedPicture(c, *segment, offset, missing)) return *converted;
    }
    const std::optional<QImage> image = c.source(*segment, frame, c.size);
    if (!image) {
        if (missing) *missing = true;
        return Picture(c.size.width(), c.size.height());
    }
    if (blend > 1e-3) {
        if (const std::optional<QImage> next = c.source(*segment, frame + 1, c.size); next && !next->isNull()) {
            // FluidMotion follows the motion between the two frames (optical_flow.h); the blended
            // types mix them.
            const model::TimewarpType type = model::timewarpOf(*segment->effect).type;
            if (type == model::TimewarpType::FluidMotion || type == model::TimewarpType::FluidMotionDraft) {
                return Picture::fromImage(interpolateFrames(*image, *next, blend, type == model::TimewarpType::FluidMotionDraft),
                                          c.size.width(), c.size.height(), c.interpolation);
            }
            return mix(Picture::fromImage(*image, c.size.width(), c.size.height(), c.interpolation),
                       Picture::fromImage(*next, c.size.width(), c.size.height(), c.interpolation), blend);
        }
    }
    return Picture::fromImage(*image, c.size.width(), c.size.height(), c.interpolation);
}

// A segment's picture with its Color Correction mode correction: "Media Composer processes relationship color
// corrections before all other effects, with the exception of Timewarp effects" (FX Guide p.597).
Picture segmentPicture(const Composition& c, const model::Segment* segment, std::int64_t frame, bool* missing, double blend = 0.0,
                       std::int64_t offset = -1) {
    Picture p = uncorrectedSegmentPicture(c, segment, frame, missing, blend, offset);
    if (c.corrections && segment && segment->hasCorrection()) p = colorCorrectedPicture(model::segmentCorrection(*segment), p);
    return p;
}

// The transitions of the dissolve family (FX Guide p.457, p.462) and the 2D transitions (wipes.cpp); any
// other is a cut at the cut point until its step comes.
Picture transitionPicture(const model::Transition& t, Picture outgoing, Picture incoming, double time, int width, int height) {
    const model::Effect& e = t.effect;
    const QString foreground = QStringLiteral("Foreground");
    // "Swaps the image source for the foreground and background layers of the effect" (p.495).
    if (parameterFlag(e, foreground, QStringLiteral("Swap Sources"))) std::swap(outgoing, incoming);
    // The Foreground Level keyframed, when it is, says how far through the blend the effect is; else
    // straight from 0 to 100 over its length (ch.4). Ours: a transition's keyframes are placed in its
    // progress, so Level keyed 0 and 100 at its ends is the same as no keyframes.
    double progress = time;
    if (!model::keyframesOf(e, foreground, QStringLiteral("Level")).keys.isEmpty()) {
        progress = std::clamp(model::numberAt(e, foreground, QStringLiteral("Level"), time, model::EffectSpan(t.duration, true)).value_or(time * 100.0) / 100.0, 0.0, 1.0);
    }
    QColor colour(Qt::black);
    if (const auto rgb = model::colorAt(e, QStringLiteral("Background Color"), QStringLiteral("Background Color"), time, model::EffectSpan(t.duration, true))) {
        colour = QColor::fromRgbF(static_cast<float>(rgb->red), static_cast<float>(rgb->green), static_cast<float>(rgb->blue));
    }
    if (is3DEffect(e)) {
        // "When you use the 3D Warp effect as a transition ... Media Composer applies the effect to the incoming
        // video. To apply the effect to the outgoing video, click the Swap Sources button" (p.294).
        return warp3dPicture(e, incoming, outgoing, time, model::EffectSpan(t.duration, true));
    }
    if (model::isAniMatte(e)) {
        // An AniMatte transition: "For single-track matte wipes, displays the incoming segment within the matte
        // selection" (Key In; Key Out the outgoing, p.548), the mattes moving by their keyframes. Swap Sources was
        // applied above.
        model::Effect unswapped = e;
        QJsonObject fg = unswapped.parameters.value(foreground).toObject();
        fg.remove(QStringLiteral("Swap Sources"));
        unswapped.parameters.insert(foreground, fg);
        return intraframePicture(unswapped, incoming, outgoing, time, model::EffectSpan(t.duration, true));
    }
    if (e.category == QLatin1String("Blend") && e.name == QLatin1String("Picture-in-Picture")) {
        // "On a transition, the incoming video appears inside the outgoing video" (p.458).
        return over(imageEffectPicture(e, incoming, time, model::EffectSpan(t.duration, true)), outgoing);
    }
    if (e.category == QLatin1String("Key") && isImageEffect(e)) {
        // Chroma Key on a transition (p.473). Ours: the incoming video keyed over the outgoing.
        return over(imageEffectPicture(e, incoming, time, model::EffectSpan(t.duration, true)), outgoing);
    }
    if (isIllusionTransition(e)) {
        return illusionTransitionPicture(e, std::move(outgoing), std::move(incoming), progress, time, model::EffectSpan(t.duration, true));
    }
    // PlasmaWipe: the gradient's threshold at the transition's progress (FX Guide p.442).
    if (isPlasmaWipe(e)) return plasmaWipePicture(e, outgoing, incoming, progress, time, model::EffectSpan(t.duration, true));
    if (isWipe(e)) return wipePicture(e, std::move(outgoing), std::move(incoming), progress, time, model::EffectSpan(t.duration, true));
    const QString& name = e.name;
    if (name == QLatin1String("Dissolve")) return mix(outgoing, incoming, progress);
    if (name == QLatin1String("Fade to Color")) return mix(outgoing, Picture::solid(width, height, colour), progress);
    if (name == QLatin1String("Fade from Color")) return mix(Picture::solid(width, height, colour), incoming, progress);
    if (name == QLatin1String("Dip to Color")) {
        // "fades from the outgoing video to any color and then fades up to the incoming video".
        const Picture solid = Picture::solid(width, height, colour);
        return progress < 0.5 ? mix(outgoing, solid, progress * 2.0) : mix(solid, incoming, progress * 2.0 - 1.0);
    }
    if (name == QLatin1String("Film Dissolve") || name == QLatin1String("Film Fade")) return filmMix(outgoing, incoming, progress);
    return {};
}

// Matte Key: "A three-layer track effect. The bottom layer is the background image, the middle layer is
// the foreground image, and the top layer contains the grayscale matte or alpha channel" (FX Guide
// p.474). `withForeground` is the foreground over the background, `background` what lay under the
// foreground. Ours: the matte's brightness is the foreground's opacity; the Foreground's Invert Key,
// Level and Show Alpha (p.495) apply.
Picture matteKeyPicture(const model::Effect& e, const Picture& background, const Picture& withForeground, const Picture& matte,
                        double time, const model::EffectSpan& span) {
    const QString fg = QStringLiteral("Foreground");
    const bool invert = parameterFlag(e, fg, QStringLiteral("Invert Key"));
    const bool showAlpha = parameterFlag(e, fg, QStringLiteral("Show Alpha"));
    const float level = static_cast<float>(std::clamp(model::numberAt(e, fg, QStringLiteral("Level"), time, span).value_or(100.0), 0.0, 100.0) / 100.0);
    Picture out(withForeground.width(), withForeground.height());
    for (std::size_t i = 0; i < out.data().size(); i += 4) {
        const float* m = &matte.data()[i];
        float a = std::clamp(0.2126f * m[0] + 0.7152f * m[1] + 0.0722f * m[2], 0.0f, 1.0f);
        if (invert) a = 1.0f - a;
        a *= level;
        float* o = &out.data()[i];
        if (showAlpha) {
            o[0] = o[1] = o[2] = a;
            o[3] = 1.0f;
            continue;
        }
        for (std::size_t c = 0; c < 4; ++c) o[c] = background.data()[i + c] * (1.0f - a) + withForeground.data()[i + c] * a;
    }
    return out;
}

// A classic Title, Rolling Title or Crawling Title effect (ROADMAP 6.8d4a; FX Guide p.365, p.488-489;
// lumenis/model/title_clip.h): "Media Composer keys the title over the underlying video layers" (p.488). Its fill
// precompute keyed by its matte precompute over `below`, the pages placed where the segment's frame `offset` scrolls them
// (classicTitleScroll); the Foreground parameters apply as Matte Key's. A fast-saved title, or precomputes offline, show
// black and count as missing ("Fast-saved titles appear as black in the monitors and are labeled as "media offline"",
// p.358).
Picture classicTitleLayer(const Composition& c, const model::Segment& segment, std::int64_t offset, const Picture& below, bool* missing) {
    const int w = c.size.width(), h = c.size.height();
    const auto title = model::classicTitleOf(*segment.effect);
    const model::TitleMedia media = model::titleMediaOf(segment);
    auto offline = [&] {
        if (missing) *missing = true;
        return Picture::solid(w, h, Qt::black);
    };
    if (!title || media.isEmpty() || !c.source) return offline();
    // "If you see an image in a monitor that says Wrong Format, the resolution of the Title effect is not compatible
    // with the resolution of the video clips in the sequence" (p.376). Ours: title media made at a raster other than the
    // project's.
    if (c.raster.isValid() && media.raster.isValid() && media.raster != c.raster) {
        QImage wrong(c.size, QImage::Format_RGB32);
        wrong.fill(Qt::black);
        QPainter p(&wrong);
        p.setPen(Qt::white);
        QFont font = p.font();
        font.setPixelSize(std::max(8, h / 12));
        p.setFont(font);
        p.drawText(QRect(0, 0, w, h), Qt::AlignCenter, QStringLiteral("Wrong Format"));
        p.end();
        return Picture::fromImage(wrong, w, h, c.interpolation);
    }
    const std::int64_t length = std::max<std::int64_t>(segment.length, 1);
    // The whole roll from where the segment starts in it, fitted to the segment (p.370).
    const double from = model::titleRollStart(*segment.effect);
    const double progress = from + (1.0 - from) * (length > 1 ? static_cast<double>(offset) / static_cast<double>(length - 1) : 0.0);
    const QPointF scroll = classicTitleScroll(*title, progress);
    const int pages = title->motion == model::ClassicMotion::Still ? 1 : std::max(1, title->pages);
    // The pages in view, drawn where the scroll puts them.
    QImage fill(c.size, QImage::Format_RGB32), matte(c.size, QImage::Format_RGB32);
    fill.fill(Qt::black);
    matte.fill(Qt::black);
    const QTransform m = QTransform::fromTranslate(-scroll.x(), -scroll.y()) * classicToPicture(c.size);
    QPainter pf(&fill), pm(&matte);
    pf.setRenderHint(QPainter::SmoothPixmapTransform);
    pm.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int page = 1; page <= pages; ++page) {
        const QPointF origin = model::classicPageOrigin(title->motion, page);
        const QRectF where = m.mapRect(QRectF(origin, QSizeF(model::kClassicWidth, model::kClassicHeight)));
        if (!where.intersects(QRectF(0, 0, w, h))) continue;
        auto pageOf = [&](const QString& clip) {
            model::Segment s;
            s.clipId = clip;
            s.binId = media.binId;
            s.sourceTrack = QStringLiteral("V1");
            s.length = pages;
            return c.source(s, page - 1, c.size);
        };
        const std::optional<QImage> f = pageOf(media.fill), a = pageOf(media.matte);
        if (!f || !a) {
            pf.end();
            pm.end();
            return offline();
        }
        if (!f->isNull()) pf.drawImage(where, *f);
        if (!a->isNull()) pm.drawImage(where, *a);
    }
    pf.end();
    pm.end();
    Picture fillPicture = Picture::fromImage(fill, w, h, c.interpolation);
    Picture mattePicture = Picture::fromImage(matte, w, h, c.interpolation);
    // "You can replace the fill track of Title effects with video or graphics. The moving video or graphic effect then
    // appears inside the title itself" (p.373): material edited onto the nest's V1 other than the fill precompute is
    // the fill.
    const model::Sequence nest = model::nestOf(segment);
    if (const model::Track* v1 = nest.track(QStringLiteral("V1"))) {
        std::int64_t start = 0;
        const int i = v1->segmentAt(segment.nestIn + offset, &start);
        if (i >= 0 && v1->segments.at(i).clipId != media.fill) {
            Composition inner = c;
            inner.sequence = &nest;
            inner.tracks = {QStringLiteral("V1")};
            fillPicture = composeFrame(inner, segment.nestIn + offset, missing);
        }
    }
    const double time = length > 1 ? static_cast<double>(offset) / static_cast<double>(length - 1) : 0.0;
    const model::EffectSpan span(length);
    // The Title effect's Scaling, Position and Crop (p.489; p.494, p.498, p.499) move the fill and the matte alike, as
    // Resize moves a picture (black where they leave none, so the matte keys nothing there).
    const QJsonObject& p = segment.effect->parameters;
    if (p.contains(QStringLiteral("Scaling")) || p.contains(QStringLiteral("Position")) || p.contains(QStringLiteral("Crop"))) {
        model::Effect placed{QStringLiteral("Image"), QStringLiteral("Resize"), {}};
        for (const char* cat : {"Scaling", "Position", "Crop", model::kKeyframesKey}) {
            if (p.contains(QLatin1String(cat))) placed.parameters.insert(QLatin1String(cat), p.value(QLatin1String(cat)));
        }
        fillPicture = imageEffectPicture(placed, fillPicture, time, span);
        mattePicture = imageEffectPicture(placed, mattePicture, time, span);
    }
    if (is3DEffect(*segment.effect)) {
        // A title promoted to 3D (FX Guide p.371): the keyed title, transparent around it, placed by the 3D Warp over
        // the tracks below.
        const Picture keyed = matteKeyPicture(*segment.effect, Picture(w, h), fillPicture, mattePicture, time, span);
        return warp3dPicture(*segment.effect, keyed, below, time, span);
    }
    return matteKeyPicture(*segment.effect, below, fillPicture, mattePicture, time, span);
}

// Stamp (FX Guide p.298-299, p.584-585): "The Stamp category of the 3D Warp effect lets you make certain
// effects ... remain on screen while other effects come and go, even though the effect segment itself has
// ended." The track's stamp buffer at `frame`: every Stamp Image press on the track's 3D effects at or before
// the frame, after its last Clear, each the effect's picture as it was at that moment at the Stamp's Opac.;
// those whose effect has Foregnd. On Top go `under` the track's own picture ("If Foregnd. On Top is not
// enabled, the Stamp buffer is always in the foreground"), the rest `over` it.
struct Stamps {
    Picture under;
    Picture over;
};

Stamps stampsOf(const Composition& c, const QString& name, std::int64_t frame) {
    Stamps out;
    const model::Track* track = c.sequence->track(name);
    if (!track) return out;
    struct Press {
        std::int64_t at;
        const model::Segment* segment;
        std::int64_t offset;
        double time;
    };
    QList<Press> presses;
    std::int64_t cleared = -1;
    std::int64_t pos = 0;
    for (const model::Segment& g : track->segments) {
        if (pos > frame) break;
        if (g.effect && g.length > 0) {
            const model::EffectSpan span(g.length);
            auto offsetOf = [&](double t) {
                return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::llround(span.frameOf(t))), 0, g.length - 1);
            };
            for (const double t : clearTimes(*g.effect)) {
                const std::int64_t at = pos + offsetOf(t);
                if (at <= frame) cleared = std::max(cleared, at);
            }
            for (const double t : stampTimes(*g.effect)) {
                const std::int64_t o = offsetOf(t);
                if (pos + o <= frame) presses.push_back(Press{pos + o, &g, o, t});
            }
        }
        pos += g.length;
    }
    for (const Press& p : presses) {
        if (p.at <= cleared) continue; // "Clear: Lets you clear the stamp buffer of everything in it" (p.585)
        const model::Effect& e = *p.segment->effect;
        Picture picture = segmentPicture(c, p.segment, segmentClipFrame(*p.segment, p.offset), nullptr,
                                         segmentBlend(*p.segment, p.offset), p.offset);
        const double opacity = std::clamp(model::numberAt(e, QStringLiteral("Stamp"), QStringLiteral("Opac."), p.time,
                                                          model::EffectSpan(p.segment->length)).value_or(100.0), 0.0, 100.0) / 100.0;
        Picture layer = faded(warp3dLayer(e, picture, p.time, model::EffectSpan(p.segment->length)), opacity);
        Picture& into = parameterFlag(e, QStringLiteral("Stamp"), QStringLiteral("Foregnd. On Top")) ? out.under : out.over;
        into = into.isNull() ? std::move(layer) : over(layer, into);
    }
    return out;
}

} // namespace

Picture composeFrame(const Composition& c, std::int64_t frame, bool* missing) {
    const int w = c.size.width();
    const int h = c.size.height();
    Picture result(w, h);
    if (!c.sequence || w <= 0 || h <= 0) return result;
    // What lay below the track under the one being added, for Matte Key's three layers.
    Picture beneath(w, h);
    Picture lastStart(w, h);
    // The topmost track with a render in date here holds everything up to it ("Media Composer renders
    // the composite result of all tracks into the top track", FX Guide p.57): start from its picture.
    qsizetype first = 0;
    if (c.rendered) {
        for (qsizetype i = c.tracks.size() - 1; i >= 0; --i) {
            const auto target = model::renderTargetAt(*c.sequence, c.tracks.at(i), frame);
            if (!target || !model::isRenderedAt(*c.sequence, *target, frame - target->start)) continue;
            const std::optional<QImage> image = c.rendered(*model::renderOf(*c.sequence, *target), frame - target->start, c.size);
            if (!image) continue; // offline: the effect is computed
            result = Picture::fromImage(*image, w, h, c.interpolation);
            result.quantize(c.precision);
            lastStart = result;
            first = i + 1;
            break;
        }
    }
    // A track's stamps over its own picture go on before the next track (or at the end).
    Picture pendingStamps;
    for (qsizetype i = first; i < c.tracks.size(); ++i) {
        const QString& name = c.tracks.at(i);
        if (!pendingStamps.isNull()) {
            result = over(pendingStamps, result);
            pendingStamps = {};
        }
        if (c.effects) {
            Stamps stamps = stampsOf(c, name, frame);
            if (!stamps.under.isNull()) result = over(stamps.under, result);
            pendingStamps = std::move(stamps.over);
        }
        beneath = std::move(lastStart);
        lastStart = result;
        const auto tf = trackFrame(*c.sequence, name, frame);
        if (!tf) continue;
        Picture layer;
        if (tf->transition) {
            Picture out = segmentPicture(c, tf->outgoing, tf->outgoingFrame, missing, tf->outgoingBlend, tf->outgoingOffset);
            Picture in = segmentPicture(c, tf->segment, tf->frame, missing, tf->blend, tf->segmentOffset);
            // Without effects (Render On-the-Fly off) a transition is its cut.
            if (c.effects && isFluid(tf->transition->effect, "FluidMorph")) {
                // FluidMorph (FX Guide p.467, p.511-512): "Still" takes the outgoing clip's last frame of the
                // transition, or the incoming clip's first, for every frame; "Stream" each frame as it comes.
                const model::Effect& e = tf->transition->effect;
                const QString cat = QStringLiteral("FluidMorph Effect Source and Input Parameters");
                const QString source = e.parameters.value(cat).toObject().value(QStringLiteral("Source")).toString(QStringLiteral("Stream->Stream"));
                const std::int64_t last = tf->transition->duration - 1 - tf->offset; // frames to the transition's end
                Picture a = out;
                Picture b = in;
                if (source.startsWith(QLatin1String("Still"))) {
                    const std::int64_t o = tf->outgoingOffset + last;
                    a = segmentPicture(c, tf->outgoing, segmentClipFrame(*tf->outgoing, o), missing, segmentBlend(*tf->outgoing, o), o);
                }
                if (source.endsWith(QLatin1String("Still"))) {
                    const std::int64_t o = tf->segmentOffset - tf->offset;
                    b = segmentPicture(c, tf->segment, segmentClipFrame(*tf->segment, o), missing, segmentBlend(*tf->segment, o), o);
                }
                if (parameterFlag(e, cat, QStringLiteral("Swap Sources"))) std::swap(a, b);
                const bool feature = e.parameters.value(cat).toObject().value(QStringLiteral("Feature Match")).toBool(true);
                layer = Picture::fromImage(fluidMorph(a.toImage(), b.toImage(), tf->progress, feature), w, h, c.interpolation);
            } else if (c.effects) {
                layer = transitionPicture(*tf->transition, out, in, tf->progress, w, h);
            }
            if (layer.isNull()) {
                // Not built yet: the cut, which lies after the outgoing side's frames.
                const double cutAt = static_cast<double>(tf->transition->before() + 1) / static_cast<double>(tf->transition->duration + 1);
                layer = tf->progress < cutAt ? std::move(out) : std::move(in);
            }
            layer.quantize(c.precision);
        } else if (tf->segment && tf->segment->effect && model::isClassicTitleEffect(*tf->segment->effect)) {
            // A Title Effect clip's segment is media, keyed whether or not effects are computed.
            result = classicTitleLayer(c, *tf->segment, tf->offset, result, missing);
            result.quantize(c.precision);
            continue;
        } else {
            layer = segmentPicture(c, tf->segment, tf->frame, missing, tf->blend, tf->segmentOffset);
        }
        if (c.effects && tf->segment && tf->segment->effect && !tf->transition) {
            // Where in the segment the frame is, 0 at its first frame and 1 at its last.
            const std::int64_t length = tf->segment->length;
            const double time = length > 1 ? static_cast<double>(tf->offset) / static_cast<double>(length - 1) : 0.0;
            // The effect with its tracking data's Position, Scaling and Rotation folded in (FX Guide ch.10).
            const model::Effect tracked =
                trackedEffect(*tf->segment->effect, time, length, w > 0 && h > 0 ? static_cast<double>(w) / h : 16.0 / 9.0);
            const model::Effect& e = tracked;
            const QString foreground = QStringLiteral("Foreground");
            const QString level = QStringLiteral("Level");
            const bool keyed = !model::keyframesOf(e, foreground, level).keys.isEmpty();
            if (model::isTitlerEffect(e)) {
                // Avid Titler+ (p.302-324): the title over the segment, filler's empty picture included.
                layer = titlePicture(e, layer, time, model::EffectSpan(std::max<std::int64_t>(length, 1)));
                layer.quantize(c.precision);
                result = over(layer, result);
                result.quantize(c.precision);
                continue;
            }
            if (isBurnIn(e)) {
                // Timecode Burn-In's displays over the segment's own picture (burn_in.cpp).
                layer = burnInPicture(e, layer, c, name, frame);
                layer.quantize(c.precision);
                result = over(layer, result);
                result.quantize(c.precision);
                continue;
            }
            if (e.category == QLatin1String("Key") && e.name == QLatin1String("Matte Key")) {
                result = matteKeyPicture(e, beneath, result, layer, time, length);
                result.quantize(c.precision);
                continue;
            }
            if (isRegionStabilize(e)) {
                // Region Stabilize (p.279-281) tracks its own segment's pictures; the Effect Preview shows the original
                // picture under the region of interest's wire frame (ours, as Pan and Scan's).
                if (!c.effectPreview) {
                    const model::Segment& seg = *tf->segment;
                    const int trackWidth = std::min(w, 480); // as Start Tracking's frames (MainWindow)
                    auto pictureAt = [&](std::int64_t o) -> QImage {
                        if (o < 0 || o >= seg.length) return {};
                        const QImage image = segmentPicture(c, &seg, segmentClipFrame(seg, o), nullptr, segmentBlend(seg, o), o).toImage();
                        return image.width() > trackWidth ? image.scaledToWidth(trackWidth, Qt::SmoothTransformation) : image;
                    };
                    const QString material = QStringLiteral("%1:%2:%3x%4").arg(seg.clipId).arg(seg.sourceIn).arg(w).arg(h);
                    layer = regionStabilizePicture(e, layer, tf->offset, seg.length, material, pictureAt);
                }
                layer.quantize(c.precision);
                result = over(layer, result);
                result.quantize(c.precision);
                continue;
            }
            if (isFluid(e, "FluidBlur") || isFluid(e, "FluidColorMap")) {
                // FluidBlur and FluidColorMap (FX Guide p.467, p.510-511): the motion between the segment's
                // frames, this one and the next (at its last frame, the one before).
                const model::Segment& seg = *tf->segment;
                auto pictureAt = [&](std::int64_t o) -> QImage {
                    if (o < 0 || o >= seg.length) return {};
                    return segmentPicture(c, &seg, segmentClipFrame(seg, o), nullptr, segmentBlend(seg, o), o).toImage();
                };
                const QImage current = layer.toImage();
                const QImage before = pictureAt(tf->offset - 1);
                QImage after = pictureAt(tf->offset + 1);
                if (after.isNull()) after = before;
                QImage made;
                if (e.name == QLatin1String("FluidBlur")) {
                    const QString cat = QStringLiteral("FluidBlur Effect Input Parameters");
                    FluidBlurOptions o;
                    o.amount = fluidNumber(e, cat, QStringLiteral("Blur Amount"), time, length);
                    o.lower = fluidNumber(e, cat, QStringLiteral("Lower Motion Threshold"), time, length);
                    o.upper = fluidNumber(e, cat, QStringLiteral("Upper Motion Threshold"), time, length);
                    o.trail = fluidNumber(e, cat, QStringLiteral("Trail"), time, length);
                    o.sharpenLeadingEdge = parameterFlag(e, cat, QStringLiteral("Sharpen Leading Edge"));
                    made = fluidBlur(before, current, after, o);
                } else {
                    const QString cat = QStringLiteral("FluidColorMap Effect Input Parameters");
                    FluidColorMapOptions o;
                    o.minMotion = fluidNumber(e, cat, QStringLiteral("Min Motion Threshold"), time, length);
                    o.maxMotion = fluidNumber(e, cat, QStringLiteral("Max Motion Threshold"), time, length);
                    o.constantSaturation = fluidNumber(e, cat, QStringLiteral("Constant Saturation"), time, length);
                    made = fluidColorMap(current, after, o);
                }
                if (!made.isNull()) layer = Picture::fromImage(made, w, h, c.interpolation);
                layer.quantize(c.precision);
                result = over(layer, result);
                result.quantize(c.precision);
                continue;
            }
            if (is3DEffect(e)) {
                // A 3D effect places the segment in 3D over the tracks below (FX Guide ch.11; warp3d.cpp).
                result = warp3dPicture(e, layer, result, time, length);
                result.quantize(c.precision);
                continue;
            }
            if (isPanAndZoom(e)) {
                // Avid Pan & Zoom: the image over the placeholder segment (FX Guide p.239-241).
                layer = panZoomPicture(e, layer, time, length, c.effectPreview);
                layer.quantize(c.precision);
                result = over(layer, result);
                result.quantize(c.precision);
                continue;
            }
            if (model::isColorCorrectionEffect(e)) {
                // The Color Correction effect (FX Guide p.693): its correction on the segment over the tracks below.
                layer = colorCorrectedPicture(model::colorCorrectionOf(e), layer);
                layer.quantize(c.precision);
                result = over(layer, result);
                result.quantize(c.precision);
                continue;
            }
            if (isPlasmaWipe(e)) {
                // A PlasmaWipe on a segment, "static wipes" of the segment over the tracks below (p.443).
                result = plasmaWipeSegmentPicture(e, layer, result, time, length);
                result.quantize(c.precision);
                continue;
            }
            if (model::isIntraframeEffect(e)) {
                // The Intraframe effects' objects on the segment over the tracks below (FX Guide ch.15; intraframe.cpp);
                // Scratch Removal takes material from the segment's other frames, never outside it (p.428).
                const model::Segment& seg = *tf->segment;
                IntraframeFrames frames;
                frames.offset = tf->offset;
                frames.length = seg.length;
                frames.pictureAt = [&](std::int64_t o) -> Picture {
                    if (o < 0 || o >= seg.length) return Picture();
                    return segmentPicture(c, &seg, segmentClipFrame(seg, o), nullptr, segmentBlend(seg, o), o);
                };
                frames.interlaced = c.interlaced;
                frames.topFieldFirst = c.topFieldFirst ? c.topFieldFirst(seg) : true;
                result = intraframePicture(e, layer, result, time, length, &frames);
                result.quantize(c.precision);
                continue;
            }
            if (isImageEffect(e)) {
                // Stabilize's Tracking category moves the picture back onto its reference frame's (p.276-279).
                if (model::isStabilizeEffect(e)) {
                    if (const auto map = trackedTransform(e, time, length, w > 0 && h > 0 ? static_cast<double>(w) / h : 16.0 / 9.0)) {
                        layer = transformed(layer, *map);
                    }
                }
                layer = imageEffectPicture(e, layer, time, length);
                layer.quantize(c.precision);
                result = over(layer, result);
                result.quantize(c.precision);
                continue;
            }
            if (isPanAndScan(e)) {
                // Pan and Scan reformats its segment and the tracks below, as the other Reformat effects do
                // (p.229-231), unless the Effect Preview shows the original.
                result = over(layer, result);
                if (!c.effectPreview) result = panScanPicture(e, result, time, length);
                result.quantize(c.precision);
                continue;
            }
            if (isReformat(e)) {
                // "A Reformat effect works on all tracks below the track to which you apply it" (p.229),
                // and on its own segment.
                result = reformatPicture(e, over(layer, result), time, length);
                result.quantize(c.precision);
                continue;
            }
            if (isWipe(e)) {
                // A 2D transition on a segment ("multilayer segments", p.459) wipes from the tracks below
                // to the segment. Ours: over the segment's length unless its Level is keyframed, as a
                // transition's is.
                const double progress = keyed ? model::numberAt(e, foreground, level, time, length).value_or(100.0) / 100.0 : time;
                Picture below = std::move(result);
                if (parameterFlag(e, foreground, QStringLiteral("Swap Sources"))) std::swap(below, layer);
                result = wipePicture(e, std::move(below), std::move(layer), progress, time, length);
                result.quantize(c.precision);
                continue;
            }
            if (e.name == QLatin1String("Superimpose") || keyed) {
                // "The Foreground Level slider controls the blend percentage ... When Level=100, only the
                // top image is visible, and when Level=0, only the bottom image is visible" (p.458); the Fade
                // Effect button keyframes it (p.46-47). Ours: Superimpose is 50 until a level is set.
                const double fallback = e.name == QLatin1String("Superimpose") ? 50.0 : 100.0;
                layer = faded(layer, model::numberAt(e, foreground, level, time, length).value_or(fallback) / 100.0);
                layer.quantize(c.precision);
            }
        }
        result = over(layer, result);
        result.quantize(c.precision);
    }
    if (!pendingStamps.isNull()) result = over(pendingStamps, result);
    return result;
}

QImage composeImage(const Composition& c, std::int64_t frame, bool* missing) {
    if (!c.sequence) return {};
    // Stamps outlive their segments (p.298): with any on the tracks, every frame is composed.
    if (c.effects) {
        for (const QString& name : c.tracks) {
            const model::Track* t = c.sequence->track(name);
            if (!t) continue;
            for (const model::Segment& g : t->segments) {
                if (g.effect && !stampTimes(*g.effect).isEmpty()) return composeFrame(c, frame, missing).toImage();
            }
        }
    }
    // From the top down: the first track with a picture hides those below when it is a plain segment.
    for (auto it = c.tracks.crbegin(); it != c.tracks.crend(); ++it) {
        const auto tf = trackFrame(*c.sequence, *it, frame);
        if (!tf || (!tf->transition && tf->segment->isFiller() && !tf->segment->effect)) continue;
        if (tf->transition || tf->segment->effect || tf->segment->isFiller() || !c.source) break;
        if (c.corrections && tf->segment->hasCorrection()) break; // its correction (p.597) is made by composeFrame
        const std::optional<QImage> image = c.source(*tf->segment, tf->frame, c.size);
        if (!image) {
            if (missing) *missing = true;
            return {};
        }
        return *image;
    }
    return composeFrame(c, frame, missing).toImage();
}

} // namespace lumenis::media
