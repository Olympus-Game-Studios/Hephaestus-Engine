#include "Construction/ConstructionGeometry.h"
#include "Construction/ConstructionSystem.h"
#include "Construction/ConstructionTool.h"
#include "Navigation/NavMesh.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

using namespace Hephaestus::Construction;

namespace {

bool g_pass = true;

void Check(bool condition, const std::string& message)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << "\n";
    g_pass = g_pass && condition;
}

bool InsideSolid(const EvaluatedGeometry& geometry, const Vec3& p)
{
    for (const LocalBox& box : geometry.solids) {
        if (p.x > box.min.x && p.x < box.max.x && p.y > box.min.y && p.y < box.max.y && p.z > box.min.z &&
            p.z < box.max.z) {
            return true;
        }
    }
    return false;
}

float SolidVolume(const EvaluatedGeometry& geometry)
{
    float volume = 0.0f;
    for (const LocalBox& box : geometry.solids) {
        volume += (box.max.x - box.min.x) * (box.max.y - box.min.y) * (box.max.z - box.min.z);
    }
    return volume;
}

bool HasGroup(const EvaluatedGeometry& geometry, FaceGroup group)
{
    for (const FaceTag& tag : geometry.triangleTags) {
        if (tag.group == group) {
            return true;
        }
    }
    return false;
}

// Every triangle's normal must agree with its winding, or back-face culling would hide it.
bool WindingMatchesNormals(const EvaluatedGeometry& geometry)
{
    const MeshData& mesh = geometry.mesh;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[i]];
        const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const Vec3 pa(a.pos[0], a.pos[1], a.pos[2]);
        const Vec3 n = vcross(vsub(Vec3(b.pos[0], b.pos[1], b.pos[2]), pa), vsub(Vec3(c.pos[0], c.pos[1], c.pos[2]), pa));
        if (vdot(n, Vec3(a.normal[0], a.normal[1], a.normal[2])) <= 0.0f) {
            return false;
        }
    }
    return true;
}

struct FakeSink {
    size_t nextHandle = 0;
    int uploads = 0;
    int releases = 0;
    int live = 0;
    MeshSink Make()
    {
        MeshSink sink;
        sink.upload = [this](size_t existing, const std::string&, const MeshData&) {
            ++uploads;
            if (existing != MeshRendererComponent::InvalidMesh) {
                return existing;
            }
            ++live;
            return nextHandle++;
        };
        sink.release = [this](size_t) {
            ++releases;
            --live;
        };
        return sink;
    }
};

Entity AddBox(Scene& scene, const std::string& name, const BoxParams& params, const Vec3& position = Vec3(0, 0, 0))
{
    Entity entity = scene.CreateEntity(name);
    entity.GetComponent<TransformComponent>().position = position;
    auto& shape = entity.AddComponent<ConstructionShapeComponent>();
    shape.recipe = Recipe::Box;
    shape.box = params;
    return entity;
}

Entity AddOpening(Scene& scene, Entity host, const OpeningParams& params)
{
    Entity entity = scene.CreateEntity("Opening");
    auto& shape = entity.AddComponent<ConstructionShapeComponent>();
    shape.recipe = Recipe::Opening;
    shape.opening = params;
    scene.SetParent(entity, host, false);
    return entity;
}

BoxParams Room()
{
    BoxParams room;
    room.width = 6.0f;
    room.depth = 4.0f;
    room.height = 3.0f;
    room.hollow = true;
    room.wallThickness = 0.2f;
    room.floor = true;
    room.ceiling = false;
    return room;
}

OpeningParams Door()
{
    OpeningParams door;
    door.side = WallSide::PosZ;
    door.offset = 0.0f;
    door.sill = 0.2f;  // on top of the floor slab
    door.width = 1.2f;
    door.height = 2.1f;
    return door;
}

int CountGenerated(Scene& scene)
{
    int count = 0;
    for (auto entity : scene.GetRegistry().view<ConstructionGeneratedComponent>()) {
        (void)entity;
        ++count;
    }
    return count;
}

void TestRecipes()
{
    EvaluatedGeometry solid;
    BoxParams box;
    Check(EvaluateBox(box, {}, solid), "a solid box evaluates");
    Check(solid.mesh.vertices.size() == 24 && solid.mesh.indices.size() == 36, "a solid box is six quads");
    Check(solid.solids.size() == 1 && std::fabs(SolidVolume(solid) - 48.0f) < 1e-3f,
          "a solid box has one collision box with the right volume");
    Check(WindingMatchesNormals(solid), "solid box winding matches its normals");
    Check(std::fabs(solid.mesh.bounds.min.y) < 1e-6f && std::fabs(solid.mesh.bounds.max.y - 3.0f) < 1e-6f,
          "the box stands on its origin");

    EvaluatedGeometry room;
    Check(EvaluateBox(Room(), {}, room), "a hollow room evaluates");
    const float shell = 6.0f * 4.0f * 3.0f - 5.6f * 3.6f * 2.8f;
    Check(std::fabs(SolidVolume(room) - shell) < 1e-3f, "room collision covers exactly the walls and floor");
    Check(HasGroup(room, FaceGroup::Inner) && !HasGroup(room, FaceGroup::Reveal), "a room without openings has inner faces and no reveals");
    Check(!InsideSolid(room, Vec3(0.0f, 1.5f, 0.0f)) && InsideSolid(room, Vec3(0.0f, 1.5f, 1.9f)) &&
              InsideSolid(room, Vec3(0.0f, 0.1f, 0.0f)),
          "the room is hollow with a wall at +z and a floor slab");
    Check(WindingMatchesNormals(room), "room winding matches its normals");

    EvaluatedGeometry doorRoom;
    Check(EvaluateBox(Room(), {Door()}, doorRoom), "a room with a door evaluates");
    Check(!InsideSolid(doorRoom, Vec3(0.0f, 1.0f, 1.9f)), "the door cuts through the +z wall");
    Check(InsideSolid(doorRoom, Vec3(0.0f, 1.0f, -1.9f)), "the door does not cut the opposite wall");
    Check(InsideSolid(doorRoom, Vec3(1.0f, 1.0f, 1.9f)) && InsideSolid(doorRoom, Vec3(0.0f, 2.6f, 1.9f)),
          "the wall beside and above the door stays solid");
    Check(InsideSolid(doorRoom, Vec3(0.0f, 0.1f, 1.9f)), "the floor slab under the door stays solid");
    Check(HasGroup(doorRoom, FaceGroup::Reveal), "the door adds reveal faces");
    Check(std::fabs(SolidVolume(room) - SolidVolume(doorRoom) - 1.2f * 2.1f * 0.2f) < 1e-3f,
          "the door removes exactly its volume");
    Check(WindingMatchesNormals(doorRoom), "room with door winding matches its normals");

    OpeningParams window = Door();
    window.side = WallSide::NegX;
    window.offset = 0.5f;
    window.sill = 1.0f;
    window.height = 1.0f;
    EvaluatedGeometry ab;
    EvaluatedGeometry ba;
    EvaluateBox(Room(), {Door(), window}, ab);
    EvaluateBox(Room(), {window, Door()}, ba);
    Check(HashGeometry(ab) == HashGeometry(ba), "opening order does not change the result");
    EvaluatedGeometry again;
    EvaluateBox(Room(), {Door(), window}, again);
    Check(HashGeometry(ab) == HashGeometry(again), "evaluation is deterministic");

    ConstructionShapeComponent shape;
    shape.box = Room();
    const std::uint64_t base = HashInputs(shape, {Door()});
    OpeningParams moved = Door();
    moved.offset = 0.5f;
    Check(HashInputs(shape, {moved}) != base, "moving an opening changes the input hash");
    Check(HashInputs(shape, {Door()}) == base, "the same inputs hash the same");
}

void TestSystem()
{
    Scene scene("Construction");
    FakeSink fake;
    ConstructionSystem system;
    system.SetMeshSink(fake.Make());

    Entity room = AddBox(scene, "Room", Room());
    Entity other = AddBox(scene, "Block", BoxParams{}, Vec3(10.0f, 0.0f, 0.0f));
    Entity door = AddOpening(scene, room, Door());

    Check(system.Update(scene) == 2, "the first update evaluates both shapes");
    Check(room.HasComponent<MeshRendererComponent>() && room.GetComponent<MeshRendererComponent>().HasValidMesh(),
          "a construction shape gets a mesh");
    const EvaluatedGeometry* roomGeometry = system.Geometry(room);
    Check(roomGeometry && CountGenerated(scene) ==
              static_cast<int>(roomGeometry->solids.size() + system.Geometry(other)->solids.size()),
          "one generated collision box per solid box");
    Check(system.Update(scene) == 0, "an update with nothing changed evaluates nothing");

    door.GetComponent<ConstructionShapeComponent>().opening.offset = 1.0f;
    Check(system.Update(scene) == 1, "moving the door re-evaluates only its host");
    Check(fake.live == 2, "re-evaluation reuses the mesh slot");

    Hephaestus::Construction::ConstructionHit hit;
    Check(system.Raycast(scene, Ray(Vec3(1.0f, 1.0f, 10.0f), Vec3(0.0f, 0.0f, -1.0f)), hit) &&
              hit.entity == room && hit.tag.group == FaceGroup::Inner && hit.tag.wall == static_cast<int>(WallSide::NegZ),
          "a ray through the door hits the far wall's inner face");
    Check(system.Raycast(scene, Ray(Vec3(-1.5f, 1.0f, 10.0f), Vec3(0.0f, 0.0f, -1.0f)), hit) &&
              hit.tag.group == FaceGroup::PosZ && std::fabs(hit.worldPoint.z - 2.0f) < 1e-4f,
          "a ray beside the door hits the +z wall's outer face");

    // Navigation sees the generated collision: a path from outside into the room goes through the door.
    Entity ground = scene.CreateEntity("Ground");
    ground.GetComponent<TransformComponent>().position = Vec3(0.0f, -0.5f, 0.0f);
    ground.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(15.0f, 0.5f, 15.0f)));
    auto& groundBody = ground.AddComponent<RigidBodyComponent>();
    groundBody.isStatic = true;
    groundBody.motion = RigidBodyMotion::Static;
    Hephaestus::Navigation::NavMesh nav;
    std::string error;
    std::vector<Vec3> path;
    const bool built = nav.Build(Hephaestus::Navigation::CollectSceneGeometry(scene),
                                 Hephaestus::Navigation::NavBuildSettings{}, error);
    Check(built && nav.FindPath(Vec3(1.0f, 0.0f, 6.0f), Vec3(0.0f, 0.2f, 0.0f), path) && !path.empty() &&
              std::fabs(path.back().x) < 0.5f && std::fabs(path.back().z) < 0.5f,
          "a navmesh path reaches the middle of the room (" + error + ")");
    bool throughDoor = false;
    for (size_t i = 1; i < path.size(); ++i) {
        const Vec3 a = path[i - 1];
        const Vec3 b = path[i];
        if ((a.z - 2.0f) * (b.z - 2.0f) <= 0.0f && std::fabs(b.z - a.z) > 1e-4f) {
            const float t = (2.0f - a.z) / (b.z - a.z);
            const float x = a.x + (b.x - a.x) * t;
            throughDoor = throughDoor || (x > 0.4f && x < 1.6f);
        }
    }
    Check(throughDoor, "the path crosses the +z wall inside the moved door");

    // Save and load: parameters survive, generated entities are never written.
    SceneSerializer writer(scene);
    const std::string saved = writer.SerializeToString();
    Check(saved.find("Construction Collision") == std::string::npos, "generated collision is not saved");
    Check(saved.find("\"ConstructionShape\"") != std::string::npos, "construction shapes are saved");
    Scene loaded("Loaded");
    Check(SceneSerializer::DeserializeFromString(loaded, saved), "the saved scene loads");
    ConstructionSystem loadedSystem;
    FakeSink loadedSink;
    loadedSystem.SetMeshSink(loadedSink.Make());
    loadedSystem.Update(loaded);
    Entity loadedRoom = loaded.FindEntityByName("Room");
    Check(loadedRoom && loadedSystem.Geometry(loadedRoom) &&
              HashGeometry(*loadedSystem.Geometry(loadedRoom)) == HashGeometry(*system.Geometry(room)),
          "the loaded room regenerates identical geometry");

    // Undo restores a snapshot into the same scene: shapes keep their cached geometry, collision comes back.
    const std::uint64_t evaluationsBefore = system.Stats().evaluations;
    Check(SceneSerializer::DeserializeFromString(scene, saved), "a snapshot restores into the live scene");
    system.Update(scene);
    Check(system.Stats().evaluations == evaluationsBefore, "restoring a snapshot re-uses cached geometry");
    room = scene.FindEntityByName("Room");
    other = scene.FindEntityByName("Block");
    Check(CountGenerated(scene) == static_cast<int>(system.Geometry(room)->solids.size() +
                                                    system.Geometry(other)->solids.size()),
          "restoring a snapshot rebuilds the collision");
    Check(scene.FindEntityByName("Room").GetComponent<MeshRendererComponent>().HasValidMesh(),
          "restoring a snapshot reconnects the mesh");

    // Deleting a shape releases its mesh and its collision.
    scene.DestroyEntity(other);
    system.Update(scene);
    Check(fake.live == 1 && CountGenerated(scene) == static_cast<int>(system.Geometry(room)->solids.size()),
          "a deleted shape releases its mesh and collision");

    // A duplicated room copies its shape and openings but not the generated collision.
    auto copies = scene.DuplicateEntities({static_cast<entt::entity>(scene.FindEntityByName("Room"))});
    Check(copies.size() == 1 && copies[0].HasComponent<ConstructionShapeComponent>() &&
              ConstructionSystem::CollectOpenings(scene, copies[0]).size() == 1,
          "duplicating a room copies its shape and door");
    system.Update(scene);
    Check(CountGenerated(scene) == 2 * static_cast<int>(system.Geometry(room)->solids.size()),
          "the copy gets its own collision and no stray copies remain");
}


struct ToolCamera {
    Vec3 eye{0.0f, 6.0f, 14.0f};
    Mat4 viewProj;
    ToolCamera()
    {
        const Mat4 view = Mat4::LookAt(eye, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
        const Mat4 proj = Mat4::Perspective(60.0f * 3.14159265f / 180.0f, 16.0f / 9.0f, 0.1f, 500.0f);
        viewProj = Mat4::Multiply(proj, view);
    }
    // Input for the cursor over a world point, as the viewport would build it.
    ToolInput At(const Vec3& target, bool shift = false) const
    {
        ToolInput input;
        input.ray = Ray(eye, vsub(target, eye));
        input.viewProj = viewProj;
        input.viewportWidth = 1600.0f;
        input.viewportHeight = 900.0f;
        const Vec3 ndc = Mat4::TransformPoint(viewProj, target);
        input.screenX = (ndc.x + 1.0f) * 0.5f * 1600.0f;
        input.screenY = (ndc.y + 1.0f) * 0.5f * 900.0f;
        input.shift = shift;
        return input;
    }
};

void TestTool()
{
    Scene scene("Tool");
    FakeSink fake;
    ConstructionSystem system;
    system.SetMeshSink(fake.Make());
    ConstructionTool tool;
    int undoSteps = 0;
    int discarded = 0;
    entt::entity selected = entt::null;
    ToolHost host;
    host.pushUndo = [&](const std::string&) { ++undoSteps; };
    host.discardUndo = [&]() { ++discarded; };
    host.select = [&](entt::entity e) { selected = e; };
    host.selected = [&]() { return selected; };
    host.changed = [] {};
    tool.SetHost(host);
    const ToolCamera camera;

    tool.SetMode(ToolMode::DrawRoom);
    Check(tool.OnPress(scene, system, camera.At(Vec3(-3.0f, 0.0f, -2.0f))), "pressing on the ground starts a room");
    tool.OnMove(scene, system, camera.At(Vec3(3.0f, 0.0f, 2.0f)));
    Check(tool.OnRelease(scene, system, camera.At(Vec3(3.0f, 0.0f, 2.0f))) && tool.IsBusy(),
          "releasing the footprint asks for a height");
    tool.OnMove(scene, system, camera.At(Vec3(0.0f, 3.0f, 0.0f)));
    tool.OnPress(scene, system, camera.At(Vec3(0.0f, 3.0f, 0.0f)));
    Entity room(selected, &scene);
    Check(room && ConstructionSystem::IsBox(scene, selected) && !tool.IsBusy() && undoSteps == 1,
          "clicking finishes the room as one undo step");
    const auto& roomShape = room.GetComponent<ConstructionShapeComponent>();
    Check(std::fabs(roomShape.box.width - 6.0f) < 1e-4f && std::fabs(roomShape.box.depth - 4.0f) < 1e-4f &&
              std::fabs(roomShape.box.height - 3.0f) < 1e-4f && roomShape.box.hollow,
          "the drawn room is 6 x 4 x 3 m and hollow");
    const Vec3 roomPosition = room.GetComponent<TransformComponent>().position;
    Check(vlen(roomPosition) < 1e-4f, "the room is centred on its footprint at ground level");
    system.Update(scene);

    tool.SetMode(ToolMode::DrawOpening);
    tool.OnMove(scene, system, camera.At(Vec3(1.0f, 0.5f, 2.0f)));
    Check(tool.OnPress(scene, system, camera.At(Vec3(1.0f, 0.5f, 2.0f))), "pressing on a wall starts an opening");
    tool.OnRelease(scene, system, camera.At(Vec3(1.0f, 0.5f, 2.0f)));
    auto openings = ConstructionSystem::CollectOpenings(scene, room);
    Check(openings.size() == 1 && openings[0].side == WallSide::PosZ && std::fabs(openings[0].offset - 1.0f) < 1e-4f &&
              std::fabs(openings[0].sill - 0.2f) < 1e-4f && std::fabs(openings[0].height - 2.1f) < 1e-4f,
          "a click low on a wall adds a door standing on the floor");
    Check(system.Update(scene) == 1, "the new door re-evaluates the room");

    tool.OnPress(scene, system, camera.At(Vec3(1.0f, 1.0f, 2.0f)));
    tool.OnMove(scene, system, camera.At(Vec3(-0.5f, 1.0f, 2.0f)));
    tool.OnRelease(scene, system, camera.At(Vec3(-0.5f, 1.0f, 2.0f)));
    openings = ConstructionSystem::CollectOpenings(scene, room);
    Check(openings.size() == 1 && std::fabs(openings[0].offset + 0.5f) < 1e-4f && std::fabs(openings[0].sill - 0.2f) < 1e-4f,
          "dragging the door moves it along the wall (offset " + std::to_string(openings.empty() ? 0.0f : openings[0].offset) +
              ", sill " + std::to_string(openings.empty() ? 0.0f : openings[0].sill) + ")");

    tool.SetMode(ToolMode::Select);
    selected = room;
    Check(tool.OnPress(scene, system, camera.At(Vec3(3.0f, 1.5f, 0.0f))), "pressing on the +x handle starts a resize");
    tool.OnMove(scene, system, camera.At(Vec3(4.0f, 1.5f, 0.0f)));
    tool.OnRelease(scene, system, camera.At(Vec3(4.0f, 1.5f, 0.0f)));
    Check(std::fabs(roomShape.box.width - 7.0f) < 1e-3f &&
              std::fabs(room.GetComponent<TransformComponent>().position.x - 0.5f) < 1e-3f,
          "dragging the +x face 1 m out widens the room and keeps the -x wall in place");
    tool.OnPress(scene, system, camera.At(Vec3(4.0f, 1.5f, 0.0f)));
    tool.OnMove(scene, system, camera.At(Vec3(6.0f, 1.5f, 0.0f)));
    tool.OnKey(scene, system, 0x01000000);
    Check(std::fabs(roomShape.box.width - 7.0f) < 1e-3f && discarded == 1, "Esc during a resize puts the room back");

    tool.SetMode(ToolMode::DrawBox);
    tool.OnPress(scene, system, camera.At(Vec3(8.0f, 0.0f, 0.0f)));
    for (int key : std::initializer_list<int>{'2', ',', '3', 0x01000004, '1', '.', '5', 0x01000004}) {
        tool.OnKey(scene, system, key);
    }
    Entity block(selected, &scene);
    const auto* blockShape = selected != entt::null ? scene.GetRegistry().try_get<ConstructionShapeComponent>(selected) : nullptr;
    Check(blockShape && std::fabs(blockShape->box.width - 2.0f) < 1e-4f && std::fabs(blockShape->box.depth - 3.0f) < 1e-4f &&
              std::fabs(blockShape->box.height - 1.5f) < 1e-4f && !blockShape->box.hollow,
          "typed sizes draw a 2 x 3 x 1.5 m block");

    tool.SetMode(ToolMode::DrawStairs);
    host.stepHeight = [] { return 0.3f; };
    tool.SetHost(host);
    tool.OnPress(scene, system, camera.At(Vec3(-6.0f, 0.0f, 0.0f)));
    tool.OnMove(scene, system, camera.At(Vec3(-2.0f, 0.0f, 1.0f)));
    tool.OnRelease(scene, system, camera.At(Vec3(-2.0f, 0.0f, 1.0f)));
    tool.OnKey(scene, system, '2');
    tool.OnKey(scene, system, 0x01000004);
    const auto* stairShape = selected != entt::null ? scene.GetRegistry().try_get<ConstructionShapeComponent>(selected) : nullptr;
    Check(stairShape && stairShape->recipe == Recipe::Stairs && std::fabs(stairShape->box.depth - 4.0f) < 1e-4f &&
              std::fabs(stairShape->box.width - 1.0f) < 1e-4f && std::fabs(stairShape->box.height - 2.0f) < 1e-4f &&
              std::fabs(stairShape->stairs.maxRise - 0.285f) < 1e-4f,
          "drawn stairs run along the longer side and keep their steps under the step height");
    if (stairShape) {
        const Mat4 world = scene.GetWorldTransform(Entity(selected, &scene));
        const Vec3 top = Mat4::TransformPoint(world, Vec3(0.0f, 2.0f, 2.0f));
        Check(std::fabs(top.x + 2.0f) < 1e-3f && std::fabs(top.z - 0.5f) < 1e-3f,
              "the stairs climb towards where the drag ended");
    }
}

void TestStairs()
{
    ConstructionShapeComponent shape;
    shape.recipe = Recipe::Stairs;
    shape.box.width = 1.2f;
    shape.box.depth = 3.0f;
    shape.box.height = 1.5f;
    shape.stairs.maxRise = 0.285f;
    Check(StairCount(shape) == 6, "1.5 m with steps up to 0.285 m gives 6 steps (" + std::to_string(StairCount(shape)) + ")");
    EvaluatedGeometry stairs;
    Check(EvaluateShape(shape, {}, stairs), "stairs evaluate");
    Check(WindingMatchesNormals(stairs), "stairs winding matches their normals");
    const float run = 3.0f / 6.0f;
    const float rise = 1.5f / 6.0f;
    bool stepsRight = true;
    for (int step = 0; step < 6; ++step) {
        const float z = -1.5f + run * (step + 0.5f);
        stepsRight = stepsRight && InsideSolid(stairs, Vec3(0.0f, rise * (step + 0.5f), z)) &&
                     !InsideSolid(stairs, Vec3(0.0f, rise * (step + 1.5f), z));
    }
    Check(stepsRight, "each step is solid up to its tread and empty above it");
    Check(std::fabs(SolidVolume(stairs) - 1.2f * run * rise * 21.0f) < 1e-3f, "stairs collision has the volume of the steps");
    shape.stairs.steps = 10;
    EvaluatedGeometry fixed;
    EvaluateShape(shape, {}, fixed);
    Check(StairCount(shape) == 10 && HashGeometry(fixed) != HashGeometry(stairs), "an explicit step count is used");

    // A character with the default settings can walk up the stairs on the navmesh.
    Scene scene("Stairs");
    FakeSink fake;
    ConstructionSystem system;
    system.SetMeshSink(fake.Make());
    Entity flight = scene.CreateEntity("Stairs");
    auto& flightShape = flight.AddComponent<ConstructionShapeComponent>();
    flightShape.recipe = Recipe::Stairs;
    flightShape.box.width = 1.6f;
    flightShape.box.depth = 4.0f;
    flightShape.box.height = 2.0f;
    flightShape.stairs.maxRise = 0.285f;
    BoxParams landing;
    landing.width = 4.0f;
    landing.depth = 4.0f;
    landing.height = 2.0f;
    AddBox(scene, "Landing", landing, Vec3(0.0f, 0.0f, 4.0f));
    Entity ground = scene.CreateEntity("Ground");
    ground.GetComponent<TransformComponent>().position = Vec3(0.0f, -0.5f, 0.0f);
    ground.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(Vec3(15.0f, 0.5f, 15.0f)));
    auto& body = ground.AddComponent<RigidBodyComponent>();
    body.isStatic = true;
    body.motion = RigidBodyMotion::Static;
    system.Update(scene);
    Hephaestus::Navigation::NavMesh nav;
    std::string error;
    std::vector<Vec3> path;
    const bool built = nav.Build(Hephaestus::Navigation::CollectSceneGeometry(scene),
                                 Hephaestus::Navigation::NavBuildSettings{}, error);
    Check(built && nav.FindPath(Vec3(0.0f, 0.0f, -6.0f), Vec3(0.0f, 2.0f, 4.0f), path) && !path.empty() &&
              std::fabs(path.back().y - 2.0f) < 0.3f && std::fabs(path.back().z - 4.0f) < 0.5f,
          "a navmesh path climbs the stairs onto a 2 m landing (" + error + ")");
}

void TestSubtract()
{
    Scene scene("Subtract");
    FakeSink fake;
    ConstructionSystem system;
    system.SetMeshSink(fake.Make());
    BoxParams wallParams;
    wallParams.width = 6.0f;
    wallParams.height = 3.0f;
    wallParams.depth = 0.4f;
    Entity wall = AddBox(scene, "Wall", wallParams);
    Entity other = AddBox(scene, "Far", BoxParams{}, Vec3(20.0f, 0.0f, 0.0f));
    BoxParams holeParams;
    holeParams.width = 1.0f;
    holeParams.height = 1.0f;
    holeParams.depth = 2.0f;
    Entity hole = AddBox(scene, "Hole", holeParams, Vec3(1.0f, 1.0f, 0.0f));
    hole.GetComponent<ConstructionShapeComponent>().operation = Operation::Subtract;

    Check(system.Update(scene) == 3, "the wall, the far block and the subtract box evaluate");
    const EvaluatedGeometry* wallGeometry = system.Geometry(wall);
    Check(wallGeometry && !InsideSolid(*wallGeometry, Vec3(1.0f, 1.5f, 0.0f)) && InsideSolid(*wallGeometry, Vec3(-1.0f, 1.5f, 0.0f)),
          "a subtract box cuts a hole through the wall it overlaps");
    Check(std::fabs(SolidVolume(*wallGeometry) - (6.0f * 3.0f * 0.4f - 1.0f * 1.0f * 0.4f)) < 1e-3f,
          "the hole removes exactly the overlapping volume");
    int holeColliders = 0;
    for (auto entity : scene.GetRegistry().view<ConstructionGeneratedComponent>()) {
        holeColliders += scene.GetRegistry().get<ConstructionGeneratedComponent>(entity).ownerEntity ==
                         static_cast<std::uint32_t>(static_cast<entt::entity>(hole)) ? 1 : 0;
    }
    Check(holeColliders == 0 && hole.GetComponent<MeshRendererComponent>().hideInGame,
          "a subtract box has no collision and is hidden in the game");

    hole.GetComponent<TransformComponent>().position = Vec3(-2.0f, 1.0f, 0.0f);
    Check(system.Update(scene) == 1, "moving the subtract box re-evaluates only the wall it cuts");
    Check(!InsideSolid(*system.Geometry(wall), Vec3(-2.0f, 1.5f, 0.0f)) && InsideSolid(*system.Geometry(wall), Vec3(1.0f, 1.5f, 0.0f)),
          "the hole follows the subtract box");

    ConstructionHit hit;
    Check(system.Raycast(scene, Ray(Vec3(-2.0f, 1.5f, 10.0f), Vec3(0.0f, 0.0f, -1.0f)), hit) == false,
          "rays pass through the hole and ignore the subtract box itself");

    const std::string saved = SceneSerializer(scene).SerializeToString();
    Scene loaded("Loaded");
    SceneSerializer::DeserializeFromString(loaded, saved);
    ConstructionSystem loadedSystem;
    loadedSystem.Update(loaded);
    Entity loadedWall = loaded.FindEntityByName("Wall");
    Check(loadedWall && HashGeometry(*loadedSystem.Geometry(loadedWall)) == HashGeometry(*system.Geometry(wall)),
          "a saved and reloaded subtract box cuts the same hole");
    (void)other;
}

void TestRoundShapes()
{
    Scene scene("Round");
    FakeSink fake;
    ConstructionSystem system;
    system.SetMeshSink(fake.Make());
    BoxParams size;
    size.width = 2.0f;
    size.height = 3.0f;
    size.depth = 2.0f;
    Entity plane = AddBox(scene, "Plane", size);
    Entity cylinder = AddBox(scene, "Cylinder", size, Vec3(5.0f, 0.0f, 0.0f));
    Entity sphere = AddBox(scene, "Sphere", size, Vec3(10.0f, 0.0f, 0.0f));
    plane.GetComponent<ConstructionShapeComponent>().recipe = Recipe::Plane;
    cylinder.GetComponent<ConstructionShapeComponent>().recipe = Recipe::Cylinder;
    sphere.GetComponent<ConstructionShapeComponent>().recipe = Recipe::Sphere;
    sphere.GetComponent<ConstructionShapeComponent>().segments = 16;

    Check(system.Update(scene) == 3, "plane, cylinder and sphere evaluate as construction shapes");
    const EvaluatedGeometry* planeGeometry = system.Geometry(plane);
    const EvaluatedGeometry* cylinderGeometry = system.Geometry(cylinder);
    const EvaluatedGeometry* sphereGeometry = system.Geometry(sphere);
    Check(planeGeometry && planeGeometry->mesh.indices.size() == 6 && planeGeometry->solids.size() == 1,
          "a plane is two triangles over a thin walkable slab");
    Check(cylinderGeometry && cylinderGeometry->rounds.size() == 1 &&
          cylinderGeometry->rounds[0].kind == RoundCollider::Kind::Cylinder &&
          std::fabs(cylinderGeometry->mesh.bounds.max.y - 3.0f) < 1e-4f && std::fabs(cylinderGeometry->mesh.bounds.min.y) < 1e-4f,
          "a cylinder stands on its origin with a cylinder collider");
    Check(sphereGeometry && sphereGeometry->rounds.size() == 1 && std::fabs(sphereGeometry->rounds[0].center.y - 1.5f) < 1e-4f &&
          sphereGeometry->mesh.indices.size() == static_cast<size_t>((16 * 8 * 2 - 2 * 16) * 3),
          "a sphere rests on its origin and uses its segment count");
    for (const EvaluatedGeometry* geometry : {cylinderGeometry, sphereGeometry}) {
        const Vec3 center(0.0f, 1.5f, 0.0f);
        bool outward = true;
        const auto& v = geometry->mesh.vertices;
        const auto& idx = geometry->mesh.indices;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const Vec3 a(v[idx[i]].pos[0], v[idx[i]].pos[1], v[idx[i]].pos[2]);
            const Vec3 b(v[idx[i + 1]].pos[0], v[idx[i + 1]].pos[1], v[idx[i + 1]].pos[2]);
            const Vec3 c(v[idx[i + 2]].pos[0], v[idx[i + 2]].pos[1], v[idx[i + 2]].pos[2]);
            const Vec3 n = vcross(vsub(b, a), vsub(c, a));
            const Vec3 centroid = vscale(vadd(vadd(a, b), c), 1.0f / 3.0f);
            outward &= vlen(n) < 1e-8f || vdot(n, vsub(centroid, center)) > 0.0f;
        }
        Check(outward, "round shape triangles face outward");
    }

    int colliders = 0;
    for (auto entity : scene.GetRegistry().view<ConstructionGeneratedComponent, PhysicsShapeComponent>()) {
        const auto type = scene.GetRegistry().get<PhysicsShapeComponent>(entity).type;
        colliders += type == PhysicsShapeType::Box || type == PhysicsShapeType::Cylinder || type == PhysicsShapeType::Sphere ? 1 : 0;
    }
    Check(colliders == 3, "each round shape gets one generated collider");

    sphere.GetComponent<ConstructionShapeComponent>().segments = 32;
    Check(system.Update(scene) == 1, "changing segments re-evaluates only that shape");

    const std::string saved = SceneSerializer(scene).SerializeToString();
    Scene loaded("Loaded");
    SceneSerializer::DeserializeFromString(loaded, saved);
    Entity loadedSphere = loaded.FindEntityByName("Sphere");
    Check(loadedSphere && loadedSphere.GetComponent<ConstructionShapeComponent>().recipe == Recipe::Sphere &&
          loadedSphere.GetComponent<ConstructionShapeComponent>().segments == 32,
          "round shapes save and load their recipe and segments");
}

void TestSetOperation()
{
    Scene scene("Operation");
    ConstructionSystem system;
    Entity block = AddBox(scene, "Block", BoxParams{});
    ConstructionSystem::SetOperation(scene, block, Operation::Add);
    Check(block.HasComponent<MaterialComponent>() && block.GetComponent<MaterialComponent>().useAlbedoTexture,
          "an add block gets the blockout material");
    system.Update(scene);
    ConstructionSystem::SetOperation(scene, block, Operation::Subtract);
    system.Update(scene);
    Check(IsCutter(block.GetComponent<ConstructionShapeComponent>()) &&
          block.GetComponent<MaterialComponent>().blendMode == MaterialBlendMode::AlphaBlend &&
          block.GetComponent<MeshRendererComponent>().hideInGame,
          "switching to subtract makes it a hidden see-through cutter");
    ConstructionSystem::SetOperation(scene, block, Operation::Add);
    Check(!IsCutter(block.GetComponent<ConstructionShapeComponent>()) &&
          block.GetComponent<MaterialComponent>().useAlbedoTexture && !block.GetComponent<MeshRendererComponent>().hideInGame,
          "switching back to add restores the blockout look and in-game visibility");
}

void TestScale()
{
    Scene scene("Scale");
    FakeSink fake;
    ConstructionSystem system;
    system.SetMeshSink(fake.Make());
    std::vector<Entity> rooms;
    std::vector<Entity> doors;
    for (int i = 0; i < 1000; ++i) {
        Entity room = AddBox(scene, "Room" + std::to_string(i), Room(), Vec3(float(i % 40) * 8.0f, 0.0f, float(i / 40) * 6.0f));
        doors.push_back(AddOpening(scene, room, Door()));
        rooms.push_back(room);
    }
    auto start = std::chrono::steady_clock::now();
    Check(system.Update(scene) == 1000, "1,000 rooms evaluate on the first update");
    const double fullMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    system.Update(scene);
    const double idleMs = system.Stats().lastUpdateMs;
    doors[500].GetComponent<ConstructionShapeComponent>().opening.offset = 1.0f;
    Check(system.Update(scene) == 1, "one door edit among 1,000 rooms re-evaluates one room");
    const double editMs = system.Stats().lastUpdateMs;
    const double evalMs = system.Stats().lastEvaluateMs;
    const auto snapshotStart = std::chrono::steady_clock::now();
    const std::string snapshot = SceneSerializer(scene).SerializeToString();
    const double snapshotMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - snapshotStart).count();
    std::cout << "[INFO] 1,000 rooms: undo snapshot (scene serialisation) " << snapshotMs << " ms, "
              << snapshot.size() / 1024 << " KB\n";
    // Timings are reported, not asserted: they depend on the machine and build configuration.
    std::cout << "[INFO] 1,000 rooms: first update " << fullMs << " ms, idle update " << idleMs
              << " ms, one door edit " << editMs << " ms (evaluation " << evalMs << " ms)\n";
}

} // namespace

int main()
{
    std::cout << std::unitbuf;
    TestRecipes();
    TestSystem();
    TestTool();
    TestStairs();
    TestSubtract();
    TestRoundShapes();
    TestSetOperation();
    TestScale();
    if (!g_pass) {
        return 1;
    }
    std::cout << "ConstructionTests passed\n";
    return 0;
}
