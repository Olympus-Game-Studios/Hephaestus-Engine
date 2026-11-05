/**
 * RuntimeBootstrap.cpp
 * 
 * Implementation of platform-specific bootstrap utilities for the
 * Hephaestus Engine game runtime.
 */

#include "RuntimeBootstrap.h"
#include "AppPaths.h"
#include "EngineLog.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#pragma comment(lib, "dbghelp.lib")
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <signal.h>
#include <execinfo.h>
#endif

namespace Hephaestus {
namespace Runtime {

// Static member definitions
bool Bootstrap::s_Initialized = false;
BootstrapConfig Bootstrap::s_Config;
InitStage Bootstrap::s_CurrentStage = InitStage::Bootstrap;
std::filesystem::path Bootstrap::s_ExeDirectory;
std::filesystem::path Bootstrap::s_RuntimeRoot;
std::ofstream Bootstrap::s_LogFile;

#ifdef _WIN32
// Windows crash handler
static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionInfo);
static LPTOP_LEVEL_EXCEPTION_FILTER s_PreviousExceptionFilter = nullptr;
static std::filesystem::path s_CrashDirectory;

static std::filesystem::path ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        DWORD result = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
        if (result == 0) {
            return {};
        }
        if (result < buffer.size() - 1) {
            buffer.resize(result);
            return std::filesystem::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
        if (buffer.size() > 32768) {
            return {};
        }
    }
}
#endif

bool Bootstrap::Initialize(const BootstrapConfig& config) {
    if (s_Initialized) {
        return true;
    }

    s_Config = config;
    s_CurrentStage = InitStage::Bootstrap;

    // Get executable directory first
    s_ExeDirectory = GetExecutableDirectory();
    if (s_ExeDirectory.empty()) {
        std::cerr << "[Bootstrap] FATAL: Could not determine executable directory" << std::endl;
        return false;
    }

    // Determine runtime root (Contents/Resources inside a macOS .app)
    s_RuntimeRoot = ResourceRootFor(s_ExeDirectory);

    // Set working directory to the runtime root
    if (!SetWorkingDirectoryToExe()) {
        std::cerr << "[Bootstrap] WARNING: Could not set working directory to exe location" << std::endl;
        // Continue anyway, but log the warning
    }

#ifdef _WIN32
    const std::filesystem::path localData = LocalAppDataFor(ExecutablePath().stem());
    s_CrashDirectory = localData.empty() ? s_RuntimeRoot / "crashes" : localData / "Crashes";
#else
    const std::filesystem::path localData;
#endif

    // Initialize logging
    if (config.enableFileLogging) {
        std::filesystem::path logDir = localData.empty()
            ? LogRootFor(s_ExeDirectory) / config.logDirectory
            : localData / "Logs";
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        
        std::filesystem::path logPath = logDir / config.logFileName;
        s_LogFile.open(logPath, std::ios::out | std::ios::trunc);
        
        if (s_LogFile.is_open()) {
            Log(LogLevel::Info, "========================================");
            Log(LogLevel::Info, config.applicationName + " Runtime Bootstrap");
            Log(LogLevel::Info, "========================================");
            Log(LogLevel::Info, "Executable directory: " + s_ExeDirectory.string());
            Log(LogLevel::Info, "Runtime root: " + s_RuntimeRoot.string());
            Log(LogLevel::Info, "Working directory: " + std::filesystem::current_path().string());
        } else {
            std::cerr << "[Bootstrap] WARNING: Could not open log file: " << logPath << std::endl;
        }
    }

    // Configure DLL search paths (Windows)
    if (!ConfigureDllSearchPaths()) {
        Log(LogLevel::Warning, "Could not configure DLL search paths");
    }

    // Install crash handler
    if (config.enableCrashHandler) {
        if (InstallCrashHandler()) {
            Log(LogLevel::Info, "Crash handler installed successfully");
        } else {
            Log(LogLevel::Warning, "Could not install crash handler");
        }
    }

    s_Initialized = true;
    Log(LogLevel::Info, "Bootstrap initialization complete");
    
    return true;
}

void Bootstrap::Shutdown() {
    if (!s_Initialized) {
        return;
    }

    Log(LogLevel::Info, "Bootstrap shutdown");
    
    if (s_LogFile.is_open()) {
        s_LogFile.close();
    }

#ifdef _WIN32
    if (s_PreviousExceptionFilter) {
        SetUnhandledExceptionFilter(s_PreviousExceptionFilter);
        s_PreviousExceptionFilter = nullptr;
    }
#endif

    s_Initialized = false;
}

std::filesystem::path Bootstrap::GetExecutableDirectory() {
#ifdef _WIN32
    return ExecutablePath().parent_path();
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        std::filesystem::path exePath(buffer);
        std::error_code ec;
        exePath = std::filesystem::canonical(exePath, ec);
        if (!ec) {
            return exePath.parent_path();
        }
    }
    return {};
#else
    // Linux: read /proc/self/exe
    std::error_code ec;
    auto exePath = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return exePath.parent_path();
    }
    return {};
#endif
}

std::filesystem::path Bootstrap::GetRuntimeRoot() {
    return s_RuntimeRoot;
}

bool Bootstrap::SetWorkingDirectoryToExe() {
    if (s_RuntimeRoot.empty()) {
        return false;
    }
    
    std::error_code ec;
    std::filesystem::current_path(s_RuntimeRoot, ec);
    return !ec;
}

bool Bootstrap::ConfigureDllSearchPaths() {
#ifdef _WIN32
    // Use SetDefaultDllDirectories to restrict DLL search to safe locations
    // LOAD_LIBRARY_SEARCH_APPLICATION_DIR: Application's directory
    // LOAD_LIBRARY_SEARCH_SYSTEM32: System32 directory
    // LOAD_LIBRARY_SEARCH_USER_DIRS: Directories added with AddDllDirectory
    
    typedef BOOL(WINAPI* SetDefaultDllDirectoriesFunc)(DWORD);
    typedef DLL_DIRECTORY_COOKIE(WINAPI* AddDllDirectoryFunc)(PCWSTR);
    
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32) {
        return false;
    }

    auto setDefaultDllDirs = reinterpret_cast<SetDefaultDllDirectoriesFunc>(
        GetProcAddress(kernel32, "SetDefaultDllDirectories"));
    auto addDllDir = reinterpret_cast<AddDllDirectoryFunc>(
        GetProcAddress(kernel32, "AddDllDirectory"));

    if (setDefaultDllDirs && addDllDir) {
        // Set default search flags
        DWORD searchFlags = LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                           LOAD_LIBRARY_SEARCH_SYSTEM32 |
                           LOAD_LIBRARY_SEARCH_USER_DIRS;
        
        if (!setDefaultDllDirs(searchFlags)) {
            Log(LogLevel::Warning, "SetDefaultDllDirectories failed: " + 
                std::to_string(GetLastError()));
        }

        // Add executable directory explicitly
        std::wstring exeDirW = s_ExeDirectory.wstring();
        DLL_DIRECTORY_COOKIE cookie = addDllDir(exeDirW.c_str());
        if (!cookie) {
            Log(LogLevel::Warning, "AddDllDirectory failed for exe directory: " +
                std::to_string(GetLastError()));
        }

        // Add bin/ subdirectory if it exists
        std::filesystem::path binDir = s_ExeDirectory / "bin";
        if (std::filesystem::exists(binDir)) {
            std::wstring binDirW = binDir.wstring();
            addDllDir(binDirW.c_str());
        }

        Log(LogLevel::Info, "DLL search paths configured successfully");
        return true;
    } else {
        // Fallback for older Windows versions: use SetDllDirectory
        std::wstring exeDirW = s_ExeDirectory.wstring();
        if (SetDllDirectoryW(exeDirW.c_str())) {
            Log(LogLevel::Info, "DLL directory set to: " + s_ExeDirectory.string());
            return true;
        }
    }
    
    return false;
#else
    // On Unix-like systems, LD_LIBRARY_PATH / DYLD_LIBRARY_PATH 
    // should be set before launch, or use rpath
    return true;
#endif
}

DependencyCheckResult Bootstrap::ValidateDependencies(
    const std::vector<std::string>& requiredDlls,
    const std::vector<std::string>& optionalDlls) {
    
    DependencyCheckResult result;
    result.success = true;

#ifdef _WIN32
    auto checkDll = [](const std::string& dllName) -> bool {
        // First check if it's in the exe directory
        std::filesystem::path dllPath = s_ExeDirectory / dllName;
        if (std::filesystem::exists(dllPath)) {
            return true;
        }
        
        // Try to load from system paths
        HMODULE hMod = LoadLibraryExA(dllName.c_str(), nullptr, 
                                       LOAD_LIBRARY_AS_DATAFILE);
        if (hMod) {
            FreeLibrary(hMod);
            return true;
        }
        return false;
    };

    for (const auto& dll : requiredDlls) {
        if (!checkDll(dll)) {
            result.missingRequired.push_back(dll);
            result.success = false;
            Log(LogLevel::Error, "Missing required dependency: " + dll);
        } else {
            Log(LogLevel::Debug, "Found dependency: " + dll);
        }
    }

    for (const auto& dll : optionalDlls) {
        if (!checkDll(dll)) {
            result.missingOptional.push_back(dll);
            Log(LogLevel::Warning, "Missing optional dependency: " + dll);
        } else {
            Log(LogLevel::Debug, "Found optional dependency: " + dll);
        }
    }

    if (!result.success) {
        std::ostringstream oss;
        oss << "Missing required dependencies:\n";
        for (const auto& dll : result.missingRequired) {
            oss << "  - " << dll << "\n";
        }
        oss << "\nPlease ensure all required DLLs are in the game directory.";
        result.errorMessage = oss.str();
    }
#else
    // On Unix, shared libraries are typically handled by the linker
    result.success = true;
#endif

    return result;
}

#ifdef _WIN32
static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionInfo) {
    // Generate timestamp for crash files
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);
    
    std::ostringstream timestamp;
    timestamp << std::put_time(&tm, "%Y%m%d_%H%M%S");
    
    const std::filesystem::path& crashDir = s_CrashDirectory;
    std::error_code ec;
    std::filesystem::create_directories(crashDir, ec);
    
    // Write minidump
    std::filesystem::path dumpPathFs = crashDir / ("crash_" + timestamp.str() + ".dmp");
    std::wstring dumpPath = dumpPathFs.wstring();
    HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = exceptionInfo;
        mei.ClientPointers = FALSE;
        
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                         MiniDumpWithDataSegs, &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }
    
    // Write crash log
    std::filesystem::path logPath = crashDir / ("crash_" + timestamp.str() + ".txt");
    std::ofstream crashLog(logPath);
    if (crashLog.is_open()) {
        crashLog << "Hephaestus Engine Crash Report\n";
        crashLog << "==============================\n\n";
        crashLog << "Time: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n";
        crashLog << "Exception Code: 0x" << std::hex << exceptionInfo->ExceptionRecord->ExceptionCode << std::dec << "\n";
        crashLog << "Exception Address: 0x" << std::hex << reinterpret_cast<uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionAddress) << std::dec << "\n";
        
        // Exception code descriptions
        const char* exceptionName = "Unknown";
        switch (exceptionInfo->ExceptionRecord->ExceptionCode) {
            case EXCEPTION_ACCESS_VIOLATION: exceptionName = "ACCESS_VIOLATION"; break;
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: exceptionName = "ARRAY_BOUNDS_EXCEEDED"; break;
            case EXCEPTION_BREAKPOINT: exceptionName = "BREAKPOINT"; break;
            case EXCEPTION_DATATYPE_MISALIGNMENT: exceptionName = "DATATYPE_MISALIGNMENT"; break;
            case EXCEPTION_FLT_DENORMAL_OPERAND: exceptionName = "FLT_DENORMAL_OPERAND"; break;
            case EXCEPTION_FLT_DIVIDE_BY_ZERO: exceptionName = "FLT_DIVIDE_BY_ZERO"; break;
            case EXCEPTION_FLT_INEXACT_RESULT: exceptionName = "FLT_INEXACT_RESULT"; break;
            case EXCEPTION_FLT_INVALID_OPERATION: exceptionName = "FLT_INVALID_OPERATION"; break;
            case EXCEPTION_FLT_OVERFLOW: exceptionName = "FLT_OVERFLOW"; break;
            case EXCEPTION_FLT_STACK_CHECK: exceptionName = "FLT_STACK_CHECK"; break;
            case EXCEPTION_FLT_UNDERFLOW: exceptionName = "FLT_UNDERFLOW"; break;
            case EXCEPTION_ILLEGAL_INSTRUCTION: exceptionName = "ILLEGAL_INSTRUCTION"; break;
            case EXCEPTION_IN_PAGE_ERROR: exceptionName = "IN_PAGE_ERROR"; break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO: exceptionName = "INT_DIVIDE_BY_ZERO"; break;
            case EXCEPTION_INT_OVERFLOW: exceptionName = "INT_OVERFLOW"; break;
            case EXCEPTION_INVALID_DISPOSITION: exceptionName = "INVALID_DISPOSITION"; break;
            case EXCEPTION_NONCONTINUABLE_EXCEPTION: exceptionName = "NONCONTINUABLE_EXCEPTION"; break;
            case EXCEPTION_PRIV_INSTRUCTION: exceptionName = "PRIV_INSTRUCTION"; break;
            case EXCEPTION_SINGLE_STEP: exceptionName = "SINGLE_STEP"; break;
            case EXCEPTION_STACK_OVERFLOW: exceptionName = "STACK_OVERFLOW"; break;
        }
        crashLog << "Exception Type: " << exceptionName << "\n\n";
        
        // Register dump
        crashLog << "Registers:\n";
        CONTEXT* ctx = exceptionInfo->ContextRecord;
#ifdef _M_X64
        crashLog << "  RAX: 0x" << std::hex << ctx->Rax << "\n";
        crashLog << "  RBX: 0x" << ctx->Rbx << "\n";
        crashLog << "  RCX: 0x" << ctx->Rcx << "\n";
        crashLog << "  RDX: 0x" << ctx->Rdx << "\n";
        crashLog << "  RSI: 0x" << ctx->Rsi << "\n";
        crashLog << "  RDI: 0x" << ctx->Rdi << "\n";
        crashLog << "  RSP: 0x" << ctx->Rsp << "\n";
        crashLog << "  RBP: 0x" << ctx->Rbp << "\n";
        crashLog << "  RIP: 0x" << ctx->Rip << "\n";
#else
        crashLog << "  EAX: 0x" << std::hex << ctx->Eax << "\n";
        crashLog << "  EBX: 0x" << ctx->Ebx << "\n";
        crashLog << "  ECX: 0x" << ctx->Ecx << "\n";
        crashLog << "  EDX: 0x" << ctx->Edx << "\n";
        crashLog << "  ESI: 0x" << ctx->Esi << "\n";
        crashLog << "  EDI: 0x" << ctx->Edi << "\n";
        crashLog << "  ESP: 0x" << ctx->Esp << "\n";
        crashLog << "  EBP: 0x" << ctx->Ebp << "\n";
        crashLog << "  EIP: 0x" << ctx->Eip << "\n";
#endif
        crashLog << std::dec << "\n";
        
        crashLog << "Current initialization stage: " << 
            static_cast<int>(Bootstrap::GetCurrentStage()) << "\n";
        crashLog << "\nA minidump file has been created: " << dumpPathFs.filename().string() << "\n";
        crashLog.close();
    }
    
    // Show error message
    std::wstring msg = L"The game has crashed unexpectedly.\n\n";
    msg += L"Exception: ";
    msg += std::to_wstring(exceptionInfo->ExceptionRecord->ExceptionCode);
    msg += L"\n\nA crash report has been saved to:\n";
    msg += crashDir.wstring();
    msg += L"\n\nPlease report this issue with the crash files.";
    
    MessageBoxW(nullptr, msg.c_str(), L"Hephaestus Game - Fatal Error", 
                MB_OK | MB_ICONERROR);
    
    // Call previous handler if any
    if (s_PreviousExceptionFilter) {
        return s_PreviousExceptionFilter(exceptionInfo);
    }
    
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

bool Bootstrap::InstallCrashHandler() {
#ifdef _WIN32
    s_PreviousExceptionFilter = SetUnhandledExceptionFilter(UnhandledExceptionHandler);
    return true;
#elif defined(__APPLE__) || defined(__linux__)
    // Basic signal handler for Unix
    // TODO: Implement proper stack trace capture
    return true;
#else
    return false;
#endif
}

Hephaestus::EngineLogLevel ToEngineLogLevel(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return Hephaestus::EngineLogLevel::Debug;
    case LogLevel::Warning: return Hephaestus::EngineLogLevel::Warning;
    case LogLevel::Error: return Hephaestus::EngineLogLevel::Error;
    case LogLevel::Fatal: return Hephaestus::EngineLogLevel::Fatal;
    case LogLevel::Info: return Hephaestus::EngineLogLevel::Info;
    }
    return Hephaestus::EngineLogLevel::Info;
}

void Bootstrap::Log(LogLevel level, const std::string& message) {
    std::string timestamp = GetTimestamp();
    std::string levelStr = LevelToString(level);
    
    std::ostringstream oss;
    oss << "[" << timestamp << "] [" << levelStr << "] " << message;
    std::string formatted = oss.str();

    Hephaestus::EngineLog::RecordOnly(ToEngineLogLevel(level), message, "Runtime");
    
    // Console output
    std::ostream& out = (level >= LogLevel::Error) ? std::cerr : std::cout;
    out << formatted << std::endl;
    
    // File output
    if (s_LogFile.is_open()) {
        s_LogFile << formatted << std::endl;
        s_LogFile.flush();
    }
}

void Bootstrap::Log(LogLevel level, InitStage stage, const std::string& message) {
    std::string stageStr = StageToString(stage);
    Log(level, "[" + stageStr + "] " + message);
}

void Bootstrap::BeginStage(InitStage stage) {
    s_CurrentStage = stage;
    Log(LogLevel::Info, stage, "Starting...");
}

void Bootstrap::EndStage(InitStage stage, bool success) {
    if (success) {
        Log(LogLevel::Info, stage, "Completed successfully");
    } else {
        Log(LogLevel::Error, stage, "Failed!");
    }
}

InitStage Bootstrap::GetCurrentStage() {
    return s_CurrentStage;
}

bool Bootstrap::IsPackagedBuild() {
    // Check for markers that indicate a packaged build
    std::filesystem::path exeDir = ResourceRootFor(GetExecutableDirectory());
    
    // Packaged builds have these markers
    if (std::filesystem::exists(exeDir / "game.ini") ||
        std::filesystem::exists(exeDir / "export_manifest.txt") ||
        std::filesystem::exists(exeDir / "PACKAGE_MANIFEST.txt") ||
        std::filesystem::exists(exeDir / "RunGame.bat")) {
        return true;
    }
    
    // Check for assets but no editor folder (runtime package)
    bool hasAssets = std::filesystem::exists(exeDir / "assets");
    bool hasEditor = std::filesystem::exists(exeDir / "editor");
    bool hasEngine = std::filesystem::exists(exeDir / "engine"); // Source code folder
    
    // Packaged build has assets but no source/editor
    if (hasAssets && !hasEditor && !hasEngine) {
        return true;
    }
    
    return false;
}

void Bootstrap::ShowFatalError(const std::string& title, const std::string& message) {
    Log(LogLevel::Fatal, title + ": " + message);
    
#ifdef _WIN32
    std::wstring wTitle(title.begin(), title.end());
    std::wstring wMessage(message.begin(), message.end());
    MessageBoxW(nullptr, wMessage.c_str(), wTitle.c_str(), MB_OK | MB_ICONERROR);
#else
    std::cerr << "\n=== FATAL ERROR ===\n";
    std::cerr << title << "\n";
    std::cerr << message << "\n";
    std::cerr << "===================\n";
#endif
}

std::string Bootstrap::StageToString(InitStage stage) {
    switch (stage) {
        case InitStage::Bootstrap: return "Bootstrap";
        case InitStage::Platform: return "Platform";
        case InitStage::Window: return "Window";
        case InitStage::Renderer: return "Renderer";
        case InitStage::Assets: return "Assets";
        case InitStage::Physics: return "Physics";
        case InitStage::Scripting: return "Scripting";
        case InitStage::SceneLoad: return "SceneLoad";
        case InitStage::Ready: return "Ready";
        default: return "Unknown";
    }
}

std::string Bootstrap::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default: return "???";
    }
}

std::string Bootstrap::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace Runtime
} // namespace Hephaestus
