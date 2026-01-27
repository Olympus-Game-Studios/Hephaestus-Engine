#pragma once
/**
 * RuntimeBootstrap.h
 * 
 * Platform-specific bootstrap utilities for the Hephaestus Engine game runtime.
 * Handles:
 * - Working directory setup
 * - DLL search path configuration (Windows)
 * - Dependency validation
 * - Crash handling and minidump generation
 * - Structured startup logging
 */

#include <filesystem>
#include <string>
#include <vector>
#include <functional>

namespace Hephaestus {
namespace Runtime {

/**
 * Startup logging levels
 */
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

/**
 * Initialization stage for tracking startup progress
 */
enum class InitStage {
    Bootstrap,
    Platform,
    Window,
    Renderer,
    Assets,
    Physics,
    Scripting,
    SceneLoad,
    Ready
};

/**
 * Result of dependency validation
 */
struct DependencyCheckResult {
    bool success = false;
    std::vector<std::string> missingRequired;
    std::vector<std::string> missingOptional;
    std::string errorMessage;
};

/**
 * Bootstrap configuration
 */
struct BootstrapConfig {
    bool enableCrashHandler = true;
    bool enableFileLogging = true;
    bool validateDependencies = true;
    std::filesystem::path logDirectory = "logs";
    std::string logFileName = "runtime.log";
    std::string applicationName = "HephaestusGame";
};

/**
 * Core bootstrap class for game runtime initialization
 */
class Bootstrap {
public:
    /**
     * Initialize the runtime bootstrap system.
     * Must be called very early in main(), before any other initialization.
     * 
     * @param config Bootstrap configuration
     * @return true if bootstrap succeeded, false if critical failure
     */
    static bool Initialize(const BootstrapConfig& config = {});

    /**
     * Shutdown the bootstrap system. Call before exit.
     */
    static void Shutdown();

    /**
     * Get the executable directory path.
     */
    static std::filesystem::path GetExecutableDirectory();

    /**
     * Get the resolved runtime root directory.
     * This is where assets, shaders, scripts, etc. are located.
     */
    static std::filesystem::path GetRuntimeRoot();

    /**
     * Set the working directory to the executable directory.
     * Critical for relative path resolution.
     */
    static bool SetWorkingDirectoryToExe();

    /**
     * Configure DLL search directories (Windows-specific).
     * Adds the executable directory and subdirectories to the DLL search path.
     */
    static bool ConfigureDllSearchPaths();

    /**
     * Validate that all required dependencies are present.
     * @param requiredDlls List of required DLL names
     * @param optionalDlls List of optional DLL names
     * @return Validation result
     */
    static DependencyCheckResult ValidateDependencies(
        const std::vector<std::string>& requiredDlls,
        const std::vector<std::string>& optionalDlls = {});

    /**
     * Install crash handler for generating minidumps and crash logs.
     */
    static bool InstallCrashHandler();

    /**
     * Log a message at the specified level.
     */
    static void Log(LogLevel level, const std::string& message);
    static void Log(LogLevel level, InitStage stage, const std::string& message);

    /**
     * Mark initialization stage as started.
     */
    static void BeginStage(InitStage stage);

    /**
     * Mark initialization stage as completed.
     */
    static void EndStage(InitStage stage, bool success = true);

    /**
     * Get current initialization stage.
     */
    static InitStage GetCurrentStage();

    /**
     * Check if running as a packaged game (vs development/editor mode).
     */
    static bool IsPackagedBuild();

    /**
     * Display a fatal error message box (Windows) or console message.
     * @param title Error title
     * @param message Error details
     */
    static void ShowFatalError(const std::string& title, const std::string& message);

private:
    static bool s_Initialized;
    static BootstrapConfig s_Config;
    static InitStage s_CurrentStage;
    static std::filesystem::path s_ExeDirectory;
    static std::filesystem::path s_RuntimeRoot;
    static std::ofstream s_LogFile;

    static std::string StageToString(InitStage stage);
    static std::string LevelToString(LogLevel level);
    static std::string GetTimestamp();
};

/**
 * RAII helper for tracking initialization stages
 */
class StageGuard {
public:
    StageGuard(InitStage stage) : m_Stage(stage), m_Success(false) {
        Bootstrap::BeginStage(stage);
    }
    
    ~StageGuard() {
        Bootstrap::EndStage(m_Stage, m_Success);
    }
    
    void SetSuccess(bool success = true) { m_Success = success; }
    
private:
    InitStage m_Stage;
    bool m_Success;
};

} // namespace Runtime
} // namespace Hephaestus

// Convenience macros for stage tracking
#define HP_RUNTIME_STAGE(stage) \
    Hephaestus::Runtime::StageGuard _stageGuard##__LINE__(Hephaestus::Runtime::InitStage::stage)

#define HP_RUNTIME_STAGE_SUCCESS() \
    _stageGuard##__LINE__.SetSuccess(true)

#define HP_LOG(level, msg) \
    Hephaestus::Runtime::Bootstrap::Log(Hephaestus::Runtime::LogLevel::level, msg)

#define HP_LOG_STAGE(level, stage, msg) \
    Hephaestus::Runtime::Bootstrap::Log(Hephaestus::Runtime::LogLevel::level, \
                                         Hephaestus::Runtime::InitStage::stage, msg)
