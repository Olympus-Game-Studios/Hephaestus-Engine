#pragma once
/**
 * Early file logger for Hephaestus Engine.
 *
 * Writes hephaestus.log next to the executable and mirrors stdout/stderr
 * into the same file so startup hangs (console-only, no window) can be
 * diagnosed from disk even if the Qt window never appears.
 */

#include <cstddef>
#include <string>
#include <string_view>

namespace Hephaestus {

enum class EngineLogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

class EngineLog {
public:
    /**
     * Open hephaestus.log, tee stdout/stderr into it, and dump environment.
     * Must be called before QApplication (or any other heavy init).
     */
    static bool Initialize(const char* applicationName, int argc, char** argv);

    static void Shutdown();
    static bool IsInitialized();
    static std::string LogFilePath();

    static void Write(EngineLogLevel level, std::string_view message);
    static void Debug(std::string_view message);
    static void Info(std::string_view message);
    static void Warn(std::string_view message);
    static void Error(std::string_view message);
    static void Fatal(std::string_view message);

    /** Append raw bytes from the stdout/stderr tee. */
    static void AppendRaw(const char* data, std::size_t size);

private:
    static void LogEnvironment(int argc, char** argv);
    static void InstallCrashHandlers();
    static std::string LevelToString(EngineLogLevel level);
    static std::string Timestamp();
};

} // namespace Hephaestus
