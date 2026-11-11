#include "Skeleton.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace Hephaestus {
namespace {

int Quantize(float value) {
    return static_cast<int>(std::lround(static_cast<double>(value) * 10000.0));
}

void HashMix(uint64_t& hash, uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}

void HashString(uint64_t& hash, const std::string& value) {
    for (unsigned char c : value) {
        HashMix(hash, static_cast<uint64_t>(c));
    }
    HashMix(hash, 0xFF);
}

void HashMatrixRest(uint64_t& hash, const Mat4& localRest) {
    for (int i = 0; i < 16; ++i) {
        HashMix(hash, static_cast<uint64_t>(static_cast<uint32_t>(Quantize(localRest.m[i]))));
    }
}

std::string Hex16(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

struct IdentityRecord {
    std::string name;
    std::string parentName;
    Mat4 localRest;
    int evalIndex = -1;
};

std::vector<IdentityRecord> IdentityRecords(const Skeleton& skeleton) {
    std::vector<IdentityRecord> records;
    records.reserve(skeleton.bones.size());
    for (const SkeletonBone& bone : skeleton.bones) {
        IdentityRecord record;
        record.name = bone.name;
        if (bone.parentEval >= 0 && bone.parentEval < skeleton.BoneCount()) {
            record.parentName = skeleton.bones[static_cast<size_t>(bone.parentEval)].name;
        }
        record.localRest = bone.localRest;
        record.evalIndex = bone.evalIndex;
        records.push_back(std::move(record));
    }
    std::sort(records.begin(), records.end(), [](const IdentityRecord& a, const IdentityRecord& b) {
        return a.name < b.name;
    });
    return records;
}

} // namespace

const SkeletonBone* Skeleton::FindBone(const std::string& name) const {
    for (const SkeletonBone& bone : bones) {
        if (bone.name == name) {
            return &bone;
        }
    }
    return nullptr;
}

int Skeleton::FindBoneByBaseName(const std::string& baseName) const {
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const std::string wanted = lower(baseName);
    for (int i = 0; i < BoneCount(); ++i) {
        const std::string& name = bones[static_cast<size_t>(i)].name;
        const size_t colon = name.find_last_of(':');
        if (lower(colon == std::string::npos ? name : name.substr(colon + 1)) == wanted) {
            return i;
        }
    }
    return -1;
}

Mat4 ConvertSkeletalMatrix(const Mat4& gltfMatrix, float unitScale) {
    if (std::abs(unitScale - 1.0f) <= 1e-8f) {
        return gltfMatrix;
    }
    const Mat4 scale = Mat4::Scale(Vec3(unitScale, unitScale, unitScale));
    const float inv = (std::abs(unitScale) > 1e-12f) ? (1.0f / unitScale) : 1.0f;
    const Mat4 invScale = Mat4::Scale(Vec3(inv, inv, inv));
    return Mat4::Multiply(scale, Mat4::Multiply(gltfMatrix, invScale));
}

Vec3 ConvertSkeletalPoint(const Vec3& gltfPoint, float unitScale) {
    return Vec3(gltfPoint.x * unitScale, gltfPoint.y * unitScale, gltfPoint.z * unitScale);
}

bool SkeletonBuilder::Finalize(Skeleton& skeleton, std::string& outError) {
    const int count = skeleton.BoneCount();
    if (count <= 0) {
        outError = "Skeleton has no bones.";
        return false;
    }
    if (count > kMaxCharacterBones) {
        outError = "Skeleton has " + std::to_string(count) + " bones; maximum is " +
                   std::to_string(kMaxCharacterBones) + ".";
        return false;
    }

    std::unordered_map<std::string, int> nameCount;
    for (const SkeletonBone& bone : skeleton.bones) {
        if (bone.name.empty()) {
            outError =
                "Character skeleton contains unnamed bones.\n\n"
                "Hephaestus requires stable bone names so animation compatibility can be verified.\n\n"
                "Name these bones in the source rig and export the character again.";
            return false;
        }
        nameCount[bone.name] += 1;
    }
    std::vector<std::string> duplicates;
    for (const auto& [name, n] : nameCount) {
        if (n > 1) {
            duplicates.push_back(name);
        }
    }
    if (!duplicates.empty()) {
        std::sort(duplicates.begin(), duplicates.end());
        outError = "Character skeleton contains duplicate bone names:\n";
        for (const std::string& name : duplicates) {
            outError += "- " + name + "\n";
        }
        outError += "\nRename these bones in the source rig so each name is unique.";
        return false;
    }

    std::vector<int> identityOrder(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        identityOrder[static_cast<size_t>(i)] = i;
    }
    std::sort(identityOrder.begin(), identityOrder.end(), [&](int a, int b) {
        return skeleton.bones[static_cast<size_t>(a)].name <
               skeleton.bones[static_cast<size_t>(b)].name;
    });

    std::vector<char> visiting(static_cast<size_t>(count), 0);
    std::vector<char> visited(static_cast<size_t>(count), 0);
    std::vector<int> evalFromCurrent;
    evalFromCurrent.reserve(static_cast<size_t>(count));

    std::function<bool(int)> visit = [&](int index) -> bool {
        if (index < 0 || index >= count) {
            outError = "Skeleton parent index is out of range.";
            return false;
        }
        if (visiting[static_cast<size_t>(index)]) {
            outError = "Skeleton hierarchy contains a cycle at bone '" +
                       skeleton.bones[static_cast<size_t>(index)].name + "'.";
            return false;
        }
        if (visited[static_cast<size_t>(index)]) {
            return true;
        }
        visiting[static_cast<size_t>(index)] = 1;
        const int parent = skeleton.bones[static_cast<size_t>(index)].parentEval;
        if (parent >= 0) {
            if (!visit(parent)) {
                return false;
            }
        }
        visiting[static_cast<size_t>(index)] = 0;
        visited[static_cast<size_t>(index)] = 1;
        evalFromCurrent.push_back(index);
        return true;
    };

    for (int index : identityOrder) {
        if (!visit(index)) {
            return false;
        }
    }

    std::vector<SkeletonBone> reordered;
    reordered.resize(static_cast<size_t>(count));
    std::vector<int> currentToEval(static_cast<size_t>(count), -1);
    for (int eval = 0; eval < count; ++eval) {
        currentToEval[static_cast<size_t>(evalFromCurrent[static_cast<size_t>(eval)])] = eval;
    }

    int maxSource = -1;
    for (const SkeletonBone& bone : skeleton.bones) {
        maxSource = std::max(maxSource, bone.sourceJoint);
    }
    skeleton.sourceJointToEval.assign(static_cast<size_t>(maxSource + 1), -1);

    for (int eval = 0; eval < count; ++eval) {
        const int old = evalFromCurrent[static_cast<size_t>(eval)];
        SkeletonBone bone = skeleton.bones[static_cast<size_t>(old)];
        if (bone.parentEval >= 0) {
            bone.parentEval = currentToEval[static_cast<size_t>(bone.parentEval)];
        }
        bone.evalIndex = eval;
        reordered[static_cast<size_t>(eval)] = std::move(bone);
        if (reordered[static_cast<size_t>(eval)].sourceJoint >= 0) {
            const int source = reordered[static_cast<size_t>(eval)].sourceJoint;
            if (source >= static_cast<int>(skeleton.sourceJointToEval.size())) {
                skeleton.sourceJointToEval.resize(static_cast<size_t>(source + 1), -1);
            }
            skeleton.sourceJointToEval[static_cast<size_t>(source)] = eval;
        }
    }
    skeleton.bones = std::move(reordered);

    skeleton.identityToEval.resize(static_cast<size_t>(count));
    std::vector<int> byName(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        byName[static_cast<size_t>(i)] = i;
    }
    std::sort(byName.begin(), byName.end(), [&](int a, int b) {
        return skeleton.bones[static_cast<size_t>(a)].name <
               skeleton.bones[static_cast<size_t>(b)].name;
    });
    for (int identity = 0; identity < count; ++identity) {
        const int eval = byName[static_cast<size_t>(identity)];
        skeleton.bones[static_cast<size_t>(eval)].identityIndex = identity;
        skeleton.identityToEval[static_cast<size_t>(identity)] = eval;
    }

    skeleton.sourceJointToIdentity.assign(skeleton.sourceJointToEval.size(), -1);
    for (size_t source = 0; source < skeleton.sourceJointToEval.size(); ++source) {
        const int eval = skeleton.sourceJointToEval[source];
        if (eval >= 0 && eval < skeleton.BoneCount()) {
            skeleton.sourceJointToIdentity[source] =
                skeleton.bones[static_cast<size_t>(eval)].identityIndex;
        }
    }

    skeleton.signature = ComputeSignature(skeleton);
    return true;
}

std::string SkeletonBuilder::ComputeSignature(const Skeleton& skeleton) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    const auto records = IdentityRecords(skeleton);
    HashMix(hash, static_cast<uint64_t>(records.size()));
    for (const IdentityRecord& record : records) {
        HashString(hash, record.name);
        HashString(hash, record.parentName);
        HashMatrixRest(hash, record.localRest);
    }
    return Hex16(hash);
}

SkeletonCompatibilityReport SkeletonBuilder::Compare(const Skeleton& character,
                                                     const Skeleton& other) {
    SkeletonCompatibilityReport report;
    const auto a = IdentityRecords(character);
    const auto b = IdentityRecords(other);
    std::unordered_map<std::string, const IdentityRecord*> aByName;
    std::unordered_map<std::string, const IdentityRecord*> bByName;
    for (const auto& rec : a) {
        aByName[rec.name] = &rec;
    }
    for (const auto& rec : b) {
        bByName[rec.name] = &rec;
    }

    for (const auto& rec : a) {
        if (!bByName.count(rec.name)) {
            report.missingBones.push_back(rec.name);
        }
    }
    for (const auto& rec : b) {
        if (!aByName.count(rec.name)) {
            report.extraBones.push_back(rec.name);
        }
    }

    for (const auto& rec : b) {
        auto it = aByName.find(rec.name);
        if (it == aByName.end()) {
            continue;
        }
        if (it->second->parentName != rec.parentName) {
            report.hierarchyMismatches.push_back(
                rec.name + " (source parent: " + rec.parentName +
                ", character parent: " + it->second->parentName + ")");
        }
    }

    if (report.missingBones.empty() && report.extraBones.empty() &&
        report.hierarchyMismatches.empty()) {
        if (ComputeSignature(character) != ComputeSignature(other)) {
            report.restPoseMismatch = true;
        }
    }

    report.compatible = report.missingBones.empty() && report.extraBones.empty() &&
                        report.hierarchyMismatches.empty() && !report.restPoseMismatch;
    return report;
}

bool SkeletonBuilder::MapsOnto(const Skeleton& part, const Skeleton& canonical) {
    if (part.BoneCount() <= 0 || canonical.BoneCount() <= 0) {
        return false;
    }
    const auto canonicalRecords = IdentityRecords(canonical);
    std::unordered_map<std::string, const IdentityRecord*> canonicalByName;
    for (const auto& rec : canonicalRecords) {
        canonicalByName[rec.name] = &rec;
    }
    const auto partRecords = IdentityRecords(part);
    for (const auto& rec : partRecords) {
        auto it = canonicalByName.find(rec.name);
        if (it == canonicalByName.end()) {
            return false;
        }
        if (it->second->parentName != rec.parentName) {
            return false;
        }
        for (int e = 0; e < 16; ++e) {
            if (std::abs(it->second->localRest.m[e] - rec.localRest.m[e]) > 1e-3f) {
                return false;
            }
        }
    }
    return true;
}

void SkeletonBuilder::EvaluateWorld(const Skeleton& skeleton,
                                    const std::vector<Mat4>& localPose,
                                    std::vector<Mat4>& outWorld) {
    const int count = skeleton.BoneCount();
    outWorld.assign(static_cast<size_t>(count), Mat4::Identity());
    for (int i = 0; i < count; ++i) {
        const Mat4& local =
            (i < static_cast<int>(localPose.size())) ? localPose[static_cast<size_t>(i)]
                                                     : skeleton.bones[static_cast<size_t>(i)].localRest;
        const int parent = skeleton.bones[static_cast<size_t>(i)].parentEval;
        if (parent < 0) {
            outWorld[static_cast<size_t>(i)] = local;
        } else {
            outWorld[static_cast<size_t>(i)] =
                Mat4::Multiply(outWorld[static_cast<size_t>(parent)], local);
        }
    }
}

void SkeletonBuilder::EvaluateRestWorld(const Skeleton& skeleton, std::vector<Mat4>& outWorld) {
    std::vector<Mat4> local;
    local.reserve(skeleton.bones.size());
    for (const SkeletonBone& bone : skeleton.bones) {
        local.push_back(bone.localRest);
    }
    EvaluateWorld(skeleton, local, outWorld);
}

bool SkeletonBuilder::RestMatchesBind(const Skeleton& skeleton,
                                      const Mat4& meshNodeWorld,
                                      float epsilon,
                                      std::string& outError) {
    std::vector<Mat4> restWorld;
    EvaluateRestWorld(skeleton, restWorld);
    std::vector<std::string> mismatches;
    for (int i = 0; i < skeleton.BoneCount(); ++i) {
        const Mat4 expected =
            Mat4::Multiply(Mat4::Invert(restWorld[static_cast<size_t>(i)]), meshNodeWorld);
        const Mat4& ibm = skeleton.bones[static_cast<size_t>(i)].inverseBind;
        bool close = true;
        for (int e = 0; e < 16; ++e) {
            if (std::abs(expected.m[e] - ibm.m[e]) > epsilon) {
                close = false;
                break;
            }
        }
        if (!close) {
            mismatches.push_back(skeleton.bones[static_cast<size_t>(i)].name);
        }
    }
    if (mismatches.empty()) {
        outError.clear();
        return true;
    }
    outError = "Rest pose does not match inverse bind matrices for: ";
    for (size_t i = 0; i < mismatches.size(); ++i) {
        if (i > 0) {
            outError += ", ";
        }
        outError += mismatches[i];
    }
    return false;
}

void SkeletonBuilder::FillBonePalette(const Skeleton& skeleton,
                                      const std::vector<Mat4>& world,
                                      std::vector<Mat4>& outPalette) {
    outPalette.assign(static_cast<size_t>(kMaxCharacterBones), Mat4::Identity());
    const int count = skeleton.BoneCount();
    for (int i = 0; i < count && i < kMaxCharacterBones; ++i) {
        const Mat4& boneWorld = (i < static_cast<int>(world.size()))
                                    ? world[static_cast<size_t>(i)]
                                    : Mat4::Identity();
        outPalette[static_cast<size_t>(i)] =
            Mat4::Multiply(boneWorld, skeleton.bones[static_cast<size_t>(i)].inverseBind);
    }
}

void SkeletonBuilder::FillRestBonePalette(const Skeleton& skeleton, std::vector<Mat4>& outPalette) {
    std::vector<Mat4> restWorld;
    EvaluateRestWorld(skeleton, restWorld);
    FillBonePalette(skeleton, restWorld, outPalette);
}

} // namespace Hephaestus
