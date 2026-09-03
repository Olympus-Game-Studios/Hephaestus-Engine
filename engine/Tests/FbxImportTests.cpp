/**
 * @file FbxImportTests.cpp
 * @brief Headless ufbx static/skeletal import, bake, helper folding, and owned mesh.fbx.
 */

#include "Animation/AnimationClip.h"
#include "Animation/Skeleton.h"
#include "Assets/CharacterAsset.h"
#include "Assets/CharacterImport.h"
#include "Assets/FbxDocument.h"
#include "Assets/FbxStaticImporter.h"
#include "Assets/MeshAsset.h"
#include "Assets/ModelImporter.h"
#include "Assets/SkeletalModelImporter.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using Hephaestus::AnimationInterpolation;
using Hephaestus::AnimationPath;
using Hephaestus::CharacterAsset;
using Hephaestus::CharacterImporter;
using Hephaestus::CharacterImportRequest;
using Hephaestus::CharacterSerializer;
using Hephaestus::FbxSpaceConversion;
using Hephaestus::FbxStaticImporter;
using Hephaestus::FbxStaticImportResult;
using Hephaestus::SkeletalImportResult;
using Hephaestus::SkeletalImportSettings;
using Hephaestus::SkeletalModelImporter;
using Hephaestus::SkeletonBone;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

fs::path Fixture(const fs::path& repoRoot, const char* name) {
    return repoRoot / "engine" / "Tests" / "Fixtures" / "Fbx" / name;
}

bool TestMalformed(const fs::path& repoRoot) {
    MeshData mesh;
    std::string error;
    const bool loaded = ModelImporter::LoadFBX(Fixture(repoRoot, "malformed.fbx"), mesh, error);
    bool ok = Check(!loaded, "malformed FBX must fail");
    ok &= Check(!error.empty(), "malformed FBX reports an error");
    return ok;
}

bool TestStaticTriangle(const fs::path& repoRoot) {
    MeshData mesh;
    FbxStaticImportResult meta;
    std::string error;
    bool ok = Check(FbxStaticImporter::Load(Fixture(repoRoot, "triangle.fbx"),
                                            FbxSpaceConversion::ModifyGeometry, mesh, meta, error),
                    "load triangle.fbx: " + error);
    ok &= Check(mesh.vertices.size() >= 3, "triangle has vertices");
    ok &= Check(mesh.indices.size() >= 3, "triangle has indices");
    ok &= Check(!meta.containsSkin, "static triangle is not skinned");
    MeshData viaModel;
    ok &= Check(ModelImporter::LoadModel(Fixture(repoRoot, "triangle.fbx"), viaModel, error),
                "LoadModel triangle.fbx: " + error);
    ok &= Check(viaModel.vertices.size() == mesh.vertices.size(), "LoadFBX matches FbxStaticImporter");
    return ok;
}

const SkeletonBone* FindBone(const Hephaestus::Skeleton& skeleton, const char* name) {
    return skeleton.FindBone(name);
}

bool TestSkinnedRestAndHelpers(const fs::path& repoRoot) {
    SkeletalImportResult result;
    std::string error;
    SkeletalImportSettings settings;
    settings.fbxSpaceConversion = FbxSpaceConversion::ModifyGeometry;
    bool ok = Check(SkeletalModelImporter::Load(Fixture(repoRoot, "skinned.fbx"), settings, result,
                                                error),
                    "load skinned.fbx: " + error);
    ok &= Check(result.importedFromFbx, "importedFromFbx is set");
    ok &= Check(result.skeleton.BoneCount() == 2, "helper Null is not a deforming bone");
    ok &= Check(FindBone(result.skeleton, "Root") != nullptr, "Root bone exists");
    ok &= Check(FindBone(result.skeleton, "Child") != nullptr, "Child bone exists");
    ok &= Check(FindBone(result.skeleton, "Helper") == nullptr, "Helper is folded, not a bone");
    const SkeletonBone* child = FindBone(result.skeleton, "Child");
    if (child) {
        ok &= Check(child->parentEval >= 0, "Child parents to Root after folding");
    }
    return ok;
}

bool TestLinearBake(const fs::path& repoRoot) {
    SkeletalImportResult result;
    std::string error;
    SkeletalImportSettings settings;
    settings.fbxSpaceConversion = FbxSpaceConversion::ModifyGeometry;
    settings.animationSampleRate = 30.0f;
    bool ok = Check(SkeletalModelImporter::Load(Fixture(repoRoot, "skinned.fbx"), settings, result,
                                                error),
                    "load skinned.fbx for bake: " + error);
    ok &= Check(!result.clips.empty(), "baked at least one clip");
    bool foundLinear = false;
    for (const auto& clip : result.clips) {
        for (const auto& channel : clip.channels) {
            if (channel.path == AnimationPath::Translation &&
                channel.interpolation == AnimationInterpolation::Linear &&
                channel.times.size() >= 2) {
                foundLinear = true;
            }
            ok &= Check(channel.interpolation == AnimationInterpolation::Linear,
                        "FBX 1.0 bake is Linear-only");
        }
    }
    ok &= Check(foundLinear, "Child translation bake produced Linear keys");
    return ok;
}

bool TestOwnedMeshFbx(const fs::path& repoRoot) {
    const fs::path project = fs::temp_directory_path() / "hephaestus_fbx_import_owned";
    fs::remove_all(project);
    fs::create_directories(project / "assets" / "characters");
    CharacterImportRequest request;
    request.sourceGlb = Fixture(repoRoot, "skinned.fbx");
    request.projectRoot = project;
    request.destinationFolderAbs = project / "assets" / "characters";
    request.characterName = "FbxHero";
    request.createController = false;
    request.settings.fbxSpaceConversion = FbxSpaceConversion::ModifyGeometry;
    std::string portable;
    std::string error;
    bool ok = Check(CharacterImporter::Import(request, portable, error), "import FBX character: " + error);
    const fs::path owned = project / "assets" / "characters" / ".source" / "FbxHero" / "mesh.fbx";
    ok &= Check(fs::exists(owned), "owned mesh.fbx exists");
    ok &= Check(!fs::exists(project / "assets" / "characters" / ".source" / "FbxHero" / "mesh.glb"),
                "did not write mesh.glb for an FBX source");
    CharacterAsset asset;
    const fs::path characterAbs = project / "assets" / "characters" / "FbxHero.hpcharacter";
    ok &= Check(CharacterSerializer::Deserialize(asset, characterAbs, error),
                "load imported character: " + error);
    ok &= Check(asset.mesh.source.find("mesh.fbx") != std::string::npos, "JSON mesh.source is mesh.fbx");
    ok &= Check(asset.import.fbxSpaceResolvedStored, "resolved FBX space conversion is persisted");

    ok &= Check(CharacterImporter::Reimport(project, portable, Fixture(repoRoot, "skinned.fbx"), error),
                "reimport same FBX: " + error);
    ok &= Check(fs::exists(owned), "reimport keeps mesh.fbx");

    fs::remove_all(project);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: FbxImportTests <repo-root>\n";
        return 1;
    }
    const fs::path repoRoot = argv[1];
    bool ok = true;
    ok &= TestMalformed(repoRoot);
    ok &= TestStaticTriangle(repoRoot);
    ok &= TestSkinnedRestAndHelpers(repoRoot);
    ok &= TestLinearBake(repoRoot);
    ok &= TestOwnedMeshFbx(repoRoot);
    if (ok) {
        std::cout << "FBX import tests passed.\n";
        return 0;
    }
    return 1;
}
