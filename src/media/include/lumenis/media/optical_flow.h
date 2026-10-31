#pragma once

#include <QImage>

#include <vector>

// Our optical flow (ROADMAP 6.5d1; docs/spec/motion-effects.md): FluidMotion's in-between frames. "FluidMotion
// examines each pixel in the outgoing image, finds the corresponding pixel in the incoming image, and
// creates a motion path between the two that it uses to build the intermediate images" (FX Guide p.197).
// Ours: coarse-to-fine block matching on the brightness of a reduced copy of each frame, cleaned up with a
// median of the neighbouring blocks, then each pixel of the in-between frame fetched along its motion path
// from both frames and mixed by how near it is to each. Our own code; nothing from Avid or a library.
namespace lumenis::media {

// Motion from one frame to another: a vector (in pixels of the frames given) for each block.
struct FlowField {
    int columns = 0;
    int rows = 0;
    double block = 1.0;     // a block's size, in pixels of the frames given
    std::vector<float> dx;  // columns × rows, row by row
    std::vector<float> dy;
    bool isEmpty() const { return dx.empty(); }
    // The motion at a point, from the blocks around it.
    void at(double x, double y, double* outX, double* outY) const;
};

// The motion from `from` to `to` (images of the same size). `draft` works on a smaller copy and looks
// less far, for FluidMotion Draft ("Lets you view the motion that would be created by the FluidMotion
// setting, but at a lower quality", p.186).
FlowField estimateFlow(const QImage& from, const QImage& to, bool draft = false);

// The frame `t` of the way (0 to 1) from `a` to `b`, along the motion between them. Images of different
// sizes are mixed as they are.
QImage interpolateFrames(const QImage& a, const QImage& b, double t, bool draft = false);

} // namespace lumenis::media
