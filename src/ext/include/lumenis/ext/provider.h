#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lumenis::ext {

// Where a provider's right to do its job comes from. This decides how it may
// be distributed; see docs/EXTENSIONS.md.
enum class LicenseTier {
    OpenSource,      // our code or a bundled library with a compatible licence
    OperatingSystem, // a framework the OS vendor ships and licenses (VideoToolbox, Media Foundation, VA-API)
    ThirdParty,      // needs an SDK or licence we must obtain (AAX, AVX, a commercial codec pack)
};

enum class Category {
    VideoCodec,
    AudioCodec,
    Container,
    Transcription,    // speech-to-text for ScriptSync/PhraseFind equivalents
    EffectPluginHost, // OFX, AVX
    AudioPluginHost,  // CLAP, VST3, AAX
    VideoIoDevice,    // AJA, Blackmagic, ...
    SharedStorage,    // NAS bin locking, vendor storage clients
};

std::string_view toString(LicenseTier tier);
std::string_view toString(Category category);

struct ProviderInfo {
    std::string id; // stable, reverse-DNS style: "org.ffmpeg.libavcodec"
    std::string displayName;
    Category category = Category::VideoCodec;
    LicenseTier tier = LicenseTier::OpenSource;
    std::string vendor;
    std::string version;
};

struct Availability {
    bool available = true;
    std::string reason; // why not, for the user, when unavailable
};

// Base of every extension point. Category interfaces (CodecProvider,
// TranscriptionProvider, PluginHostProvider, ...) derive from this.
// Header-only so that modules can implement providers without linking
// against the host.
class Provider {
public:
    virtual ~Provider() = default;

    virtual ProviderInfo info() const = 0;

    // Checks at runtime whether the provider can work on this machine: the OS
    // framework exists, the licence is activated, the hardware is present.
    virtual Availability probe() const { return {}; }

    // Higher wins when several available providers can do the same job.
    virtual int priority() const { return 0; }
};

class ProviderRegistry {
public:
    // Fails (returns false) if a provider with the same id is already registered.
    bool add(std::shared_ptr<Provider> provider);

    // The user can switch a provider off, e.g. to force software decoding.
    void setEnabled(std::string_view id, bool enabled);
    bool isEnabled(std::string_view id) const;

    std::shared_ptr<Provider> find(std::string_view id) const;
    const std::vector<std::shared_ptr<Provider>>& all() const { return providers_; }

    // Enabled, available providers implementing T, best first.
    template <typename T>
    std::vector<std::shared_ptr<T>> usable() const {
        std::vector<std::shared_ptr<T>> result;
        for (const auto& p : ranked()) {
            if (auto typed = std::dynamic_pointer_cast<T>(p)) {
                result.push_back(std::move(typed));
            }
        }
        return result;
    }

private:
    std::vector<std::shared_ptr<Provider>> ranked() const;

    std::vector<std::shared_ptr<Provider>> providers_;
    std::vector<std::string> disabled_;
};

} // namespace lumenis::ext
