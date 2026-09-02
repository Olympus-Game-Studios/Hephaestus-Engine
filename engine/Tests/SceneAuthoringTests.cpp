#include "Assets/EditorViewState.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scene/SceneAuthoring.h"

#include <cmath>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const char* message)
{
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

bool Near(float a, float b)
{
    return std::abs(a - b) < 0.001f;
}

bool TestSnapAndRaycast()
{
    bool ok = true;
    ok &= Check(Near(Hephaestus::SceneAuthoring::SnapScalar(1.24f, 0.5f), 1.0f),
                "scalar snapping");
    const std::vector<float> distributed =
        Hephaestus::SceneAuthoring::EvenlySpacedValues(-2.0f, 4.0f, 4);
    ok &= Check(distributed.size() == 4 &&
                Near(distributed[0], -2.0f) &&
                Near(distributed[1], 0.0f) &&
                Near(distributed[2], 2.0f) &&
                Near(distributed[3], 4.0f),
                "distribution math");

    Scene scene;
    Entity cube = scene.CreateEntity("Cube");
    auto& mesh = cube.AddComponent<MeshRendererComponent>();
    mesh.meshHandle = 0;
    mesh.localBounds = AABB(Vec3(-0.5f, -0.5f, -0.5f),
                            Vec3(0.5f, 0.5f, 0.5f));
    Hephaestus::SceneAuthoring::SceneRaycastHit hit;
    ok &= Check(Hephaestus::SceneAuthoring::RaycastBounds(
                    scene, Ray(Vec3(0, 0, 5), Vec3(0, 0, -1)), hit),
                "bounds raycast");
    ok &= Check(hit.entity == static_cast<entt::entity>(cube),
                "raycast entity");
    ok &= Check(Near(hit.point.z, 0.5f), "raycast point");
    return ok;
}

bool TestParentAndDuplicate()
{
    bool ok = true;
    Scene scene;
    Entity parent = scene.CreateEntity("Parent");
    parent.GetComponent<TransformComponent>().position = Vec3(10, 0, 0);
    Entity child = scene.CreateEntity("Child");
    child.GetComponent<TransformComponent>().position = Vec3(2, 0, 0);
    Entity grandchild = scene.CreateEntity("Grandchild");
    scene.SetParent(grandchild, child);

    scene.SetParent(child, parent, true);
    const Mat4 childWorld = scene.GetWorldTransform(child);
    ok &= Check(Near(childWorld.m[12], 2.0f),
                "world-preserving parent");

    auto duplicates = scene.DuplicateEntities(
        {static_cast<entt::entity>(child)}, true);
    ok &= Check(duplicates.size() == 1, "duplicate root");
    if (!duplicates.empty()) {
        ok &= Check(scene.GetParent(duplicates.front()) == parent,
                    "duplicate keeps parent");
        ok &= Check(scene.GetChildren(duplicates.front()).size() == 1,
                    "duplicate keeps hierarchy");
    }
    return ok;
}

bool TestEditorViewState(const fs::path& scratch)
{
    Hephaestus::EditorViewState state;
    state.hiddenEntities.insert(12);
    state.lockedEntities.insert(42);
    Hephaestus::EditorCameraBookmark bookmark;
    bookmark.positionX = 3.0f;
    bookmark.orthographic = true;
    state.bookmarks[2] = bookmark;
    const fs::path path = scratch / "view.json";
    if (!Check(state.Save(path), "view-state save")) return false;

    Hephaestus::EditorViewState loaded;
    bool ok = Check(loaded.Load(path), "view-state load");
    ok &= Check(loaded.hiddenEntities.contains(12), "hidden roundtrip");
    ok &= Check(loaded.lockedEntities.contains(42), "locked roundtrip");
    ok &= Check(loaded.bookmarks.contains(2) &&
                loaded.bookmarks[2].orthographic,
                "bookmark roundtrip");
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::current_path();
    const fs::path scratch = root / "build" / "scene_authoring_test_out";
    std::error_code ec;
    fs::remove_all(scratch, ec);
    fs::create_directories(scratch, ec);

    bool ok = TestSnapAndRaycast();
    ok &= TestParentAndDuplicate();
    ok &= TestEditorViewState(scratch);
    if (ok) std::cout << "SceneAuthoringTests passed.\n";
    return ok ? 0 : 1;
}
