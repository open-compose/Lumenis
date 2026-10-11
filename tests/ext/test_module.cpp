// A minimal module, built the way a third party would build one: against the
// module SDK headers only. Used by tst_module_loader and the app tests.

#include "lumenis/ext/codec.h"
#include "lumenis/ext/module_abi.h"

namespace {

using namespace lumenis::ext;

class TestCodecProvider final : public CodecProvider {
public:
    ProviderInfo info() const override {
        ProviderInfo i;
        i.id = "org.lumenis.test.codec";
        i.displayName = "Test codec";
        i.category = Category::VideoCodec;
        i.tier = LicenseTier::ThirdParty;
        i.vendor = "Lumenis tests";
        i.version = "1.0";
        return i;
    }

    bool canDecode(const CodecQuery& query) const override { return query.codec == "test-codec"; }
    bool canEncode(const CodecQuery&) const override { return false; }
    std::unique_ptr<Decoder> createDecoder(const CodecQuery&) override { return nullptr; }
    std::unique_ptr<Encoder> createEncoder(const CodecQuery&) override { return nullptr; }
};

#if defined(TEST_MODULE_BAD_ABI)
constexpr std::uint32_t kAbi = LUMENIS_MODULE_ABI_VERSION + 1;
constexpr const char* kId = "org.lumenis.test.module-bad-abi";
#else
constexpr std::uint32_t kAbi = LUMENIS_MODULE_ABI_VERSION;
constexpr const char* kId = "org.lumenis.test.module";
#endif

const LumenisModuleInfo kInfo{kAbi, LUMENIS_TOOLCHAIN_ID, kId, "Test module", "1.0", "Lumenis tests",
                              "GPL-3.0-or-later"};

} // namespace

LUMENIS_MODULE_EXPORT const LumenisModuleInfo* lumenis_module_info() {
    return &kInfo;
}

LUMENIS_MODULE_EXPORT int lumenis_module_register(lumenis::ext::ModuleHost* host) {
    return host->addProvider(std::make_shared<TestCodecProvider>()) ? 0 : 1;
}
