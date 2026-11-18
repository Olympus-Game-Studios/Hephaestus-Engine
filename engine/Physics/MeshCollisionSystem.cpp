#include "MeshCollisionSystem.h"

#include "../Assets/MeshAsset.h"
#include "../Assets/MeshGeometryCache.h"
#include "../Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <vector>

namespace {

bool AabbOverlap(const AABB& a, const AABB& b) {
    return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
           (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
           (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

void BuildTriangleBounds(const MeshData& mesh, const Mat4& worldMatrix, std::vector<AABB>& outBounds) {
    outBounds.clear();
    if (mesh.indices.size() < 3 || mesh.vertices.empty()) {
        return;
    }

    outBounds.reserve(mesh.indices.size() / 3);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            continue;
        }

        const Vertex& v0 = mesh.vertices[i0];
        const Vertex& v1 = mesh.vertices[i1];
        const Vertex& v2 = mesh.vertices[i2];
        Vec3 p0 = Mat4::TransformPoint(worldMatrix, Vec3(v0.pos[0], v0.pos[1], v0.pos[2]));
        Vec3 p1 = Mat4::TransformPoint(worldMatrix, Vec3(v1.pos[0], v1.pos[1], v1.pos[2]));
        Vec3 p2 = Mat4::TransformPoint(worldMatrix, Vec3(v2.pos[0], v2.pos[1], v2.pos[2]));

        Vec3 minV(std::min({p0.x, p1.x, p2.x}),
                  std::min({p0.y, p1.y, p2.y}),
                  std::min({p0.z, p1.z, p2.z}));
        Vec3 maxV(std::max({p0.x, p1.x, p2.x}),
                  std::max({p0.y, p1.y, p2.y}),
                  std::max({p0.z, p1.z, p2.z}));
        outBounds.emplace_back(minV, maxV);
    }
}

} // namespace

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
        bool useComplex = false;
        std::shared_ptr<const MeshData> meshData;
        std::vector<AABB> triBounds;
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
        entry.useComplex = collider.useComplexCollision;
        if (entry.useComplex) {
            entry.meshData = renderer.meshAssetPath.empty() ? nullptr
                : Hephaestus::MeshGeometryCache::Get(renderer.meshAssetPath);
            if (!entry.meshData) {
                entry.useComplex = false;
            }
        }
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

            bool aComplex = a.useComplex && a.meshData;
            bool bComplex = b.useComplex && b.meshData;
            bool hasContact = false;

            if (!aComplex && !bComplex) {
                hasContact = true;
            } else {
                if (!AabbOverlap(a.bounds, b.bounds)) {
                    continue;
                }

                if (aComplex && a.triBounds.empty()) {
                    BuildTriangleBounds(*a.meshData, a.transform->GetLocalMatrix(), a.triBounds);
                }
                if (bComplex && b.triBounds.empty()) {
                    BuildTriangleBounds(*b.meshData, b.transform->GetLocalMatrix(), b.triBounds);
                }

                if (aComplex && bComplex) {
                    for (const auto& triA : a.triBounds) {
                        for (const auto& triB : b.triBounds) {
                            if (AabbOverlap(triA, triB)) {
                                hasContact = true;
                                break;
                            }
                        }
                        if (hasContact) break;
                    }
                } else if (aComplex) {
                    for (const auto& triA : a.triBounds) {
                        if (AabbOverlap(triA, b.bounds)) {
                            hasContact = true;
                            break;
                        }
                    }
                } else if (bComplex) {
                    for (const auto& triB : b.triBounds) {
                        if (AabbOverlap(triB, a.bounds)) {
                            hasContact = true;
                            break;
                        }
                    }
                }
            }

            if (!hasContact) {
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
