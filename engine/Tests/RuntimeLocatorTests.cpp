#include "Build/RuntimeLocator.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace Hephaestus::Build;

namespace {

void Write(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << "fixture";
}

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

bool HasProblem(const RuntimeLocationResult& result, const std::string& code) {
    for (const auto& problem : result.problems) {
        if (problem.code == code) {
            return true;
        }
    }
    return false;
}

void WriteRuntime(const fs::path& root, const char* luaName = "lua54.dll") {
    Write(root / kPlayerExecutable);
    Write(root / luaName);
    Write(root / "shaders" / "fixture.spv");
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "hephaestus_runtime_locator_tests";
    fs::remove_all(root);
    bool ok = true;
    RuntimeLocator locator;

    const fs::path distribution = root / "distribution";
    WriteRuntime(distribution, "lua.dll");
    auto located = locator.locate(distribution, distribution, BuildConfiguration::Release);
    ok &= Check(located.success(), "distributed Release runtime is located beside the tools");
    ok &= Check(!kRuntimeShipsLua || located.artifacts.luaLibrary.filename() == "lua.dll",
                "legacy supported Lua DLL name is accepted");
    ok &= Check(RuntimeLocator::discoverEngineRoot(distribution) == distribution,
                "distribution root does not escape into arbitrary parents");

    located = locator.locate(distribution, distribution, BuildConfiguration::Development);
    ok &= Check(located.success(), "Release player can satisfy a Development package");
    ok &= Check(HasProblem(located, "BUILD_RUNTIME_RELEASE_FALLBACK"),
                "Development Release fallback is reported");

    const fs::path developer = root / "developer";
    Write(developer / "CMakeLists.txt");
    WriteRuntime(developer / "build" / "Debug");
    located = locator.locate(developer, developer / "build" / "Release",
                             BuildConfiguration::Development);
    ok &= Check(located.success(), "developer Debug runtime is found in the build tree");
    ok &= Check(located.artifacts.executable == developer / "build" / "Debug" / kPlayerExecutable,
                "Development prefers the Debug player");
    ok &= Check(RuntimeLocator::discoverEngineRoot(developer / "build" / "Release") == developer,
                "developer source root is discovered from a build output");

    ok &= Check(RuntimeBuildDirectory(developer, true) == developer / "build-automation" &&
                    RuntimeBuildDirectory(developer, false) == developer / "build",
                "automation runtimes build outside the shared build tree");
    WriteRuntime(developer / "build-automation" / "Debug");
    located = locator.locate(developer, developer / "build" / "Release", BuildConfiguration::Development, true);
    ok &= Check(located.artifacts.executable == developer / "build-automation" / "Debug" / kPlayerExecutable,
                "an automation request uses the automation build tree");
    located = locator.locate(developer, developer / "build" / "Release", BuildConfiguration::Development);
    ok &= Check(located.artifacts.executable == developer / "build" / "Debug" / kPlayerExecutable,
                "a normal request never uses the automation build tree");

    const fs::path incomplete = root / "incomplete";
    Write(incomplete / kPlayerExecutable);
    located = locator.locate(incomplete, incomplete, BuildConfiguration::Release);
    ok &= Check(!located.success(), "incomplete distributed runtime is rejected");
    ok &= Check(HasProblem(located, "BUILD_RUNTIME_SHADERS_MISSING"),
                "missing shaders are actionable");
    ok &= Check(!kRuntimeShipsLua || HasProblem(located, "BUILD_RUNTIME_LUA_MISSING"),
                "missing Lua is actionable");

    fs::remove_all(root);
    if (ok) {
        std::cout << "Runtime locator tests passed.\n";
    }
    return ok ? 0 : 1;
}
