#include "lumenis/ext/module_loader.h"

#include "lumenis/ext/module_abi.h"

#include <QLibrary>
#include <QString>

#include <algorithm>
#include <bit>
#include <cstring>

// ModuleLoader: loads separately built modules (shared libraries) and lets them add providers to
// the registry. The ABI they must follow is in module_abi.h; app/main.cpp loads every module in
// the module search paths at start-up. docs/EXTENSIONS.md describes the extension model and
// tests/ext/test_module.cpp is a complete example module.
namespace lumenis::ext {

namespace {

std::string orEmpty(const char* s) {
    return s ? std::string(s) : std::string();
}

// The ModuleHost handed to one module's register function; counts the providers it adds.
class RegistryHost final : public ModuleHost {
public:
    explicit RegistryHost(ProviderRegistry& registry) : registry_(registry) {}

    std::uint32_t abiVersion() const override { return LUMENIS_MODULE_ABI_VERSION; }

    bool addProvider(std::shared_ptr<Provider> provider) override {
        const bool added = registry_.add(std::move(provider));
        if (added) {
            ++added_;
        }
        return added;
    }

    std::size_t added() const { return added_; }

private:
    ProviderRegistry& registry_;
    std::size_t added_ = 0;
};

} // namespace

// Keeps every loaded QLibrary alive (they are never unloaded; see load()).
struct ModuleLoader::Impl {
    std::vector<std::unique_ptr<QLibrary>> libraries;
};

ModuleLoader::ModuleLoader(ProviderRegistry& registry)
    : registry_(registry), impl_(std::make_unique<Impl>()) {}

ModuleLoader::~ModuleLoader() = default;

ModuleRecord ModuleLoader::load(const std::filesystem::path& file) {
    ModuleRecord record;
    record.path = file;

    auto fail = [&](std::string message) {
        record.error = std::move(message);
        records_.push_back(record);
        return record;
    };

    auto library = std::make_unique<QLibrary>(QString::fromStdU16String(file.u16string()));
    // Qt reference-counts a library across QLibrary instances, so unloading
    // a rejected duplicate would unmap the copy whose code backs live
    // providers. Libraries are therefore never unmapped, even when rejected.
    library->setLoadHints(QLibrary::PreventUnloadHint);
    if (!library->load()) {
        return fail(library->errorString().toStdString());
    }

    // resolve() returns a generic function pointer; bit_cast restores the real
    // signature without tripping -Wcast-function-type.
    const auto infoFn = std::bit_cast<ModuleInfoFunction>(library->resolve(kModuleInfoSymbol));
    const auto registerFn =
        std::bit_cast<ModuleRegisterFunction>(library->resolve(kModuleRegisterSymbol));
    if (!infoFn || !registerFn) {
        return fail("not a module: missing lumenis_module_info or lumenis_module_register");
    }

    // The info is plain C, so it is safe to read before any compatibility check.
    const LumenisModuleInfo* info = infoFn();
    if (!info) {
        return fail("module returned no info");
    }
    record.id = orEmpty(info->id);
    record.name = orEmpty(info->name);
    record.version = orEmpty(info->version);
    record.vendor = orEmpty(info->vendor);
    record.license = orEmpty(info->license);

    if (info->abiVersion != LUMENIS_MODULE_ABI_VERSION) {
        return fail("module ABI version " + std::to_string(info->abiVersion) + ", application needs "
                    + std::to_string(LUMENIS_MODULE_ABI_VERSION));
    }
    if (!info->toolchain || std::strcmp(info->toolchain, LUMENIS_TOOLCHAIN_ID) != 0) {
        return fail("module built with toolchain '" + orEmpty(info->toolchain)
                    + "', application uses '" LUMENIS_TOOLCHAIN_ID "'");
    }
    const bool duplicate = std::any_of(records_.begin(), records_.end(), [&](const ModuleRecord& r) {
        return r.loaded && r.id == record.id;
    });
    if (duplicate) {
        // Keep the first copy; the same module may sit in several search paths.
        return fail("module '" + record.id + "' is already loaded");
    }

    // Only now is it safe to pass C++ objects. Providers added before a failing return code stay
    // registered, so the library is kept either way.
    RegistryHost host(registry_);
    const int rc = registerFn(&host);
    record.providersAdded = host.added();
    impl_->libraries.push_back(std::move(library));
    if (rc != 0) {
        return fail("module registration failed with code " + std::to_string(rc));
    }

    record.loaded = true;
    records_.push_back(record);
    return record;
}

// Directories are taken in the order given, so an earlier directory's copy of a module wins.
std::vector<ModuleRecord> ModuleLoader::loadDirectories(const std::vector<std::filesystem::path>& dirs) {
    std::vector<ModuleRecord> result;
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec)
                && QLibrary::isLibrary(QString::fromStdU16String(entry.path().u16string()))) {
                files.push_back(entry.path());
            }
        }
        // Deterministic order regardless of filesystem.
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            result.push_back(load(file));
        }
    }
    return result;
}

} // namespace lumenis::ext
