// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#include "app/crash_handler.hpp"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define NOMINMAX
// windows.h doit précéder dbghelp.h : ne pas laisser clang-format réordonner.
// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on
#else
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#endif

namespace rigger::app {

namespace {

// Lecture d'une variable d'environnement ("" si absente) : _dupenv_s sous Windows (getenv est déprécié par
// MSVC).
std::string environment(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    std::string out;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) out = value;
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);  // NOLINT
    return value != nullptr ? value : "";
#endif
}

char gBanner[256] = "Rigger";
char gDirectory[1024] = "";  // chemin UTF-8 (ou ANSI) du dossier des rapports, fixé à l'installation

void makeStamp(char* out, std::size_t size) {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::strftime(out, size, "%Y%m%d-%H%M%S", &tm);
}

#if defined(_WIN32)

const char* exceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "access violation";
        case EXCEPTION_STACK_OVERFLOW:
            return "stack overflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "integer division by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "illegal instruction";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            return "array bounds exceeded";
        default:
            return "unhandled exception";
    }
}

void writeReport(const char* cause, EXCEPTION_POINTERS* info) {
    char stamp[32];
    makeStamp(stamp, sizeof stamp);
    char base[1200];
    std::snprintf(base, sizeof base, "%s\\crash-%s", gDirectory, stamp);
    char textPath[1300], dumpPath[1300];
    std::snprintf(textPath, sizeof textPath, "%s.txt", base);
    std::snprintf(dumpPath, sizeof dumpPath, "%s.dmp", base);

    // Minidump (lisible dans Visual Studio / WinDbg).
    HANDLE dump =
        CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal,
                          info != nullptr ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(dump);
    }

    // Rapport texte : cause, pile d'appels (adresses + symboles si disponibles).
    FILE* file = nullptr;
    if (fopen_s(&file, textPath, "w") != 0 || file == nullptr) return;
    std::fprintf(file, "%s crash report\nTime: %s\nCause: %s\n", gBanner, stamp, cause);
    if (info != nullptr) {
        std::fprintf(file, "Exception code: 0x%08lX at address %p\n", info->ExceptionRecord->ExceptionCode,
                     info->ExceptionRecord->ExceptionAddress);
    }
    std::fprintf(file, "Minidump: %s\nStack trace:\n", dumpPath);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    // Pile d'appels : à partir du contexte de l'exception (le point réel du plantage, pas le gestionnaire).
    void* frames[64];
    USHORT count = 0;
#if defined(_M_X64)
    if (info != nullptr) {
        CONTEXT context = *info->ContextRecord;
        STACKFRAME64 frame{};
        frame.AddrPC = {context.Rip, 0, AddrModeFlat};
        frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
        frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
        while (count < 64 &&
               StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                           SymFunctionTableAccess64, SymGetModuleBase64, nullptr) &&
               frame.AddrPC.Offset != 0) {
            frames[count++] = reinterpret_cast<void*>(frame.AddrPC.Offset);
        }
    }
#endif
    if (count == 0) count = CaptureStackBackTrace(0, 64, frames, nullptr);
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
    for (USHORT i = 0; i < count; ++i) {
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), &displacement, symbol)) {
            std::fprintf(file, "  #%u %s + 0x%llx (%p)\n", i, symbol->Name,
                         static_cast<unsigned long long>(displacement), frames[i]);
        } else {
            std::fprintf(file, "  #%u %p\n", i, frames[i]);
        }
    }
    std::fclose(file);
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* info) {
    writeReport(exceptionName(info->ExceptionRecord->ExceptionCode), info);
    return EXCEPTION_EXECUTE_HANDLER;  // termine le processus (code d'erreur non nul)
}

void terminateHandler() {
    writeReport("std::terminate (uncaught C++ exception or noexcept violation)", nullptr);
    std::abort();
}

void installPlatform() {
    SetUnhandledExceptionFilter(unhandledFilter);
    std::set_terminate(terminateHandler);
    _set_abort_behavior(
        0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);  // pas de boîte de dialogue Windows pendant l'abort
    signal(SIGABRT, [](int) {
        writeReport("abort()", nullptr);
        _exit(3);
    });
}

#else  // POSIX

void writeReport(const char* cause) {
    char stamp[32];
    makeStamp(stamp, sizeof stamp);
    char path[1300];
    std::snprintf(path, sizeof path, "%s/crash-%s.txt", gDirectory, stamp);
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    char header[600];
    const int n = std::snprintf(header, sizeof header, "%s crash report\nTime: %s\nCause: %s\nStack trace:\n",
                                gBanner, stamp, cause);
    if (n > 0) (void)!::write(fd, header, static_cast<std::size_t>(n));
    void* frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, fd);
    ::close(fd);
}

void signalHandler(int sig) {
    const char* name = sig == SIGSEGV   ? "segmentation fault"
                       : sig == SIGABRT ? "abort()"
                       : sig == SIGFPE  ? "arithmetic exception"
                       : sig == SIGILL  ? "illegal instruction"
                                        : "fatal signal";
    writeReport(name);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void terminateHandler() {
    writeReport("std::terminate (uncaught C++ exception or noexcept violation)");
    std::abort();
}

void installPlatform() {
    std::set_terminate(terminateHandler);
    for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) std::signal(sig, signalHandler);
#ifdef SIGBUS
    std::signal(SIGBUS, signalHandler);
#endif
}

#endif

}  // namespace

std::filesystem::path crashDirectory() {
    if (const std::string override = environment("RIGGER_CRASH_DIR"); !override.empty()) return override;
    std::filesystem::path base;
#if defined(_WIN32)
    base = environment("APPDATA");
#else
    if (const std::string home = environment("HOME"); !home.empty())
        base = std::filesystem::path(home) / ".local" / "share";
#endif
    if (base.empty()) base = std::filesystem::temp_directory_path();
    return base / "Rigger" / "crashes";
}

std::filesystem::path logDirectory() { return crashDirectory().parent_path() / "logs"; }

void installCrashHandler(const char* banner) {
    std::snprintf(gBanner, sizeof gBanner, "%s", banner != nullptr ? banner : "Rigger");
    const std::filesystem::path dir = crashDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::snprintf(gDirectory, sizeof gDirectory, "%s", dir.string().c_str());
    installPlatform();
}

std::vector<std::filesystem::path> pendingReports() {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(crashDirectory(), ec)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.rfind("crash-", 0) == 0 && entry.path().extension() == ".txt")
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

void markReported(const std::filesystem::path& report) {
    std::error_code ec;
    const auto target = crashDirectory() / "reported";
    std::filesystem::create_directories(target, ec);
    std::filesystem::rename(report, target / report.filename(), ec);
    std::filesystem::path dump = report;
    dump.replace_extension(".dmp");
    if (std::filesystem::exists(dump, ec)) std::filesystem::rename(dump, target / dump.filename(), ec);
}

void triggerTestCrash() {
    volatile int* nothing = nullptr;
    *nothing = 42;  // NOLINT : violation d'accès volontaire
    std::abort();
}

}  // namespace rigger::app
