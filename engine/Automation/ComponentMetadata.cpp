#include "ComponentMetadata.h"

#include <algorithm>
#include <utility>

namespace Hephaestus::Automation {
namespace {

PropertyDescriptor Property(std::string path, PropertyKind kind)
{
    PropertyDescriptor descriptor;
    descriptor.path = std::move(path);
    descriptor.kind = kind;
    return descriptor;
}

ComponentDescriptor Component(std::string type, std::string displayName,
                              std::string category, bool addable = true)
{
    ComponentDescriptor descriptor;
    descriptor.type = std::move(type);
    descriptor.displayName = std::move(displayName);
    descriptor.category = std::move(category);
    descriptor.readable = true;
    descriptor.writable = true;
    descriptor.addable = addable;
    descriptor.removable = addable;
    return descriptor;
}

} // namespace

ComponentMetadataRegistry::ComponentMetadataRegistry()
{
    ComponentDescriptor transform = Component("Transform", "Transform", "Core", false);
    transform.properties = {
        Property("position", PropertyKind::Vec3),
        Property("rotation", PropertyKind::Vec3),
        Property("scale", PropertyKind::Vec3),
    };
    m_Descriptors.push_back(std::move(transform));

    m_Descriptors.push_back(Component("AudioSource", "Audio Source", "Audio"));
    m_Descriptors.push_back(Component("Camera", "Camera", "Rendering"));
    m_Descriptors.push_back(Component("Character", "Character", "Animation"));
    m_Descriptors.push_back(Component("Light", "Light", "Rendering"));
    m_Descriptors.push_back(Component("LuaScript", "Lua Script", "Scripting"));
    m_Descriptors.push_back(Component("Material", "Material", "Rendering"));
    m_Descriptors.push_back(Component("MeshCollider", "Mesh Collider", "Physics"));
    m_Descriptors.push_back(Component("MeshRenderer", "Mesh Renderer", "Rendering"));
    m_Descriptors.push_back(Component("PhysicsShape", "Physics Shape", "Physics"));
    m_Descriptors.push_back(Component("RigidBody", "Rigid Body", "Physics"));
    m_Descriptors.push_back(Component("ScriptComponent", "C++ Script", "Scripting"));

    std::sort(m_Descriptors.begin(), m_Descriptors.end(),
              [](const ComponentDescriptor& left, const ComponentDescriptor& right) {
                  return left.type < right.type;
              });
}

const ComponentMetadataRegistry& ComponentMetadataRegistry::Builtins()
{
    static const ComponentMetadataRegistry registry;
    return registry;
}

const ComponentDescriptor* ComponentMetadataRegistry::Find(std::string_view type) const noexcept
{
    const auto it = std::lower_bound(
        m_Descriptors.begin(), m_Descriptors.end(), type,
        [](const ComponentDescriptor& descriptor, std::string_view value) {
            return descriptor.type < value;
        });
    return it != m_Descriptors.end() && it->type == type ? &*it : nullptr;
}

const PropertyDescriptor* ComponentMetadataRegistry::FindProperty(
    std::string_view component, std::string_view path) const noexcept
{
    const ComponentDescriptor* descriptor = Find(component);
    if (!descriptor) return nullptr;
    const auto it = std::find_if(descriptor->properties.begin(), descriptor->properties.end(),
                                 [path](const PropertyDescriptor& property) {
                                     return property.path == path;
                                 });
    return it == descriptor->properties.end() ? nullptr : &*it;
}

} // namespace Hephaestus::Automation
