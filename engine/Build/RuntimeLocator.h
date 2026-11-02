#pragma once

#include "Build/BuildTypes.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Hephaestus::Build {

// The generic player, relative to the folder that holds the editor's tools and shaders.
#ifdef __APPLE__
inline constexpr const char* kPlayerExecutable = "HephaestusGame.app/Contents/MacOS/HephaestusGame";
#elif defined(_WIN32)
inline constexpr const char* kPlayerExecutable = "HephaestusGame.exe";
#else
inline constexpr const char* kPlayerExecutable = "HephaestusGame";
#endif

#ifdef _WIN32
inline constexpr const char* kExecutableSuffix = ".exe";
inline constexpr bool kRuntimeShipsLua = true;
#else
// The player links the platform's shared Lua library rather than shipping one beside it.
inline constexpr const char* kExecutableSuffix = "";
inline constexpr bool kRuntimeShipsLua = false;
#endif

struct RuntimeArtifacts {
    std::filesystem::path executable;
    std::filesystem::path shadersDirectory;
    std::filesystem::path luaLibrary;

    bool complete() const {
        return !executable.empty() && !shadersDirectory.empty() &&
               (!kRuntimeShipsLua || !luaLibrary.empty());
    }
};

struct RuntimeLocationResult {
    RuntimeArtifacts artifacts;
    std::vector<EditorProblem> problems;

    bool success() const { return artifacts.complete(); }
};

class RuntimeLocator {
public:
    RuntimeLocationResult locate(const std::filesystem::path& engineRoot,
                                 const std::filesystem::path& searchRoot,
                                 BuildConfiguration configuration,
                                 bool enableRuntimeAutomation = false) const;

    static std::filesystem::path discoverEngineRoot(
        const std::filesystem::path& executableDirectory);
};

class RuntimeBuilder {
public:
    virtual ~RuntimeBuilder() = default;
    virtual bool build(const std::filesystem::path& engineRoot,
                       BuildConfiguration configuration,
                       const std::filesystem::path& applicationIcon,
                       const LogCallback& log,
                       const CancellationCallback& cancelled,
                       bool enableRuntimeAutomation = false) = 0;
};

class CMakeRuntimeBuilder final : public RuntimeBuilder {
public:
    explicit CMakeRuntimeBuilder(std::filesystem::path buildDirectory = {});

    bool build(const std::filesystem::path& engineRoot,
               BuildConfiguration configuration,
               const std::filesystem::path& applicationIcon,
               const LogCallback& log,
               const CancellationCallback& cancelled,
               bool enableRuntimeAutomation = false) override;

private:
    std::filesystem::path m_buildDirectory;
};

} // namespace Hephaestus::Build
