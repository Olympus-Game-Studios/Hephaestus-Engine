#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Hephaestus::Automation {

enum class PropertyKind {
    Null,
    Boolean,
    Number,
    String,
    Vec2,
    Vec3,
    Vec4,
    Array,
    Object,
};

struct NumericConstraint {
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
};

struct PropertyDescriptor {
    std::string path;
    PropertyKind kind = PropertyKind::Null;
    NumericConstraint numeric;
    std::vector<std::string> enumValues;
    std::optional<std::string> assetReferenceType;
    bool readOnly = false;
    bool editorOnly = false;
};

struct ComponentDescriptor {
    std::string type;
    std::string displayName;
    std::string category;
    bool sceneUnique = false;
    bool readable = false;
    bool writable = false;
    bool addable = false;
    bool removable = false;
    std::vector<PropertyDescriptor> properties;
};

class ComponentMetadataRegistry {
public:
    static const ComponentMetadataRegistry& Builtins();

    [[nodiscard]] const ComponentDescriptor* Find(std::string_view type) const noexcept;
    [[nodiscard]] std::span<const ComponentDescriptor> All() const noexcept { return m_Descriptors; }
    [[nodiscard]] const PropertyDescriptor* FindProperty(std::string_view component,
                                                          std::string_view path) const noexcept;

private:
    ComponentMetadataRegistry();
    std::vector<ComponentDescriptor> m_Descriptors;
};

} // namespace Hephaestus::Automation
