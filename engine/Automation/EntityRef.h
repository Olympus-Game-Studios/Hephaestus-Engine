#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

class Scene;

namespace Hephaestus::Automation {

struct EntityRef {
    std::string value;

    explicit operator bool() const noexcept { return !value.empty(); }
    friend bool operator==(const EntityRef&, const EntityRef&) = default;
};

enum class EntityResolutionStatus {
    Resolved,
    UnknownRef,
    SceneChanged,
    Destroyed,
};

struct ResolvedEntity {
    EntityResolutionStatus status = EntityResolutionStatus::UnknownRef;
    Scene* scene = nullptr;
    entt::entity handle = entt::null;

    [[nodiscard]] bool IsResolved() const noexcept
    {
        return status == EntityResolutionStatus::Resolved;
    }
};

class EntityRefTable {
public:
    EntityRef Bind(Scene& scene, entt::entity handle);
    ResolvedEntity Resolve(const EntityRef& ref, Scene* activeScene) const;
    void Erase(const EntityRef& ref);
    void Reset();
    void ObserveActiveScene(Scene* activeScene);
    [[nodiscard]] std::size_t Size() const noexcept { return m_Entries.size(); }

private:
    struct Entry {
        Scene* scene = nullptr;
        entt::entity handle = entt::null;
    };

    Scene* m_ObservedScene = nullptr;
    std::uint64_t m_NextId = 1;
    std::unordered_map<std::string, Entry> m_Entries;
};

} // namespace Hephaestus::Automation
