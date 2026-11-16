#include "Assets/MeshGeometryCache.h"
#include "Navigation/NavMesh.h"
#include "Scene/CharacterControllerComponent.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using Hephaestus::Navigation::NavBuildSettings;
using Hephaestus::Navigation::NavGeometry;
using Hephaestus::Navigation::NavMesh;

namespace {

bool g_pass = true;

void Check(bool condition, const std::string& message)
{
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << message << "\n";
    g_pass = g_pass && condition;
}

Mat4 Translation(const Vec3& p)
{
    Mat4 m = Mat4::Identity();
    m.m[12] = p.x;
    m.m[13] = p.y;
    m.m[14] = p.z;
    return m;
}

// A 20 x 20 m floor with a 2 m high wall across the middle (x -0.5..0.5, z -5..5).
NavGeometry Course()
{
    NavGeometry geometry;
    geometry.AddBox(Translation(Vec3(0.0f, -0.5f, 0.0f)), Vec3(10.0f, 0.5f, 10.0f), Vec3(0.0f, 0.0f, 0.0f));
    geometry.AddBox(Translation(Vec3(0.0f, 1.0f, 0.0f)), Vec3(0.5f, 1.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f));
    return geometry;
}

} // namespace

int main()
{
    NavMesh mesh;
    std::string error;
    Check(mesh.Build(Course(), NavBuildSettings{}, error) && mesh.IsReady() && mesh.PolygonCount() > 0,
          "a floor with a wall builds a navmesh (" + error + ")");

    std::vector<Vec3> path;
    Check(mesh.FindPath(Vec3(-5.0f, 0.0f, 0.0f), Vec3(5.0f, 0.0f, 0.0f), path) && path.size() > 2,
          "a path across the wall has corners");
    bool aroundWall = false;
    bool onFloor = !path.empty();
    for (const Vec3& corner : path) {
        aroundWall = aroundWall || std::fabs(corner.z) > 5.0f;
        onFloor = onFloor && std::fabs(corner.y) < 0.3f;
    }
    Check(aroundWall && onFloor, "the path goes round the end of the wall on the floor");
    Check(!path.empty() && std::fabs(path.back().x - 5.0f) < 0.3f && std::fabs(path.back().z) < 0.3f,
          "the path ends at the goal");

    Vec3 closest;
    Check(mesh.ClosestPoint(Vec3(0.0f, 0.0f, 0.0f), closest) && std::fabs(closest.x) > 0.5f,
          "the closest walkable point to a spot inside the wall is beside it");
    Check(!mesh.ClosestPoint(Vec3(0.0f, 30.0f, 0.0f), closest), "a point far above the navmesh has no closest point");

    std::uint32_t rng = 7;
    bool allNear = true;
    bool allWalkable = true;
    for (int i = 0; i < 40; ++i) {
        Vec3 p;
        if (!mesh.RandomPointAround(Vec3(-5.0f, 0.0f, -5.0f), 3.0f, rng, p)) {
            allNear = false;
            break;
        }
        const float dx = p.x + 5.0f;
        const float dz = p.z + 5.0f;
        allNear = allNear && std::sqrt(dx * dx + dz * dz) <= 3.01f;
        allWalkable = allWalkable && std::fabs(p.y) < 0.3f && !(std::fabs(p.x) < 0.5f && std::fabs(p.z) < 5.0f);
    }
    Check(allNear && allWalkable, "random points stay near the centre and on walkable floor");

    Scene scene("NavScene");
    auto addBox = [&](const char* name, const Vec3& position, const Vec3& half, RigidBodyMotion motion, bool trigger) {
        Entity entity = scene.CreateEntity(name);
        entity.GetComponent<TransformComponent>().position = position;
        auto& body = entity.AddComponent<RigidBodyComponent>();
        body.SetMotion(motion);
        body.mass = motion == RigidBodyMotion::Static ? 0.0f : 1.0f;
        body.isTrigger = trigger;
        entity.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(half));
    };
    addBox("Floor", Vec3(0.0f, -0.5f, 0.0f), Vec3(10.0f, 0.5f, 10.0f), RigidBodyMotion::Static, false);
    addBox("Crate", Vec3(3.0f, 1.0f, 0.0f), Vec3(0.5f, 0.5f, 0.5f), RigidBodyMotion::Dynamic, false);
    addBox("Sensor", Vec3(-3.0f, 1.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), RigidBodyMotion::Static, true);
    int skipped = 0;
    const NavGeometry collected = Hephaestus::Navigation::CollectSceneGeometry(scene, &skipped);
    Check(collected.triangles.size() == 12 * 3 && skipped == 0,
          "only static solid boxes are collected from a scene");

    NavMesh empty;
    Check(!empty.Build(NavGeometry{}, NavBuildSettings{}, error) && !empty.IsReady(), "empty geometry is rejected");

    // A 1 m high, 0.3 m thick wall right across a floor: the only way over is a vault link.
    Scene vaultScene("VaultScene");
    auto addStatic = [&](Scene& target, const char* name, const Vec3& position, const Vec3& half) {
        Entity entity = target.CreateEntity(name);
        entity.GetComponent<TransformComponent>().position = position;
        auto& body = entity.AddComponent<RigidBodyComponent>();
        body.SetMotion(RigidBodyMotion::Static);
        body.mass = 0.0f;
        entity.AddComponent<PhysicsShapeComponent>(PhysicsShapeComponent::CreateBox(half));
    };
    addStatic(vaultScene, "Floor", Vec3(0.0f, -0.5f, 0.0f), Vec3(10.0f, 0.5f, 10.0f));
    addStatic(vaultScene, "HalfWall", Vec3(0.0f, 0.5f, 0.0f), Vec3(0.15f, 0.5f, 10.0f));
    NavGeometry vaultGeometry = Hephaestus::Navigation::CollectSceneGeometry(vaultScene);
    const NavBuildSettings settings;
    const int links = Hephaestus::Navigation::AddVaultLinks(vaultScene, settings, vaultGeometry);
    Check(links == 13 && vaultGeometry.links.size() == 13, "a 20 m half wall gets a vault link every 1.5 m");
    const auto& first = vaultGeometry.links.front();
    Check(std::fabs(first.from.x + 0.75f) < 1e-3f && std::fabs(first.to.x - 0.75f) < 1e-3f &&
              std::fabs(first.from.y) < 1e-3f,
          "a vault link runs across the wall, 0.6 m out from each face, on the floor");
    NavMesh vaultMesh;
    std::vector<unsigned char> linkStarts;
    Check(vaultMesh.Build(vaultGeometry, settings, error) &&
              vaultMesh.FindPath(Vec3(-5.0f, 0.0f, 1.0f), Vec3(5.0f, 0.0f, 1.0f), path, &linkStarts) &&
              path.size() == linkStarts.size(),
          "a path across a wall the navmesh cannot walk round is found");
    size_t linkAt = path.size();
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        if (linkStarts[i]) {
            linkAt = i;
            break;
        }
    }
    Check(linkAt + 1 < path.size() && path[linkAt].x < -0.5f && path[linkAt + 1].x > 0.5f,
          "the path marks the corner where it vaults, and the next corner is over the wall");

    Scene shortScene("ShortWallScene");
    addStatic(shortScene, "Floor", Vec3(0.0f, -0.5f, 0.0f), Vec3(10.0f, 0.5f, 10.0f));
    addStatic(shortScene, "HalfWall", Vec3(0.0f, 0.5f, 0.0f), Vec3(0.15f, 0.5f, 1.0f));
    NavGeometry shortGeometry = Hephaestus::Navigation::CollectSceneGeometry(shortScene);
    Hephaestus::Navigation::AddVaultLinks(shortScene, settings, shortGeometry);
    NavMesh shortMesh;
    bool vaulted = false;
    NavBuildSettings costly = settings;
    costly.vaultLinkCost = 20.0f;
    Check(shortMesh.Build(shortGeometry, costly, error) &&
              shortMesh.FindPath(Vec3(-3.0f, 0.0f, 0.0f), Vec3(3.0f, 0.0f, 0.0f), path, &linkStarts),
          "a path past a short half wall is found");
    for (unsigned char start : linkStarts) {
        vaulted = vaulted || start != 0;
    }
    Check(!shortGeometry.links.empty() && !vaulted, "with costly vault links a short half wall is walked round");

    Scene lintelScene("LintelScene");
    addStatic(lintelScene, "Floor", Vec3(0.0f, -0.5f, 0.0f), Vec3(10.0f, 0.5f, 10.0f));
    addStatic(lintelScene, "HalfWall", Vec3(0.0f, 0.5f, 0.0f), Vec3(0.15f, 0.5f, 1.0f));
    addStatic(lintelScene, "Lintel", Vec3(0.0f, 2.5f, 0.0f), Vec3(0.15f, 0.5f, 1.0f));
    addStatic(lintelScene, "TallBox", Vec3(5.0f, 1.0f, 5.0f), Vec3(1.0f, 1.0f, 1.0f));
    NavGeometry lintelGeometry = Hephaestus::Navigation::CollectSceneGeometry(lintelScene);
    Check(Hephaestus::Navigation::AddVaultLinks(lintelScene, settings, lintelGeometry) == 0,
          "no vault links under a lintel or over cover too tall to vault");

    // A 4 x 3 x 4 m block on the floor: Recast only sees surfaces, so its inside must be filled in.
    {
        NavGeometry blockGeometry;
        blockGeometry.AddBox(Translation(Vec3(0.0f, -0.5f, 0.0f)), Vec3(10.0f, 0.5f, 10.0f), Vec3(0.0f, 0.0f, 0.0f));
        blockGeometry.AddBox(Translation(Vec3(0.0f, 1.5f, 0.0f)), Vec3(2.0f, 1.5f, 2.0f), Vec3(0.0f, 0.0f, 0.0f));
        NavMesh blockMesh;
        Vec3 inside;
        const bool built = blockMesh.Build(blockGeometry, NavBuildSettings{}, error);
        const bool found = built && blockMesh.ClosestPoint(Vec3(0.0f, 0.1f, 0.0f), inside, Vec3(0.5f, 0.5f, 0.5f));
        Check(built && !found, "the inside of a block taller than an agent is not walkable");
        Vec3 top;
        Check(blockMesh.ClosestPoint(Vec3(0.0f, 3.0f, 0.0f), top, Vec3(0.5f, 0.5f, 0.5f)) && std::fabs(top.y - 3.0f) < 0.2f,
              "its top still is");
    }

    // Round and hull shapes are collected as the physics system builds them.
    {
        Scene shapes("Shapes");
        auto addShape = [&](const char* name, const Vec3& position, const PhysicsShapeComponent& shape) {
            Entity entity = shapes.CreateEntity(name);
            entity.GetComponent<TransformComponent>().position = position;
            auto& body = entity.AddComponent<RigidBodyComponent>();
            body.SetMotion(RigidBodyMotion::Static);
            body.mass = 0.0f;
            entity.AddComponent<PhysicsShapeComponent>(shape);
            return entity;
        };
        addShape("Floor", Vec3(0.0f, -0.5f, 0.0f), PhysicsShapeComponent::CreateBox(Vec3(10.0f, 0.5f, 10.0f)));
        PhysicsShapeComponent pillar;
        pillar.type = PhysicsShapeType::Cylinder;
        pillar.radius = 1.0f;
        pillar.height = 3.0f;
        addShape("Pillar", Vec3(0.0f, 1.5f, 0.0f), pillar);
        addShape("Ball", Vec3(5.0f, 1.0f, 5.0f), PhysicsShapeComponent::CreateSphere(1.0f));
        PhysicsShapeComponent ramp;
        ramp.type = PhysicsShapeType::ConvexHull;
        ramp.points = {Vec3(-1, 0, -2), Vec3(1, 0, -2), Vec3(-1, 0, 2), Vec3(1, 0, 2), Vec3(-1, 1, 2), Vec3(1, 1, 2)};
        addShape("Wedge", Vec3(-5.0f, 0.0f, -5.0f), ramp);
        Entity mesh = addShape("Mesh", Vec3(5.0f, 0.0f, -5.0f), PhysicsShapeComponent::CreateBox(Vec3(1.5f, 0.5f, 1.5f)));
        auto& renderer = mesh.AddComponent<MeshRendererComponent>();
        renderer.localBounds = AABB(Vec3(-1.5f, 0.0f, -1.5f), Vec3(1.5f, 1.0f, 1.5f));
        int skippedShapes = -1;
        const NavGeometry shapeGeometry = Hephaestus::Navigation::CollectSceneGeometry(shapes, &skippedShapes);
        Check(skippedShapes == 0 && shapeGeometry.pieces.size() == 5, "boxes, cylinders, spheres and hulls are all collected");
        NavMesh shapeMesh;
        Check(shapeMesh.Build(shapeGeometry, NavBuildSettings{}, error) &&
                  shapeMesh.FindPath(Vec3(-3.0f, 0.0f, 0.0f), Vec3(3.0f, 0.0f, 0.0f), path),
              "a path past a cylinder pillar is found");
        bool around = false;
        for (const Vec3& corner : path) {
            around = around || std::fabs(corner.z) > 1.2f;
        }
        Check(around, "the path goes round the pillar");
        Vec3 onWedge;
        Check(shapeMesh.ClosestPoint(Vec3(-5.0f, 1.0f, -3.4f), onWedge, Vec3(0.3f, 1.0f, 0.3f)) &&
                  std::fabs(onWedge.y - 0.9f) < 0.2f,
              "the hull's 14 degree slope is walkable");
        Vec3 onMesh;
        Check(shapeMesh.ClosestPoint(Vec3(5.0f, 1.0f, -5.0f), onMesh, Vec3(0.3f, 0.3f, 0.3f)) &&
                  std::fabs(onMesh.y - 1.0f) < 0.15f,
              "a box collider is centred on its mesh's bounds, as the physics system places it");
    }

    // Build settings follow the scene's characters.
    {
        Scene characters("Characters");
        const NavBuildSettings none = Hephaestus::Navigation::SettingsForScene(characters);
        Check(std::fabs(none.agentRadius - CharacterControllerComponent{}.radius) < 1e-5f &&
                  std::fabs(none.agentMaxClimb - CharacterControllerComponent{}.stepHeight) < 1e-5f,
              "with no characters the navmesh uses the default controller's size");
        auto& player = characters.CreateEntity("Player").AddComponent<CharacterControllerComponent>();
        player.radius = 0.3f;
        player.stepHeight = 0.35f;
        auto& brute = characters.CreateEntity("Brute").AddComponent<CharacterControllerComponent>();
        brute.radius = 0.5f;
        brute.height = 2.2f;
        brute.stepHeight = 0.25f;
        const NavBuildSettings fitted = Hephaestus::Navigation::SettingsForScene(characters);
        Check(std::fabs(fitted.agentRadius - 0.5f) < 1e-5f && std::fabs(fitted.agentHeight - 2.2f) < 1e-5f &&
                  std::fabs(fitted.agentMaxClimb - 0.25f) < 1e-5f && std::fabs(fitted.cellSize - 0.25f) < 1e-5f,
              "the widest, tallest and least able character sets the navmesh");
    }

    // Imported level art with mesh collision: the navmesh reads the model's triangles from the shared cache.
    {
        namespace fs = std::filesystem;
        const fs::path folder = fs::temp_directory_path() / "hephaestus_navmesh_mesh_tests";
        fs::create_directories(folder);
        const fs::path model = folder / "Ground.obj";
        auto writeGround = [&](float height) {
            std::ofstream out(model, std::ios::trunc);
            out << "v -8 " << height << " -8\nv -8 " << height << " 8\nv 8 " << height << " 8\nv 8 " << height << " -8\n";
            out << "f 1 2 3\nf 1 3 4\n";
        };
        writeGround(0.0f);
        Hephaestus::MeshGeometryCache::Clear();
        const auto first = Hephaestus::MeshGeometryCache::Get(model.generic_string());
        Check(first && first->vertices.size() >= 4 && first->indices.size() == 6, "the shared cache loads a model's triangles");
        Check(Hephaestus::MeshGeometryCache::Get(model.generic_string()) == first, "a second request reuses the loaded model");
        writeGround(2.0f);
        fs::last_write_time(model, fs::last_write_time(model) + std::chrono::seconds(5));
        const auto second = Hephaestus::MeshGeometryCache::Get(model.generic_string());
        Check(second && second != first && std::fabs(second->vertices[0].pos[1] - 2.0f) < 1e-4f &&
                  std::fabs(first->vertices[0].pos[1]) < 1e-4f,
              "a changed model file is loaded again, and earlier holders keep the old mesh safely");
        writeGround(0.0f);
        fs::last_write_time(model, fs::last_write_time(model) + std::chrono::seconds(10));

        Scene art("Art");
        Entity ground = art.CreateEntity("Ground");
        ground.AddComponent<MeshRendererComponent>().meshAssetPath = model.generic_string();
        auto& collider = ground.AddComponent<MeshColliderComponent>();
        collider.enabled = true;
        collider.useComplexCollision = true;
        Entity rock = art.CreateEntity("Rock");
        rock.GetComponent<TransformComponent>().position = Vec3(0.0f, 0.0f, 0.0f);
        rock.AddComponent<MeshRendererComponent>().meshAssetPath = model.generic_string();
        rock.GetComponent<TransformComponent>().scale = Vec3(0.25f, 1.0f, 0.25f);
        rock.GetComponent<TransformComponent>().position = Vec3(4.0f, 1.0f, 4.0f);
        auto& rockBody = rock.AddComponent<RigidBodyComponent>();
        rockBody.SetMotion(RigidBodyMotion::Static);
        rockBody.mass = 0.0f;
        PhysicsShapeComponent meshShape;
        meshShape.type = PhysicsShapeType::Mesh;
        rock.AddComponent<PhysicsShapeComponent>(meshShape);
        int skippedMeshes = -1;
        const NavGeometry artGeometry = Hephaestus::Navigation::CollectSceneGeometry(art, &skippedMeshes);
        Check(skippedMeshes == 0 && artGeometry.pieces.size() == 2 && !artGeometry.pieces[0].solid,
              "static mesh colliders and Mesh shapes are collected as surfaces");
        NavMesh artMesh;
        Vec3 onGround;
        Check(artMesh.Build(artGeometry, NavBuildSettings{}, error) &&
                  artMesh.ClosestPoint(Vec3(-2.0f, 0.5f, -2.0f), onGround, Vec3(0.3f, 1.0f, 0.3f)) &&
                  std::fabs(onGround.y) < 0.2f,
              "bots can walk on imported ground with mesh collision");
        Vec3 onRock;
        Check(artMesh.ClosestPoint(Vec3(4.0f, 1.5f, 4.0f), onRock, Vec3(0.2f, 0.6f, 0.2f)) && std::fabs(onRock.y - 1.0f) < 0.2f,
              "a Mesh shape is placed by its entity's transform, as physics places it");
    }

    if (!g_pass) {
        return 1;
    }
    std::cout << "NavMeshTests passed\n";
    return 0;
}
