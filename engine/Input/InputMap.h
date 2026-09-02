#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus {

enum class InputBindingType {
    Keyboard,
    MouseButton,
    MouseDelta,
    MouseWheel,
    GamepadButton,
    GamepadAxis
};

struct InputBinding {
    InputBindingType type = InputBindingType::Keyboard;
    std::string control;
    float scale = 1.0f;
    float deadzone = 0.15f;
    int controlCode = -1;
};

struct InputActionDefinition {
    std::string name;
    std::vector<InputBinding> bindings;
};

struct InputAxisDefinition {
    std::string name;
    bool clamp = true;
    std::vector<InputBinding> bindings;
};

struct InputMap {
    static constexpr int VERSION = 1;

    int version = VERSION;
    std::vector<InputActionDefinition> actions;
    std::vector<InputAxisDefinition> axes;

    static InputMap CreateDefault();
    static bool Load(const std::filesystem::path& path, InputMap& outMap, std::string& outError);
    bool Save(const std::filesystem::path& path, std::string& outError) const;
    bool Validate(std::string& outError);
};

const char* ToString(InputBindingType type);
bool InputBindingTypeFromString(const std::string& value, InputBindingType& outType);
int ResolveInputControl(InputBindingType type, const std::string& control);

} // namespace Hephaestus
