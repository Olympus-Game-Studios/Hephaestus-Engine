#include "NavMesh.h"

#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace Hephaestus::Navigation {
namespace {

constexpr int kMaxPathPolys = 256;
constexpr unsigned short kWalkableFlag = 1;

struct HeightfieldDeleter { void operator()(rcHeightfield* p) const { rcFreeHeightField(p); } };
struct CompactDeleter { void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); } };
struct ContourDeleter { void operator()(rcContourSet* p) const { rcFreeContourSet(p); } };
struct PolyMeshDeleter { void operator()(rcPolyMesh* p) const { rcFreePolyMesh(p); } };
struct DetailDeleter { void operator()(rcPolyMeshDetail* p) const { rcFreePolyMeshDetail(p); } };

thread_local std::uint32_t* t_rngState = nullptr;

float NextRandom()
{
    std::uint32_t& s = *t_rngState;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(s & 0xFFFFFF) / static_cast<float>(0x1000000);
}

} // namespace

void NavGeometry::AddBox(const Mat4& world, const Vec3& halfExtents, const Vec3& offset)
{
    const int base = static_cast<int>(vertices.size() / 3);
    for (int i = 0; i < 8; ++i) {
        const Vec3 local(offset.x + ((i & 1) ? halfExtents.x : -halfExtents.x),
                         offset.y + ((i & 2) ? halfExtents.y : -halfExtents.y),
                         offset.z + ((i & 4) ? halfExtents.z : -halfExtents.z));
        const Vec3 p = Mat4::TransformPoint(world, local);
        vertices.insert(vertices.end(), {p.x, p.y, p.z});
    }
    // Corner bit 0 is +x, bit 1 +y, bit 2 +z. Recast marks a triangle walkable from the normal
    // cross(v1 - v0, v2 - v0), so the top face is wound to face +y.
    static const int kFaces[12][3] = {
        {2, 6, 3}, {3, 6, 7},  // +y
        {0, 1, 4}, {1, 5, 4},  // -y
        {0, 4, 2}, {2, 4, 6},  // -x
        {1, 3, 5}, {3, 7, 5},  // +x
        {0, 2, 1}, {1, 2, 3},  // -z
        {4, 5, 6}, {5, 7, 6},  // +z
    };
    for (const auto& face : kFaces) {
        triangles.insert(triangles.end(), {base + face[0], base + face[1], base + face[2]});
    }
}

NavGeometry CollectSceneGeometry(Scene& scene, int* skippedShapes)
{
    NavGeometry geometry;
    int skipped = 0;
    auto view = scene.GetRegistry().view<TransformComponent, RigidBodyComponent, PhysicsShapeComponent>();
    for (auto handle : view) {
        const auto& body = view.get<RigidBodyComponent>(handle);
        if (!body.IsStatic() || body.isTrigger) {
            continue;
        }
        const auto& shape = view.get<PhysicsShapeComponent>(handle);
        if (shape.type != PhysicsShapeType::Box) {
            ++skipped;
            continue;
        }
        geometry.AddBox(scene.GetWorldTransform(Entity(handle, &scene)), shape.extents, shape.offset);
    }
    if (skippedShapes) {
        *skippedShapes = skipped;
    }
    return geometry;
}

NavMesh::~NavMesh()
{
    Clear();
}

void NavMesh::Clear()
{
    dtFreeNavMeshQuery(m_query);
    dtFreeNavMesh(m_navMesh);
    m_query = nullptr;
    m_navMesh = nullptr;
}

int NavMesh::PolygonCount() const
{
    if (!m_navMesh) {
        return 0;
    }
    const dtNavMesh* mesh = m_navMesh;
    const dtMeshTile* tile = mesh->getTile(0);
    return tile && tile->header ? tile->header->polyCount : 0;
}

bool NavMesh::Build(const NavGeometry& geometry, const NavBuildSettings& s, std::string& error)
{
    Clear();
    if (geometry.Empty()) {
        error = "no walkable geometry (static box colliders) in the scene";
        return false;
    }
    const float* verts = geometry.vertices.data();
    const int vertCount = static_cast<int>(geometry.vertices.size() / 3);
    const int* tris = geometry.triangles.data();
    const int triCount = static_cast<int>(geometry.triangles.size() / 3);

    rcConfig cfg{};
    cfg.cs = s.cellSize;
    cfg.ch = s.cellHeight;
    cfg.walkableSlopeAngle = s.agentMaxSlopeDegrees;
    cfg.walkableHeight = static_cast<int>(std::ceil(s.agentHeight / s.cellHeight));
    cfg.walkableClimb = static_cast<int>(std::floor(s.agentMaxClimb / s.cellHeight));
    cfg.walkableRadius = static_cast<int>(std::ceil(s.agentRadius / s.cellSize));
    cfg.maxEdgeLen = static_cast<int>(s.edgeMaxLength / s.cellSize);
    cfg.maxSimplificationError = s.edgeMaxError;
    cfg.minRegionArea = static_cast<int>(s.regionMinSize * s.regionMinSize);
    cfg.mergeRegionArea = static_cast<int>(s.regionMergeSize * s.regionMergeSize);
    cfg.maxVertsPerPoly = 6;
    cfg.detailSampleDist = s.detailSampleDistance < 0.9f ? 0.0f : s.cellSize * s.detailSampleDistance;
    cfg.detailSampleMaxError = s.cellHeight * s.detailSampleMaxError;
    rcCalcBounds(verts, vertCount, cfg.bmin, cfg.bmax);
    rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

    rcContext ctx(false);
    std::unique_ptr<rcHeightfield, HeightfieldDeleter> solid(rcAllocHeightfield());
    if (!solid || !rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
        error = "could not create the heightfield";
        return false;
    }
    std::vector<unsigned char> areas(static_cast<size_t>(triCount), 0);
    rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, vertCount, tris, triCount, areas.data());
    if (!rcRasterizeTriangles(&ctx, verts, vertCount, tris, areas.data(), triCount, *solid, cfg.walkableClimb)) {
        error = "could not rasterize the geometry";
        return false;
    }
    rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
    rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

    std::unique_ptr<rcCompactHeightfield, CompactDeleter> chf(rcAllocCompactHeightfield());
    if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf)) {
        error = "could not build the compact heightfield";
        return false;
    }
    solid.reset();
    if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf) || !rcBuildDistanceField(&ctx, *chf) ||
        !rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea)) {
        error = "could not build walkable regions";
        return false;
    }
    std::unique_ptr<rcContourSet, ContourDeleter> contours(rcAllocContourSet());
    if (!contours || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours)) {
        error = "could not build contours";
        return false;
    }
    std::unique_ptr<rcPolyMesh, PolyMeshDeleter> pmesh(rcAllocPolyMesh());
    if (!pmesh || !rcBuildPolyMesh(&ctx, *contours, cfg.maxVertsPerPoly, *pmesh)) {
        error = "could not build the polygon mesh";
        return false;
    }
    std::unique_ptr<rcPolyMeshDetail, DetailDeleter> dmesh(rcAllocPolyMeshDetail());
    if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) {
        error = "could not build the detail mesh";
        return false;
    }
    if (pmesh->npolys == 0) {
        error = "no walkable area";
        return false;
    }
    for (int i = 0; i < pmesh->npolys; ++i) {
        pmesh->flags[i] = pmesh->areas[i] == RC_WALKABLE_AREA ? kWalkableFlag : 0;
    }

    dtNavMeshCreateParams params{};
    params.verts = pmesh->verts;
    params.vertCount = pmesh->nverts;
    params.polys = pmesh->polys;
    params.polyAreas = pmesh->areas;
    params.polyFlags = pmesh->flags;
    params.polyCount = pmesh->npolys;
    params.nvp = pmesh->nvp;
    params.detailMeshes = dmesh->meshes;
    params.detailVerts = dmesh->verts;
    params.detailVertsCount = dmesh->nverts;
    params.detailTris = dmesh->tris;
    params.detailTriCount = dmesh->ntris;
    params.walkableHeight = s.agentHeight;
    params.walkableRadius = s.agentRadius;
    params.walkableClimb = s.agentMaxClimb;
    rcVcopy(params.bmin, pmesh->bmin);
    rcVcopy(params.bmax, pmesh->bmax);
    params.cs = cfg.cs;
    params.ch = cfg.ch;
    params.buildBvTree = true;

    unsigned char* data = nullptr;
    int dataSize = 0;
    if (!dtCreateNavMeshData(&params, &data, &dataSize)) {
        error = "could not create the navmesh data";
        return false;
    }
    m_navMesh = dtAllocNavMesh();
    if (!m_navMesh || dtStatusFailed(m_navMesh->init(data, dataSize, DT_TILE_FREE_DATA))) {
        dtFree(data);
        Clear();
        error = "could not initialise the navmesh";
        return false;
    }
    m_query = dtAllocNavMeshQuery();
    if (!m_query || dtStatusFailed(m_query->init(m_navMesh, 2048))) {
        Clear();
        error = "could not initialise the navmesh query";
        return false;
    }
    return true;
}

bool NavMesh::ClosestPoint(const Vec3& point, Vec3& out, const Vec3& searchExtents) const
{
    if (!m_query) {
        return false;
    }
    dtQueryFilter filter;
    const float p[3] = {point.x, point.y, point.z};
    const float extents[3] = {searchExtents.x, searchExtents.y, searchExtents.z};
    dtPolyRef ref = 0;
    float nearest[3];
    if (dtStatusFailed(m_query->findNearestPoly(p, extents, &filter, &ref, nearest)) || ref == 0) {
        return false;
    }
    out = Vec3(nearest[0], nearest[1], nearest[2]);
    return true;
}

bool NavMesh::FindPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& corners) const
{
    corners.clear();
    if (!m_query) {
        return false;
    }
    dtQueryFilter filter;
    const float extents[3] = {2.0f, 4.0f, 2.0f};
    const float start[3] = {from.x, from.y, from.z};
    const float end[3] = {to.x, to.y, to.z};
    dtPolyRef startRef = 0;
    dtPolyRef endRef = 0;
    float startPoint[3];
    float endPoint[3];
    m_query->findNearestPoly(start, extents, &filter, &startRef, startPoint);
    m_query->findNearestPoly(end, extents, &filter, &endRef, endPoint);
    if (startRef == 0 || endRef == 0) {
        return false;
    }
    dtPolyRef polys[kMaxPathPolys];
    int polyCount = 0;
    if (dtStatusFailed(m_query->findPath(startRef, endRef, startPoint, endPoint, &filter, polys, &polyCount,
                                         kMaxPathPolys)) ||
        polyCount == 0) {
        return false;
    }
    // A partial path stops at the reachable polygon nearest the goal.
    float target[3] = {endPoint[0], endPoint[1], endPoint[2]};
    if (polys[polyCount - 1] != endRef) {
        m_query->closestPointOnPoly(polys[polyCount - 1], endPoint, target, nullptr);
    }
    float straight[kMaxPathPolys * 3];
    int straightCount = 0;
    if (dtStatusFailed(m_query->findStraightPath(startPoint, target, polys, polyCount, straight, nullptr, nullptr,
                                                 &straightCount, kMaxPathPolys)) ||
        straightCount == 0) {
        return false;
    }
    corners.reserve(static_cast<size_t>(straightCount));
    for (int i = 0; i < straightCount; ++i) {
        corners.emplace_back(straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]);
    }
    return true;
}

bool NavMesh::RandomPointAround(const Vec3& center, float radius, std::uint32_t& rngState, Vec3& out) const
{
    if (!m_query) {
        return false;
    }
    if (rngState == 0) {
        rngState = 0x9E3779B9u;
    }
    dtQueryFilter filter;
    const float extents[3] = {2.0f, 4.0f, 2.0f};
    const float c[3] = {center.x, center.y, center.z};
    dtPolyRef startRef = 0;
    float startPoint[3];
    if (dtStatusFailed(m_query->findNearestPoly(c, extents, &filter, &startRef, startPoint)) || startRef == 0) {
        return false;
    }
    // Detour picks a random point inside a polygon that touches the circle, which can lie well outside it on
    // large polygons, so only points within the radius are accepted.
    t_rngState = &rngState;
    bool found = false;
    for (int attempt = 0; attempt < 32 && !found; ++attempt) {
        dtPolyRef ref = 0;
        float point[3];
        const dtStatus status = m_query->findRandomPointAroundCircle(startRef, startPoint, radius, &filter,
                                                                      NextRandom, &ref, point);
        if (dtStatusFailed(status) || ref == 0) {
            break;
        }
        const float dx = point[0] - startPoint[0];
        const float dz = point[2] - startPoint[2];
        if (dx * dx + dz * dz <= radius * radius) {
            out = Vec3(point[0], point[1], point[2]);
            found = true;
        }
    }
    t_rngState = nullptr;
    return found;
}

} // namespace Hephaestus::Navigation
