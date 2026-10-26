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
// A host provider only discovers plug-ins here. Audio plug-ins process through
// media::AudioEffect (src/media/audio_fx.h): a host registers them with
// media::registerAudioEffects. Video effects get theirs with the effects engine
// (Phase 6).

// The plug-in standards a host provider can serve.
enum class PluginStandard { OFX, AVX, CLAP, VST3, AAX };

// One plug-in found by a scan: its standard, id and display details, and the file or bundle it
// lives in.
struct PluginDescriptor {
    PluginStandard standard = PluginStandard::OFX;
    std::string id;
    std::string name;
    std::string vendor;
    std::string version;
    std::filesystem::path location;
};

// A provider that finds plug-ins of one standard on disk.
class PluginHostProvider : public Provider {
public:
    virtual PluginStandard standard() const = 0;
    // The platform's conventional install locations for this standard.
    virtual std::vector<std::filesystem::path> defaultSearchPaths() const = 0;
    // Finds the plug-ins in the given directories (usually defaultSearchPaths() plus the user's own).
    virtual std::vector<PluginDescriptor> scan(const std::vector<std::filesystem::path>& paths) = 0;
};

} // namespace lumenis::ext
