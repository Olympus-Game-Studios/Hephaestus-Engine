#pragma once
#include <entt/entt.hpp>
#include <string>
#include <vector>
#include <optional>
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
    void SetParent(Entity child, Entity parent);
    void RemoveParent(Entity child);
    Entity GetParent(Entity entity);
    std::vector<Entity> GetChildren(Entity entity);

    // Get world transform (including parent transforms)
    Mat4 GetWorldTransform(Entity entity);

    // Scene queries
    Entity FindEntityByName(const std::string& name);
    template<typename... Components>
    auto GetEntitiesWith() {
        return m_Registry.view<Components...>();
    }

    // Scene management
    void Clear();
    const std::string& GetName() const { return m_Name; }
    void SetName(const std::string& name) { m_Name = name; }

    // Selection management
    Entity GetSelectedEntity();
    std::vector<Entity> GetSelectedEntities();
    void SetSelectedEntity(Entity entity);
    void AddSelectedEntity(Entity entity);
    void ToggleSelectedEntity(Entity entity);
    void ClearSelection();

    // Simple in-memory clipboard for editor copy/paste (single-entity)
    // Copies a shallow snapshot of commonly-used components so users can duplicate objects with Ctrl+C / Ctrl+V
    void CopyEntityToClipboard(Entity entity);
    bool HasClipboardEntity() const;
    Entity PasteClipboard();

    // Registry access (for advanced usage)
    entt::registry& GetRegistry() { return m_Registry; }
    const entt::registry& GetRegistry() const { return m_Registry; }

private:
    std::string m_Name;
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
