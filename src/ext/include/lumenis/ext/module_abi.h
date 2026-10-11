#pragma once

// Binary interface between the application and separately built modules
// (shared libraries that add providers). This is the hook for components we
// cannot ship in the main package: a licensed codec pack, an AAX or AVX
// bridge, a vendor storage client. See docs/EXTENSIONS.md.
//
// A module exports two C functions:
//
//   const LumenisModuleInfo* lumenis_module_info(void);
//   int lumenis_module_register(lumenis::ext::ModuleHost* host);
//
// The info function is plain C and is always safe to call. The register
// function passes C++ objects, so the loader calls it only when the module's
// ABI version and toolchain match the application's.

#include "lumenis/ext/provider.h"

#include <cstdint>
#include <memory>
#include <version>

// Bump whenever any provider interface or this header changes in a way that
// breaks binary compatibility.
#define LUMENIS_MODULE_ABI_VERSION 1u

#if defined(_MSC_VER)
#  if defined(_DEBUG)
#    define LUMENIS_TOOLCHAIN_ID "msvc-v14-debug"
#  else
#    define LUMENIS_TOOLCHAIN_ID "msvc-v14"
#  endif
#elif defined(_LIBCPP_VERSION)
#  define LUMENIS_TOOLCHAIN_ID "libc++"
#elif defined(__GLIBCXX__)
#  if _GLIBCXX_USE_CXX11_ABI
#    define LUMENIS_TOOLCHAIN_ID "libstdc++-cxx11"
#  else
#    define LUMENIS_TOOLCHAIN_ID "libstdc++-legacy"
#  endif
#else
#  error "Unknown C++ toolchain: add it to LUMENIS_TOOLCHAIN_ID"
#endif

#if defined(_WIN32)
#  define LUMENIS_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#  define LUMENIS_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

extern "C" {

struct LumenisModuleInfo {
    std::uint32_t abiVersion; // LUMENIS_MODULE_ABI_VERSION the module was built with
    const char* toolchain;    // LUMENIS_TOOLCHAIN_ID the module was built with
    const char* id;           // reverse-DNS, e.g. "com.example.aax-bridge"
    const char* name;
    const char* version;
    const char* vendor;
    const char* license; // SPDX expression or "Proprietary"
};

} // extern "C"

namespace lumenis::ext {

// What a module may do to the application while registering.
class ModuleHost {
public:
    virtual ~ModuleHost() = default;
    virtual std::uint32_t abiVersion() const = 0;
    // Returns false if a provider with the same id already exists.
    virtual bool addProvider(std::shared_ptr<Provider> provider) = 0;
};

using ModuleInfoFunction = const LumenisModuleInfo* (*)();
using ModuleRegisterFunction = int (*)(ModuleHost* host); // 0 on success

inline constexpr const char* kModuleInfoSymbol = "lumenis_module_info";
inline constexpr const char* kModuleRegisterSymbol = "lumenis_module_register";

} // namespace lumenis::ext
