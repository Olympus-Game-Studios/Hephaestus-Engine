#include "Navigation/NavMesh.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <cmath>
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

    if (!g_pass) {
        return 1;
    }
    std::cout << "NavMeshTests passed\n";
    return 0;
}
