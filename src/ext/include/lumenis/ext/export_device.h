// The extension point behind File > Output > Export to Device (Phase 7.2b; docs/spec/export-device.md; Editing Guide
// p.883-884, p.901, p.1187): an XDCAM disk or a P2 card that takes an exported clip. The application writes the clip as
// an MXF OP1a file in one of the device's video formats (the ids of media::movieCompressions(), "xdcam_50", ...) and
// hands it to deliver(), which puts it on the device under the device's own name. The built-in provider writes XDCAM
// disks seen as folders (media::XdcamDiskProvider); a P2 card or a networked deck comes from a module.
// Header-only, standard types only, so that modules can implement it (docs/EXTENSIONS.md).
#pragma once

#include "lumenis/ext/provider.h"

#include <string>
#include <vector>

namespace lumenis::ext {

// One disk or card the device can write now.
struct ExportDeviceTarget {
    std::string id;       // stable while it is connected
    std::string name;     // for the Target XDCAM Disk or P2 Device list
    // The number of audio channels of the clips already on it, 0 when it has none: "If the target XDCAM disk you are
    // exporting to already has other clips on it, you are only allowed to export a clip with the same number of audio
    // tracks" (p.884).
    int audioChannels = 0;
};

class ExportDeviceProvider : public Provider {
public:
    // "xdcam" or "p2": which Export to Device command lists it.
    virtual std::string deviceKind() const = 0;
    // The disks or cards connected now.
    virtual std::vector<ExportDeviceTarget> targets() const = 0;
    // The video formats it takes, as media::movieCompressions() ids, best first.
    virtual std::vector<std::string> videoFormats() const = 0;
    // The audio bit depths it takes: 16 and 24 bits by default (p.884, p.1187).
    virtual std::vector<int> bitDepths() const { return {16, 24}; }
    // Puts `file` (an MXF OP1a clip the application wrote; the provider may move it) on the target, setting
    // `clipName` to the name the device gave it ("C0019.MXF"). False with the reason in `error`.
    virtual bool deliver(const std::string& targetId, const std::string& file, std::string* clipName, std::string* error) = 0;
};

} // namespace lumenis::ext
