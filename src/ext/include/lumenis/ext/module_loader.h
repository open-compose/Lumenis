#pragma once

#include "lumenis/ext/provider.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lumenis::ext {

struct ModuleRecord {
    std::filesystem::path path;
    bool loaded = false;
    std::string error; // set when !loaded
    std::string id;
    std::string name;
    std::string version;
    std::string vendor;
    std::string license;
    std::size_t providersAdded = 0;
};

// Loads modules into a registry. A module is never unloaded, not even one
// that was rejected: providers it created may outlive the loader.
class ModuleLoader {
public:
    explicit ModuleLoader(ProviderRegistry& registry);
    ~ModuleLoader();

    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

    ModuleRecord load(const std::filesystem::path& file);

    // Loads every shared library directly inside each directory (not
    // recursive). Missing directories are skipped.
    std::vector<ModuleRecord> loadDirectories(const std::vector<std::filesystem::path>& dirs);

    const std::vector<ModuleRecord>& records() const { return records_; }

private:
    struct Impl;
    ProviderRegistry& registry_;
    std::unique_ptr<Impl> impl_;
    std::vector<ModuleRecord> records_;
};

} // namespace lumenis::ext
