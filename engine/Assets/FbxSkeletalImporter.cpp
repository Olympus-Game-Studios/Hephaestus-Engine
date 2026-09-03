/**
 * @file FbxSkeletalImporter.cpp
 * @brief FBX skins/clips into Hephaestus Skeleton / SkinnedVertexData / AnimationClip.
 */

#include "FbxSkeletalImporter.h"

#include "FbxDocument.h"

#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Hephaestus {
namespace {

struct Influence {
    uint16_t joint = 0;
    float weight = 0.0f;
};

void ClampInfluences(std::vector<Influence>& influences, bool& clamped) {
    influences.erase(std::remove_if(influences.begin(), influences.end(),
                                    [](const Influence& inf) { return inf.weight <= 1e-8f; }),
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

ufbx_matrix ClosedLocalMatrix(ufbx_node* node, ufbx_node* stopParent) {
    ufbx_matrix acc = ufbx_identity_matrix;
    ufbx_node* current = node;
    while (current && current != stopParent && !current->is_root) {
        acc = ufbx_matrix_mul(&current->node_to_parent, &acc);
        current = current->parent;
    }
    return acc;
}

ufbx_node* DeformingParent(ufbx_node* node,
                           const std::unordered_set<ufbx_node*>& deforming) {
    ufbx_node* ancestor = node ? node->parent : nullptr;
    while (ancestor && !ancestor->is_root) {
        if (deforming.count(ancestor) > 0) {
            return ancestor;
        }
        ancestor = ancestor->parent;
    }
    return nullptr;
}

bool FindSkinnedMesh(ufbx_scene* scene, ufbx_node*& outNode, ufbx_skin_deformer*& outSkin,
                     std::string& outError) {
    outNode = nullptr;
    outSkin = nullptr;
    int skinned = 0;
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        ufbx_node* node = scene->nodes.data[i];
        if (!node || !node->mesh || node->mesh->skin_deformers.count == 0) {
            continue;
        }
        ++skinned;
        outNode = node;
        outSkin = node->mesh->skin_deformers.data[0];
    }
    if (skinned == 0) {
        outError =
            "Character source has no skin.\n\n"
            "Import this file as a static mesh instead, or export a skinned armature.";
        return false;
    }
    if (skinned != 1) {
        outError = "Character sources must contain exactly one skin (found " +
                   std::to_string(skinned) + ").";
        return false;
    }
    if (!outSkin) {
        outError = "Skin is missing clusters.";
        return false;
    }
    return true;
}

std::string BoneName(ufbx_node* node, bool stripNamespace) {
    std::string name = node ? FbxDocument::ToString(node->name) : std::string();
    name = FbxDocument::StripBoneNamespace(name, stripNamespace);
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
        name.erase(name.begin());
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
        name.pop_back();
    }
    return name;
}

bool BuildSkeleton(ufbx_skin_deformer* skin,
                   const SkeletalImportSettings& settings,
                   Skeleton& skeleton,
                   std::unordered_map<ufbx_node*, int>& nodeToSource,
                   std::unordered_set<ufbx_node*>& deforming,
                   std::string& outError) {
    skeleton = {};
    nodeToSource.clear();
    deforming.clear();
    skeleton.skinName = FbxDocument::ToString(skin->name);
    if (skeleton.skinName.empty()) {
        skeleton.skinName = "Armature";
    }

    std::vector<ufbx_node*> joints;
    for (size_t i = 0; i < skin->clusters.count; ++i) {
        ufbx_skin_cluster* cluster = skin->clusters.data[i];
        if (!cluster || !cluster->bone_node) {
            outError = "Skin cluster is missing a bone node.";
            return false;
        }
        if (deforming.insert(cluster->bone_node).second) {
            joints.push_back(cluster->bone_node);
        }
    }
    if (joints.empty()) {
        outError = "Skin is missing joints.";
        return false;
    }
    if (static_cast<int>(joints.size()) > kMaxCharacterBones) {
        outError = "Skeleton has " + std::to_string(joints.size()) +
                   " bones; maximum is " + std::to_string(kMaxCharacterBones) + ".";
        return false;
    }

    std::vector<int> unnamed;
    std::unordered_map<std::string, std::vector<int>> nameToJoints;
    for (int source = 0; source < static_cast<int>(joints.size()); ++source) {
        nodeToSource[joints[static_cast<size_t>(source)]] = source;
        const std::string name = BoneName(joints[static_cast<size_t>(source)],
                                          settings.stripBoneNamespace);
        if (name.empty()) {
            unnamed.push_back(source);
        } else {
            nameToJoints[name].push_back(source);
        }
    }
    if (!unnamed.empty()) {
        outError =
            "Character skeleton contains unnamed bones.\n\n"
            "Hephaestus requires stable bone names so animation compatibility can be verified.\n\n"
            "Unnamed joints:\n";
        for (int index : unnamed) {
            outError += "- Joint index " + std::to_string(index) + "\n";
        }
        outError += "\nName these bones in the source rig and export the character again.";
        return false;
    }
    std::vector<std::string> duplicates;
    for (const auto& entry : nameToJoints) {
        if (entry.second.size() > 1) {
            duplicates.push_back(entry.first);
        }
    }
    if (!duplicates.empty()) {
        outError = "Character skeleton contains duplicate bone names.\n\n";
        for (const std::string& name : duplicates) {
            outError += "- " + name + "\n";
        }
        return false;
    }

    skeleton.bones.resize(joints.size());
    for (int source = 0; source < static_cast<int>(joints.size()); ++source) {
        ufbx_node* node = joints[static_cast<size_t>(source)];
        ufbx_node* parentJoint = DeformingParent(node, deforming);
        int parentSource = -1;
        if (parentJoint) {
            auto it = nodeToSource.find(parentJoint);
            if (it != nodeToSource.end()) {
                parentSource = it->second;
            }
        }

        ufbx_skin_cluster* cluster = nullptr;
        for (size_t c = 0; c < skin->clusters.count; ++c) {
            if (skin->clusters.data[c] && skin->clusters.data[c]->bone_node == node) {
                cluster = skin->clusters.data[c];
                break;
            }
        }
        if (!cluster) {
            outError = "Invalid skin cluster.";
            return false;
        }

        SkeletonBone bone;
        bone.name = BoneName(node, settings.stripBoneNamespace);
        bone.parentEval = parentSource;
        bone.sourceJoint = source;
        bone.localRest = ConvertSkeletalMatrix(FbxDocument::ToMat4(ClosedLocalMatrix(node, parentJoint)),
                                               settings.unitScale);
        bone.inverseBind =
            ConvertSkeletalMatrix(FbxDocument::ToMat4(cluster->geometry_to_bone), settings.unitScale);
        bone.restPrefix = ConvertSkeletalMatrix(
            FbxDocument::ToMat4(ClosedLocalMatrix(node->parent, parentJoint)), settings.unitScale);
        bone.restTranslation = ConvertSkeletalPoint(FbxDocument::ToVec3(node->local_transform.translation),
                                                    settings.unitScale);
        bone.restRotation = FbxDocument::ToQuat(node->local_transform.rotation);
        bone.restScale = FbxDocument::ToVec3(node->local_transform.scale);
        skeleton.bones[static_cast<size_t>(source)] = std::move(bone);
    }

    return SkeletonBuilder::Finalize(skeleton, outError);
}

bool AppendSkinnedMesh(ufbx_node* meshNode,
                       ufbx_skin_deformer* skin,
                       const SkeletalImportSettings& settings,
                       const Skeleton& skeleton,
                       const std::unordered_map<ufbx_node*, int>& nodeToSource,
                       SkeletalImportResult& outResult,
                       std::string& outError) {
    ufbx_mesh* mesh = meshNode->mesh;
    if (!mesh || !mesh->vertex_position.exists) {
        outError = "Skinned mesh has no vertex positions.";
        return false;
    }
    if (mesh->blend_deformers.count > 0) {
        outError = "Morph targets are not supported on character meshes.";
        return false;
    }

    std::vector<uint32_t> tri(std::max<size_t>(mesh->max_face_triangles * 3, 3));
    Vec3 minB(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max());
    Vec3 maxB(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max());

    const size_t partCount = mesh->material_parts.count > 0 ? mesh->material_parts.count : 1;
    for (size_t partIx = 0; partIx < partCount; ++partIx) {
        const ufbx_mesh_part* part =
            mesh->material_parts.count > 0 ? &mesh->material_parts.data[partIx] : nullptr;
        SkinnedSubmeshData submesh;
        submesh.indexOffset = static_cast<uint32_t>(outResult.indices.size());
        submesh.materialSlot = static_cast<int>(outResult.submeshes.size());
        std::string slotName;
        ufbx_material* material = nullptr;
        if (meshNode->materials.count > partIx) {
            material = meshNode->materials.data[partIx];
        } else if (mesh->materials.count > partIx) {
            material = mesh->materials.data[partIx];
        }
        if (material) {
            slotName = FbxDocument::ToString(material->name);
        }
        if (slotName.empty()) {
            slotName = FbxDocument::ToString(mesh->name);
        }
        if (slotName.empty()) {
            slotName = FbxDocument::ToString(meshNode->name);
        }
        submesh.name = slotName.empty() ? "Submesh" : slotName;

        const size_t faceCount = part ? part->num_faces : mesh->faces.count;
        for (size_t fi = 0; fi < faceCount; ++fi) {
            const uint32_t faceIndex =
                part ? part->face_indices.data[fi] : static_cast<uint32_t>(fi);
            const ufbx_face face = mesh->faces.data[faceIndex];
            if (face.num_indices < 3) {
                continue;
            }
            const uint32_t triCount = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
            for (uint32_t t = 0; t < triCount; ++t) {
                for (uint32_t c = 0; c < 3; ++c) {
                    const uint32_t index = tri[t * 3 + c];
                    SkinnedVertexData vert{};
                    const Vec3 pos = ConvertSkeletalPoint(
                        FbxDocument::ToVec3(ufbx_get_vertex_vec3(&mesh->vertex_position, index)),
                        settings.unitScale);
                    vert.pos[0] = pos.x;
                    vert.pos[1] = pos.y;
                    vert.pos[2] = pos.z;
                    minB.x = std::min(minB.x, pos.x);
                    minB.y = std::min(minB.y, pos.y);
                    minB.z = std::min(minB.z, pos.z);
                    maxB.x = std::max(maxB.x, pos.x);
                    maxB.y = std::max(maxB.y, pos.y);
                    maxB.z = std::max(maxB.z, pos.z);

                    Vec3 normal(0.0f, 1.0f, 0.0f);
                    if (mesh->vertex_normal.exists) {
                        normal = FbxDocument::ToVec3(ufbx_get_vertex_vec3(&mesh->vertex_normal, index));
                    }
                    vert.normal[0] = normal.x;
                    vert.normal[1] = normal.y;
                    vert.normal[2] = normal.z;
                    vert.color[0] = 1.0f;
                    vert.color[1] = 1.0f;
                    vert.color[2] = 1.0f;
                    if (mesh->vertex_uv.exists) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                        vert.texCoord[0] = static_cast<float>(uv.x);
                        vert.texCoord[1] = static_cast<float>(uv.y);
                    }

                    std::vector<Influence> influences;
                    const uint32_t vertexIx = mesh->vertex_position.indices.data[index];
                    if (vertexIx < skin->vertices.count) {
                        const ufbx_skin_vertex sv = skin->vertices.data[vertexIx];
                        for (uint32_t w = 0; w < sv.num_weights; ++w) {
                            const ufbx_skin_weight weight =
                                skin->weights.data[sv.weight_begin + w];
                            if (weight.cluster_index >= skin->clusters.count) {
                                outError = "Invalid skin cluster.";
                                return false;
                            }
                            ufbx_node* boneNode =
                                skin->clusters.data[weight.cluster_index]->bone_node;
                            auto it = nodeToSource.find(boneNode);
                            if (it == nodeToSource.end()) {
                                outError = "Invalid skin cluster.";
                                return false;
                            }
                            const int eval = skeleton.sourceJointToEval.empty()
                                                 ? it->second
                                                 : skeleton.sourceJointToEval[static_cast<size_t>(it->second)];
                            influences.push_back(
                                {static_cast<uint16_t>(eval), static_cast<float>(weight.weight)});
                        }
                    }
                    bool clamped = false;
                    ClampInfluences(influences, clamped);
                    if (clamped) {
                        outResult.clampedInfluences = true;
                    }
                    for (int i = 0; i < kMaxBoneInfluences; ++i) {
                        if (i < static_cast<int>(influences.size())) {
                            vert.joints[i] = influences[static_cast<size_t>(i)].joint;
                            vert.weights[i] = influences[static_cast<size_t>(i)].weight;
                        }
                    }

                    outResult.indices.push_back(static_cast<uint32_t>(outResult.vertices.size()));
                    outResult.vertices.push_back(vert);
                }
            }
        }
        submesh.indexCount =
            static_cast<uint32_t>(outResult.indices.size()) - submesh.indexOffset;
        if (submesh.indexCount > 0) {
            outResult.submeshes.push_back(submesh);
        }
    }
    outResult.bounds = AABB(minB, maxB);
    if (outResult.vertices.empty()) {
        outError = "Unsupported geometry.";
        return false;
    }
    return true;
}

bool AlmostEqualVec3(const Vec3& a, const Vec3& b) {
    return std::abs(a.x - b.x) <= 1e-5f && std::abs(a.y - b.y) <= 1e-5f &&
           std::abs(a.z - b.z) <= 1e-5f;
}

bool AlmostEqualQuat(const Vec4& a, const Vec4& b) {
    const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return std::abs(std::abs(dot) - 1.0f) <= 1e-4f ||
           (std::abs(a.x - b.x) <= 1e-5f && std::abs(a.y - b.y) <= 1e-5f &&
            std::abs(a.z - b.z) <= 1e-5f && std::abs(a.w - b.w) <= 1e-5f);
}

bool HasAnimatedHelpers(ufbx_node* node,
                        ufbx_node* deformingParent,
                        const ufbx_baked_anim* bake) {
    ufbx_node* current = node ? node->parent : nullptr;
    while (current && current != deformingParent && !current->is_root) {
        for (size_t i = 0; i < bake->nodes.count; ++i) {
            if (bake->nodes.data[i].typed_id == current->typed_id) {
                const ufbx_baked_node& bn = bake->nodes.data[i];
                if (!bn.constant_translation || !bn.constant_rotation || !bn.constant_scale) {
                    return true;
                }
            }
        }
        current = current->parent;
    }
    return false;
}

const ufbx_baked_node* FindBakedNode(const ufbx_baked_anim* bake, uint32_t typedId) {
    for (size_t i = 0; i < bake->nodes.count; ++i) {
        if (bake->nodes.data[i].typed_id == typedId) {
            return &bake->nodes.data[i];
        }
    }
    return nullptr;
}

void AppendLinearVecChannel(AnimationClip& clip,
                            int evalBone,
                            AnimationPath path,
                            const std::vector<float>& times,
                            const std::vector<Vec3>& values) {
    if (times.empty() || values.empty()) {
        return;
    }
    AnimationChannel channel;
    channel.evalBone = evalBone;
    channel.path = path;
    channel.interpolation = AnimationInterpolation::Linear;
    channel.times = times;
    channel.vectors = values;
    clip.channels.push_back(std::move(channel));
}

void AppendLinearQuatChannel(AnimationClip& clip,
                             int evalBone,
                             const std::vector<float>& times,
                             const std::vector<Vec4>& values) {
    if (times.empty() || values.empty()) {
        return;
    }
    AnimationChannel channel;
    channel.evalBone = evalBone;
    channel.path = AnimationPath::Rotation;
    channel.interpolation = AnimationInterpolation::Linear;
    channel.times = times;
    channel.rotations = values;
    clip.channels.push_back(std::move(channel));
}

bool BakeClips(ufbx_scene* scene,
               const SkeletalImportSettings& settings,
               const Skeleton& skeleton,
               const std::unordered_map<ufbx_node*, int>& nodeToSource,
               const std::unordered_set<ufbx_node*>& deforming,
               std::vector<AnimationClip>& outClips,
               std::string& outError) {
    outClips.clear();
    double sampleRate = settings.animationSampleRate > 0.0f
                            ? static_cast<double>(settings.animationSampleRate)
                            : scene->settings.frames_per_second;
    if (sampleRate <= 0.0) {
        sampleRate = 30.0;
    }

    std::vector<ufbx_anim*> anims;
    std::vector<std::string> names;
    if (scene->anim_stacks.count > 0) {
        for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
            ufbx_anim_stack* stack = scene->anim_stacks.data[i];
            if (!stack || !stack->anim) {
                continue;
            }
            anims.push_back(stack->anim);
            names.push_back(FbxDocument::ToString(stack->name));
        }
    } else if (scene->anim) {
        anims.push_back(scene->anim);
        names.emplace_back("Take");
    }
    if (anims.empty()) {
        return true;
    }

    ufbx_bake_opts bakeOpts = {};
    bakeOpts.resample_rate = sampleRate;
    bakeOpts.step_handling = UFBX_BAKE_STEP_HANDLING_IGNORE;
    bakeOpts.key_reduction_enabled = true;
    bakeOpts.key_reduction_rotation = true;

    for (size_t a = 0; a < anims.size(); ++a) {
        ufbx_error error;
        ufbx_baked_anim* bake = ufbx_bake_anim(scene, anims[a], &bakeOpts, &error);
        if (!bake) {
            outError = error.description.length
                           ? std::string(error.description.data, error.description.length)
                           : "Failed to bake FBX animation.";
            return false;
        }

        AnimationClip clip;
        clip.name = names[a].empty() ? "Take" : names[a];
        clip.sourceClip = clip.name;
        clip.skeletonSignature = skeleton.signature;
        clip.startTime = static_cast<float>(bake->playback_time_begin);
        clip.endTime = static_cast<float>(bake->playback_time_end);
        clip.duration = static_cast<float>(bake->playback_duration);
        if (clip.endTime <= clip.startTime) {
            clip.endTime = clip.startTime + clip.duration;
        }

        for (const SkeletonBone& bone : skeleton.bones) {
            ufbx_node* node = nullptr;
            for (const auto& entry : nodeToSource) {
                if (entry.second == bone.sourceJoint || entry.second == bone.evalIndex) {
                    node = entry.first;
                    break;
                }
            }
            if (!node) {
                continue;
            }
            ufbx_node* parentJoint = DeformingParent(node, deforming);
            const bool helpersAnimated = HasAnimatedHelpers(node, parentJoint, bake);
            const ufbx_baked_node* baked = FindBakedNode(bake, node->typed_id);

            if (helpersAnimated) {
                const double begin = bake->playback_time_begin;
                const double end = bake->playback_time_end;
                const double step = 1.0 / sampleRate;
                std::vector<float> times;
                std::vector<Vec3> translations;
                std::vector<Vec4> rotations;
                std::vector<Vec3> scales;
                for (double t = begin; t <= end + 1e-8; t += step) {
                    ufbx_matrix acc = ufbx_identity_matrix;
                    ufbx_node* current = node;
                    while (current && current != parentJoint && !current->is_root) {
                        const ufbx_transform local =
                            ufbx_evaluate_transform(anims[a], current, t);
                        const ufbx_matrix localM = ufbx_transform_to_matrix(&local);
                        acc = ufbx_matrix_mul(&localM, &acc);
                        current = current->parent;
                    }
                    const ufbx_transform composed = ufbx_matrix_to_transform(&acc);
                    times.push_back(static_cast<float>(t));
                    translations.push_back(ConvertSkeletalPoint(FbxDocument::ToVec3(composed.translation),
                                                                settings.unitScale));
                    rotations.push_back(FbxDocument::ToQuat(composed.rotation));
                    scales.push_back(FbxDocument::ToVec3(composed.scale));
                }
                AppendLinearVecChannel(clip, bone.evalIndex, AnimationPath::Translation, times,
                                       translations);
                AppendLinearQuatChannel(clip, bone.evalIndex, times, rotations);
                AppendLinearVecChannel(clip, bone.evalIndex, AnimationPath::Scale, times, scales);
                continue;
            }

            if (!baked) {
                continue;
            }

            auto convertTimes = [](const auto& keys) {
                std::vector<float> times;
                times.reserve(keys.count);
                for (size_t i = 0; i < keys.count; ++i) {
                    times.push_back(static_cast<float>(keys.data[i].time));
                }
                return times;
            };

            if (!baked->constant_translation ||
                baked->translation_keys.count == 0 ||
                !AlmostEqualVec3(ConvertSkeletalPoint(FbxDocument::ToVec3(baked->translation_keys.data[0].value),
                                                      settings.unitScale),
                                 bone.restTranslation)) {
                std::vector<Vec3> values;
                for (size_t i = 0; i < baked->translation_keys.count; ++i) {
                    values.push_back(ConvertSkeletalPoint(
                        FbxDocument::ToVec3(baked->translation_keys.data[i].value),
                        settings.unitScale));
                }
                if (baked->constant_translation && values.size() > 1) {
                    values.resize(1);
                    std::vector<float> times = {static_cast<float>(baked->translation_keys.data[0].time)};
                    AppendLinearVecChannel(clip, bone.evalIndex, AnimationPath::Translation, times,
                                           values);
                } else {
                    AppendLinearVecChannel(clip, bone.evalIndex, AnimationPath::Translation,
                                           convertTimes(baked->translation_keys), values);
                }
            }

            if (!baked->constant_rotation || baked->rotation_keys.count == 0 ||
                !AlmostEqualQuat(FbxDocument::ToQuat(baked->rotation_keys.data[0].value),
                                 bone.restRotation)) {
                std::vector<Vec4> values;
                for (size_t i = 0; i < baked->rotation_keys.count; ++i) {
                    values.push_back(FbxDocument::ToQuat(baked->rotation_keys.data[i].value));
                }
                if (baked->constant_rotation && values.size() > 1) {
                    values.resize(1);
                    std::vector<float> times = {static_cast<float>(baked->rotation_keys.data[0].time)};
                    AppendLinearQuatChannel(clip, bone.evalIndex, times, values);
                } else {
                    AppendLinearQuatChannel(clip, bone.evalIndex, convertTimes(baked->rotation_keys),
                                            values);
                }
            }

            if (!baked->constant_scale || baked->scale_keys.count == 0 ||
                !AlmostEqualVec3(FbxDocument::ToVec3(baked->scale_keys.data[0].value), bone.restScale)) {
                std::vector<Vec3> values;
                for (size_t i = 0; i < baked->scale_keys.count; ++i) {
                    values.push_back(FbxDocument::ToVec3(baked->scale_keys.data[i].value));
                }
                if (baked->constant_scale && values.size() > 1) {
                    values.resize(1);
                    std::vector<float> times = {static_cast<float>(baked->scale_keys.data[0].time)};
                    AppendLinearVecChannel(clip, bone.evalIndex, AnimationPath::Scale, times, values);
                } else {
                    AppendLinearVecChannel(clip, bone.evalIndex, AnimationPath::Scale,
                                           convertTimes(baked->scale_keys), values);
                }
            }
        }

        ufbx_free_baked_anim(bake);
        if (!clip.channels.empty() || clip.duration > 0.0f) {
            outClips.push_back(std::move(clip));
        }
    }
    return true;
}

bool BindClipsByName(ufbx_scene* scene,
                     const SkeletalImportSettings& settings,
                     const Skeleton& character,
                     std::vector<AnimationClip>& outClips,
                     std::string& outError) {
    std::unordered_map<ufbx_node*, int> nodeToSource;
    std::unordered_set<ufbx_node*> deforming;
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        ufbx_node* node = scene->nodes.data[i];
        const std::string name = BoneName(node, settings.stripBoneNamespace);
        if (name.empty()) {
            continue;
        }
        const SkeletonBone* bone = character.FindBone(name);
        if (!bone) {
            continue;
        }
        deforming.insert(node);
        nodeToSource[node] = bone->evalIndex;
    }
    Skeleton bound = character;
    return BakeClips(scene, settings, bound, nodeToSource, deforming, outClips, outError);
}

} // namespace

bool FbxSkeletalImporter::Load(const std::filesystem::path& path,
                               const SkeletalImportSettings& settings,
                               SkeletalImportResult& outResult,
                               std::string& outError) {
    outResult = {};
    outError.clear();
    FbxDocument document;
    FbxLoadSettings loadSettings;
    loadSettings.spaceConversion = settings.fbxSpaceConversion;
    loadSettings.skeletalMesh = true;
    if (!document.Load(path, loadSettings, outError)) {
        return false;
    }
    ufbx_scene* scene = document.Scene();
    ufbx_node* meshNode = nullptr;
    ufbx_skin_deformer* skin = nullptr;
    if (!FindSkinnedMesh(scene, meshNode, skin, outError)) {
        return false;
    }

    std::unordered_map<ufbx_node*, int> nodeToSource;
    std::unordered_set<ufbx_node*> deforming;
    if (!BuildSkeleton(skin, settings, outResult.skeleton, nodeToSource, deforming, outError)) {
        return false;
    }
    if (!AppendSkinnedMesh(meshNode, skin, settings, outResult.skeleton, nodeToSource, outResult,
                           outError)) {
        return false;
    }
    outResult.meshNodeWorld =
        ConvertSkeletalMatrix(FbxDocument::ToMat4(meshNode->geometry_to_world), settings.unitScale);

    std::string restError;
    if (!SkeletonBuilder::RestMatchesBind(outResult.skeleton, outResult.meshNodeWorld, 1e-3f,
                                          restError)) {
        outResult.restBindMismatch = true;
        outResult.warnings.push_back(restError);
    }
    if (outResult.clampedInfluences) {
        outResult.warnings.push_back("Vertex influences were clamped to 4.");
    }
    outResult.warnings.push_back(
        "Hephaestus imports the resulting skeletal animation, not the author's DCC control rig.");

    if (!BakeClips(scene, settings, outResult.skeleton, nodeToSource, deforming, outResult.clips,
                   outError)) {
        return false;
    }
    outResult.importedFromFbx = true;
    outResult.fbxResolvedSpaceConversion = document.ResolvedSpaceConversion();
    return true;
}

bool FbxSkeletalImporter::LoadClips(const std::filesystem::path& path,
                                    const SkeletalImportSettings& settings,
                                    const Skeleton& character,
                                    std::vector<AnimationClip>& outClips,
                                    std::string& outError) {
    outClips.clear();
    outError.clear();
    FbxDocument document;
    FbxLoadSettings loadSettings;
    loadSettings.spaceConversion = settings.fbxSpaceConversion;
    loadSettings.skeletalMesh = true;
    if (!document.Load(path, loadSettings, outError)) {
        return false;
    }
    ufbx_scene* scene = document.Scene();
    if (document.ContainsSkin()) {
        SkeletalImportResult imported;
        if (!Load(path, settings, imported, outError)) {
            return false;
        }
        const SkeletonCompatibilityReport report =
            SkeletonBuilder::Compare(character, imported.skeleton);
        if (!report.compatible) {
            outError = FormatSkeletonIncompatibility(report);
            return false;
        }
        outClips = std::move(imported.clips);
        return true;
    }
    return BindClipsByName(scene, settings, character, outClips, outError);
}

} // namespace Hephaestus
