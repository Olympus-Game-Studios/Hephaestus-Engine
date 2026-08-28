/**
 * @file EngineLog.cpp
 * @brief File logger that captures startup diagnostics and console output.
 */

#include "EngineLog.h"
#include "AppConfig.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace Hephaestus {
namespace {

class TeeStreambuf : public std::streambuf {
public:
    TeeStreambuf(std::streambuf* primary)
        : m_primary(primary)
    {
    }

protected:
    int overflow(int ch) override
    {
        if (ch == EOF) {
            return !EOF;
        }
        const char c = static_cast<char>(ch);
        if (m_primary && m_primary->sputc(c) == EOF) {
            return EOF;
        }
        EngineLog::AppendRaw(&c, 1);
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        if (n <= 0) {
            return 0;
        }
        if (m_primary) {
            m_primary->sputn(s, n);
        }
        EngineLog::AppendRaw(s, static_cast<std::size_t>(n));
        return n;
    }

    int sync() override
    {
        int result = 0;
        if (m_primary && m_primary->pubsync() != 0) {
            result = -1;
        }
        EngineLog::AppendRaw(nullptr, 0); // flush
        return result;
    }

private:
    std::streambuf* m_primary = nullptr;
};

std::string PathToUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path QueryExecutableDirectory()
{
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        DWORD result = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
        if (result == 0) {
            return {};
        }
        if (result < buffer.size() - 1) {
            buffer.resize(result);
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2);
        if (buffer.size() > 32768) {
            return {};
        }
    }
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        std::error_code ec;
        auto exePath = std::filesystem::canonical(buffer, ec);
        if (!ec) {
            return exePath.parent_path();
        }
    }
    return {};
#else
    std::error_code ec;
    auto exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return exePath.parent_path();
    }
    return {};
#endif
}

bool FileExists(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

#ifdef _WIN32
std::string WideToUtf8(const wchar_t* text)
{
    if (!text || !*text) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
    return out;
}
#endif

} // namespace

namespace {

bool g_Initialized = false;
bool g_ShuttingDown = false;
std::ofstream g_File;
std::filesystem::path g_LogPath;
std::mutex g_Mutex;
std::string g_ApplicationName = "Hephaestus";

std::streambuf* g_OldCout = nullptr;
std::streambuf* g_OldCerr = nullptr;
std::unique_ptr<TeeStreambuf> g_CoutTee;
std::unique_ptr<TeeStreambuf> g_CerrTee;

std::terminate_handler g_PreviousTerminate = nullptr;

#ifdef _WIN32
LPTOP_LEVEL_EXCEPTION_FILTER g_PreviousExceptionFilter = nullptr;
#endif

void FlushFileLocked()
{
    if (g_File.is_open()) {
        g_File.flush();
    }
}

void WriteToOriginalConsole(const std::string& line)
{
    std::streambuf* buf = g_OldCout ? g_OldCout : std::cout.rdbuf();
    if (!buf) {
        return;
    }
    buf->sputn(line.data(), static_cast<std::streamsize>(line.size()));
    buf->sputc('\n');
    buf->pubsync();
}

#ifdef _WIN32
LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* info)
{
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    void* address = info && info->ExceptionRecord
        ? info->ExceptionRecord->ExceptionAddress
        : nullptr;

    std::ostringstream oss;
    oss << "Unhandled SEH exception 0x" << std::hex << code
        << " at 0x" << reinterpret_cast<uintptr_t>(address);
    EngineLog::Fatal(oss.str());
    EngineLog::Shutdown();

    if (g_PreviousExceptionFilter) {
        return g_PreviousExceptionFilter(info);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void TerminateHandler()
{
    EngineLog::Fatal("std::terminate() called (uncaught exception or abort during startup)");
    try {
        if (auto ex = std::current_exception()) {
            std::rethrow_exception(ex);
        }
    } catch (const std::exception& e) {
        EngineLog::Fatal(std::string("Uncaught std::exception: ") + e.what());
    } catch (...) {
        EngineLog::Fatal("Uncaught exception of unknown type");
    }
    EngineLog::Shutdown();
    if (g_PreviousTerminate) {
        g_PreviousTerminate();
    }
    std::_Exit(1);
}

} // namespace

bool EngineLog::Initialize(const char* applicationName, int argc, char** argv)
{
    std::string banner;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_Initialized) {
            return true;
        }

        if (applicationName && *applicationName) {
            g_ApplicationName = applicationName;
        }

        std::filesystem::path exeDir = QueryExecutableDirectory();
        if (exeDir.empty()) {
            std::error_code ec;
            exeDir = std::filesystem::current_path(ec);
        }

        g_LogPath = exeDir / "hephaestus.log";
        g_File.open(g_LogPath, std::ios::out | std::ios::trunc);
        if (!g_File.is_open()) {
            std::cerr << "[EngineLog] Failed to open " << PathToUtf8(g_LogPath) << std::endl;
            return false;
        }

        g_OldCout = std::cout.rdbuf();
        g_OldCerr = std::cerr.rdbuf();
        g_CoutTee = std::make_unique<TeeStreambuf>(g_OldCout);
        g_CerrTee = std::make_unique<TeeStreambuf>(g_OldCerr);
        std::cout.rdbuf(g_CoutTee.get());
        std::cerr.rdbuf(g_CerrTee.get());
        std::cout << std::unitbuf;
        std::cerr << std::unitbuf;

        g_Initialized = true;
        g_ShuttingDown = false;

        banner =
            "========================================\n"
            "  " + g_ApplicationName + "\n"
            "  Log file: " + PathToUtf8(g_LogPath) + "\n"
            "========================================";
        g_File << banner << std::endl;
        FlushFileLocked();
        WriteToOriginalConsole(std::string("[EngineLog] Writing ") + PathToUtf8(g_LogPath));
    }

    InstallCrashHandlers();
    Write(EngineLogLevel::Info, "EngineLog initialized");
    LogEnvironment(argc, argv);
    return true;
}

void EngineLog::Shutdown()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (!g_Initialized || g_ShuttingDown) {
        return;
    }
    g_ShuttingDown = true;

    if (g_File.is_open()) {
        g_File << "[" << Timestamp() << "] [INFO] EngineLog shutdown" << std::endl;
        g_File.flush();
        g_File.close();
    }

    if (g_OldCout) {
        std::cout.rdbuf(g_OldCout);
        g_OldCout = nullptr;
    }
    if (g_OldCerr) {
        std::cerr.rdbuf(g_OldCerr);
        g_OldCerr = nullptr;
    }

    g_CoutTee.reset();
    g_CerrTee.reset();

#ifdef _WIN32
    if (g_PreviousExceptionFilter) {
        SetUnhandledExceptionFilter(g_PreviousExceptionFilter);
        g_PreviousExceptionFilter = nullptr;
    }
#endif

    if (g_PreviousTerminate) {
        std::set_terminate(g_PreviousTerminate);
        g_PreviousTerminate = nullptr;
    }

    g_Initialized = false;
}

bool EngineLog::IsInitialized()
{
    return g_Initialized;
}

std::string EngineLog::LogFilePath()
{
    return PathToUtf8(g_LogPath);
}

void EngineLog::AppendRaw(const char* data, std::size_t size)
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (!g_File.is_open() || g_ShuttingDown) {
        return;
    }
    if (data && size > 0) {
        g_File.write(data, static_cast<std::streamsize>(size));
    }
    g_File.flush();
}

void EngineLog::Write(EngineLogLevel level, std::string_view message)
{
    std::ostringstream oss;
    oss << "[" << Timestamp() << "] [" << LevelToString(level) << "] " << message;
    const std::string line = oss.str();

    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (g_File.is_open()) {
            g_File << line << std::endl;
            g_File.flush();
        }
    }

    WriteToOriginalConsole(line);
}

void EngineLog::Debug(std::string_view message) { Write(EngineLogLevel::Debug, message); }
void EngineLog::Info(std::string_view message) { Write(EngineLogLevel::Info, message); }
void EngineLog::Warn(std::string_view message) { Write(EngineLogLevel::Warning, message); }
void EngineLog::Error(std::string_view message) { Write(EngineLogLevel::Error, message); }
void EngineLog::Fatal(std::string_view message) { Write(EngineLogLevel::Fatal, message); }

void EngineLog::InstallCrashHandlers()
{
    g_PreviousTerminate = std::set_terminate(TerminateHandler);

#ifdef _WIN32
    g_PreviousExceptionFilter = SetUnhandledExceptionFilter(UnhandledExceptionFilter);
#endif
}

void EngineLog::LogEnvironment(int argc, char** argv)
{
    Info("---- Startup diagnostics ----");

#ifdef HEPHAESTUS_VERSION_STRING
    Info(std::string("Version: ") + HEPHAESTUS_VERSION_STRING);
#endif
#ifdef HEPHAESTUS_APP_NAME
    Info(std::string("App: ") + HEPHAESTUS_APP_NAME);
#endif

#ifdef _WIN32
    Info(std::string("PID: ") + std::to_string(GetCurrentProcessId()));
#else
    Info(std::string("PID: ") + std::to_string(::getpid()));
#endif

    Info(std::string("Hardware threads: ") + std::to_string(std::max(1u, std::thread::hardware_concurrency())));

    const std::filesystem::path exeDir = QueryExecutableDirectory();
    Info(std::string("Executable directory: ") + PathToUtf8(exeDir));

    std::error_code cwdEc;
    const auto cwd = std::filesystem::current_path(cwdEc);
    if (!cwdEc) {
        Info(std::string("Working directory: ") + PathToUtf8(cwd));
    } else {
        Warn(std::string("Could not read working directory: ") + cwdEc.message());
    }

    std::ostringstream cmd;
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            cmd << ' ';
        }
        cmd << (argv && argv[i] ? argv[i] : "(null)");
    }
    Info(std::string("Command line: ") + cmd.str());

#ifdef _WIN32
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtlGetVersion) {
            RTL_OSVERSIONINFOW vi{};
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (rtlGetVersion(&vi) == 0) {
                std::ostringstream os;
                os << "Windows " << vi.dwMajorVersion << "." << vi.dwMinorVersion
                   << " build " << vi.dwBuildNumber;
                Info(os.str());
            }
        }
    }

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        std::ostringstream os;
        os << "Physical RAM: " << (mem.ullTotalPhys / (1024ull * 1024ull)) << " MiB ("
           << mem.dwMemoryLoad << "% in use)";
        Info(os.str());
    }

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                             sizeof(pmc))) {
        std::ostringstream os;
        os << "Process working set: " << (pmc.WorkingSetSize / (1024ull * 1024ull)) << " MiB";
        Info(os.str());
    }

    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if (dd.StateFlags & DISPLAY_DEVICE_ACTIVE) {
            std::ostringstream os;
            os << "Display[" << i << "]: " << WideToUtf8(dd.DeviceString);
            if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
                os << " (primary)";
            }
            Info(os.str());
        }
        dd.cb = sizeof(dd);
    }

    const char* qtEnvKeys[] = {
        "QT_PLUGIN_PATH",
        "QT_QPA_PLATFORM_PLUGIN_PATH",
        "QT_QPA_PLATFORM",
        "QT_DEBUG_PLUGINS",
        "QT_OPENGL",
        "QSG_RHI_BACKEND",
        "VK_ICD_FILENAMES",
        "VK_LAYER_PATH",
        "DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1",
        "DISABLE_LAYER_NV_OPTIMUS_1"
    };
    for (const char* key : qtEnvKeys) {
        if (const char* value = std::getenv(key)) {
            Info(std::string(key) + "=" + value);
        }
    }

    const std::filesystem::path filesToCheck[] = {
        exeDir / "Qt6Core.dll",
        exeDir / "Qt6Gui.dll",
        exeDir / "Qt6Widgets.dll",
        exeDir / "platforms" / "qwindows.dll",
        exeDir / "vulkan-1.dll"
    };
    for (const auto& path : filesToCheck) {
        Info(std::string(FileExists(path) ? "Found: " : "Missing: ") + PathToUtf8(path));
    }

    HMODULE vulkan = LoadLibraryW(L"vulkan-1.dll");
    if (vulkan) {
        Info("vulkan-1.dll loaded from the system/DLL search path");
        FreeLibrary(vulkan);
    } else {
        Warn(std::string("LoadLibrary(vulkan-1.dll) failed, GetLastError=") +
             std::to_string(GetLastError()));
    }
#endif

    Info("---- End diagnostics ----");
    Info("If the editor console stays open with no window, the last line above is the hang/crash point.");
}

std::string EngineLog::LevelToString(EngineLogLevel level)
{
    switch (level) {
        case EngineLogLevel::Debug: return "DEBUG";
        case EngineLogLevel::Info: return "INFO";
        case EngineLogLevel::Warning: return "WARN";
        case EngineLogLevel::Error: return "ERROR";
        case EngineLogLevel::Fatal: return "FATAL";
        default: return "???";
    }
}

std::string EngineLog::Timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace Hephaestus
