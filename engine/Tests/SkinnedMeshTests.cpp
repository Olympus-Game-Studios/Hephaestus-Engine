/**
 * @file SkinnedMeshTests.cpp
 * @brief Stage C rest-pose palette and SkinnedVertex layout checks.
 */

#include "Animation/Skeleton.h"
#include "Assets/SkeletalModelImporter.h"
#include "Gfx/Vertex.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

using Hephaestus::Skeleton;
using Hephaestus::SkeletonBone;
using Hephaestus::SkeletonBuilder;
using Hephaestus::SkeletalImportResult;
using Hephaestus::SkeletalModelImporter;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
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

bool TestVertexLayout() {
    bool ok = true;
    ok &= Check(offsetof(SkinnedVertex, pos) == 0, "pos at 0");
    ok &= Check(offsetof(SkinnedVertex, normal) == 12, "normal at 12");
    ok &= Check(offsetof(SkinnedVertex, color) == 24, "color at 24");
    ok &= Check(offsetof(SkinnedVertex, texCoord) == 36, "uv at 36");
    ok &= Check(offsetof(SkinnedVertex, joints) == 44, "joints at 44");
    ok &= Check(offsetof(SkinnedVertex, weights) == 52, "weights at 52");
    ok &= Check(sizeof(SkinnedVertex) == 68, "SkinnedVertex is 68 bytes");
    const auto attribs = SkinnedVertex::getAttributeDescriptions();
    ok &= Check(attribs.size() == 6, "six vertex attributes");
    ok &= Check(attribs[4].location == 4 && attribs[4].format == VK_FORMAT_R16G16B16A16_UINT,
                "location 4 is u16vec4 joints");
    ok &= Check(attribs[5].location == 5 && attribs[5].format == VK_FORMAT_R32G32B32A32_SFLOAT,
                "location 5 is vec4 weights");
    return ok;
}

bool TestRestPaletteIdentitySkinning() {
    bool ok = true;
    const float kPiOver2 = 1.5707963267948966f;
    Skeleton skeleton;
    SkeletonBone root;
    root.name = "Root";
    root.parentEval = -1;
    root.sourceJoint = 0;
    root.localRest = Mat4::RotationX(kPiOver2);
    root.inverseBind = Mat4::Multiply(Mat4::Invert(root.localRest),
                                      Mat4::Translation(Vec3(0.0f, 0.0f, 2.0f)));
    skeleton.bones.push_back(root);
    std::string error;
    ok &= Check(SkeletonBuilder::Finalize(skeleton, error), "finalize: " + error);

    std::vector<Mat4> palette;
    SkeletonBuilder::FillRestBonePalette(skeleton, palette);
    ok &= Check(palette.size() == 128, "palette padded to 128");
    ok &= Check(MatricesClose(palette[0], Mat4::Translation(Vec3(0.0f, 0.0f, 2.0f))),
                "restWorld * IBM equals mesh-node world (T 0,0,2)");
    const Vec3 skinned = Mat4::TransformPoint(palette[0], Vec3(1.0f, 0.0f, 0.0f));
    ok &= Check(AlmostEqual(skinned.x, 1.0f) && AlmostEqual(skinned.y, 0.0f) &&
                    AlmostEqual(skinned.z, 2.0f),
                "palette skins mesh-local (1,0,0) to (1,0,2)");
    ok &= Check(MatricesClose(palette[1], Mat4::Identity()), "unused slots stay identity");
    return ok;
}

bool TestRestFixturePalette() {
    SkeletalImportResult fixture;
    std::vector<Mat4> palette;
    SkeletalModelImporter::BuildRestPoseFixture(fixture, palette);
    bool ok = Check(fixture.skeleton.BoneCount() == 2, "fixture has two bones");
    ok &= Check(!fixture.vertices.empty() && !fixture.indices.empty(), "fixture has geometry");
    const SkeletonBone* root = fixture.skeleton.FindBone("Root");
    const SkeletonBone* child = fixture.skeleton.FindBone("Child");
    if (!Check(root && child, "fixture names")) {
        return false;
    }
    ok &= Check(MatricesClose(palette[static_cast<size_t>(root->evalIndex)], Mat4::Identity()),
                "Root rest palette is identity");
    ok &= Check(MatricesClose(palette[static_cast<size_t>(child->evalIndex)], Mat4::Identity()),
                "Child rest palette is identity (bind == rest)");
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= TestVertexLayout();
    ok &= TestRestPaletteIdentitySkinning();
    ok &= TestRestFixturePalette();
    if (ok) {
        std::cout << "Skinned mesh tests passed.\n";
        return 0;
    }
    return 1;
}
