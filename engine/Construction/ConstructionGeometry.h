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
    std::vector<std::vector<Vec3>> hulls;  // convex collision pieces that are not axis-aligned boxes
};

// A wall path segment in the wall's local frame: d runs from its first point to its second, n is d turned a
// quarter turn to the left seen from above (the +Z face of the segment's own box), mid is its centre on the ground.
struct WallSegmentFrame {
    Vec3 mid;
    Vec3 d;
    Vec3 n;
    float length = 0.0f;
};

// False for a segment index out of range or a segment too short to have a direction.
bool GetWallSegmentFrame(const ConstructionShapeComponent& shape, int segment, WallSegmentFrame& out);

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
// The face of a box or wall host an opening is placed on: a box side, or the PosZ or NegZ face of a wall segment.
bool GetHostWallFrame(const ConstructionShapeComponent& host, WallSide side, int segment, WallFrame& out);
// Height of the top of the host's floor above its base, where doors stand.
float HostFloorTop(const ConstructionShapeComponent& host);

// Openings are evaluated in a canonical order, so the result does not depend on child order.
// cuts are subtract boxes already in this box's local frame.
bool EvaluateBox(const BoxParams& box, std::vector<OpeningParams> openings, EvaluatedGeometry& out,
                 const std::vector<LocalBox>& cuts = {});

// Solid stairs climbing towards +Z, one step per column; the steps' tops and fronts are the walkable faces. Turning
// stairs (StairsParams::turn) put half the steps in each flight with a landing of one step between them.
bool EvaluateStairs(const BoxParams& size, int steps, EvaluatedGeometry& out, const StairsParams& params = {});

// A solid slope from the ground at -Z up to height at +Z; it collides as one convex hull.
bool EvaluateRamp(const BoxParams& size, EvaluatedGeometry& out);
// Fan-triangulates each face; collision is the convex hull of all the points.
bool EvaluateEditableMesh(const EditableMesh& mesh, EvaluatedGeometry& out);

// A wall of BoxParams height and wallThickness along the shape's path. Each segment is a solid box, so its openings
// (by OpeningParams::segment) and the cuts work as on blocks; corners are filled with mitred prisms, and a closed
// path with a floor gets a slab over the whole path. Corners and floors are not cut.
bool EvaluateWall(const ConstructionShapeComponent& shape, const std::vector<OpeningParams>& openings,
                  EvaluatedGeometry& out, const std::vector<LocalBox>& cuts = {});

// Triangles covering a simple polygon on the XZ plane, as index triples, wound counter-clockwise in (x, z).
bool PointInPolygon(const std::vector<WallPoint>& polygon, float x, float z);
std::vector<int> TriangulatePolygon(const std::vector<WallPoint>& polygon);

// Evaluates any shape with its own geometry (HasOwnGeometry), using the openings for boxes.
bool EvaluateShape(const ConstructionShapeComponent& shape, std::vector<OpeningParams> openings,
                   EvaluatedGeometry& out, const std::vector<LocalBox>& cuts = {});

// Hash of every input that changes the evaluated output (parameters, openings and the evaluator version).
std::uint64_t HashInputs(const ConstructionShapeComponent& shape, std::vector<OpeningParams> openings,
                         const std::vector<LocalBox>& cuts = {});

// Groups the triangles of painted faces into one submesh per material, slot 0 keeping the shape's own material.
// Returns the material of each slot (slot 0 is empty).
std::vector<std::string> AssignFaceMaterials(const std::vector<FaceMaterial>& faces, EvaluatedGeometry& geometry);

// Byte-level hash of an evaluation result, used to prove regeneration is deterministic.
std::uint64_t HashGeometry(const EvaluatedGeometry& geometry);

void SortOpenings(std::vector<OpeningParams>& openings);

} // namespace Hephaestus::Construction
