#pragma once

/**
 * @file ComponentClipboard.h
 * @brief Copy/paste of component payloads and typed property values.
 *
 * Uses PrefabComponentIO JSON capture so paste does not require per-component
 * hand-written copiers.
 */

#include "../Core/Math.h"
#include "../Scene/PrefabAsset.h"

#include <string>
#include <vector>

class Entity;
class Scene;

namespace Hephaestus {

class ComponentClipboard {
public:
    bool CopyFrom(Entity entity, const std::string& type);
    bool HasComponent() const { return m_hasComponent; }
    const std::string& Type() const { return m_entry.type; }
    const PrefabComponentEntry& Entry() const { return m_entry; }

    /** Add the component if missing, then apply captured values. */
    int PasteComponent(Scene& scene, const std::vector<entt::entity>& entities) const;
    /** Apply values only where the component already exists. */
    int PasteValues(Scene& scene, const std::vector<entt::entity>& entities) const;

    enum class PropertyKind { None, Number, Bool, String, Vec3 };

    void CopyNumber(double value);
    void CopyBool(bool value);
    void CopyString(const std::string& value);
    void CopyVec3(const Vec3& value);

    PropertyKind PropertyType() const { return m_propertyKind; }
    bool HasProperty() const { return m_propertyKind != PropertyKind::None; }
    double NumberValue() const { return m_number; }
    bool BoolValue() const { return m_bool; }
    const std::string& StringValue() const { return m_string; }
    Vec3 Vec3Value() const { return m_vec3; }

    static ComponentClipboard& Instance();

private:
    bool m_hasComponent = false;
    PrefabComponentEntry m_entry;
    PropertyKind m_propertyKind = PropertyKind::None;
    double m_number = 0.0;
    bool m_bool = false;
    std::string m_string;
    Vec3 m_vec3;
};

} // namespace Hephaestus
