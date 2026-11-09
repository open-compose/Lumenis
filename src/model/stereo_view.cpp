// The S3D View and Overlay choices and the S3D Settings' parallax (stereo_view.h; Editing Guide p.1162-1163, p.1190-1191,
// p.1216, p.1226-1227).
#include "lumenis/model/stereo_view.h"

namespace lumenis::model {

S3DView s3dViewOf(const QString& value) {
    S3DView v;
    using K = S3DView::Kind;
    if (value == QLatin1String("followProject")) v.kind = K::FollowProject;
    else if (value == QLatin1String("followSR")) v.kind = K::FollowSourceRecord;
    else if (value == QLatin1String("mix")) v.kind = K::Mix;
    else if (value == QLatin1String("difference")) v.kind = K::Difference;
    else if (value == QLatin1String("fullFrame")) v.kind = K::FullFrame;
    else if (value.startsWith(QLatin1String("mono"))) {
        v.kind = K::Mono;
        v.mono = value == QLatin1String("monoLeft") ? S3DView::MonoEye::Left
                 : value == QLatin1String("monoRight") ? S3DView::MonoEye::Right
                                                        : S3DView::MonoEye::Leading;
    } else if (value.startsWith(QLatin1String("anaglyph")) || value.startsWith(QLatin1String("bw"))) {
        v.kind = value.startsWith(QLatin1String("bw")) ? K::BwAnaglyph : K::Anaglyph;
        v.colors = value.endsWith(QLatin1String("GreenMagenta")) ? S3DView::Colors::GreenMagenta
                   : value.endsWith(QLatin1String("AmberBlue")) ? S3DView::Colors::AmberBlue
                                                                 : S3DView::Colors::RedCyan;
    } else if (value.startsWith(QLatin1String("checker"))) {
        v.kind = K::Checkerboard;
        bool ok = false;
        const int n = value.mid(7).toInt(&ok);
        v.block = ok && n > 0 ? n : 10;
    } else if (value.startsWith(QLatin1String("compare"))) {
        v.kind = K::Compare;
        bool ok = false;
        const int n = value.mid(7).toInt(&ok);
        v.percent = ok && n > 0 && n < 100 ? n : 50;
    }
    return v;
}

S3DOverlay s3dOverlayOf(const QString& value) {
    if (value == QLatin1String("near")) return S3DOverlay::Near;
    if (value == QLatin1String("far")) return S3DOverlay::Far;
    if (value == QLatin1String("nearFar")) return S3DOverlay::NearFar;
    return S3DOverlay::Off;
}

double parallaxPixels(const S3DSettings& s, double depth) {
    const double distance = s.viewingDistance + depth;
    if (s.viewingDistance <= 0.0 || s.screenWidth <= 0.0 || distance <= 0.0) return 0.0;
    // The eye separation in the distances' unit: centimetres to metres, or inches to feet.
    const double eyes = s.imperial ? s.eyeSeparation / 12.0 : s.eyeSeparation / 100.0;
    const double onScreen = eyes * depth / distance;
    return onScreen / s.screenWidth * s.rasterWidth;
}

double parallaxNear(const S3DSettings& s) {
    return s.manualNear ? s.parallaxNearValue : parallaxPixels(s, s.depthNear);
}

double parallaxFar(const S3DSettings& s) {
    return s.manualFar ? s.parallaxFarValue : parallaxPixels(s, s.depthFar);
}

} // namespace lumenis::model
