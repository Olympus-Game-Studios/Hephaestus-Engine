#pragma once
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "Entity.h"
#include "Components.h"

/**
 * Scene class - manages a collection of entities and their components
 * Provides scene graph functionality with parent-child relationships
 */
class Scene {
public:
    Scene(const std::string& name = "Untitled Scene");
    ~Scene();

    // Entity creation and destruction
    Entity CreateEntity(const std::string& name = "Entity");
    Entity CreateEntityWithID(entt::entity id, const std::string& name = "Entity");
    void DestroyEntity(Entity entity);

    // Entity hierarchy management
    void SetParent(Entity child, Entity parent, bool preserveWorldTransform = false);
    void RemoveParent(Entity child, bool preserveWorldTransform = false);
    Entity GetParent(Entity entity);
    std::vector<Entity> GetChildren(Entity entity);

    // Get world transform (including parent transforms)
    Mat4 GetWorldTransform(Entity entity);

    // Scene queries
    Entity FindEntityByName(const std::string& name);
    std::vector<Entity> FindEntitiesWithNamePrefix(const std::string& prefix);
    template<typename... Components>
    auto GetEntitiesWith() {
        return m_Registry.view<Components...>();
    }

    // Scene management
    void Clear();
    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

    // Authored source identity is retained independently of the display name.
    // Virtual URIs (for example plugin://) are kept verbatim; filesystem paths
    // are stored as normalized absolute paths.
    void SetSourceAssetPath(const std::string& path);
    const std::string& GetSourceAssetPath() const { return m_SourceAssetPath; }
    bool IsReloadable() const { return !m_SourceAssetPath.empty(); }
    std::uint64_t GetIdentity() const { return m_SceneIdentity; }
    std::uint64_t GetRuntimeGeneration() const { return m_RuntimeGeneration; }
    void MarkReconstructedFromSource() { ++m_RuntimeGeneration; }

    // Selection management
    Entity GetSelectedEntity();
    std::vector<Entity> GetSelectedEntities();
    void SetSelectedEntity(Entity entity);
    /** Replace selection with the given entities; primary is the focused entity for gizmo/inspector. */
    void SetSelectedEntities(const std::vector<entt::entity>& entities, entt::entity primary);
    void AddSelectedEntity(Entity entity);
    void ToggleSelectedEntity(Entity entity);
    void ClearSelection();

    // Simple in-memory clipboard for editor copy/paste (single-entity)
    // Copies a shallow snapshot of commonly-used components so users can duplicate objects with Ctrl+C / Ctrl+V
    void CopyEntityToClipboard(Entity entity);
    bool HasClipboardEntity() const;
    Entity PasteClipboard();

    /**
     * Duplicate selected roots with their hierarchy. Prefab instance roots are
     * re-instantiated so the copies remain linked instances.
     */
    std::vector<Entity> DuplicateEntities(const std::vector<entt::entity>& entities,
                                          bool preserveParent = true);

    // Registry access (for advanced usage)
    entt::registry& GetRegistry() { return m_Registry; }
    const entt::registry& GetRegistry() const { return m_Registry; }

private:
    std::string m_Name;
    std::string m_SourceAssetPath;
    std::uint64_t m_SceneIdentity = 0;
    std::uint64_t m_RuntimeGeneration = 1;
    entt::registry m_Registry;
    entt::entity m_PrimarySelected{ entt::null };

    friend class Entity;
};

// ═══════════════════════════════════════════════════════════════
// Entity template implementation (after Scene is defined)
// ═══════════════════════════════════════════════════════════════

template<typename T, typename... Args>
T& Entity::AddComponent(Args&&... args) {
    if (HasComponent<T>()) {
        return GetComponent<T>();
    }
    return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
}

template<typename T>
T& Entity::GetComponent() {
    return m_Scene->m_Registry.get<T>(m_EntityHandle);
}

template<typename T>
bool Entity::HasComponent() {
    return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
}

template<typename T>
void Entity::RemoveComponent() {
    m_Scene->m_Registry.remove<T>(m_EntityHandle);
}
