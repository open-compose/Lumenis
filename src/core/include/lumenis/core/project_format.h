#pragma once

#include "lumenis/core/rational.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumenis {

// Project formats offered when creating a project. The preset list follows
// the "Project Formats and Resolutions" tables of the Media Composer
// Editing Guide 2025.12 (ch. 35, p.1241-1275); docs/spec/ui-inventory.md.

// SD (NTSC, PAL), HD (720 and 1080 lines) and the formats greater than HD (UHD, 2K, 4K, 8K, 16K).
enum class FormatFamily { SD, HD, HighRes };
// Progressive or interlaced scanning; labels show it as the "p" or "i" after the lines or rate.
enum class ScanMode { Progressive, Interlaced };

// One raster, frame rate and scan mode a project can be created in.
struct ProjectFormat {
    std::string group;  // as listed in the guide: "HD 1080p", "UHD", "2K DCI Flat", ...
    FormatFamily family = FormatFamily::HD;
    // Raster size in pixels.
    int width = 1920;
    int height = 1080;
    Rational editRate{24};  // frames per second (interlaced: frames, not fields)
    ScanMode scan = ScanMode::Progressive;

    // "1080p/23.976", "1080i/59.94", "UHD/29.97", "2048x858p/24", ...
    std::string label() const;
    // Picture aspect ratio. SD may be 4:3 or 16:9 (chosen per project); HD
    // is 16:9; high-resolution formats use their raster's ratio.
    Rational defaultAspect() const;

    friend bool operator==(const ProjectFormat&, const ProjectFormat&) = default;
};

// Every preset, grouped by family (high-resolution first, then HD, then SD),
// in the order the guide lists them.
const std::vector<ProjectFormat>& projectFormatPresets();

// Display text for a frame rate: "23.976", "29.97", "25", "119.88".
std::string frameRateText(const Rational& rate);
// Parses "23.976", "29.97", "59.94", "25", "47.952", "119.88" and friends
// into the exact NTSC-family rational (24000/1001, ...). Nullopt if unknown.
std::optional<Rational> parseFrameRate(std::string_view text);

// Choices offered in New Project (guide p.65-67, p.520-558). The guide
// names the colour-space families but does not list the menu text exactly,
// so these labels are ours (docs/spec/ui-inventory.md, "unverified").
std::vector<std::string> colorSpacesFor(FormatFamily family);
std::vector<std::string> colorDepths();
std::vector<std::string> stereoscopicModes(); // first entry is "Off"
// Film projects are offered for 23.976p, 24p and 25p in 720p and 1080p (p.67).
bool filmAvailable(const ProjectFormat& format);

// Rules for custom rasters (guide p.65): both dimensions even, 256x120 to
// 8192x8192; stereoscopic rasters need width divisible by 8 and height by 4.
// Returns the reason a raster is rejected, or nullopt if it is valid.
std::optional<std::string> validateCustomRaster(int width, int height, bool stereoscopic);

// Name rules (guide p.63): project names up to 56 characters, bin names up
// to 64; for cross-platform safety no / \ : * ? " < > |, no leading or
// trailing spaces and no trailing period. Returns the reason a name is
// rejected, or nullopt if it is valid. Lengths count Unicode code points
// in the UTF-8 input.
inline constexpr int kMaxProjectNameLength = 56;
inline constexpr int kMaxBinNameLength = 64;
// maxLength is kMaxProjectNameLength or kMaxBinNameLength. The reason is English text for the UI.
std::optional<std::string> validateName(std::string_view utf8Name, int maxLength);

} // namespace lumenis
