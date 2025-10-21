#include "MeshCollisionSystem.h"

#include "../Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Physics {

MeshCollisionSystem::MeshCollisionSystem()
    : m_Settings{}
{
}

MeshCollisionSystem::MeshCollisionSystem(const MeshCollisionSettings& settings)
    : m_Settings(settings)
{
}

void MeshCollisionSystem::SetEnabled(bool enabled)
{
    m_Enabled = enabled;
}

bool MeshCollisionSystem::IsEnabled() const
{
    return m_Enabled;
}

void MeshCollisionSystem::SetSettings(const MeshCollisionSettings& settings)
{
    m_Settings = settings;
}

const MeshCollisionSettings& MeshCollisionSystem::GetSettings() const
{
    return m_Settings;
}

void MeshCollisionSystem::ClearContacts(Scene& scene)
{
    auto view = scene.GetEntitiesWith<MeshColliderComponent>();
    for (auto handle : view) {
        Entity entity(handle, &scene);
        if (entity.HasComponent<MeshColliderComponent>()) {
            entity.GetComponent<MeshColliderComponent>().ClearContacts();
        }
    }
}

void MeshCollisionSystem::Update(Scene& scene, float /*deltaTime*/)
{
    if (!m_Enabled) {
        ClearContacts(scene);
        return;
    }

    auto view = scene.GetEntitiesWith<TransformComponent, MeshRendererComponent, MeshColliderComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    struct ColliderEntry {
        Entity entity;
        TransformComponent* transform = nullptr;
        MeshRendererComponent* renderer = nullptr;
        MeshColliderComponent* collider = nullptr;
        AABB bounds;
        Vec3 center;
        Vec3 halfExtent;
    };

    std::vector<ColliderEntry> entries;
    entries.reserve(32);

    for (auto handle : view) {
        Entity entity(handle, &scene);
        auto& collider = entity.GetComponent<MeshColliderComponent>();
        collider.ClearContacts();

        if (!collider.enabled) {
            continue;
        }

        auto& transform = entity.GetComponent<TransformComponent>();
        auto& renderer = entity.GetComponent<MeshRendererComponent>();

        AABB worldBounds = renderer.GetWorldBounds(transform);

        ColliderEntry entry;
        entry.entity = entity;
        entry.transform = &transform;
        entry.renderer = &renderer;
        entry.collider = &collider;
        entry.bounds = worldBounds;
        entry.center = worldBounds.Center();
        entry.halfExtent = worldBounds.HalfSize();
        entries.push_back(entry);
    }

    const size_t colliderCount = entries.size();
    if (colliderCount < 2) {
        return;
    }

    for (size_t i = 0; i < colliderCount; ++i) {
        for (size_t j = i + 1; j < colliderCount; ++j) {
            ColliderEntry& a = entries[i];
            ColliderEntry& b = entries[j];

            Vec3 delta = vsub(b.center, a.center);
            Vec3 absDelta = v3(std::fabs(delta.x), std::fabs(delta.y), std::fabs(delta.z));
            Vec3 totalHalf = v3(a.halfExtent.x + b.halfExtent.x,
                                a.halfExtent.y + b.halfExtent.y,
                                a.halfExtent.z + b.halfExtent.z);
            Vec3 overlap = v3(totalHalf.x - absDelta.x,
                              totalHalf.y - absDelta.y,
                              totalHalf.z - absDelta.z);

            if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) {
                continue;
            }

            float minOverlap = overlap.x;
            int axis = 0;
            if (overlap.y < minOverlap) {
                minOverlap = overlap.y;
                axis = 1;
            }
            if (overlap.z < minOverlap) {
                minOverlap = overlap.z;
                axis = 2;
            }

            float penetration = minOverlap - m_Settings.penetrationSlop;
            if (penetration <= 0.0f) {
                continue;
            }

            float axisDelta = (&delta.x)[axis];
            float sign = (axisDelta >= 0.0f) ? -1.0f : 1.0f;
            if (std::fabs(axisDelta) < 1e-6f) {
                sign = (i < j) ? -1.0f : 1.0f;
            }

            Vec3 normalA(0.0f, 0.0f, 0.0f);
            (&normalA.x)[axis] = sign;
            Vec3 normalB = vscale(normalA, -1.0f);

            bool trigger = a.collider->isTrigger || b.collider->isTrigger;
            Vec3 contactPoint = vscale(vadd(a.center, b.center), 0.5f);

            MeshColliderContact contactA;
            contactA.other = static_cast<entt::entity>(b.entity);
            contactA.point = contactPoint;
            contactA.normal = normalA;
            contactA.penetration = penetration;
            contactA.trigger = trigger;
            a.collider->contacts.push_back(contactA);

            MeshColliderContact contactB = contactA;
            contactB.other = static_cast<entt::entity>(a.entity);
            contactB.normal = normalB;
            b.collider->contacts.push_back(contactB);

            if (trigger || !m_Settings.resolveIntersections) {
                continue;
            }

            Vec3 resolveVector = vscale(normalA, penetration);
            bool aMovable = !a.collider->isStatic;
            bool bMovable = !b.collider->isStatic;

            if (aMovable && bMovable) {
                Vec3 halfResolve = vscale(resolveVector, 0.5f);
                a.transform->position = vadd(a.transform->position, halfResolve);
                b.transform->position = vsub(b.transform->position, halfResolve);
                a.center = vadd(a.center, halfResolve);
                b.center = vsub(b.center, halfResolve);
                a.bounds.Translate(halfResolve);
                b.bounds.Translate(vscale(halfResolve, -1.0f));
            } else if (aMovable) {
                a.transform->position = vadd(a.transform->position, resolveVector);
                a.center = vadd(a.center, resolveVector);
                a.bounds.Translate(resolveVector);
            } else if (bMovable) {
                Vec3 resolveB = vscale(resolveVector, -1.0f);
                b.transform->position = vadd(b.transform->position, resolveB);
                b.center = vadd(b.center, resolveB);
                b.bounds.Translate(resolveB);
            }
        }
    }
}

} // namespace Physics
