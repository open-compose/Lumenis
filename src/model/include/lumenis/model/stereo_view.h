#pragma once

#include "lumenis/model/stereo_clip.h"

#include <QString>

// How stereoscopic pictures are shown (8.6d2, docs/spec/stereoscopic.md): the S3D View and S3D Overlay of the Composer
// setting's monitors (Editing Guide p.1162-1163), of the Full Screen Playback setting (p.1190-1191) and of the Video Output
// tool (p.1226-1227), and the project's S3D Settings whose parallax the overlay's guides show (p.1216). The pictures
// themselves are made by media::stereoView() and media::drawParallaxGuides() (lumenis/media/stereo.h).
namespace lumenis::model {

// One S3D View choice. Its stored value names the kind and its option: "mix", "difference", "frameCompatible",
// "monoLeft"/"monoRight"/"monoLeading", "anaglyphRedCyan"/"anaglyphGreenMagenta"/"anaglyphAmberBlue",
// "bwRedCyan"/"bwGreenMagenta", "checker1"/"checker10"/"checker20"/"checker50"/"checker100",
// "compare25"/"compare50"/"compare75", and the Video Output tool's "followProject", "fullFrame" and "followSR".
struct S3DView {
    enum class Kind { FollowProject, FollowSourceRecord, Mix, Difference, FrameCompatible, FullFrame, Mono, Anaglyph, BwAnaglyph,
                      Checkerboard, Compare };
    // "Red-Cyan: Uses red for the left eye and cyan for the right"; Green-Magenta; Amber-Blue (p.1190, p.1227).
    enum class Colors { RedCyan, GreenMagenta, AmberBlue };
    // Mono's eye: "Left", "Right", "Leading eye" (p.1190).
    enum class MonoEye { Left, Right, Leading };

    Kind kind = Kind::FrameCompatible;
    MonoEye mono = MonoEye::Leading;
    Colors colors = Colors::RedCyan;
    int block = 10;  // Checkerboard: "Each block size is approximately n by n pixels" (p.1162)
    int percent = 50; // Compare: "You can set the comparison mode to 25, 50, or 75%" (p.1162)
};

// The view a stored value names; Frame Compatible for anything unknown (ours: the monitors' default, which shows a Side
// by Side or Over / Under project as 8.6d1 did).
S3DView s3dViewOf(const QString& value);

// The S3D Overlay's disparity guides: "Parallax Near: Displays green guides ...", "Parallax Far: Displays blue guides
// ...", Parallax Near/Far both (p.1163, p.1191). Stored "off", "near", "far", "nearFar".
enum class S3DOverlay { Off, Near, Far, NearFar };
S3DOverlay s3dOverlayOf(const QString& value);

// The Project setting S3D Settings (p.1216). Lengths are in the Unit System's units: metres and centimetres for the eye
// separation (Metric), feet and inches (Imperial); ours, the guide gives only "the default is 6.35 cm".
struct S3DSettings {
    bool imperial = false;
    double viewingDistance = 10.0; // "the average distance between the audience and the screen"
    double screenWidth = 10.0;     // "the width of the screen for which the final output will be delivered"
    int rasterWidth = 1920;        // "the resolution at which the final output will be projected", in pixels across
    double eyeSeparation = 6.35;   // "the average intra-ocular distance between human eyes (the default is 6.35 cm)"
    double depthNear = -2.0;       // "the depth budget available in front of the screen. This is always a negative value"
    double depthFar = 10.0;        // "... behind the screen. This is always a positive value"
    bool manualNear = false;       // "If you set the drop-down button to manual, you may override the calculated setting"
    bool manualFar = false;
    double parallaxNearValue = 0.0; // the manual values, in pixels of the raster
    double parallaxFarValue = 0.0;
};

// The screen parallax, in pixels of the raster, of a point `depth` in front of (negative) or behind (positive) the screen:
// the eyes' separation times depth / (viewing distance + depth), as a part of the screen's width (ours: the guide says
// only that it "is automatically calculated"). Negative in front of the screen.
double parallaxPixels(const S3DSettings& s, double depth);
// Parallax Near and Far: the manual values when set to Manual, else calculated from the depth limits.
double parallaxNear(const S3DSettings& s);
double parallaxFar(const S3DSettings& s);

} // namespace lumenis::model
