#include "lumenis/core/project_format.h"

#include <array>
#include <cmath>
#include <cstdio>

// The project format presets and the rules New Project applies (raster limits, name rules, frame
// rate text). The Select Project and New Project dialogs (src/ui/select_project_dialog.*) and the
// project model (src/model/project_store.*) use them; docs/spec/ui-inventory.md has the citations.
namespace lumenis {

namespace {

// A frame rate's display text and its exact value.
struct RateName {
    const char* text;
    Rational rate;
};

// Every rate the presets and parseFrameRate() know. frameRateText() shows the first spelling of a
// rate, so the canonical spellings come before the alternatives at the end.
const std::array<RateName, 15> kRates{{
    {"23.976", Rational(24000, 1001)}, {"24", Rational(24)},
    {"25", Rational(25)},               {"29.97", Rational(30000, 1001)},
    {"30", Rational(30)},               {"47.952", Rational(48000, 1001)},
    {"48", Rational(48)},               {"50", Rational(50)},
    {"59.94", Rational(60000, 1001)},   {"60", Rational(60)},
    {"100", Rational(100)},             {"119.88", Rational(120000, 1001)},
    {"120", Rational(120)},
    // Spellings that also appear in the guide's tables.
    {"23.98", Rational(24000, 1001)},   {"29.976", Rational(30000, 1001)},
}};

// Builds projectFormatPresets(): one ProjectFormat per raster and rate, in the guide's order.
std::vector<ProjectFormat> buildPresets() {
    std::vector<ProjectFormat> out;
    const auto add = [&out](const char* group, FormatFamily family, int w, int h, ScanMode scan,
                            std::initializer_list<const char*> rates) {
        for (const char* r : rates) {
            out.push_back(ProjectFormat{group, family, w, h, *parseFrameRate(r), scan});
        }
    };
    constexpr auto P = ScanMode::Progressive;
    constexpr auto I = ScanMode::Interlaced;
    constexpr auto Hi = FormatFamily::HighRes;
    const std::initializer_list<const char*> cinema = {"23.976", "24", "25", "29.97", "30", "47.952", "48",
                                                       "50", "59.94", "60", "100", "119.88", "120"};

    // Greater than HD (p.1241-1251).
    add("UHD", Hi, 3840, 2160, P, {"23.976", "24", "25", "29.97", "30", "50", "59.94", "60", "100", "119.88", "120"});
    add("2K", Hi, 2048, 1152, P, {"23.976"});
    add("2K", Hi, 2048, 1536, P, {"24", "25", "29.97", "30", "47.952", "48", "50", "59.94", "60", "100", "119.88", "120"});
    add("2K DCI Flat", Hi, 1998, 1080, P, cinema);
    add("2K DCI Full", Hi, 2048, 1080, P, cinema);
    // "2048 x 1080 S3D (stereoscopic)" (p.562): the 2K DCI Full raster, for stereoscopic projects.
    add("2K DCI Full S3D", Hi, 2048, 1080, P, cinema);
    add("2K DCI Scope", Hi, 2048, 858, P, cinema);
    add("2K DCI Full Aperture", Hi, 2048, 1556, P, cinema);
    add("4K DCI Flat", Hi, 3996, 2160, P, cinema);
    add("4K DCI Full", Hi, 4096, 2160, P, cinema);
    add("4K DCI Scope", Hi, 4096, 1716, P, cinema);
    add("4K Full Aperture", Hi, 4096, 3112, P, cinema);
    add("8K", Hi, 8192, 4320, P, cinema);
    add("8K UHD", Hi, 7680, 4320, P, cinema);
    add("16K", Hi, 16384, 4320, P, cinema);
    add("16K UHD", Hi, 15360, 8640, P, cinema);

    // HD (p.1252-1270). Interlaced rates are frame rates (1080i/50 = 25 fps).
    constexpr auto HD = FormatFamily::HD;
    add("HD 1080i", HD, 1920, 1080, I, {"25", "29.97"});
    add("HD 1080i", HD, 1440, 1080, I, {"25", "29.97"});
    add("HD 1080i", HD, 1280, 1080, I, {"29.97"});
    add("HD 1080p", HD, 1920, 1080, P, {"23.976", "24", "25", "29.97", "30", "50", "59.94", "60", "100", "119.88"});
    add("HD 1080p", HD, 1440, 1080, P, {"23.976"});
    add("HD 720p", HD, 1280, 720, P, {"23.976", "25", "29.97", "50", "59.94"});
    add("HD 720p", HD, 960, 720, P, {"23.976", "25", "50", "59.94"});

    // SD (p.1271-1275).
    constexpr auto SD = FormatFamily::SD;
    add("NTSC", SD, 720, 486, P, {"23.976", "24"});
    add("NTSC", SD, 720, 486, I, {"29.97"});
    add("PAL", SD, 720, 576, P, {"24", "25"});
    add("PAL", SD, 720, 576, I, {"25"});
    return out;
}

} // namespace

// Known rates use their table spelling; other whole-number rates print as integers and the rest
// with three decimals.
std::string frameRateText(const Rational& rate) {
    for (const RateName& r : kRates) {
        if (r.rate == rate) {
            return r.text;
        }
    }
    if (rate.den() == 1) {
        return std::to_string(rate.num());
    }
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%.3f", rate.toDouble());
    return buf.data();
}

std::optional<Rational> parseFrameRate(std::string_view text) {
    for (const RateName& r : kRates) {
        if (text == r.text) {
            return r.rate;
        }
    }
    return std::nullopt;
}

std::string ProjectFormat::label() const {
    const std::string scanSuffix = scan == ScanMode::Interlaced ? "i" : "p";
    // Interlaced formats are named by field rate: 1080i/59.94, 1080i/50.
    const Rational shownRate = scan == ScanMode::Interlaced ? editRate * Rational(2) : editRate;
    const std::string rate = frameRateText(shownRate);

    if (family == FormatFamily::HD) {
        // Full-width rasters are "1080p/23.976"; thin rasters name their width.
        const bool fullWidth = width * 9 == height * 16;
        return (fullWidth ? "" : std::to_string(width) + "x") + std::to_string(height) + scanSuffix + "/" + rate;
    }
    if (family == FormatFamily::SD || group == "UHD") {
        return group + " " + rate + scanSuffix;  // "NTSC 29.97i", "PAL 25p", "UHD 23.976p"
    }
    return group + " " + std::to_string(width) + "x" + std::to_string(height) + " " + rate + scanSuffix;
}

Rational ProjectFormat::defaultAspect() const {
    switch (family) {
    case FormatFamily::SD:
        return Rational(4, 3);
    case FormatFamily::HD:
        return Rational(16, 9);
    case FormatFamily::HighRes:
        return Rational(width, height);
    }
    return Rational(16, 9);
}

const std::vector<ProjectFormat>& projectFormatPresets() {
    static const std::vector<ProjectFormat> presets = buildPresets();
    return presets;
}

// Guide p.65-66: SD always uses YCC; HD uses YCC 709 or RGB 709; high-res has
// several. The New Project screenshot on p.522 shows "YCbCr 2020 / SMPTE 2084"
// and p.558 names "ACES RGB". The other high-res labels follow that pattern
// but are unverified, as is the order.
std::vector<std::string> colorSpacesFor(FormatFamily family) {
    switch (family) {
    case FormatFamily::SD:
        return {"YCbCr 601"};
    case FormatFamily::HD:
        return {"YCbCr 709", "RGB 709"};
    case FormatFamily::HighRes:
        return {"YCbCr 709", "RGB 709", "YCbCr 2020", "RGB 2020", "YCbCr 2020 / SMPTE 2084", "YCbCr 2020 / HLG",
                "DCI-P3", "ACES RGB"};
    }
    return {};
}

// Guide p.66: "Determines the bit depth at which the media will be stored."
// The guide does not list the values; these are unverified.
std::vector<std::string> colorDepths() {
    return {"8 bit", "10 bit", "12 bit", "16 bit float", "32 bit float"};
}

std::vector<std::string> stereoscopicModes() {
    return {"Off", "Leading Eye", "Left Eye Only", "Right Eye Only", "Side by Side", "Over / Under", "Full"};
}

// Only HD progressive 720 or 1080 line formats at 23.976, 24 or 25 fps qualify.
bool filmAvailable(const ProjectFormat& format) {
    if (format.scan != ScanMode::Progressive || (format.height != 720 && format.height != 1080)
        || format.family != FormatFamily::HD) {
        return false;
    }
    return format.editRate == Rational(24000, 1001) || format.editRate == Rational(24) || format.editRate == Rational(25);
}

// The checks run in this order, so the first failing rule is the one reported.
std::optional<std::string> validateCustomRaster(int width, int height, bool stereoscopic) {
    if (width % 2 != 0 || height % 2 != 0) {
        return "raster dimensions must be even";
    }
    if (width < 256 || height < 120) {
        return "raster must be at least 256 x 120";
    }
    if (width > 8192 || height > 8192) {
        return "raster must be at most 8192 x 8192";
    }
    if (stereoscopic && (width % 8 != 0 || height % 4 != 0)) {
        return "stereoscopic rasters need a width divisible by 8 and a height divisible by 4";
    }
    return std::nullopt;
}

std::optional<std::string> validateName(std::string_view name, int maxLength) {
    if (name.empty()) {
        return "name is empty";
    }
    int codePoints = 0;
    for (const char c : name) {
        // Count bytes that start a UTF-8 sequence.
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++codePoints;
        }
    }
    if (codePoints > maxLength) {
        return "name is longer than " + std::to_string(maxLength) + " characters";
    }
    for (const char c : std::string_view("/\\:*?\"<>|")) {
        if (name.find(c) != std::string_view::npos) {
            return std::string("name contains '") + c + "'";
        }
    }
    if (name.front() == ' ') {
        return "name starts with a space";
    }
    if (name.back() == ' ') {
        return "name ends with a space";
    }
    if (name.back() == '.') {
        return "name ends with a period";
    }
    return std::nullopt;
}

} // namespace lumenis
