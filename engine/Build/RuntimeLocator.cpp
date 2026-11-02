#include "Build/RuntimeLocator.h"

#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Hephaestus::Build {
namespace {

bool HasSpirv(const std::filesystem::path& directory) {
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return false;
    }
    for (std::filesystem::recursive_directory_iterator it(directory, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            return false;
        }
        if (it->is_regular_file(ec) && it->path().extension() == ".spv") {
            return true;
        }
    }
    return false;
}

void AddUnique(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    const auto normalized = path.lexically_normal();
    if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) {
        paths.push_back(normalized);
    }
}

EditorProblem Missing(std::string code, std::string message, std::string detail) {
    return {ProblemSeverity::Error, std::move(code), std::move(message), std::move(detail)};
}

std::filesystem::path FindLua(const std::filesystem::path& root) {
    std::error_code ec;
    for (const char* name : {"lua54.dll", "lua.dll"}) {
        const auto candidate = root / name;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

RuntimeArtifacts ProbeRuntimeRoot(const std::filesystem::path& root) {
    RuntimeArtifacts artifacts;
    std::error_code ec;
    const auto executable = root / kPlayerExecutable;
    if (std::filesystem::is_regular_file(executable, ec)) {
        artifacts.executable = executable;
    }
    if (HasSpirv(root / "shaders")) {
        artifacts.shadersDirectory = root / "shaders";
    }
    artifacts.luaLibrary = FindLua(root);
    return artifacts;
}

bool HasCMakeExecutable() {
#ifdef _WIN32
    wchar_t resolved[MAX_PATH]{};
    return SearchPathW(nullptr, L"cmake.exe", nullptr, MAX_PATH, resolved, nullptr) > 0;
#else
    return std::system("command -v cmake >/dev/null 2>&1") == 0;
#endif
}

} // namespace

std::filesystem::path RuntimeLocator::discoverEngineRoot(
    const std::filesystem::path& executableDirectory) {
    std::filesystem::path candidate = executableDirectory;
    std::error_code ec;
    for (int depth = 0; depth < 5 && !candidate.empty(); ++depth) {
        if (std::filesystem::is_regular_file(candidate / "CMakeLists.txt", ec)) {
            return candidate;
        }
        const auto parent = candidate.parent_path();
        if (parent == candidate) {
            break;
        }
        candidate = parent;
    }
    return executableDirectory;
}

RuntimeLocationResult RuntimeLocator::locate(const std::filesystem::path& engineRoot,
                                             const std::filesystem::path& searchRoot,
                                             BuildConfiguration configuration,
                                             bool enableRuntimeAutomation) const {
    RuntimeLocationResult result;
    const std::filesystem::path config(
        configuration == BuildConfiguration::Release ? "Release" : "Debug");
    std::vector<std::filesystem::path> roots;
    if (!searchRoot.empty()) {
        AddUnique(roots, searchRoot / config);
        if (configuration == BuildConfiguration::Release) {
            AddUnique(roots, searchRoot);
        }
    }
    std::error_code ec;
    const bool hasSourceTree =
        !engineRoot.empty() &&
        std::filesystem::is_regular_file(engineRoot / "CMakeLists.txt", ec);
    if (hasSourceTree) {
        AddUnique(roots, engineRoot / "build" / config);
        AddUnique(roots, engineRoot / "bin" / config);
        AddUnique(roots, engineRoot / "build");
        AddUnique(roots, engineRoot / "bin");
    }

    for (const auto& root : roots) {
        auto artifacts = ProbeRuntimeRoot(root);
        if (artifacts.complete()) {
            result.artifacts = std::move(artifacts);
            return result;
        }
    }

    if (configuration == BuildConfiguration::Development) {
        std::vector<std::filesystem::path> releaseRoots;
        if (!searchRoot.empty()) {
            AddUnique(releaseRoots, searchRoot / "Release");
            AddUnique(releaseRoots, searchRoot);
        }
        if (hasSourceTree) {
            AddUnique(releaseRoots, engineRoot / "build" / "Release");
            AddUnique(releaseRoots, engineRoot / "bin" / "Release");
        }
        for (const auto& root : releaseRoots) {
            auto artifacts = ProbeRuntimeRoot(root);
            if (artifacts.complete()) {
                result.artifacts = std::move(artifacts);
                result.problems.push_back(
                    {ProblemSeverity::Warning, "BUILD_RUNTIME_RELEASE_FALLBACK",
                     "Development runtime is unavailable; using the Release player",
                     root.generic_string()});
                if (enableRuntimeAutomation) {
                    result.problems.push_back(
                        {ProblemSeverity::Error, "RUNTIME_AUTOMATION_UNAVAILABLE",
                         "Runtime automation was requested but the Development runtime is "
                         "unavailable and the Release player has automation compiled out",
                         "Build the Development (Debug) HephaestusGame target with "
                         "HEPHAESTUS_RUNTIME_AUTOMATION=ON, then package again."});
                }
                return result;
            }
        }
    }

    // Preserve partial diagnostics from the highest-priority location.
    for (const auto& root : roots) {
        auto artifacts = ProbeRuntimeRoot(root);
        if (!artifacts.executable.empty() || !artifacts.shadersDirectory.empty() ||
            !artifacts.luaLibrary.empty()) {
            result.artifacts = std::move(artifacts);
            break;
        }
    }

    if (result.artifacts.executable.empty()) {
        result.problems.push_back(Missing(
            "BUILD_RUNTIME_EXECUTABLE_MISSING", "HephaestusGame.exe was not found",
            hasSourceTree
                ? "Build the HephaestusGame target or provide a complete runtime directory."
                : "This Editor distribution is missing its generic game player."));
    }
    if (result.artifacts.shadersDirectory.empty()) {
        result.problems.push_back(Missing(
            "BUILD_RUNTIME_SHADERS_MISSING", "Compiled runtime shaders were not found",
            hasSourceTree
                ? "Build the Shaders target or provide a shaders directory beside the player."
                : "This Editor distribution is missing compiled shaders beside the player."));
    }
    if (kRuntimeShipsLua && result.artifacts.luaLibrary.empty()) {
        result.problems.push_back(Missing(
            "BUILD_RUNTIME_LUA_MISSING", "Lua runtime library was not found",
            "Expected lua54.dll or lua.dll beside HephaestusGame.exe."));
    }
    return result;
}

CMakeRuntimeBuilder::CMakeRuntimeBuilder(std::filesystem::path buildDirectory)
    : m_buildDirectory(std::move(buildDirectory)) {}

bool CMakeRuntimeBuilder::build(const std::filesystem::path& engineRoot,
                                BuildConfiguration configuration,
                                const std::filesystem::path& applicationIcon,
                                const LogCallback& log,
                                const CancellationCallback& cancelled,
                                bool enableRuntimeAutomation) {
    if (cancelled && cancelled()) {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(engineRoot / "CMakeLists.txt", ec)) {
        if (log) {
            log("Runtime build unavailable: engine source was not found");
        }
        return false;
    }
    if (!HasCMakeExecutable()) {
        if (log) {
            log("Runtime build unavailable: cmake was not found on PATH");
        }
        return false;
    }
    const auto buildDirectory = m_buildDirectory.empty() ? engineRoot / "build" : m_buildDirectory;
    const char* cmakeConfiguration =
        configuration == BuildConfiguration::Release ? "Release" : "Debug";
    if (!applicationIcon.empty() &&
        (applicationIcon.extension() != ".ico" ||
         !std::filesystem::is_regular_file(applicationIcon))) {
            if (log) {
                log("Application icon must be an existing Windows .ico file");
            }
            return false;
    }
    const std::string configureCommand =
        "cmake -S \"" + engineRoot.string() + "\" -B \"" +
        buildDirectory.string() + "\" -DHEPHAESTUS_GAME_ICON=\"" +
        applicationIcon.string() + "\" -DHEPHAESTUS_RUNTIME_AUTOMATION=" +
        (enableRuntimeAutomation ? "ON" : "OFF");
    if (log && !applicationIcon.empty()) {
        log("Configuring project-specific application icon");
    }
    if (std::system(configureCommand.c_str()) != 0) {
        return false;
    }
    const std::string command =
        "cmake --build \"" + buildDirectory.string() + "\" --config " +
        cmakeConfiguration + " --target HephaestusGame";
    if (log) {
        log("Building runtime: " + command);
    }
#ifdef _WIN32
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    SECURITY_ATTRIBUTES pipeSecurity{};
    pipeSecurity.nLength = sizeof(pipeSecurity);
    pipeSecurity.bInheritHandle = TRUE;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (CreatePipe(&outputRead, &outputWrite, &pipeSecurity, 0)) {
        SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdOutput = outputWrite;
        startup.hStdError = outputWrite;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION process{};
    std::string mutableCommand = command;
    if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, engineRoot.string().c_str(),
                        &startup, &process)) {
        if (outputRead) CloseHandle(outputRead);
        if (outputWrite) CloseHandle(outputWrite);
        if (log) {
            log("Failed to start CMake (Windows error " +
                std::to_string(GetLastError()) + ")");
        }
        return false;
    }
    if (outputWrite) {
        CloseHandle(outputWrite);
        outputWrite = nullptr;
    }
    DWORD exitCode = 1;
    for (;;) {
        if (outputRead) {
            DWORD available = 0;
            while (PeekNamedPipe(outputRead, nullptr, 0, nullptr, &available, nullptr) &&
                   available > 0) {
                std::string chunk(std::min<DWORD>(available, 4096), '\0');
                DWORD bytesRead = 0;
                if (!ReadFile(outputRead, chunk.data(),
                              static_cast<DWORD>(chunk.size()), &bytesRead, nullptr) ||
                    bytesRead == 0) {
                    break;
                }
                chunk.resize(bytesRead);
                if (log) log(chunk);
                available -= bytesRead;
            }
        }
        const DWORD waitResult = WaitForSingleObject(process.hProcess, 100);
        if (waitResult == WAIT_OBJECT_0) {
            GetExitCodeProcess(process.hProcess, &exitCode);
            break;
        }
        if (cancelled && cancelled()) {
            TerminateProcess(process.hProcess, 3);
            WaitForSingleObject(process.hProcess, INFINITE);
            exitCode = 3;
            break;
        }
    }
    if (outputRead) {
        std::string chunk(4096, '\0');
        DWORD bytesRead = 0;
        while (ReadFile(outputRead, chunk.data(), static_cast<DWORD>(chunk.size()),
                        &bytesRead, nullptr) && bytesRead > 0) {
            if (log) log(std::string(chunk.data(), bytesRead));
        }
        CloseHandle(outputRead);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode == 0 && !(cancelled && cancelled());
#else
    const int exitCode = std::system(command.c_str());
    return exitCode == 0 && !(cancelled && cancelled());
#endif
}

} // namespace Hephaestus::Build
