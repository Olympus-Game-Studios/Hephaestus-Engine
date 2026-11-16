#include "ConstructionSystem.h"
#include "ConstructionGeometry.h"
#include "ConstructionKit.h"

#include "../Scene/PrefabComponentIO.h"
#include "../Scene/PrefabInstantiator.h"
#include "../Scene/PrefabOverrideSystem.h"

#include "../Scene/Components.h"
#include "../Scene/Entity.h"
#include "../Scene/Scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <tuple>
#include <unordered_set>

namespace Hephaestus::Construction {

struct ConstructionSystem::ShapeState {
    std::uint64_t inputHash = 0;
    size_t meshHandle = MeshRendererComponent::InvalidMesh;
    EvaluatedGeometry geometry;
    std::vector<entt::entity> colliders;
    std::vector<std::string> slotMaterials{std::string()};
    // Kit dressing: every entity of every placed piece, children after their parents.
    std::vector<entt::entity> pieces;
    KitSummary kit;
};

ConstructionSystem::ConstructionSystem() = default;
ConstructionSystem::~ConstructionSystem() = default;

namespace {

constexpr size_t kNoMesh = MeshRendererComponent::InvalidMesh;

double MillisecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Moller-Trumbore; returns the ray parameter of the hit.
bool IntersectTriangle(const Vec3& origin, const Vec3& dir, const Vec3& a, const Vec3& b, const Vec3& c, float& t)
{
    const Vec3 e1 = vsub(b, a);
    const Vec3 e2 = vsub(c, a);
    const Vec3 p = vcross(dir, e2);
    const float det = vdot(e1, p);
    if (std::fabs(det) < 1e-9f) {
        return false;
    }
    const float inv = 1.0f / det;
    const Vec3 s = vsub(origin, a);
    const float u = vdot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const Vec3 q = vcross(s, e1);
    const float v = vdot(dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    t = vdot(e2, q) * inv;
    return t > 0.0f;
}

struct Cutter {
    Mat4 world;
    AABB worldBounds;
    BoxParams box;
};

AABB BoundsOf(const Mat4& matrix, const Vec3& mn, const Vec3& mx)
{
    AABB bounds(Mat4::TransformPoint(matrix, mn), Mat4::TransformPoint(matrix, mn));
    for (int n = 1; n < 8; ++n) {
        const Vec3 p = Mat4::TransformPoint(matrix, Vec3(n & 1 ? mx.x : mn.x, n & 2 ? mx.y : mn.y, n & 4 ? mx.z : mn.z));
        bounds.min = Vec3(std::min(bounds.min.x, p.x), std::min(bounds.min.y, p.y), std::min(bounds.min.z, p.z));
        bounds.max = Vec3(std::max(bounds.max.x, p.x), std::max(bounds.max.y, p.y), std::max(bounds.max.z, p.z));
    }
    return bounds;
}

Vec3 BoxMin(const BoxParams& box) { return Vec3(-box.width * 0.5f, 0.0f, -box.depth * 0.5f); }
Vec3 BoxMax(const BoxParams& box) { return Vec3(box.width * 0.5f, box.height, box.depth * 0.5f); }

bool Overlaps(const AABB& a, const AABB& b)
{
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z &&
           a.max.z > b.min.z;
}

// The space a box or wall host can occupy, in its own frame.
LocalBox HostLocalBounds(const ConstructionShapeComponent& shape)
{
    if (shape.recipe != Recipe::Wall || shape.wall.points.empty()) {
        return LocalBox{BoxMin(shape.box), BoxMax(shape.box)};
    }
    // Mitred corners reach at most two thicknesses past a path point.
    const float reach = shape.box.wallThickness * 2.0f;
    LocalBox bounds{Vec3(shape.wall.points[0].x, 0.0f, shape.wall.points[0].z), Vec3(shape.wall.points[0].x, 0.0f, shape.wall.points[0].z)};
    for (const WallPoint& point : shape.wall.points) {
        bounds.min = Vec3(std::min(bounds.min.x, point.x), 0.0f, std::min(bounds.min.z, point.z));
        bounds.max = Vec3(std::max(bounds.max.x, point.x), 0.0f, std::max(bounds.max.z, point.z));
    }
    bounds.min = Vec3(bounds.min.x - reach, 0.0f, bounds.min.z - reach);
    bounds.max = Vec3(bounds.max.x + reach, shape.box.height, bounds.max.z + reach);
    return bounds;
}

// Cutters overlapping a host, as boxes in the host's frame. A cutter turned relative to the host cuts its bounding
// box in that frame.
std::vector<LocalBox> CutsFor(Scene& scene, entt::entity host, const ConstructionShapeComponent& shape,
                              const std::vector<Cutter>& cutters)
{
    std::vector<LocalBox> cuts;
    if (cutters.empty()) {
        return cuts;
    }
    const Mat4 world = scene.GetWorldTransform(Entity(host, &scene));
    const LocalBox local = HostLocalBounds(shape);
    const AABB hostBounds = BoundsOf(world, local.min, local.max);
    const Mat4 inverse = Mat4::Invert(world);
    for (const Cutter& cutter : cutters) {
        if (!Overlaps(hostBounds, cutter.worldBounds)) {
            continue;
        }
        const AABB local = BoundsOf(Mat4::Multiply(inverse, cutter.world), BoxMin(cutter.box), BoxMax(cutter.box));
        cuts.push_back(LocalBox{local.min, local.max});
    }
    std::sort(cuts.begin(), cuts.end(), [](const LocalBox& a, const LocalBox& b) {
        return std::make_tuple(a.min.x, a.min.y, a.min.z, a.max.x, a.max.y, a.max.z) <
               std::make_tuple(b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
    });
    return cuts;
}

Vec3 TransformNormal(const Mat4& inverse, const Vec3& n)
{
    // Inverse transpose, so normals stay perpendicular under non-uniform scale.
    return vnorm(Vec3(inverse.m[0] * n.x + inverse.m[1] * n.y + inverse.m[2] * n.z,
                      inverse.m[4] * n.x + inverse.m[5] * n.y + inverse.m[6] * n.z,
                      inverse.m[8] * n.x + inverse.m[9] * n.y + inverse.m[10] * n.z));
}

} // namespace

void ConstructionSystem::AddBlockoutMaterial(Scene& scene, entt::entity entity)
{
    entt::registry& registry = scene.GetRegistry();
    if (!registry.valid(entity) || registry.all_of<MaterialComponent>(entity)) {
        return;
    }
    MaterialComponent grid(Vec3(0.82f, 0.82f, 0.8f));
    grid.roughness = 0.9f;
    // Copied into every project by ProjectContext::SyncEngineAssets.
    grid.albedoTexturePath = ".engine/textures/construction_grid.png";
    grid.useAlbedoTexture = true;
    grid.useWorldAlignedTexture = true;
    grid.worldTextureScale = 1.0f;
    registry.emplace<MaterialComponent>(entity, grid);
}

void ConstructionSystem::AddCutterMaterial(Scene& scene, entt::entity entity)
{
    entt::registry& registry = scene.GetRegistry();
    if (!registry.valid(entity) || registry.all_of<MaterialComponent>(entity)) {
        return;
    }
    MaterialComponent cutter(Vec3(0.95f, 0.25f, 0.2f));
    cutter.blendMode = MaterialBlendMode::AlphaBlend;
    cutter.opacity = 0.3f;
    cutter.roughness = 1.0f;
    registry.emplace<MaterialComponent>(entity, cutter);
}

void ConstructionSystem::SetOperation(Scene& scene, entt::entity entity, Operation operation)
{
    entt::registry& registry = scene.GetRegistry();
    auto* shape = registry.try_get<ConstructionShapeComponent>(entity);
    if (!shape) {
        return;
    }
    const bool wasCutter = IsCutter(*shape);
    // Only blocks can subtract; other shapes stay additive and still get the blockout look.
    shape->operation = shape->recipe == Recipe::Box ? operation : Operation::Add;
    if (wasCutter == IsCutter(*shape) && registry.all_of<MaterialComponent>(entity)) {
        return;
    }
    registry.remove<MaterialComponent>(entity);
    if (IsCutter(*shape)) {
        AddCutterMaterial(scene, entity);
    } else {
        AddBlockoutMaterial(scene, entity);
        if (auto* renderer = registry.try_get<MeshRendererComponent>(entity)) {
            renderer->hideInGame = false;
            renderer->castShadows = true;
        }
    }
}

bool ConstructionSystem::IsBox(Scene& scene, entt::entity entity)
{
    const auto* shape = scene.GetRegistry().try_get<ConstructionShapeComponent>(entity);
    return shape && shape->source == SourceKind::Recipe && shape->recipe == Recipe::Box;
}

std::vector<OpeningParams> ConstructionSystem::CollectOpenings(Scene& scene, entt::entity host)
{
    std::vector<OpeningParams> openings;
    const auto* transform = scene.GetRegistry().try_get<TransformComponent>(host);
    if (!transform) {
        return openings;
    }
    for (entt::entity child : transform->children) {
        const auto* shape = scene.GetRegistry().try_get<ConstructionShapeComponent>(child);
        if (shape && shape->recipe == Recipe::Opening) {
            openings.push_back(shape->opening);
        }
    }
    SortOpenings(openings);
    return openings;
}

int ConstructionSystem::Update(Scene& scene)
{
    const auto start = std::chrono::steady_clock::now();
    entt::registry& registry = scene.GetRegistry();
    int evaluated = 0;
    double evaluateMs = 0.0;
    std::unordered_set<entt::entity> live;

    auto view = registry.view<ConstructionShapeComponent>();
    std::vector<entt::entity> boxes;
    for (entt::entity entity : view) {
        if (HasOwnGeometry(registry.get<ConstructionShapeComponent>(entity))) {
            boxes.push_back(entity);
        }
    }
    for (entt::entity entity : boxes) {
        live.insert(entity);
    }
    // States of shapes that are gone. A shape rebuilt from a file or an undo snapshot is a new entity with the same
    // inputs, so it adopts the old evaluation and mesh slot instead of evaluating again.
    std::unordered_multimap<std::uint64_t, std::unique_ptr<ShapeState>> spares;
    for (auto it = m_shapes.begin(); it != m_shapes.end();) {
        if (live.count(it->first)) {
            ++it;
            continue;
        }
        DestroyColliders(scene, *it->second);
        DestroyPieces(scene, *it->second);
        const std::uint64_t key = it->second->inputHash;
        spares.emplace(key, std::move(it->second));
        it = m_shapes.erase(it);
    }

    std::vector<Cutter> cutters;
    for (entt::entity entity : boxes) {
        const auto& shape = registry.get<ConstructionShapeComponent>(entity);
        if (IsCutter(shape)) {
            const Mat4 world = scene.GetWorldTransform(Entity(entity, &scene));
            cutters.push_back(Cutter{world, BoundsOf(world, BoxMin(shape.box), BoxMax(shape.box)), shape.box});
        }
    }

    for (entt::entity entity : boxes) {
        auto& shape = registry.get<ConstructionShapeComponent>(entity);
        Sanitize(shape);
        if (!registry.all_of<MeshRendererComponent>(entity)) {
            registry.emplace<MeshRendererComponent>(entity);
        }
        if (IsCutter(shape)) {
            auto& renderer = registry.get<MeshRendererComponent>(entity);
            renderer.hideInGame = true;
            renderer.castShadows = false;
        }
        const std::vector<LocalBox> cuts = CanHostOpenings(shape)
            ? CutsFor(scene, entity, shape, cutters) : std::vector<LocalBox>{};
        std::uint64_t hash = HashInputs(shape, CollectOpenings(scene, entity), cuts);
        if (!shape.kit.empty()) {
            // A kit file saved since the last fill dresses the shape again.
            std::error_code ec;
            const auto file = PrefabOverrideSystem::ResolveAssetFilePath(shape.kit, false);
            const auto stamp = std::filesystem::last_write_time(file, ec).time_since_epoch().count();
            hash ^= static_cast<std::uint64_t>(ec ? 0 : stamp) * 0x9E3779B97F4A7C15ull;
        }
        auto found = m_shapes.find(entity);
        if (found == m_shapes.end()) {
            auto spare = spares.find(hash);
            if (spare != spares.end()) {
                found = m_shapes.emplace(entity, std::move(spare->second)).first;
                spares.erase(spare);
                ++m_stats.adopted;
            }
        }
        if (found == m_shapes.end() || found->second->inputHash != hash) {
            std::unique_ptr<ShapeState>& slot = m_shapes[entity];
            if (!slot) slot = std::make_unique<ShapeState>();
            ShapeState& state = *slot;
            const auto evalStart = std::chrono::steady_clock::now();
            Evaluate(scene, entity, state, hash, cuts);
            evaluateMs += MillisecondsSince(evalStart);
            ++evaluated;
            continue;
        }
        ShapeState& state = *found->second;
        auto& renderer = registry.get<MeshRendererComponent>(entity);
        if (renderer.meshHandle != state.meshHandle) {
            ApplySlots(renderer, state);
        }
        if (!CollidersIntact(scene, entity, state)) {
            RebuildColliders(scene, entity, state);
        }
        if (!PiecesIntact(scene, state)) {
            RebuildPieces(scene, entity, state);
        }
    }

    for (auto& spare : spares) {
        Release(*spare.second);
    }

    // Generated entities copied, pasted or left behind by an owner that is gone.
    std::vector<entt::entity> strays;
    for (entt::entity entity : registry.view<ConstructionGeneratedComponent>()) {
        const auto owner = static_cast<entt::entity>(registry.get<ConstructionGeneratedComponent>(entity).ownerEntity);
        auto state = m_shapes.find(owner);
        const bool owned = state != m_shapes.end() &&
            (std::find(state->second->colliders.begin(), state->second->colliders.end(), entity) !=
                 state->second->colliders.end() ||
             std::find(state->second->pieces.begin(), state->second->pieces.end(), entity) !=
                 state->second->pieces.end());
        if (!owned) {
            strays.push_back(entity);
        }
    }
    for (entt::entity stray : strays) {
        scene.DestroyEntity(Entity(stray, &scene));
    }

    ++m_stats.updates;
    m_stats.lastEvaluations = evaluated;
    m_stats.lastEvaluateMs = evaluateMs;
    m_stats.lastUpdateMs = MillisecondsSince(start);
    return evaluated;
}

namespace {

// What stays drawn of a shape a kit dresses: its floor, ceiling and underside. The kit's pieces replace the walls.
MeshData UndressedFaces(const EvaluatedGeometry& geometry)
{
    MeshData out;
    out.vertices = geometry.mesh.vertices;
    out.bounds = geometry.mesh.bounds;
    std::vector<MeshData::Submesh> ranges = geometry.mesh.submeshes;
    if (ranges.empty()) {
        ranges.push_back({0, static_cast<uint32_t>(geometry.mesh.indices.size()), 0, std::string(), geometry.mesh.bounds});
    }
    for (MeshData::Submesh range : ranges) {
        const uint32_t start = static_cast<uint32_t>(out.indices.size());
        for (uint32_t i = range.indexOffset; i + 2 < range.indexOffset + range.indexCount; i += 3) {
            const FaceGroup group = geometry.triangleTags[i / 3].group;
            if (group == FaceGroup::Floor || group == FaceGroup::Ceiling || group == FaceGroup::Bottom) {
                out.indices.insert(out.indices.end(), geometry.mesh.indices.begin() + i,
                                   geometry.mesh.indices.begin() + i + 3);
            }
        }
        range.indexOffset = start;
        range.indexCount = static_cast<uint32_t>(out.indices.size()) - start;
        if (range.indexCount > 0) {
            out.submeshes.push_back(std::move(range));
        }
    }
    return out;
}

} // namespace

void ConstructionSystem::Evaluate(Scene& scene, entt::entity entity, ShapeState& state, std::uint64_t hash,
                                  const std::vector<LocalBox>& cuts)
{
    entt::registry& registry = scene.GetRegistry();
    const auto& shape = registry.get<ConstructionShapeComponent>(entity);
    state.inputHash = hash;
    ++m_stats.evaluations;
    const bool hasGeometry = EvaluateShape(shape, CollectOpenings(scene, entity), state.geometry, cuts);
    state.slotMaterials = AssignFaceMaterials(shape.faceMaterials, state.geometry);
    RebuildPieces(scene, entity, state);

    auto& renderer = registry.get<MeshRendererComponent>(entity);
    const bool dressed = !state.pieces.empty() && state.kit.error.empty() &&
        state.geometry.triangleTags.size() * 3 == state.geometry.mesh.indices.size();
    const MeshData shown = dressed ? UndressedFaces(state.geometry) : MeshData();
    // A dressed open wall has nothing left to draw: hide it but keep its outline selectable.
    if (dressed && shown.indices.empty()) {
        renderer.visible = false;
        renderer.showHiddenOutline = true;
    } else if (!renderer.visible && renderer.showHiddenOutline) {
        renderer.visible = true;
        renderer.showHiddenOutline = false;
    }
    if (hasGeometry && m_sink.upload) {
        const std::string name = registry.all_of<TagComponent>(entity)
            ? registry.get<TagComponent>(entity).tag : std::string("Construction");
        const size_t handle = m_sink.upload(state.meshHandle, name, dressed && !shown.indices.empty() ? shown
                                                                                                 : state.geometry.mesh);
        if (handle != state.meshHandle && state.meshHandle != kNoMesh && m_sink.release) {
            m_sink.release(state.meshHandle);
        }
        state.meshHandle = handle;
        ++m_stats.meshUploads;
    } else if (!hasGeometry) {
        Release(state);
    }
    ApplySlots(renderer, state);
    RebuildColliders(scene, entity, state);
}

bool ConstructionSystem::PiecesIntact(Scene& scene, const ShapeState& state) const
{
    // Adopted after a load or undo: the summary says pieces belong here but they were removed with the old entity.
    if (state.pieces.empty() && state.kit.pieces + state.kit.gaps > 0) {
        return false;
    }
    for (entt::entity piece : state.pieces) {
        if (!scene.GetRegistry().valid(piece) || !scene.GetRegistry().all_of<ConstructionGeneratedComponent>(piece)) {
            return false;
        }
    }
    return true;
}

void ConstructionSystem::DestroyPieces(Scene& scene, ShapeState& state)
{
    entt::registry& registry = scene.GetRegistry();
    for (auto it = state.pieces.rbegin(); it != state.pieces.rend(); ++it) {
        if (registry.valid(*it) && registry.all_of<ConstructionGeneratedComponent>(*it)) {
            scene.DestroyEntity(Entity(*it, &scene));
        }
    }
    state.pieces.clear();
}

void ConstructionSystem::RebuildPieces(Scene& scene, entt::entity owner, ShapeState& state)
{
    DestroyPieces(scene, state);
    state.kit = KitSummary{};
    entt::registry& registry = scene.GetRegistry();
    const auto& shape = registry.get<ConstructionShapeComponent>(owner);
    if (shape.kit.empty()) {
        return;
    }
    KitDefinition kit;
    std::string error;
    if (!LoadKit(PrefabOverrideSystem::ResolveAssetFilePath(shape.kit, false), kit, error)) {
        state.kit.error = error;
        return;
    }
    const KitFillResult fill = FillShape(shape, CollectOpenings(scene, owner), kit);
    state.kit.pieces = fill.pieces;
    state.kit.fillers = fill.fillers;
    state.kit.gaps = fill.gaps;
    state.kit.maxStretch = fill.maxStretch;
    Entity ownerEntity(owner, &scene);
    auto adopt = [&](Entity entity) {
        registry.emplace_or_replace<EditorOnlyComponent>(static_cast<entt::entity>(entity));
        registry.emplace_or_replace<ConstructionGeneratedComponent>(static_cast<entt::entity>(entity)).ownerEntity =
            static_cast<std::uint32_t>(owner);
        state.pieces.push_back(static_cast<entt::entity>(entity));
    };
    std::unordered_map<std::string, PrefabAsset> prefabs;
    for (const KitPlacement& placement : fill.placements) {
        Entity root;
        if (placement.piece) {
            auto found = prefabs.find(placement.piece->prefab);
            if (found == prefabs.end()) {
                PrefabAsset asset;
                if (!PrefabOverrideSystem::LoadAsset(placement.piece->prefab, asset, error)) {
                    state.kit.error = placement.piece->prefab + ": " + error;
                    continue;
                }
                found = prefabs.emplace(placement.piece->prefab, std::move(asset)).first;
            }
            PrefabInstantiateOptions options;
            options.prefabPath = placement.piece->prefab;
            options.attachInstanceComponent = false;
            root = PrefabInstantiator::Instantiate(scene, found->second, options);
        } else {
            // What the kit could not fill shows as a red box, in the editor only.
            root = scene.CreateEntity("Kit Gap");
            auto& marker = root.AddComponent<MeshRendererComponent>();
            marker.meshAssetPath = "builtin://cube";
            marker.hideInGame = true;
            marker.castShadows = false;
            MaterialComponent red(Vec3(0.95f, 0.15f, 0.1f));
            red.blendMode = MaterialBlendMode::AlphaBlend;
            red.opacity = 0.45f;
            root.AddComponent<MaterialComponent>(red);
        }
        if (!root) {
            continue;
        }
        auto& transform = root.GetComponent<TransformComponent>();
        transform.position = placement.position;
        transform.rotation = Vec3(0.0f, placement.yaw, 0.0f);
        transform.scale = placement.piece ? Vec3(placement.scaleX, 1.0f, 1.0f)
                                          : Vec3(placement.width, shape.box.height, shape.box.wallThickness * 1.05f);
        if (!placement.piece) {
            transform.position.y += shape.box.height * 0.5f;
        }
        scene.SetParent(root, ownerEntity, false);
        std::vector<entt::entity> members{static_cast<entt::entity>(root)};
        for (size_t n = 0; n < members.size(); ++n) {
            for (entt::entity child : registry.get<TransformComponent>(members[n]).children) {
                members.push_back(child);
            }
        }
        for (entt::entity member : members) {
            Entity entity(member, &scene);
            // The construction shell keeps the collision, so kit art never changes how the level plays.
            registry.remove<RigidBodyComponent, PhysicsShapeComponent, MeshColliderComponent>(member);
            adopt(entity);
            if (auto* renderer = registry.try_get<MeshRendererComponent>(member); renderer && m_sink.loadMesh) {
                m_sink.loadMesh(*renderer);
            }
        }
    }
    if (!state.kit.error.empty()) {
        std::cerr << "[Construction] Kit '" << shape.kit << "': " << state.kit.error << "\n";
    }
}

bool ConstructionSystem::Kit(entt::entity entity, KitSummary& out) const
{
    auto found = m_shapes.find(entity);
    if (found == m_shapes.end()) {
        return false;
    }
    out = found->second->kit;
    return true;
}

void ConstructionSystem::ApplySlots(MeshRendererComponent& renderer, const ShapeState& state)
{
    const int slots = static_cast<int>(state.slotMaterials.size());
    renderer.SetMesh(state.meshHandle, state.geometry.mesh.bounds, {}, false, slots);
    // Slot 0 is the shape's own material, which the Mesh Renderer can still override; the rest are painted faces.
    const std::string own = renderer.appliedMaterialPaths.empty() ? std::string() : renderer.appliedMaterialPaths[0];
    renderer.appliedMaterialPaths = state.slotMaterials;
    renderer.appliedMaterialPaths[0] = own;
    renderer.materialIndices.assign(static_cast<size_t>(slots), -1);
}

bool ConstructionSystem::CollidersIntact(Scene& scene, entt::entity owner, const ShapeState& state) const
{
    const entt::registry& registry = scene.GetRegistry();
    const auto& shape = registry.get<ConstructionShapeComponent>(owner);
    const size_t wanted = shape.collision == CollisionMode::Solid && !IsCutter(shape)
        ? state.geometry.solids.size() + state.geometry.rounds.size() + state.geometry.hulls.size() : 0;
    if (state.colliders.size() != wanted) {
        return false;
    }
    for (entt::entity collider : state.colliders) {
        if (!registry.valid(collider)) {
            return false;
        }
        const auto* generated = registry.try_get<ConstructionGeneratedComponent>(collider);
        const auto* transform = registry.try_get<TransformComponent>(collider);
        if (!generated || generated->ownerEntity != static_cast<std::uint32_t>(owner) || !transform ||
            transform->parent != owner) {
            return false;
        }
    }
    return true;
}

void ConstructionSystem::RebuildColliders(Scene& scene, entt::entity owner, ShapeState& state)
{
    DestroyColliders(scene, state);
    const auto& shape = scene.GetRegistry().get<ConstructionShapeComponent>(owner);
    if (shape.collision != CollisionMode::Solid || IsCutter(shape)) {
        return;
    }
    Entity ownerEntity(owner, &scene);
    for (const LocalBox& box : state.geometry.solids) {
        Entity collider = scene.CreateEntity("Construction Collision");
        auto& transform = collider.GetComponent<TransformComponent>();
        transform.position = vscale(vadd(box.min, box.max), 0.5f);
        collider.AddComponent<PhysicsShapeComponent>(
            PhysicsShapeComponent::CreateBox(vscale(vsub(box.max, box.min), 0.5f)));
        auto& body = collider.AddComponent<RigidBodyComponent>();
        body.isStatic = true;
        body.motion = RigidBodyMotion::Static;
        body.mass = 0.0f;
        body.useGravity = false;
        body.layer = 1;  // World, the built-in layer for static level geometry
        collider.AddComponent<EditorOnlyComponent>();
        collider.AddComponent<ConstructionGeneratedComponent>().ownerEntity = static_cast<std::uint32_t>(owner);
        scene.SetParent(collider, ownerEntity, false);
        state.colliders.push_back(static_cast<entt::entity>(collider));
        ++m_stats.collidersBuilt;
    }
    for (const std::vector<Vec3>& hull : state.geometry.hulls) {
        Vec3 center(0.0f, 0.0f, 0.0f);
        for (const Vec3& p : hull) {
            center = vadd(center, p);
        }
        center = vscale(center, 1.0f / static_cast<float>(hull.size()));
        Entity collider = scene.CreateEntity("Construction Collision");
        collider.GetComponent<TransformComponent>().position = center;
        PhysicsShapeComponent physicsShape;
        physicsShape.type = PhysicsShapeType::ConvexHull;
        for (const Vec3& p : hull) {
            physicsShape.points.push_back(vsub(p, center));
        }
        collider.AddComponent<PhysicsShapeComponent>(physicsShape);
        auto& body = collider.AddComponent<RigidBodyComponent>();
        body.isStatic = true;
        body.motion = RigidBodyMotion::Static;
        body.mass = 0.0f;
        body.useGravity = false;
        body.layer = 1;
        collider.AddComponent<EditorOnlyComponent>();
        collider.AddComponent<ConstructionGeneratedComponent>().ownerEntity = static_cast<std::uint32_t>(owner);
        scene.SetParent(collider, ownerEntity, false);
        state.colliders.push_back(static_cast<entt::entity>(collider));
        ++m_stats.collidersBuilt;
    }
    for (const RoundCollider& round : state.geometry.rounds) {
        Entity collider = scene.CreateEntity("Construction Collision");
        collider.GetComponent<TransformComponent>().position = round.center;
        PhysicsShapeComponent physicsShape = PhysicsShapeComponent::CreateSphere(round.radius);
        if (round.kind == RoundCollider::Kind::Cylinder) {
            physicsShape.type = PhysicsShapeType::Cylinder;
            physicsShape.height = round.height;
        }
        collider.AddComponent<PhysicsShapeComponent>(physicsShape);
        auto& body = collider.AddComponent<RigidBodyComponent>();
        body.isStatic = true;
        body.motion = RigidBodyMotion::Static;
        body.mass = 0.0f;
        body.useGravity = false;
        body.layer = 1;
        collider.AddComponent<EditorOnlyComponent>();
        collider.AddComponent<ConstructionGeneratedComponent>().ownerEntity = static_cast<std::uint32_t>(owner);
        scene.SetParent(collider, ownerEntity, false);
        state.colliders.push_back(static_cast<entt::entity>(collider));
        ++m_stats.collidersBuilt;
    }
}

void ConstructionSystem::DestroyColliders(Scene& scene, ShapeState& state)
{
    for (entt::entity collider : state.colliders) {
        if (scene.GetRegistry().valid(collider) &&
            scene.GetRegistry().all_of<ConstructionGeneratedComponent>(collider)) {
            scene.DestroyEntity(Entity(collider, &scene));
        }
    }
    state.colliders.clear();
}

void ConstructionSystem::Release(ShapeState& state)
{
    if (state.meshHandle != kNoMesh && m_sink.release) {
        m_sink.release(state.meshHandle);
    }
    state.meshHandle = kNoMesh;
}

void ConstructionSystem::Reset()
{
    for (auto& entry : m_shapes) {
        Release(*entry.second);
    }
    m_shapes.clear();
}

const EvaluatedGeometry* ConstructionSystem::Geometry(entt::entity entity) const
{
    auto found = m_shapes.find(entity);
    return found != m_shapes.end() ? &found->second->geometry : nullptr;
}

std::uint64_t ConstructionSystem::InputHash(entt::entity entity) const
{
    auto found = m_shapes.find(entity);
    return found != m_shapes.end() ? found->second->inputHash : 0;
}

bool ConstructionSystem::Summary(entt::entity entity, GeometrySummary& out) const
{
    auto found = m_shapes.find(entity);
    if (found == m_shapes.end()) {
        return false;
    }
    const ShapeState& state = *found->second;
    out.triangles = state.geometry.mesh.indices.size() / 3;
    out.collisionBoxes = state.geometry.solids.size();
    out.inputHash = state.inputHash;
    out.geometryHash = HashGeometry(state.geometry);
    return true;
}

bool ConstructionSystem::Raycast(Scene& scene, const Ray& ray, ConstructionHit& out, entt::entity ignore) const
{
    bool hitAny = false;
    float best = 0.0f;
    for (const auto& [entity, statePtr] : m_shapes) {
        const ShapeState& state = *statePtr;
        if (entity == ignore || !scene.GetRegistry().valid(entity) ||
            IsCutter(scene.GetRegistry().get<ConstructionShapeComponent>(entity))) {
            continue;
        }
        const Mat4 world = scene.GetWorldTransform(Entity(entity, &scene));
        const Mat4 inverse = Mat4::Invert(world);
        const Vec3 localOrigin = Mat4::TransformPoint(inverse, ray.origin);
        const Vec3 localDir = vsub(Mat4::TransformPoint(inverse, vadd(ray.origin, ray.direction)), localOrigin);
        const MeshData& mesh = state.geometry.mesh;
        for (size_t tri = 0; tri * 3 + 2 < mesh.indices.size(); ++tri) {
            const Vertex& va = mesh.vertices[mesh.indices[tri * 3]];
            const Vertex& vb = mesh.vertices[mesh.indices[tri * 3 + 1]];
            const Vertex& vc = mesh.vertices[mesh.indices[tri * 3 + 2]];
            float t = 0.0f;
            if (!IntersectTriangle(localOrigin, localDir, Vec3(va.pos[0], va.pos[1], va.pos[2]),
                                   Vec3(vb.pos[0], vb.pos[1], vb.pos[2]), Vec3(vc.pos[0], vc.pos[1], vc.pos[2]), t)) {
                continue;
            }
            const Vec3 localPoint = vadd(localOrigin, vscale(localDir, t));
            const Vec3 worldPoint = Mat4::TransformPoint(world, localPoint);
            const float distance = vlen(vsub(worldPoint, ray.origin));
            if (hitAny && distance >= best) {
                continue;
            }
            const Vec3 localNormal(va.normal[0], va.normal[1], va.normal[2]);
            hitAny = true;
            best = distance;
            out.entity = entity;
            out.localPoint = localPoint;
            out.localNormal = localNormal;
            out.worldPoint = worldPoint;
            out.worldNormal = TransformNormal(inverse, localNormal);
            out.tag = tri < state.geometry.triangleTags.size() ? state.geometry.triangleTags[tri] : FaceTag{};
            out.distance = distance;
        }
    }
    return hitAny;
}

} // namespace Hephaestus::Construction
