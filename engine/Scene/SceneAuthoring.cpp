#include "SceneAuthoring.h"

#include "Components.h"
#include "../Construction/ConstructionShape.h"
#include "Entity.h"
#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Hephaestus::SceneAuthoring {

float SnapScalar(float value, float increment)
{
    if (increment <= 0.0f || !std::isfinite(value)) {
        return value;
    }
    return std::round(value / increment) * increment;
}

Vec3 SnapVector(const Vec3& value, float increment)
{
    return Vec3(SnapScalar(value.x, increment),
                SnapScalar(value.y, increment),
                SnapScalar(value.z, increment));
}

std::vector<float> EvenlySpacedValues(float first, float last,
                                      std::size_t count)
{
    std::vector<float> values(count, first);
    if (count < 2) {
        return values;
    }
    const float step = (last - first) / static_cast<float>(count - 1);
    for (std::size_t index = 0; index < count; ++index) {
        values[index] = first + step * static_cast<float>(index);
    }
    values.back() = last;
    return values;
}

AABB TransformBounds(const AABB& localBounds, const Mat4& worldTransform)
{
    const Vec3 corners[] = {
        {localBounds.min.x, localBounds.min.y, localBounds.min.z},
        {localBounds.max.x, localBounds.min.y, localBounds.min.z},
        {localBounds.min.x, localBounds.max.y, localBounds.min.z},
        {localBounds.max.x, localBounds.max.y, localBounds.min.z},
        {localBounds.min.x, localBounds.min.y, localBounds.max.z},
        {localBounds.max.x, localBounds.min.y, localBounds.max.z},
        {localBounds.min.x, localBounds.max.y, localBounds.max.z},
        {localBounds.max.x, localBounds.max.y, localBounds.max.z},
    };

    const float maxFloat = std::numeric_limits<float>::max();
    const float minFloat = std::numeric_limits<float>::lowest();
    Vec3 minimum(maxFloat, maxFloat, maxFloat);
    Vec3 maximum(minFloat, minFloat, minFloat);
    for (const Vec3& corner : corners) {
        const Vec3 world = Mat4::TransformPoint(worldTransform, corner);
        minimum.x = std::min(minimum.x, world.x);
        minimum.y = std::min(minimum.y, world.y);
        minimum.z = std::min(minimum.z, world.z);
        maximum.x = std::max(maximum.x, world.x);
        maximum.y = std::max(maximum.y, world.y);
        maximum.z = std::max(maximum.z, world.z);
    }
    return AABB(minimum, maximum);
}

bool RaycastBounds(Scene& scene,
                   const Ray& ray,
                   SceneRaycastHit& outHit,
                   const std::unordered_set<entt::entity>& ignored)
{
    bool found = false;
    float closest = std::numeric_limits<float>::max();
    auto view = scene.GetRegistry().view<TransformComponent, MeshRendererComponent>();
    for (entt::entity handle : view) {
        if (ignored.contains(handle)) {
            continue;
        }
        const auto& renderer = view.get<MeshRendererComponent>(handle);
        if (!renderer.visible || !renderer.HasValidMesh()) {
            continue;
        }

        Entity entity(handle, &scene);
        const AABB bounds = TransformBounds(renderer.localBounds,
                                             scene.GetWorldTransform(entity));
        float nearDistance = 0.0f;
        float farDistance = 0.0f;
        if (!bounds.IntersectRay(ray, nearDistance, farDistance) ||
            nearDistance < 0.0f || nearDistance >= closest) {
            continue;
        }

        const Vec3 point = ray.at(nearDistance);
        const float epsilon = 0.001f;
        Vec3 normal(0.0f, 1.0f, 0.0f);
        if (std::abs(point.x - bounds.min.x) <= epsilon) normal = Vec3(-1, 0, 0);
        else if (std::abs(point.x - bounds.max.x) <= epsilon) normal = Vec3(1, 0, 0);
        else if (std::abs(point.y - bounds.min.y) <= epsilon) normal = Vec3(0, -1, 0);
        else if (std::abs(point.y - bounds.max.y) <= epsilon) normal = Vec3(0, 1, 0);
        else if (std::abs(point.z - bounds.min.z) <= epsilon) normal = Vec3(0, 0, -1);
        else if (std::abs(point.z - bounds.max.z) <= epsilon) normal = Vec3(0, 0, 1);

        closest = nearDistance;
        outHit.entity = handle;
        outHit.point = point;
        outHit.normal = normal;
        outHit.distance = nearDistance;
        found = true;
    }
    return found;
}

std::vector<AABB> NeighbourBounds(Scene& scene, const AABB& around, float reach,
                                  const std::function<bool(entt::entity)>& skip)
{
    entt::registry& registry = scene.GetRegistry();
    auto skipped = [&](entt::entity handle) {
        for (Entity entity(handle, &scene); entity; entity = scene.GetParent(entity)) {
            if (skip(static_cast<entt::entity>(entity))) return true;
        }
        return false;
    };
    std::vector<AABB> neighbours;
    auto view = registry.view<TransformComponent, MeshRendererComponent>();
    for (entt::entity handle : view) {
        const auto& renderer = view.get<MeshRendererComponent>(handle);
        if (!renderer.visible || !renderer.HasValidMesh() || registry.all_of<ConstructionGeneratedComponent>(handle)) {
            continue;
        }
        const AABB bounds = TransformBounds(renderer.localBounds, scene.GetWorldTransform(Entity(handle, &scene)));
        if (bounds.min.x > around.max.x + reach || bounds.max.x < around.min.x - reach ||
            bounds.min.y > around.max.y + reach || bounds.max.y < around.min.y - reach ||
            bounds.min.z > around.max.z + reach || bounds.max.z < around.min.z - reach || skipped(handle)) {
            continue;
        }
        neighbours.push_back(bounds);
    }
    return neighbours;
}

Vec3 SnapBoundsToNeighbours(const AABB& moving, const std::vector<AABB>& neighbours, float reach)
{
    float offset[3] = {0.0f, 0.0f, 0.0f};
    bool found[3] = {false, false, false};
    const float lo[3] = {moving.min.x, moving.min.y, moving.min.z};
    const float hi[3] = {moving.max.x, moving.max.y, moving.max.z};
    for (const AABB& other : neighbours) {
        const float olo[3] = {other.min.x, other.min.y, other.min.z};
        const float ohi[3] = {other.max.x, other.max.y, other.max.z};
        for (int axis = 0; axis < 3; ++axis) {
            bool beside = true;
            for (int k = 0; k < 3; ++k) {
                if (k == axis) continue;
                beside = beside && lo[k] < ohi[k] + reach && hi[k] > olo[k] - reach;
            }
            if (!beside) continue;
            // Face to face (touching) and face to face on the same side (flush).
            for (const float move : {olo[axis] - hi[axis], ohi[axis] - lo[axis], olo[axis] - lo[axis], ohi[axis] - hi[axis]}) {
                if (std::fabs(move) <= reach && (!found[axis] || std::fabs(move) < std::fabs(offset[axis]))) {
                    offset[axis] = move;
                    found[axis] = true;
                }
            }
        }
    }
    return Vec3(offset[0], offset[1], offset[2]);
}

} // namespace Hephaestus::SceneAuthoring
