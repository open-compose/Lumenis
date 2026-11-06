// The extension point behind the Video Output tool (Phase 7.3b; docs/spec/video-output.md; Editing Guide p.910-917,
// p.1225-1227): video output hardware, its sync source, its output signal, its calibration and its test patterns.
// "The Video Output tool is not available in software-only versions of Media Composer" (p.1225): without a provider the
// tool says so. AJA and Blackmagic hardware come as modules (7.3c). Header-only, standard types only.
#pragma once

#include "lumenis/ext/provider.h"

#include <memory>
#include <string>
#include <vector>

namespace lumenis::ext {

struct VideoOutputDeviceInfo {
    std::string id;
    std::string name;
    // Sync Lock (p.1225-1226): "Internal", "Reference", "REF 1", "REF 2", "TriLevel", "Loop through".
    std::vector<std::string> syncSources{"Internal", "Reference"};
    // The analog SD outputs that can be calibrated ("Component", "Composite", "S Video") and the HD ones ("HD Component
    // YPbPr", "HD Component RGB"); "You cannot calibrate output for HD-SDI" (p.913).
    std::vector<std::string> sdOutputs{"Component", "Composite", "S Video"};
    std::vector<std::string> hdOutputs{"HD Component YPbPr", "HD Component RGB"};
    bool hdmi = false; // has HDMI: the HDMI Color Space and HDMI Format menus
};

// An open device. The calibration names are the Video Output tool's sliders ("Black", "Y Gain", "Hue", "Sat", "RY Gain",
// "BY Gain", "Pr Gain", "Pb Gain", "H Phase", "SC Phase", "System Phase"), each from -100 to 100 around its factory level.
class VideoOutputSession {
public:
    virtual ~VideoOutputSession() = default;
    virtual void setSyncSource(const std::string& source) = 0;
    virtual void setOutputSignal(const std::string& signal) = 0;
    virtual void setCalibration(const std::string& name, double value) = 0;
    // Shows a picture on the output (RGBA, 8 bits a channel): a test pattern, or a frame of playback.
    virtual void showFrame(const unsigned char* rgba, int width, int height, int stride) = 0;
};

class VideoOutputProvider : public Provider {
public:
    virtual std::vector<VideoOutputDeviceInfo> devices() const = 0;
    virtual std::unique_ptr<VideoOutputSession> open(const std::string& id, std::string* error) = 0;
};

} // namespace lumenis::ext
