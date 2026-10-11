#include "lumenis/ext/provider.h"

#include <algorithm>

namespace lumenis::ext {

std::string_view toString(LicenseTier tier) {
    switch (tier) {
    case LicenseTier::OpenSource: return "open-source";
    case LicenseTier::OperatingSystem: return "operating-system";
    case LicenseTier::ThirdParty: return "third-party";
    }
    return "unknown";
}

std::string_view toString(Category category) {
    switch (category) {
    case Category::VideoCodec: return "video-codec";
    case Category::AudioCodec: return "audio-codec";
    case Category::Container: return "container";
    case Category::Transcription: return "transcription";
    case Category::EffectPluginHost: return "effect-plugin-host";
    case Category::AudioPluginHost: return "audio-plugin-host";
    case Category::VideoIoDevice: return "video-io-device";
    case Category::SharedStorage: return "shared-storage";
    }
    return "unknown";
}

bool ProviderRegistry::add(std::shared_ptr<Provider> provider) {
    if (!provider || find(provider->info().id)) {
        return false;
    }
    providers_.push_back(std::move(provider));
    return true;
}

void ProviderRegistry::setEnabled(std::string_view id, bool enabled) {
    const auto it = std::find(disabled_.begin(), disabled_.end(), id);
    if (enabled && it != disabled_.end()) {
        disabled_.erase(it);
    } else if (!enabled && it == disabled_.end()) {
        disabled_.emplace_back(id);
    }
}

bool ProviderRegistry::isEnabled(std::string_view id) const {
    return std::find(disabled_.begin(), disabled_.end(), id) == disabled_.end();
}

std::shared_ptr<Provider> ProviderRegistry::find(std::string_view id) const {
    for (const auto& p : providers_) {
        if (p->info().id == id) {
            return p;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<Provider>> ProviderRegistry::ranked() const {
    std::vector<std::shared_ptr<Provider>> result;
    for (const auto& p : providers_) {
        if (isEnabled(p->info().id) && p->probe().available) {
            result.push_back(p);
        }
    }
    // Stable so that equal priorities keep registration order.
    std::stable_sort(result.begin(), result.end(),
                     [](const auto& a, const auto& b) { return a->priority() > b->priority(); });
    return result;
}

} // namespace lumenis::ext
