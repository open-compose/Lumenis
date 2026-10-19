#include "crash_report.h"

// The crash report installed first thing in main() (app/main.cpp). Windows only: there the process
// would otherwise end silently, which leaves CI and the owner's reports with nothing to go on.
// Symbols come from DbgHelp, searching the executable's folder for cutthulhu.pdb.
#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h.
#include <dbghelp.h>

#include <cstdio>
#include <cstring>

namespace {

// A short name for the common exception codes (0xC0000374 and 0xC0000409 have no named constant
// in every SDK).
const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "division by zero";
    case EXCEPTION_BREAKPOINT: return "breakpoint";
    case 0xC0000374: return "heap corruption";
    case 0xC0000409: return "stack buffer overrun";
    default: return "exception";
    }
}

// The unhandled-exception filter: prints the exception, the faulting address and up to 48 stack
// frames to stderr, then lets Windows carry on as it would have (EXCEPTION_CONTINUE_SEARCH). Only
// the first crash is reported, should a second thread crash meanwhile.
LONG WINAPI report(EXCEPTION_POINTERS* info) {
    static LONG reported = 0;
    if (InterlockedExchange(&reported, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    std::fprintf(stderr, "cutthulhu crashed: 0x%08lX (%s) at %p, thread %lu\n",
                 static_cast<unsigned long>(record->ExceptionCode), exceptionName(record->ExceptionCode),
                 record->ExceptionAddress, static_cast<unsigned long>(GetCurrentThreadId()));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        std::fprintf(stderr, "  %s address %p\n", record->ExceptionInformation[0] == 0 ? "reading" : "writing",
                     reinterpret_cast<void*>(record->ExceptionInformation[1]));
    }
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    // The executable's folder first, for cutthulhu.pdb.
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (char* slash = std::strrchr(exe, '\\')) *slash = '\0';
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    const bool symbols = SymInitialize(process, exe, TRUE) != FALSE;
// StackWalk64 starts from the faulting thread's registers, copied: it changes the context.
#if defined(_M_X64)
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    constexpr DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = context.Pc;
    frame.AddrStack.Offset = context.Sp;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    constexpr DWORD machine = IMAGE_FILE_MACHINE_ARM64;
#endif
#if defined(_M_X64) || defined(_M_ARM64)
    for (int depth = 0; depth < 48; ++depth) {
        if (!StackWalk64(machine, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        // Each frame as module!function +offset (file:line) when symbols are found, else module +offset.
        const DWORD64 address = frame.AddrPC.Offset;
        char moduleName[MAX_PATH] = "?";
        const DWORD64 base = SymGetModuleBase64(process, address);
        if (base) {
            GetModuleFileNameA(reinterpret_cast<HMODULE>(base), moduleName, MAX_PATH);
            if (const char* slash = std::strrchr(moduleName, '\\')) std::memmove(moduleName, slash + 1, std::strlen(slash));
        }
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512] = {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (symbols && SymFromAddr(process, address, &displacement, symbol)) {
            if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
                std::fprintf(stderr, "  #%d %s!%s +0x%llx (%s:%lu)\n", depth, moduleName, symbol->Name,
                             static_cast<unsigned long long>(displacement), line.FileName,
                             static_cast<unsigned long>(line.LineNumber));
            } else {
                std::fprintf(stderr, "  #%d %s!%s +0x%llx\n", depth, moduleName, symbol->Name,
                             static_cast<unsigned long long>(displacement));
            }
        } else {
            std::fprintf(stderr, "  #%d %s +0x%llx\n", depth, moduleName,
                         static_cast<unsigned long long>(base ? address - base : address));
        }
    }
#endif
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void installCrashReport() {
    SetUnhandledExceptionFilter(report);
}

#else

// Elsewhere the system's crash reporter has the stack.
void installCrashReport() {}

#endif
