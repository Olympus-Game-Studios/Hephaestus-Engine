/**
 * @file SkeletalModelImporter.cpp
 * @brief glTF/GLB skin import. Vertices stay in mesh local space (no static TRS bake).
 */

#include "SkeletalModelImporter.h"
#include "FbxSkeletalImporter.h"
#include "GltfDocument.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

using json = nlohmann::json;

namespace Hephaestus {
namespace {

using namespace Gltf;

std::string TrimCopy(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

struct NodeTrs {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    bool hasTrs = false;
};

NodeTrs ExtractNodeTrs(const json& node) {
    NodeTrs trs;
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() >= 16) {
        return trs;
    }
    if (node.contains("translation") && node["translation"].is_array() &&
        node["translation"].size() >= 3) {
        trs.translation.x = ReadGLTFNumber(node["translation"][0]);
        trs.translation.y = ReadGLTFNumber(node["translation"][1]);
        trs.translation.z = ReadGLTFNumber(node["translation"][2]);
        trs.hasTrs = true;
    }
    if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() >= 3) {
        trs.scale.x = ReadGLTFNumber(node["scale"][0]);
        trs.scale.y = ReadGLTFNumber(node["scale"][1]);
        trs.scale.z = ReadGLTFNumber(node["scale"][2]);
        trs.hasTrs = true;
    }
    if (node.contains("rotation") && node["rotation"].is_array() &&
        node["rotation"].size() >= 4) {
        trs.rotation.x = ReadGLTFNumber(node["rotation"][0]);
        trs.rotation.y = ReadGLTFNumber(node["rotation"][1]);
        trs.rotation.z = ReadGLTFNumber(node["rotation"][2]);
        trs.rotation.w = ReadGLTFNumber(node["rotation"][3]);
        trs.hasTrs = true;
    }
    if (!node.contains("matrix")) {
        trs.hasTrs = true;
    }
    return trs;
}

uint16_t ReadJointIndex(const AccessorView& view, int element, int component, std::string& errorMessage) {
    const uint8_t* ptr = view.data + static_cast<size_t>(element) * static_cast<size_t>(view.stride) +
                         static_cast<size_t>(component) * static_cast<size_t>(ComponentSize(view.componentType));
    if (view.componentType == kComponentUByte) {
        return *ptr;
    }
    if (view.componentType == kComponentUShort) {
        uint16_t value = 0;
        std::memcpy(&value, ptr, sizeof(uint16_t));
        return value;
    }
    errorMessage = "JOINTS_0 must be UNSIGNED_BYTE or UNSIGNED_SHORT";
    return 0;
}

bool ReadMat4Array(const json& root,
                   const std::vector<std::vector<uint8_t>>& buffers,
                   int accessorIndex,
                   int expectedCount,
                   std::vector<Mat4>& outMatrices,
                   std::string& errorMessage) {
    AccessorView view;
    if (!OpenAccessor(root, buffers, accessorIndex, view, errorMessage)) {
        return false;
    }
    if (view.componentType != kComponentFloat || view.componentCount != 16) {
        errorMessage = "inverseBindMatrices must be FLOAT MAT4";
        return false;
    }
    if (view.count < expectedCount) {
        errorMessage = "inverseBindMatrices count is smaller than joints[]";
        return false;
    }
    outMatrices.resize(static_cast<size_t>(expectedCount));
    for (int i = 0; i < expectedCount; ++i) {
        for (int e = 0; e < 16; ++e) {
            outMatrices[static_cast<size_t>(i)].m[e] = ReadNormalized(view, i, e);
        }
    }
    return true;
}

bool ReadFloatScalars(const json& root,
                      const std::vector<std::vector<uint8_t>>& buffers,
                      int accessorIndex,
                      std::vector<float>& outValues,
                      std::string& errorMessage) {
    AccessorView view;
    if (!OpenAccessor(root, buffers, accessorIndex, view, errorMessage)) {
        return false;
    }
    if (view.componentType != kComponentFloat || view.componentCount != 1) {
        errorMessage = "Animation input times must be FLOAT SCALAR";
        return false;
    }
    outValues.resize(static_cast<size_t>(view.count));
    for (int i = 0; i < view.count; ++i) {
        outValues[static_cast<size_t>(i)] = ReadNormalized(view, i, 0);
    }
    return true;
}

bool ReadVec3Array(const json& root,
                   const std::vector<std::vector<uint8_t>>& buffers,
                   int accessorIndex,
                   int expectedCount,
                   std::vector<Vec3>& outValues,
                   std::string& errorMessage) {
    AccessorView view;
    if (!OpenAccessor(root, buffers, accessorIndex, view, errorMessage)) {
        return false;
    }
    if (view.componentType != kComponentFloat || view.componentCount != 3) {
        errorMessage = "Animation translation/scale must be FLOAT VEC3";
        return false;
    }
    if (view.count < expectedCount) {
        errorMessage = "Animation output count does not match input times";
        return false;
    }
    outValues.resize(static_cast<size_t>(expectedCount));
    for (int i = 0; i < expectedCount; ++i) {
        outValues[static_cast<size_t>(i)] = Vec3(ReadNormalized(view, i, 0),
                                                 ReadNormalized(view, i, 1),
                                                 ReadNormalized(view, i, 2));
    }
    return true;
}

bool ReadVec4Array(const json& root,
                   const std::vector<std::vector<uint8_t>>& buffers,
                   int accessorIndex,
                   int expectedCount,
                   std::vector<Vec4>& outValues,
                   std::string& errorMessage) {
    AccessorView view;
    if (!OpenAccessor(root, buffers, accessorIndex, view, errorMessage)) {
        return false;
    }
    if (view.componentType != kComponentFloat || view.componentCount != 4) {
        errorMessage = "Animation rotation must be FLOAT VEC4";
        return false;
    }
    if (view.count < expectedCount) {
        errorMessage = "Animation output count does not match input times";
        return false;
    }
    outValues.resize(static_cast<size_t>(expectedCount));
    for (int i = 0; i < expectedCount; ++i) {
        outValues[static_cast<size_t>(i)] = Vec4(ReadNormalized(view, i, 0),
                                                 ReadNormalized(view, i, 1),
                                                 ReadNormalized(view, i, 2),
                                                 ReadNormalized(view, i, 3));
    }
    return true;
}

int NodeToEval(const json& nodes,
               int nodeIndex,
               const Skeleton& skeleton,
               const std::unordered_map<int, int>& nodeToSource) {
    auto it = nodeToSource.find(nodeIndex);
    if (it != nodeToSource.end()) {
        if (it->second >= 0 && it->second < static_cast<int>(skeleton.sourceJointToEval.size())) {
            return skeleton.sourceJointToEval[static_cast<size_t>(it->second)];
        }
    }
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size())) {
        return -1;
    }
    const json& node = nodes[nodeIndex];
    if (!node.contains("name") || !node["name"].is_string()) {
        return -1;
    }
    const SkeletonBone* bone = skeleton.FindBone(TrimCopy(node["name"].get<std::string>()));
    return bone ? bone->evalIndex : -1;
}

bool ImportAnimations(const json& root,
                      const std::vector<std::vector<uint8_t>>& buffers,
                      const SkeletalImportSettings& settings,
                      const Skeleton& skeleton,
                      const json& nodes,
                      const std::unordered_map<int, int>& nodeToSource,
                      std::vector<AnimationClip>& outClips,
                      std::vector<std::string>& warnings,
                      std::string& errorMessage) {
    outClips.clear();
    if (!root.contains("animations") || !root["animations"].is_array()) {
        return true;
    }

    for (const json& animation : root["animations"]) {
        AnimationClip clip;
        if (animation.contains("name") && animation["name"].is_string()) {
            clip.name = TrimCopy(animation["name"].get<std::string>());
        }
        if (clip.name.empty()) {
            clip.name = "Clip" + std::to_string(outClips.size());
        }
        clip.id = clip.name;
        clip.sourceClip = clip.name;
        clip.skeletonSignature = skeleton.signature;
        clip.loop = true;
        clip.speed = 1.0f;

        if (!animation.contains("channels") || !animation["channels"].is_array() ||
            !animation.contains("samplers") || !animation["samplers"].is_array()) {
            warnings.push_back("Animation '" + clip.name + "' is missing channels or samplers.");
            continue;
        }

        const json& samplers = animation["samplers"];
        bool clipOk = true;
        std::string clipError;
        for (const json& channelJson : animation["channels"]) {
            if (!channelJson.contains("sampler") || !channelJson["sampler"].is_number_integer() ||
                !channelJson.contains("target") || !channelJson["target"].is_object()) {
                continue;
            }
            const int samplerIndex = channelJson["sampler"].get<int>();
            if (samplerIndex < 0 || samplerIndex >= static_cast<int>(samplers.size())) {
                clipOk = false;
                clipError = "sampler index out of range";
                break;
            }
            const json& target = channelJson["target"];
            if (!target.contains("node") || !target["node"].is_number_integer()) {
                continue;
            }
            const std::string path = target.value("path", "");
            if (path == "weights") {
                warnings.push_back("Animation '" + clip.name +
                                   "' morph-weight channels are ignored.");
                continue;
            }

            AnimationChannel channel;
            if (path == "translation") {
                channel.path = AnimationPath::Translation;
            } else if (path == "rotation") {
                channel.path = AnimationPath::Rotation;
            } else if (path == "scale") {
                channel.path = AnimationPath::Scale;
            } else {
                continue;
            }

            channel.evalBone = NodeToEval(nodes, target["node"].get<int>(), skeleton, nodeToSource);
            if (channel.evalBone < 0) {
                continue;
            }

            const json& sampler = samplers[samplerIndex];
            const std::string interpolation =
                ToLower(sampler.value("interpolation", "LINEAR"));
            if (interpolation == "cubicspline") {
                clipOk = false;
                clipError = "CUBICSPLINE interpolation is not supported in Animation 1.0";
                break;
            }
            channel.interpolation = (interpolation == "step") ? AnimationInterpolation::Step
                                                              : AnimationInterpolation::Linear;
            if (!sampler.contains("input") || !sampler.contains("output")) {
                clipOk = false;
                clipError = "sampler is missing input/output";
                break;
            }
            if (!ReadFloatScalars(root, buffers, sampler["input"].get<int>(), channel.times,
                                  clipError)) {
                clipOk = false;
                break;
            }
            const int keyCount = static_cast<int>(channel.times.size());
            if (channel.path == AnimationPath::Rotation) {
                if (!ReadVec4Array(root, buffers, sampler["output"].get<int>(), keyCount,
                                   channel.rotations, clipError)) {
                    clipOk = false;
                    break;
                }
            } else {
                if (!ReadVec3Array(root, buffers, sampler["output"].get<int>(), keyCount,
                                   channel.vectors, clipError)) {
                    clipOk = false;
                    break;
                }
                if (channel.path == AnimationPath::Translation) {
                    for (Vec3& value : channel.vectors) {
                        value = ConvertSkeletalPoint(value, settings.unitScale);
                    }
                }
            }
            for (float t : channel.times) {
                clip.duration = std::max(clip.duration, t);
            }
            clip.channels.push_back(std::move(channel));
        }

        if (!clipOk) {
            warnings.push_back("Animation '" + clip.name + "' was skipped: " + clipError);
            continue;
        }
        if (clip.channels.empty()) {
            warnings.push_back("Animation '" + clip.name + "' has no skeleton channels.");
            continue;
        }
        clip.endTime = clip.duration;
        outClips.push_back(std::move(clip));
    }
    errorMessage.clear();
    return true;
}

void ComputeNodeWorlds(const json& nodes,
                       const std::vector<int>& nodeParent,
                       std::vector<Mat4>& nodeLocal,
                       std::vector<Mat4>& nodeWorld) {
    const int count = static_cast<int>(nodes.size());
    nodeLocal.resize(static_cast<size_t>(count), Mat4::Identity());
    nodeWorld.resize(static_cast<size_t>(count), Mat4::Identity());
    for (int i = 0; i < count; ++i) {
        nodeLocal[static_cast<size_t>(i)] = GLTFNodeLocalMatrix(nodes[i]);
    }

    std::vector<char> computed(static_cast<size_t>(count), 0);
    std::function<void(int)> ensure = [&](int index) {
        if (index < 0 || computed[static_cast<size_t>(index)]) {
            return;
        }
        const int parent = nodeParent[static_cast<size_t>(index)];
        if (parent >= 0) {
            ensure(parent);
            nodeWorld[static_cast<size_t>(index)] =
                Mat4::Multiply(nodeWorld[static_cast<size_t>(parent)],
                               nodeLocal[static_cast<size_t>(index)]);
        } else {
            nodeWorld[static_cast<size_t>(index)] = nodeLocal[static_cast<size_t>(index)];
        }
        computed[static_cast<size_t>(index)] = 1;
    };
    for (int i = 0; i < count; ++i) {
        ensure(i);
    }
}

Mat4 ClosedLocal(int nodeIndex,
                 int stopNode,
                 const std::vector<int>& nodeParent,
                 const std::vector<Mat4>& nodeLocal) {
    Mat4 acc = Mat4::Identity();
    int current = nodeIndex;
    while (current >= 0 && current != stopNode) {
        acc = Mat4::Multiply(nodeLocal[static_cast<size_t>(current)], acc);
        current = nodeParent[static_cast<size_t>(current)];
    }
    return acc;
}

struct Influence {
    uint16_t joint = 0;
    float weight = 0.0f;
};

void ClampInfluences(std::vector<Influence>& influences, bool& clamped) {
    influences.erase(std::remove_if(influences.begin(), influences.end(),
                                    [](const Influence& inf) {
                                        return inf.weight <= 1e-8f;
                                    }),
                     influences.end());
    if (influences.size() > static_cast<size_t>(kMaxBoneInfluences)) {
        std::sort(influences.begin(), influences.end(),
                  [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
        influences.resize(static_cast<size_t>(kMaxBoneInfluences));
        clamped = true;
    }
    float sum = 0.0f;
    for (const Influence& inf : influences) {
        sum += inf.weight;
    }
    if (sum <= 1e-8f) {
        influences.clear();
        influences.push_back({0, 1.0f});
        return;
    }
    for (Influence& inf : influences) {
        inf.weight /= sum;
    }
}

bool AppendSkinnedPrimitive(const json& root,
                            const std::vector<std::vector<uint8_t>>& buffers,
                            const json& primitive,
                            const json& meshJson,
                            float unitScale,
                            const std::vector<int>& sourceJointToEval,
                            SkinnedMeshPart& part,
                            bool& clampedInfluences,
                            std::string& errorMessage) {
    if (primitive.contains("targets") && primitive["targets"].is_array() &&
        !primitive["targets"].empty()) {
        errorMessage = "Morph targets are not supported on character meshes.";
        return false;
    }
    if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION")) {
        errorMessage = "Skinned primitive is missing POSITION";
        return false;
    }
    const json& attributes = primitive["attributes"];
    if (!attributes.contains("JOINTS_0") || !attributes.contains("WEIGHTS_0")) {
        errorMessage = "Skinned primitive is missing JOINTS_0 / WEIGHTS_0";
        return false;
    }

    AccessorView posView;
    if (!OpenAccessor(root, buffers, attributes["POSITION"].get<int>(), posView, errorMessage)) {
        return false;
    }
    if (posView.componentCount != 3) {
        errorMessage = "POSITION must be VEC3";
        return false;
    }

    AccessorView joint0;
    AccessorView weight0;
    if (!OpenAccessor(root, buffers, attributes["JOINTS_0"].get<int>(), joint0, errorMessage) ||
        !OpenAccessor(root, buffers, attributes["WEIGHTS_0"].get<int>(), weight0, errorMessage)) {
        return false;
    }
    if (joint0.componentCount != 4 || weight0.componentCount != 4 ||
        joint0.count < posView.count || weight0.count < posView.count) {
        errorMessage = "JOINTS_0 / WEIGHTS_0 must be VEC4 with POSITION count";
        return false;
    }

    AccessorView joint1{};
    AccessorView weight1{};
    bool hasSet1 = attributes.contains("JOINTS_1") && attributes.contains("WEIGHTS_1");
    if (hasSet1) {
        if (!OpenAccessor(root, buffers, attributes["JOINTS_1"].get<int>(), joint1, errorMessage) ||
            !OpenAccessor(root, buffers, attributes["WEIGHTS_1"].get<int>(), weight1, errorMessage)) {
            return false;
        }
    }

    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> colors;
    if (!ReadOptionalVecAttribute(root, buffers, attributes, "NORMAL", posView.count, 3, normals,
                                  errorMessage) ||
        !ReadOptionalVecAttribute(root, buffers, attributes, "TEXCOORD_0", posView.count, 2, uvs,
                                  errorMessage)) {
        return false;
    }
    if (attributes.contains("COLOR_0")) {
        if (!ReadOptionalVecAttribute(root, buffers, attributes, "COLOR_0", posView.count, 3, colors,
                                      errorMessage)) {
            AccessorView colorView;
            std::string colorError;
            if (!OpenAccessor(root, buffers, attributes["COLOR_0"].get<int>(), colorView, colorError) ||
                colorView.componentCount != 4 || colorView.count < posView.count) {
                return false;
            }
            errorMessage.clear();
            colors.resize(static_cast<size_t>(posView.count * 3));
            for (int i = 0; i < posView.count; ++i) {
                colors[static_cast<size_t>(i * 3 + 0)] = ReadNormalized(colorView, i, 0);
                colors[static_cast<size_t>(i * 3 + 1)] = ReadNormalized(colorView, i, 1);
                colors[static_cast<size_t>(i * 3 + 2)] = ReadNormalized(colorView, i, 2);
            }
        }
    }

    std::vector<uint32_t> indices;
    if (primitive.contains("indices")) {
        AccessorView indexView;
        if (!OpenAccessor(root, buffers, primitive["indices"].get<int>(), indexView, errorMessage)) {
            return false;
        }
        indices.reserve(static_cast<size_t>(indexView.count));
        for (int i = 0; i < indexView.count; ++i) {
            indices.push_back(static_cast<uint32_t>(ReadNormalized(indexView, i, 0)));
        }
    } else {
        indices.resize(static_cast<size_t>(posView.count));
        for (int i = 0; i < posView.count; ++i) {
            indices[static_cast<size_t>(i)] = static_cast<uint32_t>(i);
        }
    }

    std::vector<SkinnedVertexData> unique(static_cast<size_t>(posView.count));
    for (int i = 0; i < posView.count; ++i) {
        SkinnedVertexData vert{};
        const Vec3 pos = ConvertSkeletalPoint(
            Vec3(ReadNormalized(posView, i, 0), ReadNormalized(posView, i, 1),
                 ReadNormalized(posView, i, 2)),
            unitScale);
        vert.pos[0] = pos.x;
        vert.pos[1] = pos.y;
        vert.pos[2] = pos.z;
        if (!normals.empty()) {
            Vec3 n(normals[static_cast<size_t>(i * 3 + 0)],
                   normals[static_cast<size_t>(i * 3 + 1)],
                   normals[static_cast<size_t>(i * 3 + 2)]);
            n = vnorm(n);
            vert.normal[0] = n.x;
            vert.normal[1] = n.y;
            vert.normal[2] = n.z;
        }
        if (!uvs.empty()) {
            vert.texCoord[0] = uvs[static_cast<size_t>(i * 2 + 0)];
            vert.texCoord[1] = uvs[static_cast<size_t>(i * 2 + 1)];
        }
        if (!colors.empty()) {
            vert.color[0] = colors[static_cast<size_t>(i * 3 + 0)];
            vert.color[1] = colors[static_cast<size_t>(i * 3 + 1)];
            vert.color[2] = colors[static_cast<size_t>(i * 3 + 2)];
        }

        std::vector<Influence> influences;
        for (int c = 0; c < 4; ++c) {
            std::string jointError;
            const uint16_t sourceJoint = ReadJointIndex(joint0, i, c, jointError);
            if (!jointError.empty()) {
                errorMessage = jointError;
                return false;
            }
            influences.push_back({sourceJoint, ReadNormalized(weight0, i, c)});
        }
        if (hasSet1) {
            for (int c = 0; c < 4 && c < joint1.componentCount; ++c) {
                std::string jointError;
                const uint16_t sourceJoint = ReadJointIndex(joint1, i, c, jointError);
                if (!jointError.empty()) {
                    errorMessage = jointError;
                    return false;
                }
                influences.push_back({sourceJoint, ReadNormalized(weight1, i, c)});
            }
        }
        bool clamped = false;
        ClampInfluences(influences, clamped);
        if (clamped) {
            clampedInfluences = true;
        }
        for (size_t k = 0; k < influences.size() && k < 4; ++k) {
            const int source = static_cast<int>(influences[k].joint);
            if (source < 0 || source >= static_cast<int>(sourceJointToEval.size()) ||
                sourceJointToEval[static_cast<size_t>(source)] < 0) {
                errorMessage = "Vertex joint index " + std::to_string(source) +
                               " is outside the skin joints[] array";
                return false;
            }
            vert.joints[k] = static_cast<uint16_t>(sourceJointToEval[static_cast<size_t>(source)]);
            vert.weights[k] = influences[k].weight;
        }
        unique[static_cast<size_t>(i)] = vert;
    }

    const uint32_t indexStart = static_cast<uint32_t>(part.indices.size());

    Vec3 minBounds{};
    Vec3 maxBounds{};
    bool boundsInit = !part.vertices.empty();
    if (boundsInit) {
        minBounds = part.bounds.min;
        maxBounds = part.bounds.max;
    }

    for (uint32_t idx : indices) {
        if (idx >= unique.size()) {
            errorMessage = "Index out of range";
            return false;
        }
        const SkinnedVertexData& vert = unique[idx];
        const Vec3 pos(vert.pos[0], vert.pos[1], vert.pos[2]);
        if (!boundsInit) {
            minBounds = pos;
            maxBounds = pos;
            boundsInit = true;
        } else {
            minBounds.x = std::min(minBounds.x, pos.x);
            minBounds.y = std::min(minBounds.y, pos.y);
            minBounds.z = std::min(minBounds.z, pos.z);
            maxBounds.x = std::max(maxBounds.x, pos.x);
            maxBounds.y = std::max(maxBounds.y, pos.y);
            maxBounds.z = std::max(maxBounds.z, pos.z);
        }
        part.vertices.push_back(vert);
        part.indices.push_back(static_cast<uint32_t>(part.vertices.size() - 1));
    }

    if (boundsInit) {
        part.bounds = AABB(minBounds, maxBounds);
    }

    SkinnedSubmeshData submesh{};
    submesh.indexOffset = indexStart;
    submesh.indexCount = static_cast<uint32_t>(part.indices.size() - indexStart);
    submesh.materialSlot = static_cast<int>(part.submeshes.size());
    if (primitive.contains("name") && primitive["name"].is_string()) {
        submesh.name = primitive["name"].get<std::string>();
    } else if (meshJson.contains("name") && meshJson["name"].is_string()) {
        submesh.name = meshJson["name"].get<std::string>();
    }
    if (submesh.name.empty()) {
        submesh.name = "Submesh";
    }
    part.submeshes.push_back(std::move(submesh));
    return true;
}

struct GltfSkinnedNode {
    int nodeIndex = -1;
    int meshIndex = -1;
    int skinIndex = -1;
};

struct GltfBuiltSkin {
    int skinIndex = -1;
    Skeleton skeleton;
    std::vector<int> jointNodes;
    std::unordered_map<int, int> nodeToSource;
};

bool BuildGltfSkeleton(const json& root,
                       const std::vector<std::vector<uint8_t>>& buffers,
                       const json& nodes,
                       const std::vector<int>& nodeParent,
                       const std::vector<Mat4>& nodeLocal,
                       int skinIndex,
                       const SkeletalImportSettings& settings,
                       GltfBuiltSkin& outSkin,
                       std::string& errorMessage) {
    outSkin = {};
    outSkin.skinIndex = skinIndex;
    if (skinIndex < 0 || skinIndex >= static_cast<int>(root["skins"].size())) {
        errorMessage = "Skinned node references an invalid skin index";
        return false;
    }
    const json& skin = root["skins"][skinIndex];
    if (!skin.contains("joints") || !skin["joints"].is_array() || skin["joints"].empty()) {
        errorMessage = "Skin is missing joints[]";
        return false;
    }
    const int nodeCount = static_cast<int>(nodes.size());
    outSkin.jointNodes.reserve(skin["joints"].size());
    for (const auto& joint : skin["joints"]) {
        if (!joint.is_number_integer()) {
            errorMessage = "Skin joints[] must be node indices";
            return false;
        }
        const int nodeIndex = joint.get<int>();
        if (nodeIndex < 0 || nodeIndex >= nodeCount) {
            errorMessage = "Skin joint node index out of range";
            return false;
        }
        outSkin.jointNodes.push_back(nodeIndex);
    }
    if (static_cast<int>(outSkin.jointNodes.size()) > kMaxCharacterBones) {
        errorMessage = "Skeleton has " + std::to_string(outSkin.jointNodes.size()) +
                       " bones; maximum is " + std::to_string(kMaxCharacterBones) + ".";
        return false;
    }

    std::vector<int> unnamed;
    std::unordered_map<std::string, std::vector<int>> nameToJoints;
    for (int source = 0; source < static_cast<int>(outSkin.jointNodes.size()); ++source) {
        const json& node = nodes[outSkin.jointNodes[static_cast<size_t>(source)]];
        std::string name;
        if (node.contains("name") && node["name"].is_string()) {
            name = TrimCopy(node["name"].get<std::string>());
        }
        if (name.empty()) {
            unnamed.push_back(source);
        } else {
            nameToJoints[name].push_back(source);
        }
    }
    if (!unnamed.empty()) {
        errorMessage =
            "Character skeleton contains unnamed bones.\n\n"
            "Hephaestus requires stable bone names so animation compatibility can be verified.\n\n"
            "Unnamed joints:\n";
        for (int index : unnamed) {
            errorMessage += "- Joint index " + std::to_string(index) + "\n";
        }
        errorMessage += "\nName these bones in the source rig and export the character again.";
        return false;
    }

    std::vector<std::string> duplicates;
    for (const auto& [name, indices] : nameToJoints) {
        if (indices.size() > 1) {
            duplicates.push_back(name);
        }
    }
    if (!duplicates.empty()) {
        std::sort(duplicates.begin(), duplicates.end());
        errorMessage = "Character skeleton contains duplicate bone names.\n\n";
        for (const std::string& name : duplicates) {
            errorMessage += "- " + name + " (joint indices ";
            const auto& indices = nameToJoints[name];
            for (size_t i = 0; i < indices.size(); ++i) {
                if (i > 0) {
                    errorMessage += ", ";
                }
                errorMessage += std::to_string(indices[i]);
            }
            errorMessage += ")\n";
        }
        errorMessage += "\nRename these bones in the source rig so each name is unique.";
        return false;
    }

    for (int source = 0; source < static_cast<int>(outSkin.jointNodes.size()); ++source) {
        outSkin.nodeToSource[outSkin.jointNodes[static_cast<size_t>(source)]] = source;
    }

    std::vector<Mat4> inverseBinds;
    if (!skin.contains("inverseBindMatrices")) {
        errorMessage = "Skin is missing inverseBindMatrices";
        return false;
    }
    if (!ReadMat4Array(root, buffers, skin["inverseBindMatrices"].get<int>(),
                       static_cast<int>(outSkin.jointNodes.size()), inverseBinds, errorMessage)) {
        return false;
    }

    Skeleton skeleton;
    if (skin.contains("name") && skin["name"].is_string()) {
        skeleton.skinName = TrimCopy(skin["name"].get<std::string>());
    }
    if (skeleton.skinName.empty()) {
        skeleton.skinName = "Armature";
    }

    skeleton.bones.resize(outSkin.jointNodes.size());
    for (int source = 0; source < static_cast<int>(outSkin.jointNodes.size()); ++source) {
        const int nodeIndex = outSkin.jointNodes[static_cast<size_t>(source)];
        int parentSource = -1;
        int ancestor = nodeParent[static_cast<size_t>(nodeIndex)];
        int parentJointNode = -1;
        while (ancestor >= 0) {
            auto it = outSkin.nodeToSource.find(ancestor);
            if (it != outSkin.nodeToSource.end()) {
                parentSource = it->second;
                parentJointNode = ancestor;
                break;
            }
            ancestor = nodeParent[static_cast<size_t>(ancestor)];
        }

        SkeletonBone bone;
        bone.name = TrimCopy(nodes[nodeIndex].value("name", ""));
        bone.parentEval = parentSource;
        bone.sourceJoint = source;
        bone.localRest =
            ConvertSkeletalMatrix(ClosedLocal(nodeIndex, parentJointNode, nodeParent, nodeLocal),
                                  settings.unitScale);
        bone.inverseBind = ConvertSkeletalMatrix(inverseBinds[static_cast<size_t>(source)],
                                                 settings.unitScale);
        const int parentOfJoint = nodeParent[static_cast<size_t>(nodeIndex)];
        const NodeTrs trs = ExtractNodeTrs(nodes[nodeIndex]);
        if (trs.hasTrs) {
            bone.restPrefix = ConvertSkeletalMatrix(
                ClosedLocal(parentOfJoint, parentJointNode, nodeParent, nodeLocal),
                settings.unitScale);
            bone.restTranslation = ConvertSkeletalPoint(trs.translation, settings.unitScale);
            bone.restRotation = trs.rotation;
            bone.restScale = trs.scale;
        } else {
            bone.restPrefix = bone.localRest;
        }
        skeleton.bones[static_cast<size_t>(source)] = std::move(bone);
    }

    if (!SkeletonBuilder::Finalize(skeleton, errorMessage)) {
        return false;
    }
    outSkin.skeleton = std::move(skeleton);
    return true;
}

bool MapSkinJointsToCanonical(const json& nodes,
                              const GltfBuiltSkin& skin,
                              const Skeleton& canonical,
                              std::vector<int>& outSourceToCanonicalEval,
                              std::string& errorMessage) {
    outSourceToCanonicalEval.assign(skin.jointNodes.size(), -1);
    for (int source = 0; source < static_cast<int>(skin.jointNodes.size()); ++source) {
        const int nodeIndex = skin.jointNodes[static_cast<size_t>(source)];
        const std::string name = TrimCopy(nodes[nodeIndex].value("name", ""));
        const SkeletonBone* bone = canonical.FindBone(name);
        if (!bone) {
            errorMessage = IncompatibleCharacterSkeletonsMessage();
            return false;
        }
        outSourceToCanonicalEval[static_cast<size_t>(source)] = bone->evalIndex;
    }
    return true;
}

bool ImportDocument(const json& root,
                    const std::vector<std::vector<uint8_t>>& buffers,
                    const SkeletalImportSettings& settings,
                    SkeletalImportResult& outResult,
                    std::string& errorMessage) {
    outResult = SkeletalImportResult{};
    if (!root.contains("skins") || !root["skins"].is_array() || root["skins"].empty()) {
        errorMessage =
            "Character source has no skin.\n\n"
            "Import this file as a static mesh instead, or export a skinned armature.";
        return false;
    }
    if (!root.contains("nodes") || !root["nodes"].is_array() || root["nodes"].empty()) {
        errorMessage = "Character source has no nodes";
        return false;
    }

    const json& nodes = root["nodes"];
    const int nodeCount = static_cast<int>(nodes.size());
    std::vector<int> nodeParent(static_cast<size_t>(nodeCount), -1);
    for (int i = 0; i < nodeCount; ++i) {
        const json& node = nodes[i];
        if (!node.contains("children") || !node["children"].is_array()) {
            continue;
        }
        for (const auto& child : node["children"]) {
            if (!child.is_number_integer()) {
                continue;
            }
            const int childIndex = child.get<int>();
            if (childIndex >= 0 && childIndex < nodeCount) {
                nodeParent[static_cast<size_t>(childIndex)] = i;
            }
        }
    }

    std::vector<Mat4> nodeLocal;
    std::vector<Mat4> nodeWorld;
    ComputeNodeWorlds(nodes, nodeParent, nodeLocal, nodeWorld);

    std::vector<GltfSkinnedNode> skinnedNodes;
    for (int i = 0; i < nodeCount; ++i) {
        const json& node = nodes[i];
        const bool hasSkin = node.contains("skin") && node["skin"].is_number_integer();
        const bool hasMesh = node.contains("mesh") && node["mesh"].is_number_integer();
        if (!hasSkin || !hasMesh) {
            continue;
        }
        GltfSkinnedNode entry;
        entry.nodeIndex = i;
        entry.meshIndex = node["mesh"].get<int>();
        entry.skinIndex = node["skin"].get<int>();
        skinnedNodes.push_back(entry);
    }
    if (skinnedNodes.empty()) {
        errorMessage = "No node references both a mesh and a skin";
        return false;
    }

    std::unordered_map<int, GltfBuiltSkin> builtSkins;
    for (const GltfSkinnedNode& entry : skinnedNodes) {
        if (builtSkins.count(entry.skinIndex) > 0) {
            continue;
        }
        GltfBuiltSkin built;
        if (!BuildGltfSkeleton(root, buffers, nodes, nodeParent, nodeLocal, entry.skinIndex,
                               settings, built, errorMessage)) {
            return false;
        }
        builtSkins[entry.skinIndex] = std::move(built);
    }

    int canonicalSkinIndex = skinnedNodes.front().skinIndex;
    for (const auto& [skinIndex, built] : builtSkins) {
        const GltfBuiltSkin& canonical = builtSkins[canonicalSkinIndex];
        if (skinIndex == canonicalSkinIndex) {
            continue;
        }
        if (SkeletonBuilder::MapsOnto(canonical.skeleton, built.skeleton)) {
            canonicalSkinIndex = skinIndex;
        } else if (!SkeletonBuilder::MapsOnto(built.skeleton, canonical.skeleton)) {
            errorMessage = IncompatibleCharacterSkeletonsMessage();
            return false;
        }
    }

    const GltfBuiltSkin& canonicalSkin = builtSkins[canonicalSkinIndex];
    outResult.skeleton = canonicalSkin.skeleton;

    size_t canonicalPartIndex = 0;
    bool foundCanonicalPart = false;
    for (const GltfSkinnedNode& entry : skinnedNodes) {
        if (!root.contains("meshes") ||
            entry.meshIndex < 0 ||
            entry.meshIndex >= static_cast<int>(root["meshes"].size())) {
            errorMessage = "Skinned mesh index is out of range";
            return false;
        }
        const json& meshJson = root["meshes"][entry.meshIndex];
        if (!meshJson.contains("primitives") || !meshJson["primitives"].is_array() ||
            meshJson["primitives"].empty()) {
            errorMessage = "Skinned mesh has no primitives";
            return false;
        }

        auto skinIt = builtSkins.find(entry.skinIndex);
        if (skinIt == builtSkins.end()) {
            errorMessage = IncompatibleCharacterSkeletonsMessage();
            return false;
        }
        std::vector<int> sourceToCanonicalEval;
        if (!MapSkinJointsToCanonical(nodes, skinIt->second, outResult.skeleton,
                                      sourceToCanonicalEval, errorMessage)) {
            return false;
        }

        SkinnedMeshPart part;
        if (nodes[entry.nodeIndex].contains("name") &&
            nodes[entry.nodeIndex]["name"].is_string()) {
            part.name = TrimCopy(nodes[entry.nodeIndex]["name"].get<std::string>());
        }
        if (part.name.empty() && meshJson.contains("name") && meshJson["name"].is_string()) {
            part.name = TrimCopy(meshJson["name"].get<std::string>());
        }
        if (part.name.empty()) {
            part.name = "Mesh";
        }
        part.meshNodeWorld = ConvertSkeletalMatrix(nodeWorld[static_cast<size_t>(entry.nodeIndex)],
                                                   settings.unitScale);
        for (const auto& primitive : meshJson["primitives"]) {
            if (!AppendSkinnedPrimitive(root, buffers, primitive, meshJson, settings.unitScale,
                                        sourceToCanonicalEval, part, outResult.clampedInfluences,
                                        errorMessage)) {
                return false;
            }
        }
        if (part.vertices.empty()) {
            errorMessage = "Skinned mesh has no geometry";
            return false;
        }
        if (!foundCanonicalPart && entry.skinIndex == canonicalSkinIndex) {
            canonicalPartIndex = outResult.meshes.size();
            foundCanonicalPart = true;
        }
        outResult.meshes.push_back(std::move(part));
    }

    SkeletalModelImporter::FinalizeImportedMeshParts(outResult, canonicalPartIndex);

    const Mat4 restMeshWorld = outResult.meshes.empty()
                                   ? Mat4::Identity()
                                   : outResult.meshes[canonicalPartIndex].meshNodeWorld;
    std::string restError;
    if (!SkeletonBuilder::RestMatchesBind(outResult.skeleton, restMeshWorld, 1e-3f, restError)) {
        outResult.restBindMismatch = true;
        outResult.warnings.push_back(restError);
    }
    if (outResult.clampedInfluences) {
        outResult.warnings.push_back(
            "Some vertices had more than 4 bone influences; extra influences were dropped and "
            "weights were renormalized.");
    }

    std::string animError;
    if (!ImportAnimations(root, buffers, settings, outResult.skeleton, nodes,
                          canonicalSkin.nodeToSource, outResult.clips, outResult.warnings,
                          animError) &&
        !animError.empty()) {
        outResult.warnings.push_back(animError);
    }
    return true;
}

void AppendBox(SkinnedMeshPart& part, const Vec3& minP, const Vec3& maxP, uint16_t bone) {
    const Vec3 nrm[6] = {
        Vec3(0, 0, 1), Vec3(0, 0, -1), Vec3(1, 0, 0),
        Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0)
    };
    const Vec3 corners[8] = {
        Vec3(minP.x, minP.y, maxP.z), Vec3(maxP.x, minP.y, maxP.z),
        Vec3(maxP.x, maxP.y, maxP.z), Vec3(minP.x, maxP.y, maxP.z),
        Vec3(minP.x, minP.y, minP.z), Vec3(maxP.x, minP.y, minP.z),
        Vec3(maxP.x, maxP.y, minP.z), Vec3(minP.x, maxP.y, minP.z)
    };
    const int faces[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {1, 5, 6, 2},
        {4, 0, 3, 7}, {3, 2, 6, 7}, {4, 5, 1, 0}
    };
    const uint32_t base = static_cast<uint32_t>(part.vertices.size());
    for (int f = 0; f < 6; ++f) {
        for (int i = 0; i < 4; ++i) {
            SkinnedVertexData vert{};
            const Vec3& p = corners[faces[f][i]];
            vert.pos[0] = p.x;
            vert.pos[1] = p.y;
            vert.pos[2] = p.z;
            vert.normal[0] = nrm[f].x;
            vert.normal[1] = nrm[f].y;
            vert.normal[2] = nrm[f].z;
            vert.joints[0] = bone;
            vert.weights[0] = 1.0f;
            part.vertices.push_back(vert);
        }
        const uint32_t q = base + static_cast<uint32_t>(f * 4);
        part.indices.push_back(q);
        part.indices.push_back(q + 1);
        part.indices.push_back(q + 2);
        part.indices.push_back(q);
        part.indices.push_back(q + 2);
        part.indices.push_back(q + 3);
    }
}

bool MatrixNearIdentity(const Mat4& m) {
    const Mat4 id = Mat4::Identity();
    for (int i = 0; i < 16; ++i) {
        if (std::abs(m.m[i] - id.m[i]) > 1e-5f) {
            return false;
        }
    }
    return true;
}

void BakePartIntoCanonicalBind(SkinnedMeshPart& part, const Mat4& canonicalMeshWorld) {
    const Mat4 relative = Mat4::Multiply(Mat4::Invert(canonicalMeshWorld), part.meshNodeWorld);
    if (MatrixNearIdentity(relative)) {
        part.meshNodeWorld = canonicalMeshWorld;
        return;
    }
    Vec3 minB{};
    Vec3 maxB{};
    bool init = false;
    for (SkinnedVertexData& vert : part.vertices) {
        const Vec3 p = Mat4::TransformPoint(relative, Vec3(vert.pos[0], vert.pos[1], vert.pos[2]));
        Vec3 n = Mat4::TransformDirection(relative, Vec3(vert.normal[0], vert.normal[1], vert.normal[2]));
        n = vnorm(n);
        vert.pos[0] = p.x;
        vert.pos[1] = p.y;
        vert.pos[2] = p.z;
        vert.normal[0] = n.x;
        vert.normal[1] = n.y;
        vert.normal[2] = n.z;
        if (!init) {
            minB = p;
            maxB = p;
            init = true;
        } else {
            minB.x = std::min(minB.x, p.x);
            minB.y = std::min(minB.y, p.y);
            minB.z = std::min(minB.z, p.z);
            maxB.x = std::max(maxB.x, p.x);
            maxB.y = std::max(maxB.y, p.y);
            maxB.z = std::max(maxB.z, p.z);
        }
    }
    if (init) {
        part.bounds = AABB(minB, maxB);
    }
    part.meshNodeWorld = canonicalMeshWorld;
}

} // namespace

bool SkeletalModelImporter::Load(const std::filesystem::path& path,
                                 const SkeletalImportSettings& settings,
                                 SkeletalImportResult& outResult,
                                 std::string& outError) {
    outError.clear();
    const std::string ext = ToLower(path.extension().string());
    if (ext == ".fbx") {
        return FbxSkeletalImporter::Load(path, settings, outResult, outError);
    }
    json root;
    std::vector<std::vector<uint8_t>> buffers;
    try {
        if (!LoadGlbOrGltf(path, root, buffers, outError)) {
            return false;
        }
        return ImportDocument(root, buffers, settings, outResult, outError);
    } catch (const std::exception& ex) {
        outError = std::string("Failed to load character source: ") + ex.what();
        return false;
    }
}

bool SkeletalModelImporter::LoadClips(const std::filesystem::path& path,
                                      const SkeletalImportSettings& settings,
                                      const Skeleton& character,
                                      std::vector<AnimationClip>& outClips,
                                      std::string& outError) {
    outError.clear();
    outClips.clear();
    const std::string ext = ToLower(path.extension().string());
    if (ext == ".fbx") {
        return FbxSkeletalImporter::LoadClips(path, settings, character, outClips, outError);
    }
    json root;
    std::vector<std::vector<uint8_t>> buffers;
    try {
        if (!LoadGlbOrGltf(path, root, buffers, outError)) {
            return false;
        }
        if (root.contains("skins") && root["skins"].is_array() && !root["skins"].empty()) {
            SkeletalImportResult imported;
            if (ImportDocument(root, buffers, settings, imported, outError)) {
                const SkeletonCompatibilityReport report =
                    SkeletonBuilder::Compare(character, imported.skeleton);
                if (!report.compatible) {
                    outError = FormatSkeletonIncompatibility(report);
                    return false;
                }
            }
        }
        if (!root.contains("nodes") || !root["nodes"].is_array()) {
            if (outError.empty()) {
                outError = "Animation source has no nodes";
            }
            return false;
        }
        std::unordered_map<int, int> nodeToSource;
        std::vector<std::string> warnings;
        std::string animError;
        if (!ImportAnimations(root, buffers, settings, character, root["nodes"], nodeToSource,
                              outClips, warnings, animError)) {
            outError = animError.empty() ? "Failed to import animation clips" : animError;
            return false;
        }
        outError.clear();
        return true;
    } catch (const std::exception& ex) {
        outError = std::string("Failed to load animation source: ") + ex.what();
        return false;
    }
}

void SkeletalModelImporter::BuildRestPoseFixture(SkeletalImportResult& outResult,
                                                 std::vector<Mat4>& outPalette) {
    outResult = SkeletalImportResult{};
    Skeleton skeleton;
    skeleton.skinName = "Armature";
    SkeletonBone root;
    root.name = "Root";
    root.parentEval = -1;
    root.sourceJoint = 0;
    root.localRest = Mat4::Identity();
    root.inverseBind = Mat4::Identity();
    root.restTranslation = Vec3(0.0f, 0.0f, 0.0f);
    root.restRotation = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    root.restScale = Vec3(1.0f, 1.0f, 1.0f);
    SkeletonBone child;
    child.name = "Child";
    child.parentEval = 0;
    child.sourceJoint = 1;
    child.localRest = Mat4::Translation(Vec3(0.0f, 1.0f, 0.0f));
    child.inverseBind = Mat4::Translation(Vec3(0.0f, -1.0f, 0.0f));
    child.restTranslation = Vec3(0.0f, 1.0f, 0.0f);
    child.restRotation = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    child.restScale = Vec3(1.0f, 1.0f, 1.0f);
    skeleton.bones = {root, child};
    std::string error;
    SkeletonBuilder::Finalize(skeleton, error);

    const SkeletonBone* rootBone = skeleton.FindBone("Root");
    const SkeletonBone* childBone = skeleton.FindBone("Child");
    const uint16_t rootEval =
        static_cast<uint16_t>(rootBone ? rootBone->evalIndex : 0);
    const uint16_t childEval =
        static_cast<uint16_t>(childBone ? childBone->evalIndex : 1);

    SkinnedMeshPart part;
    part.name = "Body";
    AppendBox(part, Vec3(-0.18f, 0.0f, -0.18f), Vec3(0.18f, 1.0f, 0.18f), rootEval);
    AppendBox(part, Vec3(-0.14f, 1.0f, -0.14f), Vec3(0.14f, 2.0f, 0.14f), childEval);
    part.bounds = AABB(Vec3(-0.18f, 0.0f, -0.18f), Vec3(0.18f, 2.0f, 0.18f));
    SkinnedSubmeshData sub{};
    sub.indexCount = static_cast<uint32_t>(part.indices.size());
    sub.name = "Body";
    part.submeshes.push_back(sub);
    outResult.bounds = part.bounds;
    outResult.meshes.push_back(std::move(part));
    outResult.skeleton = std::move(skeleton);
    SkeletonBuilder::FillRestBonePalette(outResult.skeleton, outPalette);
}

void SkeletalModelImporter::FinalizeImportedMeshParts(SkeletalImportResult& result,
                                                      size_t canonicalPartIndex) {
    if (result.meshes.empty()) {
        result.bounds = AABB{};
        return;
    }
    if (canonicalPartIndex >= result.meshes.size()) {
        canonicalPartIndex = 0;
    }
    std::unordered_map<std::string, int> nameCount;
    for (SkinnedMeshPart& part : result.meshes) {
        if (part.name.empty()) {
            part.name = "Mesh";
        }
        const int count = ++nameCount[part.name];
        if (count > 1) {
            part.name += "_" + std::to_string(count);
        }
    }
    const Mat4 canonicalWorld = result.meshes[canonicalPartIndex].meshNodeWorld;
    for (size_t i = 0; i < result.meshes.size(); ++i) {
        if (i == canonicalPartIndex) {
            continue;
        }
        BakePartIntoCanonicalBind(result.meshes[i], canonicalWorld);
    }
    AABB combined = result.meshes.front().bounds;
    for (size_t i = 1; i < result.meshes.size(); ++i) {
        const AABB& b = result.meshes[i].bounds;
        combined.min.x = std::min(combined.min.x, b.min.x);
        combined.min.y = std::min(combined.min.y, b.min.y);
        combined.min.z = std::min(combined.min.z, b.min.z);
        combined.max.x = std::max(combined.max.x, b.max.x);
        combined.max.y = std::max(combined.max.y, b.max.y);
        combined.max.z = std::max(combined.max.z, b.max.z);
    }
    result.bounds = combined;
}

} // namespace Hephaestus
