#pragma once

#include "lumenis/ext/provider.h"

#include <filesystem>
#include <string>
#include <vector>

namespace lumenis::ext {

// Extension point for hosting third-party effect and audio plug-ins.
//
//   OFX   open (BSD), bundled host     -> Category::EffectPluginHost
//   CLAP  open (MIT), bundled host     -> Category::AudioPluginHost
//   VST3  Steinberg SDK                -> Category::AudioPluginHost
//   AVX   Avid SDK, needs agreement    -> Category::EffectPluginHost, ThirdParty
//   AAX   Avid SDK, needs agreement    -> Category::AudioPluginHost, ThirdParty
//
// A host provider only discovers plug-ins here; the processing interfaces are
// added with the effects (Phase 6) and audio (Phase 5) engines.

enum class PluginStandard { OFX, AVX, CLAP, VST3, AAX };

struct PluginDescriptor {
    PluginStandard standard = PluginStandard::OFX;
    std::string id;
    std::string name;
    std::string vendor;
    std::string version;
    std::filesystem::path location;
};

class PluginHostProvider : public Provider {
public:
    virtual PluginStandard standard() const = 0;
    // The platform's conventional install locations for this standard.
    virtual std::vector<std::filesystem::path> defaultSearchPaths() const = 0;
    virtual std::vector<PluginDescriptor> scan(const std::vector<std::filesystem::path>& paths) = 0;
};

} // namespace lumenis::ext
