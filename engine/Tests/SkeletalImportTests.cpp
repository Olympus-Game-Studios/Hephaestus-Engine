/**
 * @file SkeletalImportTests.cpp
 * @brief Stage B golden skeleton import: names, identity vs eval order, world matrices.
 */

#include "Animation/Skeleton.h"
#include "Assets/SkeletalModelImporter.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Hephaestus::kMaxCharacterBones;
using Hephaestus::SkeletalImportResult;
using Hephaestus::SkeletalImportSettings;
using Hephaestus::SkeletalModelImporter;
using Hephaestus::Skeleton;
using Hephaestus::SkeletonBone;
using Hephaestus::SkeletonBuilder;
using Hephaestus::SkeletonCompatibilityReport;

namespace {

constexpr float kPiOver2 = 1.5707963267948966f;
constexpr float kSqrtHalf = 0.7071067811865476f;

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

void AppendBytes(std::vector<uint8_t>& out, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void PadTo4(std::vector<uint8_t>& out, uint8_t pad) {
    while (out.size() % 4 != 0) {
        out.push_back(pad);
    }
}

bool AlmostEqual(float a, float b, float eps = 1e-4f) {
    return std::abs(a - b) <= eps;
}

bool MatricesClose(const Mat4& a, const Mat4& b, float eps = 1e-4f) {
    for (int i = 0; i < 16; ++i) {
        if (!AlmostEqual(a.m[i], b.m[i], eps)) {
            return false;
        }
    }
    return true;
}

std::string Mat4Dump(const Mat4& m) {
    std::string out;
    for (int i = 0; i < 16; ++i) {
        if (i) {
            out += ",";
        }
        out += std::to_string(m.m[i]);
    }
    return out;
}

Mat4 ExpectedRootWorld() {
    return Mat4::RotationX(kPiOver2);
}

Mat4 ExpectedChildLocal() {
    return Mat4::Multiply(Mat4::Translation(Vec3(0.0f, 1.0f, 0.0f)), Mat4::RotationZ(kPiOver2));
}

Mat4 ExpectedChildWorld() {
    return Mat4::Multiply(ExpectedRootWorld(), ExpectedChildLocal());
}

Mat4 ExpectedMeshWorld() {
    return Mat4::Translation(Vec3(0.0f, 0.0f, 2.0f));
}

struct GoldenOptions {
    std::string joints = "[0,1]";
    const char* rootName = "Root";
    const char* childName = "Child";
    bool extraSkin = false;
    bool morphTargets = false;
    bool extraInfluence = false;
    bool identityIbm = false;
    bool nonJointAncestor = false;
};

bool WriteGoldenGlb(const fs::path& path, const GoldenOptions& options) {
    const float positions[] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f,
    };
    const float normals[] = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
    };
    const uint16_t joints0[] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
    };
    const float weights0[] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
    };
    const uint16_t indices[] = {0, 1, 2};
    const uint16_t joints1[] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
    };
    const float weights1[] = {
        0.25f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    const float weights0Five[] = {
        0.40f, 0.25f, 0.15f, 0.12f,
        1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
    };

    Mat4 ibmRoot = Mat4::Multiply(Mat4::Invert(ExpectedRootWorld()), ExpectedMeshWorld());
    Mat4 ibmChild = Mat4::Multiply(Mat4::Invert(ExpectedChildWorld()), ExpectedMeshWorld());
    if (options.identityIbm) {
        ibmRoot = Mat4::Identity();
        ibmChild = Mat4::Identity();
    }

    const bool childFirst = (options.joints == "[1,0]");
    const Mat4 ibm0 = childFirst ? ibmChild : ibmRoot;
    const Mat4 ibm1 = childFirst ? ibmRoot : ibmChild;

    std::vector<uint8_t> bin;
    const uint32_t posOff = 0;
    AppendBytes(bin, positions, sizeof(positions));
    const uint32_t nrmOff = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, normals, sizeof(normals));
    const uint32_t j0Off = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, joints0, sizeof(joints0));
    const uint32_t w0Off = static_cast<uint32_t>(bin.size());
    if (options.extraInfluence) {
        AppendBytes(bin, weights0Five, sizeof(weights0Five));
    } else {
        AppendBytes(bin, weights0, sizeof(weights0));
    }
    const uint32_t idxOff = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, indices, sizeof(indices));
    PadTo4(bin, 0);
    const uint32_t ibmOff = static_cast<uint32_t>(bin.size());
    AppendBytes(bin, ibm0.m, sizeof(ibm0.m));
    AppendBytes(bin, ibm1.m, sizeof(ibm1.m));
    uint32_t j1Off = 0;
    uint32_t w1Off = 0;
    if (options.extraInfluence) {
        j1Off = static_cast<uint32_t>(bin.size());
        AppendBytes(bin, joints1, sizeof(joints1));
        w1Off = static_cast<uint32_t>(bin.size());
        AppendBytes(bin, weights1, sizeof(weights1));
    }
    PadTo4(bin, 0);

    const char* rootNameJson = options.rootName ? options.rootName : "";
    std::string childNode = "{";
    if (options.childName) {
        childNode += std::string("\"name\":\"") + options.childName + "\",";
    }
    childNode += "\"translation\":[0,1,0],\"rotation\":[0,0," + std::to_string(kSqrtHalf) + "," +
                 std::to_string(kSqrtHalf) + "]}";

    std::string nodesJson;
    std::string sceneNodes;
    std::string jointRootName = std::string("\"name\":\"") + rootNameJson + "\",";
    if (options.nonJointAncestor) {
        nodesJson =
            "["
            "{\"name\":\"Armature\",\"rotation\":[" +
            std::to_string(kSqrtHalf) + ",0,0," + std::to_string(kSqrtHalf) +
            "],\"children\":[1]},"
            "{" +
            jointRootName + "\"children\":[2]}," + childNode +
            ","
            "{\"translation\":[0,0,2],\"mesh\":0,\"skin\":0}"
            "]";
        sceneNodes = "[0]";
    } else {
        nodesJson = "["
                    "{" +
                    jointRootName + "\"rotation\":[" + std::to_string(kSqrtHalf) + ",0,0," +
                    std::to_string(kSqrtHalf) + "],\"children\":[1]}," + childNode +
                    ","
                    "{\"translation\":[0,0,2],\"mesh\":0,\"skin\":0}"
                    "]";
        sceneNodes = "[0]";
    }

    std::string jointsJson = options.nonJointAncestor ? "[1,2]" : options.joints;
    if (options.nonJointAncestor) {
        // IBM accessor still Root then Child.
    }

    std::string attributes =
        "{\"POSITION\":0,\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3";
    if (options.extraInfluence) {
        attributes += ",\"JOINTS_1\":6,\"WEIGHTS_1\":7";
    }
    attributes += "}";

    std::string primitive = "{\"attributes\":" + attributes + ",\"indices\":4";
    if (options.morphTargets) {
        primitive += ",\"targets\":[{\"POSITION\":0}]";
    }
    primitive += "}";

    std::string skins = "[{\"name\":\"Armature\",\"joints\":" + jointsJson +
                        ",\"inverseBindMatrices\":5}";
    if (options.extraSkin) {
        skins += ",{\"joints\":" + jointsJson + ",\"inverseBindMatrices\":5}";
    }
    skins += "]";

    std::string accessors =
        "["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
        "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
        "{\"bufferView\":4,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
        "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"}";
    if (options.extraInfluence) {
        accessors +=
            ","
            "{\"bufferView\":6,\"componentType\":5123,\"count\":3,\"type\":\"VEC4\"},"
            "{\"bufferView\":7,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"}";
    }
    accessors += "]";

    std::string bufferViews =
        "["
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(posOff) + ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(nrmOff) + ",\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(j0Off) + ",\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(w0Off) + ",\"byteLength\":48},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(idxOff) + ",\"byteLength\":6},"
        "{\"buffer\":0,\"byteOffset\":" +
        std::to_string(ibmOff) + ",\"byteLength\":128}";
    if (options.extraInfluence) {
        bufferViews +=
            ","
            "{\"buffer\":0,\"byteOffset\":" +
            std::to_string(j1Off) + ",\"byteLength\":24},"
            "{\"buffer\":0,\"byteOffset\":" +
            std::to_string(w1Off) + ",\"byteLength\":48}";
    }
    bufferViews += "]";

    std::string json =
        std::string("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,") +
        "\"scenes\":[{\"nodes\":" + sceneNodes + "}]," +
        "\"nodes\":" + nodesJson + "," +
        "\"meshes\":[{\"name\":\"Body\",\"primitives\":[" + primitive + "]}]," +
        "\"skins\":" + skins + "," +
        "\"accessors\":" + accessors + "," +
        "\"bufferViews\":" + bufferViews + "," +
        "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}]}";

    std::vector<uint8_t> jsonBytes(json.begin(), json.end());
    PadTo4(jsonBytes, static_cast<uint8_t>(' '));

    std::vector<uint8_t> file;
    const uint32_t magic = 0x46546C67;
    const uint32_t version = 2;
    const uint32_t jsonLen = static_cast<uint32_t>(jsonBytes.size());
    const uint32_t jsonType = 0x4E4F534A;
    const uint32_t binLen = static_cast<uint32_t>(bin.size());
    const uint32_t binType = 0x004E4942;
    const uint32_t length = 12 + 8 + jsonLen + 8 + binLen;
    AppendBytes(file, &magic, 4);
    AppendBytes(file, &version, 4);
    AppendBytes(file, &length, 4);
    AppendBytes(file, &jsonLen, 4);
    AppendBytes(file, &jsonType, 4);
    file.insert(file.end(), jsonBytes.begin(), jsonBytes.end());
    AppendBytes(file, &binLen, 4);
    AppendBytes(file, &binType, 4);
    file.insert(file.end(), bin.begin(), bin.end());

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}

const SkeletonBone* Find(const Skeleton& skeleton, const std::string& name) {
    return skeleton.FindBone(name);
}

bool TestGoldenWorldMatrices(const fs::path& scratch) {
    bool ok = true;
    const fs::path path = scratch / "golden.glb";
    if (!WriteGoldenGlb(path, {})) {
        return Check(false, "write golden glb");
    }

    SkeletalImportResult result;
    std::string error;
    ok &= Check(SkeletalModelImporter::Load(path, {}, result, error), "load golden: " + error);
    ok &= Check(result.skeleton.BoneCount() == 2, "two bones");
    ok &= Check(result.skeleton.skinName == "Armature", "skin name");
    ok &= Check(!result.skeleton.signature.empty(), "signature computed");
    ok &= Check(!result.restBindMismatch, "rest matches bind");

    const SkeletonBone* root = Find(result.skeleton, "Root");
    const SkeletonBone* child = Find(result.skeleton, "Child");
    if (!Check(root && child, "named Root and Child")) {
        return false;
    }
    ok &= Check(root->evalIndex == 0, "eval order parent before child (Root first)");
    ok &= Check(child->evalIndex == 1, "Child second in eval order");
    ok &= Check(child->parentEval == root->evalIndex, "Child parent is Root");
    ok &= Check(root->parentEval == -1, "Root has no parent");
    ok &= Check(child->identityIndex == 0 && root->identityIndex == 1,
                "identity order is sorted names (Child, Root)");
    ok &= Check(root->sourceJoint == 0 && child->sourceJoint == 1, "source joints match file order");
    ok &= Check(result.skeleton.sourceJointToEval[0] == root->evalIndex, "source 0 -> Root eval");
    ok &= Check(result.skeleton.sourceJointToEval[1] == child->evalIndex, "source 1 -> Child eval");

    std::vector<Mat4> restWorld;
    SkeletonBuilder::EvaluateRestWorld(result.skeleton, restWorld);
    ok &= Check(MatricesClose(restWorld[static_cast<size_t>(root->evalIndex)], ExpectedRootWorld()),
                "Root rest world is RotX(90); got " +
                    Mat4Dump(restWorld[static_cast<size_t>(root->evalIndex)]));
    ok &= Check(MatricesClose(restWorld[static_cast<size_t>(child->evalIndex)], ExpectedChildWorld()),
                "Child rest world is RotX(90)*T(0,1,0)*RotZ(90); got " +
                    Mat4Dump(restWorld[static_cast<size_t>(child->evalIndex)]));
    ok &= Check(MatricesClose(result.meshNodeWorld, ExpectedMeshWorld()),
                "mesh node world is T(0,0,2)");

    ok &= Check(result.vertices.size() >= 3, "imported vertices");
    bool foundLocalX = false;
    for (const auto& vert : result.vertices) {
        if (AlmostEqual(vert.pos[0], 1.0f) && AlmostEqual(vert.pos[1], 0.0f) &&
            AlmostEqual(vert.pos[2], 0.0f)) {
            foundLocalX = true;
            ok &= Check(vert.joints[0] == static_cast<uint16_t>(root->evalIndex),
                        "JOINTS remapped to Root eval/palette index");
            const Mat4 skinMat = Mat4::Multiply(
                restWorld[static_cast<size_t>(root->evalIndex)], root->inverseBind);
            const Vec3 skinned = Mat4::TransformPoint(skinMat, Vec3(1.0f, 0.0f, 0.0f));
            ok &= Check(AlmostEqual(skinned.x, 1.0f) && AlmostEqual(skinned.y, 0.0f) &&
                            AlmostEqual(skinned.z, 2.0f),
                        "rest skinning places mesh-local (1,0,0) at world (1,0,2)");
        }
    }
    ok &= Check(foundLocalX, "vertices stay in mesh local space (not baked by mesh-node TRS)");
    return ok;
}

bool TestPermutedJointsSameSignature(const fs::path& scratch) {
    bool ok = true;
    GoldenOptions permuted;
    permuted.joints = "[1,0]";
    const fs::path a = scratch / "order_a.glb";
    const fs::path b = scratch / "order_b.glb";
    if (!WriteGoldenGlb(a, {}) || !WriteGoldenGlb(b, permuted)) {
        return Check(false, "write permuted glbs");
    }

    SkeletalImportResult ra;
    SkeletalImportResult rb;
    std::string error;
    ok &= Check(SkeletalModelImporter::Load(a, {}, ra, error), "load order a: " + error);
    ok &= Check(SkeletalModelImporter::Load(b, {}, rb, error), "load order b: " + error);
    ok &= Check(ra.skeleton.signature == rb.skeleton.signature,
                "permuted joints[] still produce the same signature");

    const SkeletonBone* root = Find(rb.skeleton, "Root");
    const SkeletonBone* child = Find(rb.skeleton, "Child");
    if (!Check(root && child, "permuted file has Root and Child")) {
        return false;
    }
    ok &= Check(root->evalIndex == 0 && child->evalIndex == 1,
                "eval order stays Root then Child after joints[] permute");
    ok &= Check(root->sourceJoint == 1 && child->sourceJoint == 0,
                "sourceJoint follows permuted joints[]");
    ok &= Check(!rb.vertices.empty() && rb.vertices[0].joints[0] ==
                                            static_cast<uint16_t>(child->evalIndex),
                "vertex JOINTS_0 value 0 remaps to Child palette after permute");
    return ok;
}

bool TestNonJointAncestorBaked(const fs::path& scratch) {
    bool ok = true;
    GoldenOptions options;
    options.nonJointAncestor = true;
    const fs::path path = scratch / "ancestor.glb";
    if (!WriteGoldenGlb(path, options)) {
        return Check(false, "write ancestor glb");
    }
    SkeletalImportResult result;
    std::string error;
    ok &= Check(SkeletalModelImporter::Load(path, {}, result, error), "load ancestor: " + error);
    const SkeletonBone* root = Find(result.skeleton, "Root");
    const SkeletonBone* child = Find(result.skeleton, "Child");
    if (!Check(root && child, "ancestor file bones")) {
        return false;
    }
    std::vector<Mat4> restWorld;
    SkeletonBuilder::EvaluateRestWorld(result.skeleton, restWorld);
    ok &= Check(MatricesClose(restWorld[static_cast<size_t>(root->evalIndex)], ExpectedRootWorld()),
                "non-joint armature TRS is baked into Root local rest");
    ok &= Check(MatricesClose(restWorld[static_cast<size_t>(child->evalIndex)], ExpectedChildWorld()),
                "child world still RotX*T*RotZ with baked ancestor");
    return ok;
}

bool TestUnnamedJointsFail(const fs::path& scratch) {
    GoldenOptions options;
    options.childName = nullptr;
    const fs::path path = scratch / "unnamed.glb";
    if (!WriteGoldenGlb(path, options)) {
        return Check(false, "write unnamed glb");
    }
    SkeletalImportResult result;
    std::string error;
    const bool loaded = SkeletalModelImporter::Load(path, {}, result, error);
    bool ok = Check(!loaded, "unnamed joints must fail import");
    ok &= Check(error.find("Unnamed joints:") != std::string::npos, "lists unnamed joints header");
    ok &= Check(error.find("Joint index 1") != std::string::npos, "lists joint index 1");
    return ok;
}

bool TestDuplicateNamesFail(const fs::path& scratch) {
    GoldenOptions options;
    options.childName = "Root";
    const fs::path path = scratch / "duplicate.glb";
    if (!WriteGoldenGlb(path, options)) {
        return Check(false, "write duplicate glb");
    }
    SkeletalImportResult result;
    std::string error;
    const bool loaded = SkeletalModelImporter::Load(path, {}, result, error);
    bool ok = Check(!loaded, "duplicate names must fail import");
    ok &= Check(error.find("duplicate bone names") != std::string::npos, "duplicate error text");
    ok &= Check(error.find("joint indices") != std::string::npos, "lists joint indices");
    return ok;
}

bool TestMultipleSkinsFail(const fs::path& scratch) {
    GoldenOptions options;
    options.extraSkin = true;
    const fs::path path = scratch / "two_skins.glb";
    if (!WriteGoldenGlb(path, options)) {
        return Check(false, "write two skins glb");
    }
    SkeletalImportResult result;
    std::string error;
    return Check(!SkeletalModelImporter::Load(path, {}, result, error),
                 "multiple skins must fail") &&
           Check(error.find("exactly one skin") != std::string::npos, "multiple-skin message");
}

bool TestMorphTargetsFail(const fs::path& scratch) {
    GoldenOptions options;
    options.morphTargets = true;
    const fs::path path = scratch / "morph.glb";
    if (!WriteGoldenGlb(path, options)) {
        return Check(false, "write morph glb");
    }
    SkeletalImportResult result;
    std::string error;
    return Check(!SkeletalModelImporter::Load(path, {}, result, error),
                 "morph targets must fail") &&
           Check(error.find("Morph targets") != std::string::npos, "morph-target message");
}

bool TestInfluenceClamp(const fs::path& scratch) {
    GoldenOptions options;
    options.extraInfluence = true;
    const fs::path path = scratch / "clamp.glb";
    if (!WriteGoldenGlb(path, options)) {
        return Check(false, "write clamp glb");
    }
    SkeletalImportResult result;
    std::string error;
    bool ok = Check(SkeletalModelImporter::Load(path, {}, result, error), "load clamp: " + error);
    ok &= Check(result.clampedInfluences, "clampedInfluences flag");
    ok &= Check(!result.vertices.empty(), "clamp mesh has vertices");
    if (!result.vertices.empty()) {
        float sum = 0.0f;
        int nonzero = 0;
        for (int i = 0; i < 4; ++i) {
            sum += result.vertices[0].weights[i];
            if (result.vertices[0].weights[i] > 1e-6f) {
                ++nonzero;
            }
        }
        ok &= Check(nonzero <= 4, "at most 4 influences stored");
        ok &= Check(AlmostEqual(sum, 1.0f, 1e-3f), "weights renormalized to 1");
    }
    return ok;
}

bool TestRestBindMismatch(const fs::path& scratch) {
    GoldenOptions options;
    options.identityIbm = true;
    const fs::path path = scratch / "mismatch.glb";
    if (!WriteGoldenGlb(path, options)) {
        return Check(false, "write mismatch glb");
    }
    SkeletalImportResult result;
    std::string error;
    bool ok = Check(SkeletalModelImporter::Load(path, {}, result, error),
                    "load mismatch ibm: " + error);
    ok &= Check(result.restBindMismatch, "restBindMismatch detected");
    std::string restError;
    ok &= Check(!SkeletonBuilder::RestMatchesBind(result.skeleton, result.meshNodeWorld, 1e-3f,
                                                  restError),
                "RestMatchesBind rejects identity IBM");
    return ok;
}

bool TestFinalizeAndCompare() {
    bool ok = true;
    Skeleton skeleton;
    SkeletonBone root;
    root.name = "Root";
    root.parentEval = -1;
    root.sourceJoint = 0;
    SkeletonBone apple;
    apple.name = "Apple";
    apple.parentEval = 0;
    apple.sourceJoint = 1;
    SkeletonBone zebra;
    zebra.name = "Zebra";
    zebra.parentEval = 0;
    zebra.sourceJoint = 2;
    skeleton.bones = {root, zebra, apple};
    std::string error;
    ok &= Check(SkeletonBuilder::Finalize(skeleton, error), "finalize siblings: " + error);
    ok &= Check(skeleton.bones.size() == 3 && skeleton.bones[0].name == "Root" &&
                    skeleton.bones[1].name == "Apple" && skeleton.bones[2].name == "Zebra",
                "sibling eval order is identity-sorted");

    Skeleton unnamed;
    SkeletonBone blank;
    blank.sourceJoint = 0;
    unnamed.bones.push_back(blank);
    ok &= Check(!SkeletonBuilder::Finalize(unnamed, error), "empty name fails finalize");

    Skeleton tooMany;
    tooMany.bones.resize(static_cast<size_t>(kMaxCharacterBones + 1));
    for (int i = 0; i < kMaxCharacterBones + 1; ++i) {
        tooMany.bones[static_cast<size_t>(i)].name = "Bone" + std::to_string(i);
        tooMany.bones[static_cast<size_t>(i)].sourceJoint = i;
    }
    ok &= Check(!SkeletonBuilder::Finalize(tooMany, error), "129 bones fail finalize");
    ok &= Check(error.find("128") != std::string::npos, "max bone count in error");

    Skeleton character = skeleton;
    Skeleton missing = skeleton;
    missing.bones.pop_back();
    std::string ignore;
    SkeletonBuilder::Finalize(missing, ignore);
    SkeletonCompatibilityReport report = SkeletonBuilder::Compare(character, missing);
    ok &= Check(!report.compatible && !report.missingBones.empty(),
                "Compare reports missing bones");

    Skeleton extra = skeleton;
    SkeletonBone other;
    other.name = "Extra";
    other.parentEval = 0;
    extra.bones.push_back(other);
    SkeletonBuilder::Finalize(extra, ignore);
    report = SkeletonBuilder::Compare(character, extra);
    ok &= Check(!report.compatible && !report.extraBones.empty(), "Compare reports extra bones");

    Skeleton rest = skeleton;
    rest.bones[1].localRest = Mat4::Translation(Vec3(0.0f, 2.0f, 0.0f));
    SkeletonBuilder::Finalize(rest, ignore);
    report = SkeletonBuilder::Compare(character, rest);
    ok &= Check(report.restPoseMismatch && !report.compatible, "Compare rest-pose mismatch");
    return ok;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "hephaestus_skeletal_import_tests";
    fs::remove_all(root);
    fs::create_directories(root);

    bool ok = true;
    ok &= TestFinalizeAndCompare();
    ok &= TestGoldenWorldMatrices(root);
    ok &= TestPermutedJointsSameSignature(root);
    ok &= TestNonJointAncestorBaked(root);
    ok &= TestUnnamedJointsFail(root);
    ok &= TestDuplicateNamesFail(root);
    ok &= TestMultipleSkinsFail(root);
    ok &= TestMorphTargetsFail(root);
    ok &= TestInfluenceClamp(root);
    ok &= TestRestBindMismatch(root);

    fs::remove_all(root);
    if (ok) {
        std::cout << "Skeletal import tests passed.\n";
        return 0;
    }
    return 1;
}
