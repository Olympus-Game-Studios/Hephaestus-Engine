#include "Assets/AssetPathPolicy.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using Hephaestus::AssetPathKind;
using Hephaestus::AssetPathPolicy;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool TestBuiltinAndEmpty() {
    bool ok = true;
    ok &= Check(AssetPathPolicy::ToPortableKey({}, "").empty(), "empty stays empty");
    ok &= Check(AssetPathPolicy::ToPortableKey({}, "builtin://Cube") == "builtin://Cube",
                "builtin passthrough");
    const auto c = AssetPathPolicy::Classify({}, "builtin://Cube");
    ok &= Check(c.kind == AssetPathKind::Builtin && c.isPortable, "builtin classification");
    return ok;
}

bool TestPortablePrefixes() {
    bool ok = true;
    ok &= Check(AssetPathPolicy::IsPortableKey("assets/meshes/A.glb"), "assets portable");
    ok &= Check(AssetPathPolicy::IsPortableKey(".engine/meshes/Capsule.glb"), ".engine portable");
    ok &= Check(AssetPathPolicy::IsPortableKey("scripts/Player.lua"), "scripts portable");
    ok &= Check(AssetPathPolicy::ToPortableKey({}, "assets\\textures\\t.png") == "assets/textures/t.png",
                "backslash normalize");
    return ok;
}

bool TestMarkerRemap() {
    bool ok = true;
    const std::string abs =
        "F:/Hephaestus Projecs/Boomer-Shooter/.engine/meshes/Capsule.glb";
    const std::string portable = AssetPathPolicy::ToPortableKey({}, abs);
    ok &= Check(portable == ".engine/meshes/Capsule.glb",
                "absolute .engine remap without project root: got " + portable);

    const std::string assetsAbs = "D:/Games/MyGame/assets/prefabs/Enemy.hpprefab";
    ok &= Check(AssetPathPolicy::ToPortableKey({}, assetsAbs) == "assets/prefabs/Enemy.hpprefab",
                "absolute assets remap");
    return ok;
}

bool TestProjectRelative(const fs::path& tempRoot) {
    bool ok = true;
    const fs::path project = tempRoot / "ProjA";
    fs::create_directories(project / "assets" / "meshes");
    const fs::path mesh = project / "assets" / "meshes" / "Building.glb";
    {
        std::ofstream out(mesh);
        out << "x";
    }

    const std::string abs = mesh.generic_string();
    const std::string portable = AssetPathPolicy::ToPortableKey(project, abs);
    ok &= Check(portable == "assets/meshes/Building.glb",
                "in-project absolute -> portable: got " + portable);

    const auto cls = AssetPathPolicy::Classify(project, abs);
    ok &= Check(cls.kind == AssetPathKind::AbsoluteInProject, "classify AbsoluteInProject");
    ok &= Check(cls.portableKey == "assets/meshes/Building.glb", "portableKey from classify");
    ok &= Check(!cls.isPortable, "absolute in project not yet portable until rewritten");

    const fs::path resolved = AssetPathPolicy::ResolvePortableKey(project, portable);
    ok &= Check(fs::equivalent(resolved, mesh), "resolve portable key");

    const fs::path engineMesh = project / ".engine" / "meshes" / "Camera.glb";
    const fs::path engineResolved =
        AssetPathPolicy::ResolvePortableKey(project, ".engine/meshes/Camera.glb");
    ok &= Check(engineResolved == engineMesh.lexically_normal(),
                "resolve .engine mesh against project root, not assets/");
    return ok;
}

bool TestExternalAbsolute(const fs::path& tempRoot) {
    bool ok = true;
    const fs::path project = tempRoot / "ProjB";
    fs::create_directories(project / "assets");
    const fs::path external = tempRoot / "Outside" / "source.fbx";
    fs::create_directories(external.parent_path());
    {
        std::ofstream out(external);
        out << "x";
    }

    const std::string abs = external.lexically_normal().generic_string();
    const std::string portable = AssetPathPolicy::ToPortableKey(project, abs);
    ok &= Check(portable.find("..") == std::string::npos || portable == abs ||
                    AssetPathPolicy::NormalizeSeparators(portable) ==
                        AssetPathPolicy::NormalizeSeparators(abs),
                "external stays absolute-ish");
    const auto cls = AssetPathPolicy::Classify(project, abs);
    ok &= Check(cls.kind == AssetPathKind::AbsoluteExternal, "classify AbsoluteExternal");
    ok &= Check(!cls.isPortable, "external not portable");
    return ok;
}

bool TestLegacyFilename() {
    bool ok = true;
    const auto cls = AssetPathPolicy::Classify({}, "Capsule.glb");
    ok &= Check(cls.kind == AssetPathKind::LegacyFilename, "legacy filename kind");
    ok &= Check(!cls.isPortable, "legacy filename not portable");
    ok &= Check(AssetPathPolicy::KeysEqual("Assets/Foo.PNG", "assets/foo.png"), "keys equal");
    return ok;
}

bool TestRelocationRoundTrip(const fs::path& tempRoot) {
    bool ok = true;
    const fs::path projectA = tempRoot / "CloneA";
    const fs::path projectB = tempRoot / "CloneB";
    fs::create_directories(projectA / "assets" / "textures");
    fs::create_directories(projectB / "assets" / "textures");
    const fs::path texA = projectA / "assets" / "textures" / "albedo.png";
    {
        std::ofstream out(texA);
        out << "png";
    }

    const std::string key =
        AssetPathPolicy::ToPortableKey(projectA, texA.generic_string());
    ok &= Check(key == "assets/textures/albedo.png", "portable key for clone");

    // Simulate moving project: same relative key resolves under new root.
    const fs::path texB = projectB / "assets" / "textures" / "albedo.png";
    {
        std::ofstream out(texB);
        out << "png";
    }
    const fs::path resolvedB = AssetPathPolicy::ResolvePortableKey(projectB, key);
    ok &= Check(fs::equivalent(resolvedB, texB), "relocated project resolves same key");
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: AssetPathPolicyTests <repo_root>\n";
        return 2;
    }

    const fs::path root = fs::path(argv[1]);
    const fs::path outDir = root / "build" / "asset_path_policy_test_out";
    fs::create_directories(outDir);
    fs::remove_all(outDir / "scratch");
    const fs::path scratch = outDir / "scratch";
    fs::create_directories(scratch);

    bool ok = true;
    ok &= TestBuiltinAndEmpty();
    ok &= TestPortablePrefixes();
    ok &= TestMarkerRemap();
    ok &= TestProjectRelative(scratch);
    ok &= TestExternalAbsolute(scratch);
    ok &= TestLegacyFilename();
    ok &= TestRelocationRoundTrip(scratch);

    if (!ok) {
        return 1;
    }

    std::cout << "AssetPathPolicy tests passed.\n";
    return 0;
}
