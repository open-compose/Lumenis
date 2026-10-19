#pragma once

#include "lumenis/ext/provider.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lumenis::ext {

// What happened when one module file was loaded; app/main.cpp prints them in its provider
// listing. The id and other info strings are filled in once the info function has been called,
// so a rejected module may still carry them.
struct ModuleRecord {
    std::filesystem::path path;
    bool loaded = false;
    std::string error; // set when !loaded
    std::string id;
    std::string name;
    std::string version;
    std::string vendor;
    std::string license;
    // Providers the module added to the registry while registering (also when registration failed).
    std::size_t providersAdded = 0;
};

// Loads modules into a registry. A module is never unloaded, not even one
// that was rejected: providers it created may outlive the loader.
class ModuleLoader {
public:
    // The registry must outlive the loader.
    explicit ModuleLoader(ProviderRegistry& registry);
    ~ModuleLoader();

    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

    // Loads one shared library, checks its ABI version, toolchain and id, and lets it register its
    // providers. The record is returned and also kept in records(); loaded is false and error says
    // why when any step fails. A module whose id is already loaded is rejected.
    ModuleRecord load(const std::filesystem::path& file);

    // Loads every shared library directly inside each directory (not
    // recursive). Missing directories are skipped.
    std::vector<ModuleRecord> loadDirectories(const std::vector<std::filesystem::path>& dirs);

    // Every load attempt so far, in order, successful or not.
    const std::vector<ModuleRecord>& records() const { return records_; }

private:
    struct Impl;
    ProviderRegistry& registry_;
    std::unique_ptr<Impl> impl_;
    std::vector<ModuleRecord> records_;
};

} // namespace lumenis::ext
