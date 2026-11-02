#include "Build/BuildService.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace Hephaestus::Build;

namespace {

void Write(const fs::path& path, const std::string& contents = "fixture") {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << contents;
}

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

class CountingBuilder final : public RuntimeBuilder {
public:
    bool build(const fs::path&, BuildConfiguration, const fs::path&,
               const LogCallback&, const CancellationCallback&,
               bool enableRuntimeAutomation = false) override {
        ++calls;
        lastAutomation = enableRuntimeAutomation;
        return false;
    }

    int calls = 0;
    bool lastAutomation = false;
};

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "hephaestus_build_service_tests";
    fs::remove_all(root);
    const fs::path project = root / "project";
    const fs::path runtime = root / "runtime";
    Write(project / "assets" / "scenes" / "Main.hpscene", "{}");
    Write(runtime / (std::string("HephaestusGame") + kExecutableSuffix));
    Write(runtime / "lua54.dll");
    Write(runtime / "shaders" / "fixture.spv");

    BuildRequest request;
    request.projectRoot = project;
    request.engineRoot = runtime;
    request.runtimeSearchRoot = runtime;
    request.outputRoot = root / "output";
    request.productName = "BuildServiceFixture";
    request.startupScene = "assets/scenes/Main.hpscene";
    request.configuration = BuildConfiguration::Release;
    request.validateFirst = false;

    auto builder = std::make_shared<CountingBuilder>();
    BuildService service(builder);
    auto result = service.execute(request);
    bool ok = true;
    ok &= Check(result.success, "complete prebuilt runtime packages successfully");
    ok &= Check(builder->calls == 0, "prebuilt runtime skips compiler fallback");

    fs::remove(runtime / "shaders" / "fixture.spv");
    request.outputRoot = root / "missing-output";
    result = service.execute(request);
    ok &= Check(!result.success, "incomplete runtime fails packaging");
    ok &= Check(builder->calls == 1, "missing runtime invokes injected fallback once");

    request.outputRoot = root / "output";
    request.action = BuildAction::Clean;
    result = service.execute(request);
    ok &= Check(result.success, "clean action succeeds without runtime discovery");
    ok &= Check(!fs::exists(root / "output" / "BuildServiceFixture"),
                "clean action removes the previous successful package");

    const int callsBeforeAutomation = builder->calls;
    request.action = BuildAction::Package;
    request.configuration = BuildConfiguration::Release;
    request.enableRuntimeAutomation = true;
    request.outputRoot = root / "release-automation";
    result = service.execute(request);
    const bool rejected = !result.success && std::any_of(
        result.problems.begin(), result.problems.end(), [](const EditorProblem& problem) {
            return problem.code == "BUILD_RUNTIME_AUTOMATION_NOT_ALLOWED";
        });
    ok &= Check(rejected, "Release packaging hard-rejects runtime automation");
    ok &= Check(builder->calls == callsBeforeAutomation,
                "rejected Release automation never invokes the runtime builder");

    fs::remove_all(root);
    if (ok) {
        std::cout << "Build service tests passed.\n";
    }
    return ok ? 0 : 1;
}
