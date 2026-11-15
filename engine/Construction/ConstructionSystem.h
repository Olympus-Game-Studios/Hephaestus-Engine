#pragma once

#include "ConstructionShape.h"

#include "../Core/Math.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Scene;
struct MeshData;

namespace Hephaestus::Construction {

struct EvaluatedGeometry;
struct LocalBox;

// Evaluated output of one shape, without the mesh itself.
struct GeometrySummary {
    size_t triangles = 0;
    size_t collisionBoxes = 0;
    std::uint64_t inputHash = 0;
    std::uint64_t geometryHash = 0;
};

// Where evaluated meshes go. upload receives the slot the shape already owns (or InvalidMesh) and returns the slot
// that now holds the mesh; release frees a slot the system no longer needs.
struct MeshSink {
    std::function<size_t(size_t existingHandle, const std::string& name, const MeshData& mesh)> upload;
    std::function<void(size_t handle)> release;
};

struct ConstructionStats {
    std::uint64_t updates = 0;
    std::uint64_t evaluations = 0;       // shapes re-evaluated, all time
    std::uint64_t meshUploads = 0;
    std::uint64_t collidersBuilt = 0;    // collision boxes created, all time
    std::uint64_t adopted = 0;           // rebuilt entities that re-used an earlier evaluation
    int lastEvaluations = 0;             // in the most recent Update
    double lastUpdateMs = 0.0;
    double lastEvaluateMs = 0.0;
};

struct ConstructionHit {
    entt::entity entity = entt::null;
    Vec3 worldPoint;
    Vec3 worldNormal;
    Vec3 localPoint;
    Vec3 localNormal;
    FaceTag tag;
    float distance = 0.0f;
};

class ConstructionSystem {
public:
    ConstructionSystem();
    ~ConstructionSystem();
    ConstructionSystem(const ConstructionSystem&) = delete;
    ConstructionSystem& operator=(const ConstructionSystem&) = delete;

    void SetMeshSink(MeshSink sink) { m_sink = std::move(sink); }

    // Brings meshes and collision of every construction shape in the scene up to date. Only shapes whose inputs
    // changed are evaluated again. Returns how many were evaluated.
    int Update(Scene& scene);

    // Forgets every shape and releases their mesh slots, for example before another scene is loaded.
    void Reset();

    const EvaluatedGeometry* Geometry(entt::entity entity) const;
    std::uint64_t InputHash(entt::entity entity) const;
    bool Summary(entt::entity entity, GeometrySummary& out) const;

    // Nearest construction surface hit by a world-space ray, ignoring `ignore`.
    bool Raycast(Scene& scene, const Ray& ray, ConstructionHit& out, entt::entity ignore = entt::null) const;

    const ConstructionStats& Stats() const { return m_stats; }

    // Gives a new subtract box its editor look (translucent red, hidden in the game).
    static void AddCutterMaterial(Scene& scene, entt::entity entity);

    // Gives a new shape the blockout look (a 1 m world-aligned grid) unless it already has a material.
    static void AddBlockoutMaterial(Scene& scene, entt::entity entity);

    // Openings parented to a box host, in canonical order.
    static std::vector<OpeningParams> CollectOpenings(Scene& scene, entt::entity host);
    static bool IsBox(Scene& scene, entt::entity entity);
    // Switches a box between adding and cutting, swapping its material and in-game visibility to match.
    static void SetOperation(Scene& scene, entt::entity entity, Operation operation);

private:
    struct ShapeState;

    void Evaluate(Scene& scene, entt::entity entity, ShapeState& state, std::uint64_t hash,
                  const std::vector<LocalBox>& cuts);
    bool CollidersIntact(Scene& scene, entt::entity owner, const ShapeState& state) const;
    void RebuildColliders(Scene& scene, entt::entity owner, ShapeState& state);
    void DestroyColliders(Scene& scene, ShapeState& state);
    void Release(ShapeState& state);

    MeshSink m_sink;
    std::unordered_map<entt::entity, std::unique_ptr<ShapeState>> m_shapes;
    ConstructionStats m_stats;
};

} // namespace Hephaestus::Construction
