#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../Core/Math.h"
#include "../Scene/Json.h"

namespace Hephaestus::Construction {

enum class SourceKind : std::uint8_t { Recipe, Asset, EditableMesh };
// Plane, Cylinder and Sphere use BoxParams width, height and depth as their size and stand on their origin.
// Wall uses WallParams for its path and BoxParams height, wallThickness and floor. Ramp is a solid slope over BoxParams
// width and depth, rising to height towards +Z like stairs.
// Balcony: a slab wallThickness thick with railings on its +Z and both X edges, box.height tall in all, open on -Z
// (the wall it stands against).
enum class Recipe : std::uint8_t { Box, Opening, Stairs, Plane, Cylinder, Sphere, Wall, Ramp, Balcony };
// The four walls of a box an opening can cut, named by the wall's outward normal.
enum class WallSide : std::uint8_t { PosX, NegX, PosZ, NegZ };
enum class CollisionMode : std::uint8_t { Solid, None };
// Subtract boxes carve every additive block or room they overlap; they are editor-only and have no collision.
enum class Operation : std::uint8_t { Add, Subtract };

// Local frame: x in [-width/2, width/2], y in [0, height], z in [-depth/2, depth/2].
struct BoxParams {
    float width = 4.0f;
    float height = 3.0f;
    float depth = 4.0f;
    bool hollow = false;
    float wallThickness = 0.2f;
    bool floor = true;
    bool ceiling = false;
};

// A point of a wall's path on the shape's local ground plane.
struct WallPoint {
    float x = 0.0f;
    float z = 0.0f;
};

// A wall of the given thickness centred on a path of points. A closed path is a room, with a floor slab inside
// the path when BoxParams::floor is set.
struct WallParams {
    std::vector<WallPoint> points;
    bool closed = false;
};

// Placed on its host box's wall: offset is the opening centre along the wall from the wall's centre (along +X for the
// Z walls, along +Z for the X walls), sill is its bottom above the box's base.
struct OpeningParams {
    WallSide side = WallSide::PosZ;
    float offset = 0.0f;
    float sill = 0.0f;
    // 1.2 m: the narrowest doorway the default navmesh settings (agent radius 0.35 on a 0.2 m grid) can pass.
    float width = 1.2f;
    float height = 2.1f;
    // On a Wall host: the path segment (from point segment to the next) the opening is on; offset runs along it
    // from its middle. Ignored on boxes.
    int segment = 0;
};

// Where turning stairs go after their landing, seen climbing the first flight.
enum class StairTurn : std::uint8_t { Straight, Left, Right, UTurnLeft, UTurnRight };

// Stairs use BoxParams width, height and depth for their footprint and total rise, and climb towards +Z. Turning
// stairs climb a first flight flightWidth wide along one side, cross a landing in the far corner (or along the whole
// far side for a U-turn) and climb a second flight across the footprint (or back down its other side).
struct StairsParams {
    int steps = 0;          // 0 picks the fewest steps whose rise is at most maxRise
    float maxRise = 0.28f;  // just under the default Character Controller step height (0.3 m)
    StairTurn turn = StairTurn::Straight;
    float flightWidth = 1.2f;
};

// Floor and Ceiling are the inside faces of a room's floor and ceiling; Inner is the inside of its walls.
enum class FaceGroup : std::uint8_t { Top, Bottom, PosX, NegX, PosZ, NegZ, Inner, Reveal, Floor, Ceiling };

struct FaceTag {
    FaceGroup group = FaceGroup::Reveal;
    std::int8_t wall = -1;  // WallSide this face belongs to, or -1
    std::int16_t segment = -1;  // wall path segment this face belongs to, or -1
};

// A material painted on every face of a shape with this group, wall and segment.
struct FaceMaterial {
    FaceTag face;
    std::string material;  // project material asset path
};

// A shape edited corner by corner: points in the shape's local frame and faces as loops of point indices, wound
// anticlockwise seen from outside. Made from a block or ramp with MakeEditable.
struct EditableMesh {
    std::vector<Vec3> vertices;
    std::vector<std::vector<int>> faces;
};

// A kit piece picked by hand for one spot of a dressed shape, in place of the kit's own choice.
struct KitSwap {
    std::string slot;    // KitPlacement::slot
    std::string prefab;  // a piece of the kit with the same role and width
};

bool SameFace(const FaceTag& a, const FaceTag& b);
const char* FaceGroupName(FaceGroup group);
bool ParseFaceGroup(const std::string& name, FaceGroup& out);
// How the editor names a face, such as "Top" or "Inside of the +X wall".
std::string DescribeFace(const FaceTag& face);

constexpr int kEvaluatorVersion = 1;

const char* SourceKindName(SourceKind kind);
const char* RecipeName(Recipe recipe);
const char* WallSideName(WallSide side);
const char* CollisionModeName(CollisionMode mode);
const char* OperationName(Operation operation);
const char* StairTurnName(StairTurn turn);
bool ParseStairTurn(const std::string& name, StairTurn& out);
bool ParseRecipe(const std::string& name, Recipe& out);
bool ParseWallSide(const std::string& name, WallSide& out);

} // namespace Hephaestus::Construction

// Construction data on a scene entity. The mesh, the collision and the editor handles are all derived from it.
struct ConstructionShapeComponent {
    Hephaestus::Construction::SourceKind source = Hephaestus::Construction::SourceKind::Recipe;
    Hephaestus::Construction::Recipe recipe = Hephaestus::Construction::Recipe::Box;
    std::string role;
    Hephaestus::Construction::CollisionMode collision = Hephaestus::Construction::CollisionMode::Solid;
    Hephaestus::Construction::Operation operation = Hephaestus::Construction::Operation::Add;
    Hephaestus::Construction::BoxParams box;
    Hephaestus::Construction::OpeningParams opening;
    Hephaestus::Construction::StairsParams stairs;
    Hephaestus::Construction::WallParams wall;
    std::vector<Hephaestus::Construction::FaceMaterial> faceMaterials;
    // A .hpkit that dresses this wall or room with prefab pieces; the shape stays the collision and navmesh shell.
    std::string kit;
    std::vector<Hephaestus::Construction::KitSwap> kitSwaps;
    Hephaestus::Construction::EditableMesh mesh;  // source EditableMesh
    int segments = 24;  // around a cylinder or sphere
    int evaluatorVersion = Hephaestus::Construction::kEvaluatorVersion;
};

// Marks helper entities the construction system creates and owns (collision boxes). Never saved or shown.
struct ConstructionGeneratedComponent {
    std::uint32_t ownerEntity = 0;
    std::string kitSlot;  // kit pieces: the spot of the dressing this entity belongs to
};

namespace Hephaestus::Construction {

// Scene file form: {"source", "recipe", "role", "collision", "version", "params": {...}}.
std::string WriteShapeJson(const ConstructionShapeComponent& shape, const std::string& indent);
// The same data as a JSON value, for prefab capture and automation.
JsonValue ShapeToJson(const ConstructionShapeComponent& shape);
bool ApplyShapeJson(ConstructionShapeComponent& shape, const JsonValue& data, std::string& outError);

// Shapes with their own geometry (boxes, rooms and stairs), as opposed to openings that cut a host.
bool HasOwnGeometry(const ConstructionShapeComponent& shape);
bool IsCutter(const ConstructionShapeComponent& shape);
int StairCount(const ConstructionShapeComponent& shape);
// What the editor calls the shape: Block, Room, Subtract, Stairs, Plane, Cylinder, Sphere or Opening.
const char* DisplayName(const ConstructionShapeComponent& shape);

// Boxes that are not cutters, and walls: shapes that doors and windows can be cut into.
bool CanHostOpenings(const ConstructionShapeComponent& shape);
// Segments of a wall's path (closed paths have one more, back to the first point).
int WallSegmentCount(const ConstructionShapeComponent& shape);

// Turns a solid block or a ramp into an editable mesh with the same shape (source EditableMesh). False for other
// shapes, which keep their recipe.
bool MakeEditable(ConstructionShapeComponent& shape);
bool CanMakeEditable(const ConstructionShapeComponent& shape);

// Clamps parameters to values the evaluator accepts (positive sizes, walls thinner than the box).
void Sanitize(ConstructionShapeComponent& shape);

} // namespace Hephaestus::Construction
