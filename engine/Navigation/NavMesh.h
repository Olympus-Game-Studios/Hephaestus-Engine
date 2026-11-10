#pragma once

#include "../Core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

class Scene;
class dtNavMesh;
class dtNavMeshQuery;

namespace Hephaestus::Navigation {

struct NavBuildSettings {
    float cellSize = 0.2f;
    float cellHeight = 0.1f;
    float agentHeight = 1.8f;
    float agentRadius = 0.35f;
    float agentMaxClimb = 0.4f;
    float agentMaxSlopeDegrees = 45.0f;
    float regionMinSize = 8.0f;
    float regionMergeSize = 20.0f;
    float edgeMaxLength = 12.0f;
    float edgeMaxError = 1.3f;
    float detailSampleDistance = 6.0f;
    float detailSampleMaxError = 1.0f;
};

// Triangle soup in world space (y up) that the navmesh is built from.
struct NavGeometry {
    std::vector<float> vertices;
    std::vector<int> triangles;

    void AddBox(const Mat4& world, const Vec3& halfExtents, const Vec3& offset);
    bool Empty() const { return triangles.empty(); }
};

// Walkable geometry of a scene: static, solid rigid bodies with box collision shapes.
NavGeometry CollectSceneGeometry(Scene& scene, int* skippedShapes = nullptr);

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

    // Corners of the shortest walkable path; the last corner is the reachable point closest to `to`.
    bool FindPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& corners) const;
    bool ClosestPoint(const Vec3& point, Vec3& out, const Vec3& searchExtents = Vec3(2.0f, 4.0f, 2.0f)) const;
    // A random walkable point within radius (along the navmesh) of center; rngState is advanced.
    bool RandomPointAround(const Vec3& center, float radius, std::uint32_t& rngState, Vec3& out) const;

private:
    dtNavMesh* m_navMesh = nullptr;
    dtNavMeshQuery* m_query = nullptr;
};

} // namespace Hephaestus::Navigation
