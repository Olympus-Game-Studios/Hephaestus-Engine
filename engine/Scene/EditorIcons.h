#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <optional>

namespace Hephaestus {

// Viewport icons the editor draws for entities that have nothing else to see or click.
enum class EditorIcon : std::size_t {
    PointLight,
    SpotLight,
    DirectionalLight,
    AudioSource,
    ParticleEmitter,
    Environment,
    ProbeVolume,
    Script,
    Count
};

// Image file name in editor/assets/billboards.
const char* EditorIconFileName(EditorIcon icon);

// The icon for an entity, or nothing. Cameras show their mesh instead, and entities with a mesh, a character or a
// physics body are already visible or belong to something that is.
std::optional<EditorIcon> EditorIconFor(const entt::registry& registry, entt::entity entity);

} // namespace Hephaestus
