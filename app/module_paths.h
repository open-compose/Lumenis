#pragma once

#include <filesystem>
#include <vector>

namespace lumenis::app {

// Directories searched for modules, in order:
//   1. each entry of LUMENIS_MODULE_PATH
//   2. the modules directory shipped with the application
//   3. the per-user modules directory
// Call after the QCoreApplication exists.
std::vector<std::filesystem::path> moduleSearchPaths();

} // namespace lumenis::app
