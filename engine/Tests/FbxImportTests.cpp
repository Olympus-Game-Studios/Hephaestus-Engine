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

#include <algorithm>
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
using Hephaestus::SampleClip;
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
    ok &= Check(result.meshes.size() == 1, "skinned.fbx has one mesh part");
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

bool TestYBotMultiMesh(const fs::path& repoRoot) {
    const fs::path ybot = repoRoot / "assets" / "Character" / "YBot" / "Y-Bot.fbx";
    std::error_code ec;
    if (!fs::exists(ybot, ec)) {
        std::cout << "[SKIP] Y-Bot.fbx not present\n";
        return true;
    }
    SkeletalImportResult result;
    std::string error;
    SkeletalImportSettings settings;
    settings.fbxSpaceConversion = FbxSpaceConversion::Auto;
    bool ok = Check(SkeletalModelImporter::Load(ybot, settings, result, error),
                    "load Y-Bot.fbx: " + error);
    ok &= Check(result.meshes.size() >= 2, "YBot imports multiple skinned mesh parts");
    ok &= Check(result.skeleton.BoneCount() > 1, "YBot has a canonical skeleton");
    for (const auto& part : result.meshes) {
        ok &= Check(!part.vertices.empty() && !part.indices.empty(),
                    "YBot part has geometry: " + part.name);
    }
    Hephaestus::CharacterInspectResult inspect;
    ok &= Check(CharacterImporter::Inspect(ybot, inspect, error), "inspect YBot: " + error);
    ok &= Check(inspect.canImport, "YBot inspect allows import");
    ok &= Check(inspect.skinnedMeshCount >= 2, "inspect reports 2+ skinned meshes");
    return ok;
}

bool TestYBotAnimationClips(const fs::path& repoRoot) {
    const fs::path ybot = repoRoot / "assets" / "Character" / "YBot" / "Y-Bot.fbx";
    const fs::path walk =
        repoRoot / "assets" / "Character" / "YBot" / "Anims" / "YBot-Walking.fbx";
    std::error_code ec;
    if (!fs::exists(ybot, ec) || !fs::exists(walk, ec)) {
        std::cout << "[SKIP] YBot animation FBX not present\n";
        return true;
    }
    SkeletalImportResult character;
    std::string error;
    SkeletalImportSettings settings;
    settings.fbxSpaceConversion = FbxSpaceConversion::Auto;
    bool ok = Check(SkeletalModelImporter::Load(ybot, settings, character, error),
                    "load Y-Bot.fbx: " + error);
    std::vector<Hephaestus::AnimationClip> clips;
    ok &= Check(SkeletalModelImporter::LoadClips(walk, settings, character.skeleton, clips, error),
                "load YBot-Walking clips: " + error);

    const std::vector<Hephaestus::AnimationClip> importable =
        Hephaestus::CharacterImporter::ImportableClips(clips);
    ok &= Check(clips.size() > 1, "YBot-Walking also carries Mixamo's Take 001");
    ok &= Check(importable.size() == 1 &&
                    Hephaestus::CharacterImporter::IsMixamoTake(importable.front().sourceClip),
                "only the mixamo.com take is importable from YBot-Walking");

    const Hephaestus::AnimationClip* take = nullptr;
    for (const Hephaestus::AnimationClip& clip : clips) {
        if (clip.sourceClip == "mixamo.com" || clip.name == "mixamo.com") {
            take = &clip;
            break;
        }
    }
    if (!take && !clips.empty()) {
        take = &clips.back();
    }
    ok &= Check(take != nullptr && !take->channels.empty(), "walking take has channels");
    if (!take) {
        return ok;
    }

    std::vector<Hephaestus::BoneLocalPose> startPose;
    std::vector<Hephaestus::BoneLocalPose> midPose;
    SampleClip(*take, character.skeleton, take->startTime, startPose);
    const float mid =
        take->startTime + 0.5f * std::max(0.001f, take->endTime - take->startTime);
    SampleClip(*take, character.skeleton, mid, midPose);
    bool moved = false;
    const size_t n = std::min(startPose.size(), midPose.size());
    for (size_t i = 0; i < n; ++i) {
        const auto& a = startPose[i];
        const auto& b = midPose[i];
        if (std::abs(a.rotation.x - b.rotation.x) > 1e-3f ||
            std::abs(a.rotation.y - b.rotation.y) > 1e-3f ||
            std::abs(a.rotation.z - b.rotation.z) > 1e-3f ||
            std::abs(a.translation.x - b.translation.x) > 1e-3f ||
            std::abs(a.translation.y - b.translation.y) > 1e-3f ||
            std::abs(a.translation.z - b.translation.z) > 1e-3f) {
            moved = true;
            break;
        }
    }
    ok &= Check(moved, "YBot walking pose changes across the clip");
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
    ok &= TestYBotMultiMesh(repoRoot);
    ok &= TestYBotAnimationClips(repoRoot);
    if (ok) {
        std::cout << "FBX import tests passed.\n";
        return 0;
    }
    return 1;
}
