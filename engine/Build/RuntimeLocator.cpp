#include "Build/RuntimeLocator.h"

#include <algorithm>
#include <cstdlib>
#include <set>

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

} // namespace

RuntimeLocationResult RuntimeLocator::locate(const std::filesystem::path& engineRoot,
                                             const std::filesystem::path& searchRoot,
                                             BuildConfiguration configuration) const {
    RuntimeLocationResult result;
    const std::filesystem::path config(
        configuration == BuildConfiguration::Release ? "Release" : "Debug");
    std::vector<std::filesystem::path> roots;
    if (!searchRoot.empty()) {
        AddUnique(roots, searchRoot / config);
        AddUnique(roots, searchRoot);
    }
    if (!engineRoot.empty()) {
        AddUnique(roots, engineRoot / "build" / config);
        AddUnique(roots, engineRoot / "build");
        AddUnique(roots, engineRoot / "bin" / config);
        AddUnique(roots, engineRoot / "bin");
        AddUnique(roots, engineRoot);
    }

    std::error_code ec;
    for (const auto& root : roots) {
        const auto executable = root / "HephaestusGame.exe";
        if (result.artifacts.executable.empty() && std::filesystem::is_regular_file(executable, ec)) {
            result.artifacts.executable = executable;
        }

        const std::filesystem::path shaderCandidates[] = {
            root / "shaders", root / "assets" / "shaders", root.parent_path() / "shaders",
            engineRoot / "shaders", engineRoot / "assets" / "shaders"
        };
        for (const auto& shaders : shaderCandidates) {
            if (result.artifacts.shadersDirectory.empty() && HasSpirv(shaders)) {
                result.artifacts.shadersDirectory = shaders;
            }
        }

        const std::filesystem::path luaCandidates[] = {
            root / "lua54.dll", root / "lua.dll", root.parent_path() / "lua54.dll",
            root.parent_path() / "lua.dll", engineRoot / "extern" / "lua" / "lua54.dll",
            engineRoot / "extern" / "lua" / "lua.dll"
        };
        for (const auto& lua : luaCandidates) {
            if (result.artifacts.luaLibrary.empty() && std::filesystem::is_regular_file(lua, ec)) {
                result.artifacts.luaLibrary = lua;
            }
        }
    }

    if (result.artifacts.executable.empty()) {
        result.problems.push_back(Missing(
            "BUILD_RUNTIME_EXECUTABLE_MISSING", "HephaestusGame.exe was not found",
            "Build the HephaestusGame target or set runtimeSearchRoot to its output directory."));
    }
    if (result.artifacts.shadersDirectory.empty()) {
        result.problems.push_back(Missing(
            "BUILD_RUNTIME_SHADERS_MISSING", "Compiled runtime shaders were not found",
            "A shaders directory containing at least one .spv file is required."));
    }
    if (result.artifacts.luaLibrary.empty()) {
        result.problems.push_back(Missing(
            "BUILD_RUNTIME_LUA_MISSING", "Lua runtime library was not found",
            "Expected lua54.dll or lua.dll beside the runtime or under extern/lua."));
    }
    return result;
}

CMakeRuntimeBuilder::CMakeRuntimeBuilder(std::filesystem::path buildDirectory)
    : m_buildDirectory(std::move(buildDirectory)) {}

bool CMakeRuntimeBuilder::build(const std::filesystem::path& engineRoot,
                                BuildConfiguration configuration,
                                const std::filesystem::path& applicationIcon,
                                const LogCallback& log,
                                const CancellationCallback& cancelled) {
    if (cancelled && cancelled()) {
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
        applicationIcon.string() + "\"";
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
