#pragma once

#include "../Scene/Scene.h"

namespace Physics {

struct MeshCollisionSettings {
    float penetrationSlop = 0.001f;
    bool resolveIntersections = true;
};

class MeshCollisionSystem {
public:
    MeshCollisionSystem();
    explicit MeshCollisionSystem(const MeshCollisionSettings& settings);

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    void SetSettings(const MeshCollisionSettings& settings);
    const MeshCollisionSettings& GetSettings() const;

    void Update(Scene& scene, float deltaTime);

private:
    void ClearContacts(Scene& scene);

    bool m_Enabled = true;
    MeshCollisionSettings m_Settings;
};

} // namespace Physics
