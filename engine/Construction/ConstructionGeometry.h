#pragma once

#include "ConstructionShape.h"

#include "../Assets/MeshAsset.h"
#include "../Core/Math.h"

#include <cstdint>
#include <vector>

namespace Hephaestus::Construction {

struct LocalBox {
    Vec3 min;
    Vec3 max;
};

// A round collider for shapes that are not made of boxes.
struct RoundCollider {
    enum class Kind : std::uint8_t { Sphere, Cylinder } kind = Kind::Sphere;
    Vec3 center;
    float radius = 0.5f;
    float height = 1.0f;  // cylinders only
};

// Everything derived from one construction entity, in its local frame.
struct EvaluatedGeometry {
    MeshData mesh;
    std::vector<FaceTag> triangleTags;  // one per triangle
    std::vector<LocalBox> solids;       // disjoint boxes covering the solid volume, for collision
    std::vector<RoundCollider> rounds;  // collision for cylinders and spheres
};

// A wall of a box in local space: u runs along the wall (the direction OpeningParams::offset is measured in),
// v is up, normal points out of the box.
struct WallFrame {
    Vec3 center;   // centre of the wall's outer face at the box's base
    Vec3 u;
    Vec3 normal;
    float halfLength = 0.0f;
    float height = 0.0f;
    float thickness = 0.0f;  // how deep an opening in this wall cuts
};

WallFrame GetWallFrame(const BoxParams& box, WallSide side);

// Openings are evaluated in a canonical order, so the result does not depend on child order.
// cuts are subtract boxes already in this box's local frame.
bool EvaluateBox(const BoxParams& box, std::vector<OpeningParams> openings, EvaluatedGeometry& out,
                 const std::vector<LocalBox>& cuts = {});

// Solid stairs climbing towards +Z, one step per column; the steps' tops and fronts are the walkable faces.
bool EvaluateStairs(const BoxParams& size, int steps, EvaluatedGeometry& out);

// Evaluates any shape with its own geometry (HasOwnGeometry), using the openings for boxes.
bool EvaluateShape(const ConstructionShapeComponent& shape, std::vector<OpeningParams> openings,
                   EvaluatedGeometry& out, const std::vector<LocalBox>& cuts = {});

// Hash of every input that changes the evaluated output (parameters, openings and the evaluator version).
std::uint64_t HashInputs(const ConstructionShapeComponent& shape, std::vector<OpeningParams> openings,
                         const std::vector<LocalBox>& cuts = {});

// Byte-level hash of an evaluation result, used to prove regeneration is deterministic.
std::uint64_t HashGeometry(const EvaluatedGeometry& geometry);

void SortOpenings(std::vector<OpeningParams>& openings);

} // namespace Hephaestus::Construction
