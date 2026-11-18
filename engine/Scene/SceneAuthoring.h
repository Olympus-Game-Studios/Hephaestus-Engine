#pragma once

#include "../Core/Math.h"

#include <entt/entt.hpp>
#include <cstddef>
#include <functional>
#include <unordered_set>
#include <vector>

class Scene;

namespace Hephaestus::SceneAuthoring {

struct SceneRaycastHit {
    entt::entity entity = entt::null;
    Vec3 point{};
    Vec3 normal{};
    float distance = 0.0f;
};

float SnapScalar(float value, float increment);
Vec3 SnapVector(const Vec3& value, float increment);
std::vector<float> EvenlySpacedValues(float first, float last,
                                      std::size_t count);
AABB TransformBounds(const AABB& localBounds, const Mat4& worldTransform);

// How far to move a box so that, on each axis, one of its faces lands on a face of a nearby box it lines up with
// (touching it, or flush with it) when one is within reach: the smallest such move per axis, zero where none is. A
// neighbour counts on an axis only if the boxes overlap, or nearly do, on the other two.
Vec3 SnapBoundsToNeighbours(const AABB& moving, const std::vector<AABB>& neighbours, float reach);
// World bounds of the visible meshes within reach of a box, for SnapBoundsToNeighbours. Leaves out construction
// helpers and every entity for which skip is true, along with its descendants.
std::vector<AABB> NeighbourBounds(Scene& scene, const AABB& around, float reach,
                                  const std::function<bool(entt::entity)>& skip);

bool RaycastBounds(Scene& scene,
                   const Ray& ray,
                   SceneRaycastHit& outHit,
                   const std::unordered_set<entt::entity>& ignored = {});

} // namespace Hephaestus::SceneAuthoring
