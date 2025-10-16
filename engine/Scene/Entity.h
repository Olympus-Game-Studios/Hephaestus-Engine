#pragma once
#include <entt/entt.hpp>
#include <string>

// Forward declaration
class Scene;

/**
 * Entity wrapper class that provides a convenient interface to EnTT entities
 * This is a lightweight handle that wraps an entt::entity and provides
 * type-safe component access
 */
class Entity {
public:
    Entity() = default;
    Entity(entt::entity handle, Scene* scene);
    Entity(const Entity& other) = default;

    // Component management (template declarations)
    template<typename T, typename... Args>
    T& AddComponent(Args&&... args);

    template<typename T>
    T& GetComponent();

    template<typename T>
    bool HasComponent();

    template<typename T>
    void RemoveComponent();

    // Validity checks
    operator bool() const { return m_EntityHandle != entt::null && m_Scene != nullptr; }
    operator entt::entity() const { return m_EntityHandle; }
    operator uint32_t() const { return static_cast<uint32_t>(m_EntityHandle); }

    bool operator==(const Entity& other) const {
        return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
    }

    bool operator!=(const Entity& other) const {
        return !(*this == other);
    }

private:
    entt::entity m_EntityHandle{ entt::null };
    Scene* m_Scene = nullptr;

    friend class Scene;
};
