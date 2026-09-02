#include "Build/BuildService.h"

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
               const LogCallback&, const CancellationCallback&) override {
        ++calls;
        return false;
    }

    int calls = 0;
};

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "hephaestus_build_service_tests";
    fs::remove_all(root);
    const fs::path project = root / "project";
    const fs::path runtime = root / "runtime";
    Write(project / "assets" / "scenes" / "Main.hpscene", "{}");
    Write(runtime / "HephaestusGame.exe");
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

    fs::remove(runtime / "lua54.dll");
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

    fs::remove_all(root);
    if (ok) {
        std::cout << "Build service tests passed.\n";
    }
    return ok ? 0 : 1;
}
