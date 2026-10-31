#pragma once

#include <QImage>

// FluidBlur, FluidColorMap and FluidMorph (FX Guide p.467, p.510-512; ROADMAP 6.5d3b;
// docs/spec/illusion-fx.md): the Illusion FX effects built on our optical flow (optical_flow.h). The
// compositor calls them with the frames around the one shown (compositor.cpp). Our own code throughout.
namespace lumenis::media {

// "FluidBlur creates a motion blur. The amount of blur is based on the amount of motion in the clip. Only
// objects that are moving from one frame to the next are blurred ... it blurs in the direction of travel"
// (p.467). Motions are in pixels of a picture 1920 wide (ours: the guide counts "pixels between frames").
struct FluidBlurOptions {
    // "The amount of blur ramps linearly from 0 to the Blur Amount value as the pixel motion increases from
    // the Lower Motion Threshold value to the Upper Motion Threshold value ... does not blur objects moving
    // less than the Lower ... or more than the Upper" (p.510). Blur Amount 0-100: the blur's length as a
    // percent of the motion.
    double amount = 50;
    double lower = 1;
    double upper = 100;
    // "add an amount from the previous frame's (or field's) blur to the current frame" (0-100).
    double trail = 0;
    // "keep the front edge of an object crisp": the blur only behind the motion.
    bool sharpenLeadingEdge = false;
};
// `frame` blurred along its motion toward `next`; `previous` (may be null) gives the trail.
QImage fluidBlur(const QImage& previous, const QImage& frame, const QImage& next, const FluidBlurOptions& options);

// "FluidColorMap creates animation based on the amount of motion in a scene ... Direction of movement maps to
// unique chroma (color) values ... the Vectorscope layout ... as a compass of color values. Motion magnitude
// maps to luminance (brightness) and color saturation values" (p.467).
struct FluidColorMapOptions {
    // "Objects moving less than the Min Motion Threshold ... have the minimum saturation and luminance ... more
    // than the Max Motion Threshold ... the maximum" (p.511), in pixels of a 1920-wide picture.
    double minMotion = 0;
    double maxMotion = 20;
    // 0-100: "If you set Constant Saturation to the maximum value, motion has no effect on color saturation".
    double constantSaturation = 50;
};
// The motion from `frame` to `next` as colour: black where nothing moves.
QImage fluidColorMap(const QImage& frame, const QImage& next, const FluidColorMapOptions& options);

// "FluidMorph lets you morph between two clips" (p.467): `t` (0 to 1) of the way from `outgoing` to
// `incoming`. With Feature Match "Media Composer attempts to match features between the two clips" (ours:
// each picture moved along the flow between them, media::interpolateFrames); without it "the morph [is]
// based on the brightness of the two images" (ours: each pixel changes over when its brightness says, the
// brighter of the two leading).
QImage fluidMorph(const QImage& outgoing, const QImage& incoming, double t, bool featureMatch);

} // namespace lumenis::media
