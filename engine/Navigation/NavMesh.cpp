#include "NavMesh.h"

#include "../Assets/MeshGeometryCache.h"
#include "../Scene/CharacterComponent.h"
#include "../Scene/CharacterControllerComponent.h"
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
constexpr unsigned char kVaultArea = 1;

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
    pieces.push_back({static_cast<int>(triangles.size() / 3), true});
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

void NavGeometry::AddCylinder(const Mat4& world, float radius, float height, const Vec3& offset)
{
    constexpr int kSides = 12;
    pieces.push_back({static_cast<int>(triangles.size() / 3), true});
    const int base = static_cast<int>(vertices.size() / 3);
    for (int i = 0; i < kSides; ++i) {
        const float a = 6.28318531f * static_cast<float>(i) / static_cast<float>(kSides);
        for (float y : {-0.5f * height, 0.5f * height}) {
            const Vec3 p = Mat4::TransformPoint(world, Vec3(offset.x + radius * std::cos(a), offset.y + y,
                                                            offset.z + radius * std::sin(a)));
            vertices.insert(vertices.end(), {p.x, p.y, p.z});
        }
    }
    // Vertex 2i is the bottom of side i, 2i + 1 its top; angles run from +x towards +z.
    for (int i = 0; i < kSides; ++i) {
        const int b0 = base + 2 * i;
        const int b1 = base + 2 * ((i + 1) % kSides);
        triangles.insert(triangles.end(), {b0, b0 + 1, b1 + 1, b0, b1 + 1, b1});
        if (i >= 1 && i + 1 < kSides) {
            triangles.insert(triangles.end(), {base + 1, b1 + 1, b0 + 1});  // top, facing +y
            triangles.insert(triangles.end(), {base, b0, b1});              // bottom
        }
    }
}

void NavGeometry::AddSphere(const Mat4& world, float radius, const Vec3& offset)
{
    constexpr int kSegments = 12;
    constexpr int kRings = 6;
    std::vector<Vec3> points;
    for (int r = 0; r <= kRings; ++r) {
        const float phi = 3.14159265f * static_cast<float>(r) / static_cast<float>(kRings);
        for (int s = 0; s < (r == 0 || r == kRings ? 1 : kSegments); ++s) {
            const float theta = 6.28318531f * static_cast<float>(s) / static_cast<float>(kSegments);
            points.push_back(Vec3(radius * std::sin(phi) * std::cos(theta), -radius * std::cos(phi),
                                  radius * std::sin(phi) * std::sin(theta)));
        }
    }
    AddConvexHull(world, points, offset);
}

void NavGeometry::AddConvexHull(const Mat4& world, const std::vector<Vec3>& points, const Vec3& offset)
{
    if (points.size() < 4) {
        return;
    }
    if (points.size() > 64) {
        Vec3 mn = points[0];
        Vec3 mx = points[0];
        for (const Vec3& p : points) {
            mn = Vec3(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
            mx = Vec3(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
        }
        AddBox(world, vscale(vsub(mx, mn), 0.5f), vadd(offset, vscale(vadd(mn, mx), 0.5f)));
        return;
    }
    pieces.push_back({static_cast<int>(triangles.size() / 3), true});
    const int base = static_cast<int>(vertices.size() / 3);
    std::vector<Vec3> worldPoints;
    for (const Vec3& local : points) {
        const Vec3 p = Mat4::TransformPoint(world, vadd(local, offset));
        worldPoints.push_back(p);
        vertices.insert(vertices.end(), {p.x, p.y, p.z});
    }
    // Every triangle of points with all the others on one side is part of the hull; it is wound so that
    // cross(v1 - v0, v2 - v0) faces out, which is how Recast tells floors from ceilings.
    const int n = static_cast<int>(worldPoints.size());
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            for (int k = j + 1; k < n; ++k) {
                const Vec3 normal = vcross(vsub(worldPoints[j], worldPoints[i]), vsub(worldPoints[k], worldPoints[i]));
                const float length = vlen(normal);
                if (length < 1e-8f) {
                    continue;
                }
                bool behind = true;
                bool front = true;
                for (int m = 0; m < n && (behind || front); ++m) {
                    const float d = vdot(normal, vsub(worldPoints[m], worldPoints[i])) / length;
                    behind = behind && d <= 1e-4f;
                    front = front && d >= -1e-4f;
                }
                if (behind) {
                    triangles.insert(triangles.end(), {base + i, base + j, base + k});
                } else if (front) {
                    triangles.insert(triangles.end(), {base + i, base + k, base + j});
                }
            }
        }
    }
}

void NavGeometry::AddMesh(const Mat4& world, const MeshData& mesh)
{
    if (mesh.vertices.empty() || mesh.indices.size() < 3) {
        return;
    }
    pieces.push_back({static_cast<int>(triangles.size() / 3), false});
    const int base = static_cast<int>(vertices.size() / 3);
    for (const Vertex& v : mesh.vertices) {
        const Vec3 p = Mat4::TransformPoint(world, Vec3(v.pos[0], v.pos[1], v.pos[2]));
        vertices.insert(vertices.end(), {p.x, p.y, p.z});
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        triangles.insert(triangles.end(), {base + static_cast<int>(mesh.indices[i]), base + static_cast<int>(mesh.indices[i + 1]),
                                           base + static_cast<int>(mesh.indices[i + 2])});
    }
}

NavGeometry CollectSceneGeometry(Scene& scene, int* skippedShapes, const std::filesystem::path& projectRoot)
{
    NavGeometry geometry;
    int skipped = 0;
    entt::registry& registry = scene.GetRegistry();
    auto addMesh = [&](entt::entity handle, const MeshRendererComponent& renderer, const Mat4& world) {
        const auto mesh = renderer.meshAssetPath.empty() ? nullptr
            : Hephaestus::MeshGeometryCache::Get(renderer.meshAssetPath, projectRoot);
        if (mesh) {
            geometry.AddMesh(world, *mesh);
        } else {
            ++skipped;
        }
        (void)handle;
    };
    // Static mesh colliders without a rigid body (JoltPhysicsSystem builds a mesh body for each).
    for (auto handle : registry.view<TransformComponent, MeshColliderComponent, MeshRendererComponent>()) {
        const auto& collider = registry.get<MeshColliderComponent>(handle);
        if (registry.all_of<RigidBodyComponent>(handle) || !collider.enabled || !collider.useComplexCollision) {
            continue;
        }
        addMesh(handle, registry.get<MeshRendererComponent>(handle), scene.GetWorldTransform(Entity(handle, &scene)));
    }
    for (auto handle : registry.view<TransformComponent, RigidBodyComponent>()) {
        const auto& body = registry.get<RigidBodyComponent>(handle);
        if (!body.IsStatic() || body.isTrigger) {
            continue;
        }
        const auto* shape = registry.try_get<PhysicsShapeComponent>(handle);
        const auto* renderer = registry.try_get<MeshRendererComponent>(handle);
        const Mat4 world = scene.GetWorldTransform(Entity(handle, &scene));
        // JoltPhysicsSystem uses the model's own triangles for a Mesh shape, or for complex collision when there is no
        // other shape than the default box.
        if (renderer) {
            const auto* collider = registry.try_get<MeshColliderComponent>(handle);
            const bool defaultBox = shape && shape->type == PhysicsShapeType::Box &&
                std::fabs(shape->extents.x - 0.5f) <= 1e-4f && std::fabs(shape->extents.y - 0.5f) <= 1e-4f &&
                std::fabs(shape->extents.z - 0.5f) <= 1e-4f && std::fabs(shape->offset.x) <= 1e-4f &&
                std::fabs(shape->offset.y) <= 1e-4f && std::fabs(shape->offset.z) <= 1e-4f;
            const bool explicitShape = shape && !defaultBox && shape->type != PhysicsShapeType::Mesh;
            if ((shape && shape->type == PhysicsShapeType::Mesh) ||
                (collider && collider->useComplexCollision && !explicitShape)) {
                addMesh(handle, *renderer, world);
                continue;
            }
        }
        // JoltPhysicsSystem centres a shape on its entity's mesh bounds; characters excepted.
        Vec3 meshCenter(0.0f, 0.0f, 0.0f);
        if (renderer && !registry.all_of<CharacterComponent>(handle)) {
            meshCenter = vscale(vadd(renderer->localBounds.min, renderer->localBounds.max), 0.5f);
        }
        if (!shape) {
            if (renderer) {
                const Vec3 half = vscale(vsub(renderer->localBounds.max, renderer->localBounds.min), 0.5f);
                geometry.AddBox(world, Vec3(std::max(half.x, 0.001f), std::max(half.y, 0.001f), std::max(half.z, 0.001f)),
                                meshCenter);
            }
            continue;
        }
        const Vec3 offset = vadd(shape->offset, meshCenter);
        switch (shape->type) {
        case PhysicsShapeType::Box:
            geometry.AddBox(world, shape->extents, offset);
            break;
        case PhysicsShapeType::Sphere:
            geometry.AddSphere(world, shape->radius, offset);
            break;
        case PhysicsShapeType::Capsule:
            geometry.AddCylinder(world, shape->radius, shape->height + 2.0f * shape->radius, offset);
            break;
        case PhysicsShapeType::Cylinder:
            geometry.AddCylinder(world, shape->radius, shape->height, offset);
            break;
        case PhysicsShapeType::ConvexHull:
            geometry.AddConvexHull(world, shape->points, offset);
            break;
        case PhysicsShapeType::Mesh:
            ++skipped;  // a Mesh shape without a mesh renderer has no triangles
            break;
        }
    }
    if (skippedShapes) {
        *skippedShapes = skipped;
    }
    return geometry;
}

NavBuildSettings SettingsForScene(Scene& scene)
{
    NavBuildSettings settings;
    bool any = false;
    for (auto handle : scene.GetRegistry().view<CharacterControllerComponent>()) {
        const auto& controller = scene.GetRegistry().get<CharacterControllerComponent>(handle);
        if (!controller.enabled) {
            continue;
        }
        if (!any) {
            settings.agentRadius = controller.radius;
            settings.agentHeight = controller.height;
            settings.agentMaxClimb = controller.stepHeight;
            settings.agentMaxSlopeDegrees = controller.maxSlopeAngleDegrees;
            any = true;
            continue;
        }
        settings.agentRadius = std::max(settings.agentRadius, controller.radius);
        settings.agentHeight = std::max(settings.agentHeight, controller.height);
        settings.agentMaxClimb = std::min(settings.agentMaxClimb, controller.stepHeight);
        settings.agentMaxSlopeDegrees = std::min(settings.agentMaxSlopeDegrees, controller.maxSlopeAngleDegrees);
    }
    settings.agentRadius = std::max(settings.agentRadius, 0.05f);
    settings.cellSize = settings.agentRadius * 0.5f;
    return settings;
}

namespace {

struct BoxBounds {
    Vec3 min;
    Vec3 max;
};

bool Overlaps(const BoxBounds& a, const BoxBounds& b)
{
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y &&
           a.min.z < b.max.z && a.max.z > b.min.z;
}

} // namespace

int AddVaultLinks(Scene& scene, const NavBuildSettings& s, NavGeometry& geometry)
{
    struct Box {
        Vec3 centre;
        Vec3 axisX;  // half extents along the box's own axes, in world space
        Vec3 axisY;
        Vec3 axisZ;
        BoxBounds bounds;
    };
    std::vector<Box> boxes;
    auto view = scene.GetRegistry().view<TransformComponent, RigidBodyComponent, PhysicsShapeComponent>();
    for (auto handle : view) {
        const auto& body = view.get<RigidBodyComponent>(handle);
        const auto& shape = view.get<PhysicsShapeComponent>(handle);
        if (!body.IsStatic() || body.isTrigger || shape.type != PhysicsShapeType::Box) {
            continue;
        }
        const Mat4 world = scene.GetWorldTransform(Entity(handle, &scene));
        Vec3 offset = shape.offset;
        if (const auto* renderer = scene.GetRegistry().try_get<MeshRendererComponent>(handle);
            renderer && !scene.GetRegistry().all_of<CharacterComponent>(handle)) {
            offset = vadd(offset, vscale(vadd(renderer->localBounds.min, renderer->localBounds.max), 0.5f));
        }
        Box box;
        box.centre = Mat4::TransformPoint(world, offset);
        auto axis = [&](const Vec3& local) {
            const Vec3 p = Mat4::TransformPoint(world, vadd(offset, local));
            return Vec3(p.x - box.centre.x, p.y - box.centre.y, p.z - box.centre.z);
        };
        box.axisX = axis(Vec3(shape.extents.x, 0.0f, 0.0f));
        box.axisY = axis(Vec3(0.0f, shape.extents.y, 0.0f));
        box.axisZ = axis(Vec3(0.0f, 0.0f, shape.extents.z));
        const Vec3 reach(std::fabs(box.axisX.x) + std::fabs(box.axisY.x) + std::fabs(box.axisZ.x),
                         std::fabs(box.axisX.y) + std::fabs(box.axisY.y) + std::fabs(box.axisZ.y),
                         std::fabs(box.axisX.z) + std::fabs(box.axisY.z) + std::fabs(box.axisZ.z));
        box.bounds = {Vec3(box.centre.x - reach.x, box.centre.y - reach.y, box.centre.z - reach.z),
                      Vec3(box.centre.x + reach.x, box.centre.y + reach.y, box.centre.z + reach.z)};
        boxes.push_back(box);
    }

    int added = 0;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const Box& box = boxes[i];
        const float halfHeight = std::sqrt(box.axisY.x * box.axisY.x + box.axisY.y * box.axisY.y +
                                           box.axisY.z * box.axisY.z);
        if (halfHeight < 1e-4f || std::fabs(box.axisY.y) / halfHeight < 0.98f) {
            continue;  // tilted: not cover to vault
        }
        const float height = 2.0f * halfHeight;
        if (height < s.vaultMinHeight || height > s.vaultMaxHeight) {
            continue;
        }
        const float halfX = std::sqrt(box.axisX.x * box.axisX.x + box.axisX.z * box.axisX.z);
        const float halfZ = std::sqrt(box.axisZ.x * box.axisZ.x + box.axisZ.z * box.axisZ.z);
        const bool thinX = halfX <= halfZ;
        const float halfDepth = thinX ? halfX : halfZ;
        const float halfLength = thinX ? halfZ : halfX;
        if (2.0f * halfDepth > s.vaultMaxDepth || halfDepth < 1e-4f) {
            continue;
        }
        const Vec3& across = thinX ? box.axisX : box.axisZ;
        const Vec3& along = thinX ? box.axisZ : box.axisX;
        const Vec3 acrossDir(across.x / halfDepth, 0.0f, across.z / halfDepth);
        const Vec3 alongDir(along.x / halfLength, 0.0f, along.z / halfLength);
        const float floorY = box.centre.y - halfHeight;
        const float topY = box.centre.y + halfHeight;
        const float toEnd = halfDepth + s.vaultLinkReach;
        const int count = std::max(1, static_cast<int>(std::floor(2.0f * halfLength / s.vaultLinkSpacing)));
        for (int k = 0; k < count; ++k) {
            const float t = (static_cast<float>(k) + 0.5f) / static_cast<float>(count) * 2.0f * halfLength -
                            halfLength;
            const Vec3 mid(box.centre.x + alongDir.x * t, floorY, box.centre.z + alongDir.z * t);
            const Vec3 from(mid.x - acrossDir.x * toEnd, floorY, mid.z - acrossDir.z * toEnd);
            const Vec3 to(mid.x + acrossDir.x * toEnd, floorY, mid.z + acrossDir.z * toEnd);
            // A standing body crossing the top must not run into anything (a window's lintel, a wall behind).
            const float r = s.agentRadius;
            const BoxBounds corridor{
                Vec3(std::min(from.x, to.x) - r, topY + 0.05f, std::min(from.z, to.z) - r),
                Vec3(std::max(from.x, to.x) + r, topY + s.agentHeight, std::max(from.z, to.z) + r)};
            bool blocked = false;
            for (size_t j = 0; j < boxes.size() && !blocked; ++j) {
                blocked = j != i && Overlaps(corridor, boxes[j].bounds);
            }
            // Both ends must stand on something level with the cover's foot (not a lintel, not a drop).
            auto standsOn = [&](const Vec3& p) {
                for (size_t j = 0; j < boxes.size(); ++j) {
                    const BoxBounds& b = boxes[j].bounds;
                    if (j != i && p.x >= b.min.x && p.x <= b.max.x && p.z >= b.min.z && p.z <= b.max.z &&
                        std::fabs(b.max.y - floorY) <= s.agentMaxClimb) {
                        return true;
                    }
                }
                return false;
            };
            if (!blocked && standsOn(from) && standsOn(to)) {
                geometry.links.push_back({from, to});
                ++added;
            }
        }
    }
    return added;
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
        error = "no walkable geometry (static colliders) in the scene";
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
    // Recast rasterizes surfaces only, so a block taller than an agent would have walkable floor inside it. Each
    // solid is rasterized on its own and its columns are filled from its lowest to its highest point instead.
    std::vector<NavGeometry::Piece> pieces = geometry.pieces;
    if (pieces.empty() || pieces.front().firstTriangle > 0) {
        pieces.insert(pieces.begin(), NavGeometry::Piece{0, false});
    }
    for (size_t n = 0; n < pieces.size(); ++n) {
        const int t0 = pieces[n].firstTriangle;
        const int t1 = n + 1 < pieces.size() ? pieces[n + 1].firstTriangle : triCount;
        if (t1 <= t0) {
            continue;
        }
        if (!pieces[n].solid) {
            if (!rcRasterizeTriangles(&ctx, verts, vertCount, tris + t0 * 3, areas.data() + t0, t1 - t0, *solid,
                                      cfg.walkableClimb)) {
                error = "could not rasterize the geometry";
                return false;
            }
            continue;
        }
        float mn[3] = {verts[tris[t0 * 3] * 3], 0.0f, verts[tris[t0 * 3] * 3 + 2]};
        float mx[3] = {mn[0], 0.0f, mn[2]};
        for (int i = t0 * 3; i < t1 * 3; ++i) {
            const float* v = &verts[tris[i] * 3];
            mn[0] = std::min(mn[0], v[0]);
            mn[2] = std::min(mn[2], v[2]);
            mx[0] = std::max(mx[0], v[0]);
            mx[2] = std::max(mx[2], v[2]);
        }
        const int ox = std::max(0, static_cast<int>(std::floor((mn[0] - cfg.bmin[0]) / cfg.cs)));
        const int oz = std::max(0, static_cast<int>(std::floor((mn[2] - cfg.bmin[2]) / cfg.cs)));
        const int w = std::min(cfg.width - ox, static_cast<int>(std::ceil((mx[0] - cfg.bmin[0]) / cfg.cs)) - ox + 1);
        const int h = std::min(cfg.height - oz, static_cast<int>(std::ceil((mx[2] - cfg.bmin[2]) / cfg.cs)) - oz + 1);
        if (w <= 0 || h <= 0) {
            continue;
        }
        const float tbmin[3] = {cfg.bmin[0] + ox * cfg.cs, cfg.bmin[1], cfg.bmin[2] + oz * cfg.cs};
        const float tbmax[3] = {tbmin[0] + w * cfg.cs, cfg.bmax[1], tbmin[2] + h * cfg.cs};
        std::unique_ptr<rcHeightfield, HeightfieldDeleter> piece(rcAllocHeightfield());
        if (!piece || !rcCreateHeightfield(&ctx, *piece, w, h, tbmin, tbmax, cfg.cs, cfg.ch) ||
            !rcRasterizeTriangles(&ctx, verts, vertCount, tris + t0 * 3, areas.data() + t0, t1 - t0, *piece,
                                  cfg.walkableClimb)) {
            error = "could not rasterize the geometry";
            return false;
        }
        for (int z = 0; z < h; ++z) {
            for (int x = 0; x < w; ++x) {
                const rcSpan* span = piece->spans[x + z * w];
                if (!span) {
                    continue;
                }
                unsigned int lo = span->smin;
                unsigned int hi = span->smax;
                unsigned char area = static_cast<unsigned char>(span->area);
                for (; span; span = span->next) {
                    lo = std::min(lo, static_cast<unsigned int>(span->smin));
                    if (span->smax >= hi) {
                        hi = span->smax;
                        area = static_cast<unsigned char>(span->area);
                    }
                }
                if (!rcAddSpan(&ctx, *solid, x + ox, z + oz, static_cast<unsigned short>(lo),
                               static_cast<unsigned short>(hi), area, cfg.walkableClimb)) {
                    error = "could not rasterize the geometry";
                    return false;
                }
            }
        }
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
    std::vector<float> linkVerts;
    std::vector<float> linkRadii;
    std::vector<unsigned char> linkDirs;
    std::vector<unsigned char> linkAreas;
    std::vector<unsigned short> linkFlags;
    std::vector<unsigned int> linkIds;
    for (const NavLink& link : geometry.links) {
        linkVerts.insert(linkVerts.end(), {link.from.x, link.from.y, link.from.z, link.to.x, link.to.y, link.to.z});
        linkRadii.push_back(s.agentRadius);
        linkDirs.push_back(DT_OFFMESH_CON_BIDIR);
        linkAreas.push_back(kVaultArea);
        linkFlags.push_back(kWalkableFlag);
        linkIds.push_back(static_cast<unsigned int>(linkIds.size()));
    }
    if (!geometry.links.empty()) {
        params.offMeshConVerts = linkVerts.data();
        params.offMeshConRad = linkRadii.data();
        params.offMeshConDir = linkDirs.data();
        params.offMeshConAreas = linkAreas.data();
        params.offMeshConFlags = linkFlags.data();
        params.offMeshConUserID = linkIds.data();
        params.offMeshConCount = static_cast<int>(geometry.links.size());
    }

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
    m_vaultLinkCost = s.vaultLinkCost;
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

std::vector<NavIsland> NavMesh::Islands(const std::vector<Vec3>& starts, const Vec3& searchExtents) const
{
    std::vector<NavIsland> islands;
    const dtNavMesh* mesh = m_navMesh;
    const dtMeshTile* tile = mesh ? mesh->getTile(0) : nullptr;
    if (!m_query || !tile || !tile->header) {
        return islands;
    }
    const int polyCount = tile->header->polyCount;
    std::vector<int> islandOf(static_cast<size_t>(polyCount), -1);
    for (int seed = 0; seed < polyCount; ++seed) {
        if (islandOf[static_cast<size_t>(seed)] >= 0) continue;
        const int id = static_cast<int>(islands.size());
        NavIsland island;
        bool first = true;
        float weightedX = 0.0f, weightedY = 0.0f, weightedZ = 0.0f;
        std::vector<int> open{seed};
        islandOf[static_cast<size_t>(seed)] = id;
        while (!open.empty()) {
            const int index = open.back();
            open.pop_back();
            const dtPoly& poly = tile->polys[index];
            for (unsigned int k = poly.firstLink; k != DT_NULL_LINK; k = tile->links[k].next) {
                const unsigned int next = mesh->decodePolyIdPoly(tile->links[k].ref);
                if (next < static_cast<unsigned int>(polyCount) && islandOf[next] < 0) {
                    islandOf[next] = id;
                    open.push_back(static_cast<int>(next));
                }
            }
            if (poly.getType() != DT_POLYTYPE_GROUND) continue;
            float area = 0.0f;
            Vec3 sum(0.0f, 0.0f, 0.0f);
            for (int v = 0; v < poly.vertCount; ++v) {
                const float* a = &tile->verts[poly.verts[v] * 3];
                const float* b = &tile->verts[poly.verts[(v + 1) % poly.vertCount] * 3];
                if (poly.neis[v] == 0) {
                    island.edges.push_back({Vec3(a[0], a[1], a[2]), Vec3(b[0], b[1], b[2])});
                }
                area += a[0] * b[2] - b[0] * a[2];
                sum = vadd(sum, Vec3(a[0], a[1], a[2]));
                const Vec3 point(a[0], a[1], a[2]);
                island.min = first ? point : Vec3(std::min(island.min.x, point.x), std::min(island.min.y, point.y),
                                                  std::min(island.min.z, point.z));
                island.max = first ? point : Vec3(std::max(island.max.x, point.x), std::max(island.max.y, point.y),
                                                  std::max(island.max.z, point.z));
                first = false;
            }
            area = std::fabs(area) * 0.5f;
            const Vec3 centre = vscale(sum, 1.0f / static_cast<float>(std::max<int>(1, poly.vertCount)));
            island.area += area;
            weightedX += centre.x * area;
            weightedY += centre.y * area;
            weightedZ += centre.z * area;
        }
        if (island.area > 0.0f) {
            island.centre = Vec3(weightedX / island.area, weightedY / island.area, weightedZ / island.area);
        }
        islands.push_back(island);
    }
    dtQueryFilter filter;
    const float extents[3] = {searchExtents.x, searchExtents.y, searchExtents.z};
    for (const Vec3& start : starts) {
        const float p[3] = {start.x, start.y, start.z};
        dtPolyRef ref = 0;
        float nearest[3];
        if (dtStatusSucceed(m_query->findNearestPoly(p, extents, &filter, &ref, nearest)) && ref != 0) {
            const unsigned int index = mesh->decodePolyIdPoly(ref);
            if (index < static_cast<unsigned int>(polyCount)) {
                islands[static_cast<size_t>(islandOf[index])].reached = true;
            }
        }
    }
    return islands;
}

bool NavMesh::FindPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& corners,
                       std::vector<unsigned char>* linkStarts) const
{
    corners.clear();
    if (linkStarts) {
        linkStarts->clear();
    }
    if (!m_query) {
        return false;
    }
    dtQueryFilter filter;
    filter.setAreaCost(kVaultArea, m_vaultLinkCost);
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
    unsigned char straightFlags[kMaxPathPolys];
    int straightCount = 0;
    if (dtStatusFailed(m_query->findStraightPath(startPoint, target, polys, polyCount, straight, straightFlags,
                                                 nullptr, &straightCount, kMaxPathPolys)) ||
        straightCount == 0) {
        return false;
    }
    corners.reserve(static_cast<size_t>(straightCount));
    for (int i = 0; i < straightCount; ++i) {
        corners.emplace_back(straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]);
        if (linkStarts) {
            linkStarts->push_back((straightFlags[i] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) ? 1 : 0);
        }
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
