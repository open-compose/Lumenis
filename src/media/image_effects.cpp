#include "effect_drawing.h"

#include <algorithm>
#include <cmath>

// The segment effects that change a segment's own picture by geometry (ROADMAP 6.3b1;
// docs/spec/image-effects.md): Picture-in-Picture (FX Guide p.458), Flip, Flop and Flip-Flop (p.469),
// Field Swap (p.469), Resize and Mask (p.470), the Film masks and Blowup (p.461-462, p.500), and the
// Reformat letterboxes and sidebar (p.229-230, p.479-480). Each maps a pixel of the result back into the
// segment's picture; where the guide gives no numbers, the mapping is ours and is said so.
namespace lumenis::media {

using detail::boxDistance;
using detail::sample;

namespace {

const QString kScaling = QStringLiteral("Scaling");
const QString kPosition = QStringLiteral("Position");
const QString kCrop = QStringLiteral("Crop");

double number(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    const QJsonValue fallback = parameterDefault(e, category, name);
    return model::numberAt(e, category, name, time, span).value_or(fallback.toDouble(0.0));
}

// The Background Color (p.492), black until set.
QColor backgroundOf(const model::Effect& e, double time, const model::EffectSpan& span) {
    const QString bg = QStringLiteral("Background Color");
    if (const auto rgb = model::colorAt(e, bg, bg, time, span)) {
        return QColor::fromRgbF(static_cast<float>(rgb->red), static_cast<float>(rgb->green), static_cast<float>(rgb->blue));
    }
    return QColor(Qt::black);
}

// Crop (p.494) as the part of the picture kept: T "0 (top of the screen) to 999 (bottom of the screen)",
// B "–999 (top of the screen) to 0 (bottom of the screen)", and L and R the same across.
struct Rect {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

Rect cropOf(const model::Effect& e, double time, const model::EffectSpan& span, double W, double H) {
    Rect r;
    r.y0 = std::clamp(number(e, kCrop, QStringLiteral("T"), time, span), 0.0, 999.0) / 999.0 * H;
    r.y1 = H + std::clamp(number(e, kCrop, QStringLiteral("B"), time, span), -999.0, 0.0) / 999.0 * H;
    r.x0 = std::clamp(number(e, kCrop, QStringLiteral("L"), time, span), 0.0, 999.0) / 999.0 * W;
    r.x1 = W + std::clamp(number(e, kCrop, QStringLiteral("R"), time, span), -999.0, 0.0) / 999.0 * W;
    return r;
}

// Scaling (p.499), "100 indicates 100% scaling (normal size)", and Position (p.498), negative X to the
// left and negative Y to the top. Ours: Position in pixels of a picture 1920 wide.
struct Placement {
    double sx = 1.0, sy = 1.0, dx = 0.0, dy = 0.0;
};

Placement placementOf(const model::Effect& e, double time, const model::EffectSpan& span, double W) {
    Placement p;
    p.sx = std::max(number(e, kScaling, QStringLiteral("X"), time, span), 0.0) / 100.0;
    p.sy = std::max(number(e, kScaling, QStringLiteral("Y"), time, span), 0.0) / 100.0;
    p.dx = number(e, kPosition, QStringLiteral("X"), time, span) * W / 1920.0;
    p.dy = number(e, kPosition, QStringLiteral("Y"), time, span) * W / 1920.0;
    return p;
}

bool is(const model::Effect& e, const char* category, const char* name) {
    return e.category == QLatin1String(category) && e.name == QLatin1String(name);
}

// A film mask's width over height (p.461), or nothing for another effect.
std::optional<double> filmMaskRatio(const model::Effect& e) {
    if (e.category != QLatin1String("Film")) return std::nullopt;
    if (e.name == QLatin1String("1.66 Mask")) return 1.66;
    if (e.name == QLatin1String("1.85 Mask")) return 1.85;
    if (e.name == QLatin1String("16:9 Mask")) return 16.0 / 9.0;
    if (e.name == QLatin1String("Anamorphic Mask")) return 2.35;
    return std::nullopt;
}

} // namespace

QJsonValue parameterDefault(const model::Effect& e, const QString& category, const QString& name) {
    if (e.category == QLatin1String("Illusion FX")) return detail::illusionDefault(e, category, name);
    // Avid Pan & Zoom until set (docs/spec/pan-zoom.md): no magnification (p.242), the first entry of each menu.
    if (isPanAndZoom(e) && category == QLatin1String("Avid Pan & Zoom Effect Parameters")) {
        if (name == QLatin1String("Zoom Factor")) return 1.0;
        if (name == QLatin1String("Display")) return QStringLiteral("Source");
        if (name == QLatin1String("Filtering")) return QStringLiteral("Real-Time");
        if (name == QLatin1String("Background")) return QStringLiteral("Color");
        if (name == QLatin1String("Path") || name == QLatin1String("Velocity In") || name == QLatin1String("Velocity Out")) return QStringLiteral("Linear");
        if (name == QLatin1String("Source Levels")) return QStringLiteral("RGB");
    }
    // Pan and Scan's ratios until chosen (ours, docs/spec/pan-scan.md): a 16:9 source made 4:3.
    if (isPanAndScan(e) && category == QLatin1String("Pan and Scan Aspect Ratios and Actions Parameters")) {
        if (name == QLatin1String("Source")) return QStringLiteral("1.78 (16:9)");
        if (name == QLatin1String("Target")) return QStringLiteral("1.33 (4:3)");
    }
    // The 3D effects (p.569-585): normal size, the whole picture, opaque; Defocus 10 (p.573), Border width 50
    // (p.571), Shadow X 50, Y -50 and Opac. 50 (p.579), the highlight's 50s (p.576). Corner Pin's corners (ours,
    // ±999 the screen's edges) where the picture's are.
    if (is3DEffect(e)) {
        if (category == QLatin1String("Scaling (3D)") && (name == QLatin1String("X") || name == QLatin1String("Y"))) return 100;
        if (category == QLatin1String("Crop (3D)")) {
            if (name == QLatin1String("T") || name == QLatin1String("R")) return 999;
            if (name == QLatin1String("B") || name == QLatin1String("L")) return -999;
        }
        if (category == QLatin1String("Foreground (3D)") && name == QLatin1String("Opac.")) return 100;
        if (category == QLatin1String("Target") && name == QLatin1String("Size")) return 100;
        if (category == QLatin1String("Defocus") && (name == QLatin1String("Horz") || name == QLatin1String("V"))) return 10;
        if (category == QLatin1String("Border (3D)") && name == QLatin1String("Wid")) return 50;
        if (category == QLatin1String("Shadow")) {
            if (name == QLatin1String("X") || name == QLatin1String("Opac.")) return 50;
            if (name == QLatin1String("Y")) return -50;
        }
        if (category == QLatin1String("Highlight") && (name == QLatin1String("Soft") || name == QLatin1String("Inty") || name == QLatin1String("Rad"))) return 50;
        if (category == QLatin1String("Corner Pin")) {
            const bool right = name.startsWith(QLatin1String("Corner 2")) || name.startsWith(QLatin1String("Corner 3"));
            const bool topCorner = name.startsWith(QLatin1String("Corner 1")) || name.startsWith(QLatin1String("Corner 2"));
            if (name.endsWith(QLatin1String(": X"))) return right ? 999 : -999;
            if (name.endsWith(QLatin1String(": Y"))) return topCorner ? 999 : -999;
        }
        // Foreground (3D)'s keys (ours, so a key keys something until set): Luma Key keeps what is brighter
        // than half (Soft 10, Range 20); Chroma Key takes out blue (H Tol. 20, H Soft 10, S Low 20, L Tol. 100).
        if (category == QLatin1String("Foreground (3D)")) {
            if (name == QLatin1String("Thresh")) return 50;
            if (name == QLatin1String("Soft") || name == QLatin1String("H Soft")) return 10;
            if (name == QLatin1String("Range") || name.endsWith(QLatin1String(": Range")) || name == QLatin1String("H Tol.") ||
                name == QLatin1String("S Low")) {
                return 20;
            }
            if (name == QLatin1String("L Tol.")) return 100;
            if (name == QLatin1String("Color")) return QStringLiteral("#0000ff");
        }
    }
    if (category == kScaling && (name == QLatin1String("X") || name == QLatin1String("Y"))) {
        // Ours: a Picture-in-Picture starts at half size, as its illustration (p.458) shows it smaller;
        // everything else at normal size.
        return is(e, "Blend", "Picture-in-Picture") ? 50 : 100;
    }
    if (category == QLatin1String("Color Effect Parameters") &&
        (name == QLatin1String("Red") || name == QLatin1String("Green") || name == QLatin1String("Blue"))) {
        return 100; // Color Gain: "100 leaves the color unchanged" (p.536)
    }
    if (category == QLatin1String("Blur Effect Blur Parameters") || category == QLatin1String("Mosaic Effect Mosaic Parameters")) {
        // Ours: at full strength, 10 pixels of a picture 1920 wide.
        if (name == QLatin1String("Opac.")) return 100;
        if (name == QLatin1String("Hor") || name == QLatin1String("Vert")) return 10;
    }
    // Ours, so a key keys something until it is set: blue screens (black for Luma Key), Gain 20, Soft
    // 10; the RGB Keyer's Level and SpectraMatte's Opacity at 100.
    if (category == QLatin1String("Key Effects Key Parameters")) {
        if (name == QLatin1String("Color controls")) return e.name == QLatin1String("Luma Key") ? QStringLiteral("#000000") : QStringLiteral("#0000ff");
        if (name == QLatin1String("Gain")) return 20;
        if (name == QLatin1String("Soft")) return 10;
    }
    if (category == QLatin1String("RGB Keyer Parameters")) {
        if (name == QLatin1String("Key Color")) return QStringLiteral("#0000ff");
        if (name == QLatin1String("Gain")) return 20;
        if (name == QLatin1String("Soft")) return 10;
        if (name == QLatin1String("Level")) return 100;
    }
    if (category == QLatin1String("SpectraMatte Parameters")) {
        if (name == QLatin1String("Key Color")) return QStringLiteral("#0000ff");
        if (name == QLatin1String("Opacity")) return 100;
    }
    if (category == QLatin1String("14:9 Letterbox Position H Parameter") && name == QLatin1String("Position H")) {
        return 50; // ours: the slider's middle keeps the picture centred
    }
    return {};
}

bool isImageEffect(const model::Effect& e) {
    if (is(e, "Blend", "Picture-in-Picture")) return true;
    if (e.category == QLatin1String("Image")) {
        static const QStringList names = {QStringLiteral("Flip"),       QStringLiteral("Flop"),        QStringLiteral("Flip-Flop"),
                                          QStringLiteral("Field Swap"), QStringLiteral("Resize"),      QStringLiteral("Mask"),
                                          QStringLiteral("Color Effect"), QStringLiteral("Blur Effect"), QStringLiteral("Mosaic Effect")};
        return names.contains(e.name);
    }
    if (detail::isIllusionSegmentEffect(e)) return true;
    if (e.category == QLatin1String("Key")) {
        return e.name == QLatin1String("Luma Key") || e.name == QLatin1String("Chroma Key") || e.name == QLatin1String("RGB Keyer") ||
               e.name == QLatin1String("SpectraMatte");
    }
    return filmMaskRatio(e) || is(e, "Film", "Mask") || is(e, "Film", "Blowup");
}

bool isReformat(const model::Effect& e) {
    return e.category == QLatin1String("Reformat") &&
           (e.name == QLatin1String("16:9 Letterbox") || e.name == QLatin1String("14:9 Letterbox") || e.name == QLatin1String("4:3 Sidebar"));
}

Picture imageEffectPicture(const model::Effect& e, const Picture& layer, double time, const model::EffectSpan& span) {
    const int width = layer.width();
    const int height = layer.height();
    if (layer.isNull()) return layer;
    const double W = width;
    const double H = height;
    Picture out(width, height);

    if (detail::isIllusionSegmentEffect(e)) return detail::illusionSegmentPicture(e, layer, time, span);
    if (e.name == QLatin1String("Color Effect")) return detail::colorEffectPicture(e, layer, time, span);
    if (e.name == QLatin1String("Luma Key") || e.name == QLatin1String("Chroma Key")) return detail::lumaChromaKeyPicture(e, layer, time, span);
    if (e.name == QLatin1String("RGB Keyer")) return detail::rgbKeyerPicture(e, layer, time, span);
    if (e.name == QLatin1String("SpectraMatte")) return detail::spectraMattePicture(e, layer, time, span);
    if (e.name == QLatin1String("Blur Effect") || e.name == QLatin1String("Mosaic Effect")) {
        return detail::detailEffectPicture(e, layer, time, span);
    }
    if (e.name == QLatin1String("Field Swap")) {
        // "Inverts the fields of any interlaced media" (p.469): each pair of lines changes places.
        for (int y = 0; y + 1 < height; y += 2) {
            std::copy(layer.pixel(0, y + 1), layer.pixel(0, y + 1) + width * 4, out.pixel(0, y));
            std::copy(layer.pixel(0, y), layer.pixel(0, y) + width * 4, out.pixel(0, y + 1));
        }
        if (height % 2) std::copy(layer.pixel(0, height - 1), layer.pixel(0, height - 1) + width * 4, out.pixel(0, height - 1));
        return out;
    }

    // Each effect below is a mapping from a pixel of the result to a point of `layer`, a region of the
    // result the picture shows in (its distance, negative inside), and what lies outside it: transparent
    // (Picture-in-Picture, over the tracks below) or the Background Color.
    float in[4];
    const QColor background = backgroundOf(e, time, span);
    const Picture fill = Picture::solid(1, 1, background);
    const float* bg = fill.pixel(0, 0);

    if (e.name == QLatin1String("Flip") || e.name == QLatin1String("Flop") || e.name == QLatin1String("Flip-Flop")) {
        // "Flip flips the video image vertically ... Flop flips the video image horizontally" (p.469).
        const bool flipY = e.name != QLatin1String("Flop");
        const bool flipX = e.name != QLatin1String("Flip");
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float* s = layer.pixel(flipX ? width - 1 - x : x, flipY ? height - 1 - y : y);
                std::copy(s, s + 4, out.pixel(x, y));
            }
        }
        return out;
    }

    if (is(e, "Blend", "Picture-in-Picture") || is(e, "Image", "Resize")) {
        // Picture-in-Picture: "Video on the top layer, where you apply the effect, appears inside the
        // video on the bottom layer" (p.458), with Border, Foreground, Scaling, Position and Crop. Resize:
        // "Modifies the size and position of the video. The background color displays where no video is
        // present" (p.470). Ours: the cropped picture is scaled about the frame's centre, then moved.
        const bool pip = e.name == QLatin1String("Picture-in-Picture");
        const Rect crop = cropOf(e, time, span, W, H);
        const Placement pl = placementOf(e, time, span, W);
        const detail::Border border = pip ? detail::Border::of(e, time, span, width) : detail::Border{};
        const float level = pip ? static_cast<float>(std::clamp(model::numberAt(e, QStringLiteral("Foreground"), QStringLiteral("Level"), time, span).value_or(100.0), 0.0, 100.0) / 100.0)
                                : 1.0f;
        // The crop's rectangle where it lands.
        const double x0 = (crop.x0 - W / 2.0) * pl.sx + W / 2.0 + pl.dx;
        const double x1 = (crop.x1 - W / 2.0) * pl.sx + W / 2.0 + pl.dx;
        const double y0 = (crop.y0 - H / 2.0) * pl.sy + H / 2.0 + pl.dy;
        const double y1 = (crop.y1 - H / 2.0) * pl.sy + H / 2.0 + pl.dy;
        const bool empty = pl.sx <= 0.0 || pl.sy <= 0.0 || crop.x1 <= crop.x0 || crop.y1 <= crop.y0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float* o = out.pixel(x, y);
                if (!pip) std::copy(bg, bg + 4, o);
                if (empty) continue;
                const double px = x + 0.5;
                const double py = y + 0.5;
                sample(layer, (px - W / 2.0 - pl.dx) / pl.sx + W / 2.0, (py - H / 2.0 - pl.dy) / pl.sy + H / 2.0, in);
                border.draw(o, boxDistance(px, py, x0, y0, x1, y1), in, level);
            }
        }
        return out;
    }

    if (e.name == QLatin1String("Mask") || filmMaskRatio(e)) {
        // Image Mask: "Masks out any area of the image that is not inside a rectangle defined by the
        // Scaling and Position parameters of the effect. The area surrounding the rectangle is masked by
        // the effect's background color" (p.470); the Film masks the same with a rectangle of their shape
        // (p.461). Ours: a film mask's rectangle is as wide as the picture and centred, and Scaling and
        // Position change it as they change Mask's.
        const Placement pl = placementOf(e, time, span, W);
        double rw = W;
        double rh = H;
        if (const auto ratio = filmMaskRatio(e)) rh = std::min(H, W / *ratio);
        const double cx = W / 2.0 + pl.dx;
        const double cy = H / 2.0 + pl.dy;
        const double hx = rw * pl.sx / 2.0;
        const double hy = rh * pl.sy / 2.0;
        const detail::Border edge; // a pixel's soft edge, no band
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float* o = out.pixel(x, y);
                std::copy(bg, bg + 4, o);
                const float* s = layer.pixel(x, y);
                std::copy(s, s + 4, in);
                edge.draw(o, boxDistance(x + 0.5, y + 0.5, cx - hx, cy - hy, cx + hx, cy + hy), in);
            }
        }
        return out;
    }

    if (e.name == QLatin1String("Blowup")) {
        // "Blowup lets you enlarge and reposition the image": Field "0 (leaves the image size unchanged)
        // to 12 (increases the image to the maximum size)", X Pos -24 to 24 and Y Pos -18 to 18, 0
        // unchanged (p.500), after Crop. Ours: Field 12 is three times the size; the positions move the
        // part shown across the whole picture at their ends.
        const QString blowup = QStringLiteral("Film Effects Blowup Parameter");
        const double scale = 1.0 + std::clamp(number(e, blowup, QStringLiteral("Field"), time, span), 0.0, 12.0) / 6.0;
        const double free = 1.0 - 1.0 / scale; // how far the shown part can move, as a part of the picture
        const double cx = W / 2.0 + std::clamp(number(e, blowup, QStringLiteral("X Pos"), time, span), -24.0, 24.0) / 24.0 * W / 2.0 * free;
        const double cy = H / 2.0 + std::clamp(number(e, blowup, QStringLiteral("Y Pos"), time, span), -18.0, 18.0) / 18.0 * H / 2.0 * free;
        const Rect crop = cropOf(e, time, span, W, H);
        const detail::Border edge;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float* o = out.pixel(x, y);
                std::copy(bg, bg + 4, o);
                const double sxp = cx + (x + 0.5 - W / 2.0) / scale;
                const double syp = cy + (y + 0.5 - H / 2.0) / scale;
                sample(layer, sxp, syp, in);
                // The cropped edges, where they land.
                const double d = boxDistance(sxp, syp, crop.x0, crop.y0, crop.x1, crop.y1) * scale;
                edge.draw(o, d, in);
            }
        }
        return out;
    }
    return layer;
}

Picture reformatPicture(const model::Effect& e, const Picture& picture, double time, const model::EffectSpan& span) {
    const int width = picture.width();
    const int height = picture.height();
    if (picture.isNull()) return picture;
    const double W = width;
    const double H = height;
    // "16:9 Letterbox: Scales the image vertically to the 16:9 aspect ratio within a 4:3 frame";
    // "14:9 Letterbox: ... expands the media horizontally by a fixed amount before scaling it down
    // vertically, resulting in a slight horizontal cropping of the image. You control what part of the
    // image is cropped with the Position slider"; "4:3 Sidebar: Reformats 4:3 source media to the 16:9
    // Anamorphic aspect ratio by scaling down the media horizontally and applying a black band to the
    // left and right sides" (p.229-230). Ours: the factors that take 16:9 anamorphic to 16:9 and 14:9
    // in 4:3 (3/4 high; 8/7 wide and 6/7 high) and 4:3 into 16:9 anamorphic (3/4 wide); Position H's 0
    // to 100 pans the 14:9 crop from the left edge to the right.
    double sx = 1.0;
    double sy = 1.0;
    double dx = 0.0;
    if (e.name == QLatin1String("16:9 Letterbox")) {
        sy = 3.0 / 4.0;
    } else if (e.name == QLatin1String("14:9 Letterbox")) {
        sx = 8.0 / 7.0;
        sy = 6.0 / 7.0;
        const double h = std::clamp(number(e, QStringLiteral("14:9 Letterbox Position H Parameter"), QStringLiteral("Position H"), time, span), 0.0, 100.0);
        dx = (0.5 - h / 100.0) * (sx - 1.0) * W;
    } else if (e.name == QLatin1String("4:3 Sidebar")) {
        sx = 3.0 / 4.0;
    }
    const Picture fill = Picture::solid(1, 1, backgroundOf(e, time, span));
    const float* bg = fill.pixel(0, 0);
    const double x0 = W / 2.0 - W * std::min(sx, 1.0) / 2.0;
    const double y0 = H / 2.0 - H * sy / 2.0;
    const detail::Border edge;
    Picture out(width, height);
    float in[4];
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float* o = out.pixel(x, y);
            std::copy(bg, bg + 4, o);
            sample(picture, (x + 0.5 - W / 2.0 - dx) / sx + W / 2.0, (y + 0.5 - H / 2.0) / sy + H / 2.0, in);
            edge.draw(o, boxDistance(x + 0.5, y + 0.5, x0, y0, W - x0, H - y0), in);
        }
    }
    return out;
}

} // namespace lumenis::media
