#pragma once

#include "../Core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

class Scene;
class dtNavMesh;
class dtNavMeshQuery;

namespace Hephaestus::Navigation {

// Agent values default to CharacterControllerComponent's defaults; SettingsForScene fits them to the scene's characters.
struct NavBuildSettings {
    float cellSize = 0.15f;  // half the agent radius, so erosion is exactly the radius
    float cellHeight = 0.1f;
    float agentHeight = 1.8f;
    float agentRadius = 0.3f;
    float agentMaxClimb = 0.3f;
    float agentMaxSlopeDegrees = 45.0f;
    float regionMinSize = 8.0f;
    float regionMergeSize = 20.0f;
    float edgeMaxLength = 12.0f;
    float edgeMaxError = 1.3f;
    float detailSampleDistance = 6.0f;
    float detailSampleMaxError = 1.0f;
    // Vault links (AddVaultLinks): upright static boxes this low and thin get links across them.
    float vaultMinHeight = 0.4f;
    float vaultMaxHeight = 1.1f;
    float vaultMaxDepth = 1.25f;
    float vaultLinkSpacing = 1.5f;  // between links along a long piece of cover
    float vaultLinkReach = 0.6f;    // from each face of the cover to the link's ends
    float vaultLinkCost = 3.0f;     // times its length, so walkers vault only when going round is longer
};

// A one-off jump between two walkable points, such as over low cover, that walking cannot connect.
struct NavLink {
    Vec3 from;
    Vec3 to;
};

// Triangle soup in world space (y up) that the navmesh is built from.
struct NavGeometry {
    std::vector<float> vertices;
    std::vector<int> triangles;
    std::vector<NavLink> links;  // both ways
    // First triangle of each closed convex solid added by the Add functions, in order. A solid's triangles run to the
    // next one's start; the navmesh fills its whole volume, so the inside of a block is never walkable.
    std::vector<int> solidStarts;

    void AddBox(const Mat4& world, const Vec3& halfExtents, const Vec3& offset);
    // Upright around local Y through offset, with flat ends; height is the whole length.
    void AddCylinder(const Mat4& world, float radius, float height, const Vec3& offset);
    void AddSphere(const Mat4& world, float radius, const Vec3& offset);
    // The convex hull of the points (local space, plus offset). More than 64 points fall back to their bounding box.
    void AddConvexHull(const Mat4& world, const std::vector<Vec3>& points, const Vec3& offset);
    bool Empty() const { return triangles.empty(); }
};

// Walkable geometry of a scene: static, solid rigid bodies, shaped and placed as the physics system builds them (box,
// sphere, capsule, cylinder and convex hull shapes, or the mesh bounds when there is no shape). Mesh collision shapes
// are not read yet; skippedShapes counts them.
NavGeometry CollectSceneGeometry(Scene& scene, int* skippedShapes = nullptr);

// Build settings fitted to the scene's character controllers: the widest radius, tallest height, lowest step and
// shallowest slope, so every character fits where the navmesh says it can go. Defaults when there are none.
NavBuildSettings SettingsForScene(Scene& scene);

// Adds links across the scene's vaultable cover: upright static boxes from vaultMinHeight to vaultMaxHeight tall and
// no deeper than vaultMaxDepth, every vaultLinkSpacing along their length, where no other box blocks a standing body
// over the top. Returns how many were added.
int AddVaultLinks(Scene& scene, const NavBuildSettings& settings, NavGeometry& geometry);

class NavMesh {
public:
    NavMesh() = default;
    ~NavMesh();
    NavMesh(const NavMesh&) = delete;
    NavMesh& operator=(const NavMesh&) = delete;

    bool Build(const NavGeometry& geometry, const NavBuildSettings& settings, std::string& error);
    void Clear();
    bool IsReady() const { return m_query != nullptr; }
    int PolygonCount() const;

    // Corners of the shortest walkable path; the last corner is the reachable point closest to `to`. With
    // linkStarts, each corner gets 1 where a link starts (the next corner is the link's other end), else 0.
    bool FindPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& corners,
                  std::vector<unsigned char>* linkStarts = nullptr) const;
    bool ClosestPoint(const Vec3& point, Vec3& out, const Vec3& searchExtents = Vec3(2.0f, 4.0f, 2.0f)) const;
    // A random walkable point within radius (along the navmesh) of center; rngState is advanced.
    bool RandomPointAround(const Vec3& center, float radius, std::uint32_t& rngState, Vec3& out) const;

private:
    dtNavMesh* m_navMesh = nullptr;
    dtNavMeshQuery* m_query = nullptr;
    float m_vaultLinkCost = 1.0f;
};

} // namespace Hephaestus::Navigation
