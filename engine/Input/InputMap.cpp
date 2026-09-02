#include "InputMap.h"

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace Hephaestus {
namespace {

std::string ToUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

InputBinding MakeBinding(InputBindingType type, const char* control,
                         float scale = 1.0f, float deadzone = 0.15f)
{
    InputBinding binding;
    binding.type = type;
    binding.control = control;
    binding.scale = scale;
    binding.deadzone = deadzone;
    binding.controlCode = ResolveInputControl(type, control);
    return binding;
}

const std::unordered_map<std::string, int>& KeyCodes()
{
    static const std::unordered_map<std::string, int> codes = {
        {"SPACE", GLFW_KEY_SPACE}, {"APOSTROPHE", GLFW_KEY_APOSTROPHE},
        {"COMMA", GLFW_KEY_COMMA}, {"MINUS", GLFW_KEY_MINUS},
        {"PERIOD", GLFW_KEY_PERIOD}, {"SLASH", GLFW_KEY_SLASH},
        {"SEMICOLON", GLFW_KEY_SEMICOLON}, {"EQUAL", GLFW_KEY_EQUAL},
        {"LEFT_BRACKET", GLFW_KEY_LEFT_BRACKET}, {"BACKSLASH", GLFW_KEY_BACKSLASH},
        {"RIGHT_BRACKET", GLFW_KEY_RIGHT_BRACKET}, {"GRAVE_ACCENT", GLFW_KEY_GRAVE_ACCENT},
        {"ESCAPE", GLFW_KEY_ESCAPE}, {"ESC", GLFW_KEY_ESCAPE},
        {"ENTER", GLFW_KEY_ENTER}, {"RETURN", GLFW_KEY_ENTER},
        {"TAB", GLFW_KEY_TAB}, {"BACKSPACE", GLFW_KEY_BACKSPACE},
        {"INSERT", GLFW_KEY_INSERT}, {"DELETE", GLFW_KEY_DELETE},
        {"RIGHT", GLFW_KEY_RIGHT}, {"LEFT", GLFW_KEY_LEFT},
        {"DOWN", GLFW_KEY_DOWN}, {"UP", GLFW_KEY_UP},
        {"PAGE_UP", GLFW_KEY_PAGE_UP}, {"PAGE_DOWN", GLFW_KEY_PAGE_DOWN},
        {"HOME", GLFW_KEY_HOME}, {"END", GLFW_KEY_END},
        {"CAPS_LOCK", GLFW_KEY_CAPS_LOCK}, {"SCROLL_LOCK", GLFW_KEY_SCROLL_LOCK},
        {"NUM_LOCK", GLFW_KEY_NUM_LOCK}, {"PRINT_SCREEN", GLFW_KEY_PRINT_SCREEN},
        {"PAUSE", GLFW_KEY_PAUSE}, {"F1", GLFW_KEY_F1}, {"F2", GLFW_KEY_F2},
        {"F3", GLFW_KEY_F3}, {"F4", GLFW_KEY_F4}, {"F5", GLFW_KEY_F5},
        {"F6", GLFW_KEY_F6}, {"F7", GLFW_KEY_F7}, {"F8", GLFW_KEY_F8},
        {"F9", GLFW_KEY_F9}, {"F10", GLFW_KEY_F10}, {"F11", GLFW_KEY_F11},
        {"F12", GLFW_KEY_F12}, {"LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT},
        {"SHIFT", GLFW_KEY_LEFT_SHIFT}, {"LEFT_CONTROL", GLFW_KEY_LEFT_CONTROL},
        {"LEFT_CTRL", GLFW_KEY_LEFT_CONTROL}, {"CTRL", GLFW_KEY_LEFT_CONTROL},
        {"CONTROL", GLFW_KEY_LEFT_CONTROL},
        {"LEFT_ALT", GLFW_KEY_LEFT_ALT}, {"ALT", GLFW_KEY_LEFT_ALT},
        {"LEFT_SUPER", GLFW_KEY_LEFT_SUPER}, {"SUPER", GLFW_KEY_LEFT_SUPER},
        {"RIGHT_SHIFT", GLFW_KEY_RIGHT_SHIFT}, {"RIGHT_CONTROL", GLFW_KEY_RIGHT_CONTROL},
        {"RIGHT_CTRL", GLFW_KEY_RIGHT_CONTROL}, {"RIGHT_ALT", GLFW_KEY_RIGHT_ALT},
        {"RIGHT_SUPER", GLFW_KEY_RIGHT_SUPER}, {"MENU", GLFW_KEY_MENU}
    };
    return codes;
}

const std::unordered_map<std::string, int>& GamepadButtons()
{
    static const std::unordered_map<std::string, int> codes = {
        {"SOUTH", GLFW_GAMEPAD_BUTTON_A}, {"A", GLFW_GAMEPAD_BUTTON_A},
        {"EAST", GLFW_GAMEPAD_BUTTON_B}, {"B", GLFW_GAMEPAD_BUTTON_B},
        {"WEST", GLFW_GAMEPAD_BUTTON_X}, {"X", GLFW_GAMEPAD_BUTTON_X},
        {"NORTH", GLFW_GAMEPAD_BUTTON_Y}, {"Y", GLFW_GAMEPAD_BUTTON_Y},
        {"LEFT_BUMPER", GLFW_GAMEPAD_BUTTON_LEFT_BUMPER},
        {"RIGHT_BUMPER", GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER},
        {"BACK", GLFW_GAMEPAD_BUTTON_BACK}, {"START", GLFW_GAMEPAD_BUTTON_START},
        {"GUIDE", GLFW_GAMEPAD_BUTTON_GUIDE},
        {"LEFT_STICK", GLFW_GAMEPAD_BUTTON_LEFT_THUMB},
        {"RIGHT_STICK", GLFW_GAMEPAD_BUTTON_RIGHT_THUMB},
        {"DPAD_UP", GLFW_GAMEPAD_BUTTON_DPAD_UP},
        {"DPAD_RIGHT", GLFW_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"DPAD_DOWN", GLFW_GAMEPAD_BUTTON_DPAD_DOWN},
        {"DPAD_LEFT", GLFW_GAMEPAD_BUTTON_DPAD_LEFT}
    };
    return codes;
}

const std::unordered_map<std::string, int>& GamepadAxes()
{
    static const std::unordered_map<std::string, int> codes = {
        {"LEFT_X", GLFW_GAMEPAD_AXIS_LEFT_X}, {"LEFT_Y", GLFW_GAMEPAD_AXIS_LEFT_Y},
        {"RIGHT_X", GLFW_GAMEPAD_AXIS_RIGHT_X}, {"RIGHT_Y", GLFW_GAMEPAD_AXIS_RIGHT_Y},
        {"LEFT_TRIGGER", GLFW_GAMEPAD_AXIS_LEFT_TRIGGER},
        {"RIGHT_TRIGGER", GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER}
    };
    return codes;
}

bool ParseBinding(const nlohmann::json& value, InputBinding& outBinding, std::string& outError)
{
    if (!value.is_object()) {
        outError = "Input binding must be an object";
        return false;
    }

    const std::string type = value.value("type", "");
    if (!InputBindingTypeFromString(type, outBinding.type)) {
        outError = "Unknown input binding type: " + type;
        return false;
    }

    outBinding.control = value.value("control", "");
    outBinding.scale = value.value("scale", 1.0f);
    outBinding.deadzone = value.value("deadzone", 0.15f);
    outBinding.controlCode = ResolveInputControl(outBinding.type, outBinding.control);
    if (outBinding.controlCode < 0) {
        outError = "Unknown control '" + outBinding.control + "' for binding type '" + type + "'";
        return false;
    }
    if (outBinding.deadzone < 0.0f || outBinding.deadzone >= 1.0f) {
        outError = "Input binding deadzone must be in [0, 1)";
        return false;
    }
    return true;
}

nlohmann::json WriteBinding(const InputBinding& binding)
{
    nlohmann::json value = {
        {"type", ToString(binding.type)},
        {"control", binding.control}
    };
    if (binding.scale != 1.0f) {
        value["scale"] = binding.scale;
    }
    if ((binding.type == InputBindingType::GamepadAxis ||
         binding.type == InputBindingType::GamepadButton) &&
        binding.deadzone != 0.15f) {
        value["deadzone"] = binding.deadzone;
    }
    return value;
}

} // namespace

const char* ToString(InputBindingType type)
{
    switch (type) {
    case InputBindingType::Keyboard: return "keyboard";
    case InputBindingType::MouseButton: return "mouse_button";
    case InputBindingType::MouseDelta: return "mouse_delta";
    case InputBindingType::MouseWheel: return "mouse_wheel";
    case InputBindingType::GamepadButton: return "gamepad_button";
    case InputBindingType::GamepadAxis: return "gamepad_axis";
    }
    return "keyboard";
}

bool InputBindingTypeFromString(const std::string& value, InputBindingType& outType)
{
    const std::string upper = ToUpper(value);
    if (upper == "KEYBOARD") outType = InputBindingType::Keyboard;
    else if (upper == "MOUSE_BUTTON") outType = InputBindingType::MouseButton;
    else if (upper == "MOUSE_DELTA") outType = InputBindingType::MouseDelta;
    else if (upper == "MOUSE_WHEEL") outType = InputBindingType::MouseWheel;
    else if (upper == "GAMEPAD_BUTTON") outType = InputBindingType::GamepadButton;
    else if (upper == "GAMEPAD_AXIS") outType = InputBindingType::GamepadAxis;
    else return false;
    return true;
}

int ResolveInputControl(InputBindingType type, const std::string& control)
{
    const std::string upper = ToUpper(control);
    if (type == InputBindingType::Keyboard) {
        if (upper.size() == 1 && upper[0] >= 'A' && upper[0] <= 'Z') {
            return GLFW_KEY_A + (upper[0] - 'A');
        }
        if (upper.size() == 1 && upper[0] >= '0' && upper[0] <= '9') {
            return GLFW_KEY_0 + (upper[0] - '0');
        }
        const auto it = KeyCodes().find(upper);
        return it != KeyCodes().end() ? it->second : -1;
    }
    if (type == InputBindingType::MouseButton) {
        if (upper == "LEFT" || upper == "LMB") return GLFW_MOUSE_BUTTON_LEFT;
        if (upper == "RIGHT" || upper == "RMB") return GLFW_MOUSE_BUTTON_RIGHT;
        if (upper == "MIDDLE" || upper == "MMB") return GLFW_MOUSE_BUTTON_MIDDLE;
        if (upper.size() == 1 && upper[0] >= '0' && upper[0] <= '7') {
            return upper[0] - '0';
        }
        return -1;
    }
    if (type == InputBindingType::MouseDelta) {
        if (upper == "X" || upper == "DELTA_X") return 0;
        if (upper == "Y" || upper == "DELTA_Y") return 1;
        return -1;
    }
    if (type == InputBindingType::MouseWheel) {
        if (upper == "X") return 0;
        if (upper == "Y") return 1;
        return -1;
    }
    if (type == InputBindingType::GamepadButton) {
        const auto it = GamepadButtons().find(upper);
        return it != GamepadButtons().end() ? it->second : -1;
    }
    if (type == InputBindingType::GamepadAxis) {
        const auto it = GamepadAxes().find(upper);
        return it != GamepadAxes().end() ? it->second : -1;
    }
    return -1;
}

InputMap InputMap::CreateDefault()
{
    InputMap map;
    map.actions = {
        {"Jump", {MakeBinding(InputBindingType::Keyboard, "SPACE"),
                  MakeBinding(InputBindingType::GamepadButton, "SOUTH")}},
        {"Interact", {MakeBinding(InputBindingType::Keyboard, "E"),
                      MakeBinding(InputBindingType::GamepadButton, "WEST")}},
        {"Sprint", {MakeBinding(InputBindingType::Keyboard, "LEFT_SHIFT"),
                    MakeBinding(InputBindingType::GamepadButton, "LEFT_STICK")}}
    };
    map.axes = {
        {"MoveForward", true, {
            MakeBinding(InputBindingType::Keyboard, "W", 1.0f),
            MakeBinding(InputBindingType::Keyboard, "S", -1.0f),
            MakeBinding(InputBindingType::GamepadAxis, "LEFT_Y", -1.0f)}},
        {"MoveRight", true, {
            MakeBinding(InputBindingType::Keyboard, "D", 1.0f),
            MakeBinding(InputBindingType::Keyboard, "A", -1.0f),
            MakeBinding(InputBindingType::GamepadAxis, "LEFT_X", 1.0f)}},
        {"LookX", false, {
            MakeBinding(InputBindingType::MouseDelta, "X", 1.0f),
            MakeBinding(InputBindingType::GamepadAxis, "RIGHT_X", 1.0f)}},
        {"LookY", false, {
            MakeBinding(InputBindingType::MouseDelta, "Y", 1.0f),
            MakeBinding(InputBindingType::GamepadAxis, "RIGHT_Y", 1.0f)}}
    };
    return map;
}

bool InputMap::Load(const std::filesystem::path& path, InputMap& outMap, std::string& outError)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        outError = "Could not open input map: " + path.generic_string();
        return false;
    }

    try {
        nlohmann::json root;
        input >> root;
        InputMap parsed;
        parsed.version = root.value("version", 0);
        if (parsed.version != VERSION) {
            outError = "Unsupported input map version: " + std::to_string(parsed.version);
            return false;
        }

        for (const auto& value : root.value("actions", nlohmann::json::array())) {
            InputActionDefinition action;
            action.name = value.value("name", "");
            for (const auto& bindingValue : value.value("bindings", nlohmann::json::array())) {
                InputBinding binding;
                if (!ParseBinding(bindingValue, binding, outError)) return false;
                action.bindings.push_back(std::move(binding));
            }
            parsed.actions.push_back(std::move(action));
        }

        for (const auto& value : root.value("axes", nlohmann::json::array())) {
            InputAxisDefinition axis;
            axis.name = value.value("name", "");
            axis.clamp = value.value("clamp", true);
            for (const auto& bindingValue : value.value("bindings", nlohmann::json::array())) {
                InputBinding binding;
                if (!ParseBinding(bindingValue, binding, outError)) return false;
                axis.bindings.push_back(std::move(binding));
            }
            parsed.axes.push_back(std::move(axis));
        }

        if (!parsed.Validate(outError)) return false;
        outMap = std::move(parsed);
        return true;
    } catch (const std::exception& error) {
        outError = "Failed to parse input map: " + std::string(error.what());
        return false;
    }
}

bool InputMap::Save(const std::filesystem::path& path, std::string& outError) const
{
    InputMap validated = *this;
    if (!validated.Validate(outError)) return false;

    nlohmann::json root;
    root["version"] = VERSION;
    root["actions"] = nlohmann::json::array();
    for (const auto& action : validated.actions) {
        nlohmann::json value = {{"name", action.name}, {"bindings", nlohmann::json::array()}};
        for (const auto& binding : action.bindings) {
            value["bindings"].push_back(WriteBinding(binding));
        }
        root["actions"].push_back(std::move(value));
    }
    root["axes"] = nlohmann::json::array();
    for (const auto& axis : validated.axes) {
        nlohmann::json value = {
            {"name", axis.name}, {"clamp", axis.clamp}, {"bindings", nlohmann::json::array()}
        };
        for (const auto& binding : axis.bindings) {
            value["bindings"].push_back(WriteBinding(binding));
        }
        root["axes"].push_back(std::move(value));
    }

    std::error_code errorCode;
    std::filesystem::create_directories(path.parent_path(), errorCode);
    if (errorCode) {
        outError = "Could not create input map directory: " + errorCode.message();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        outError = "Could not write input map: " + path.generic_string();
        return false;
    }
    output << root.dump(2) << '\n';
    if (!output.good()) {
        outError = "Failed while writing input map: " + path.generic_string();
        return false;
    }
    return true;
}

bool InputMap::Validate(std::string& outError)
{
    if (version != VERSION) {
        outError = "Input map version must be 1";
        return false;
    }

    std::unordered_set<std::string> names;
    const auto validateBindings = [&outError](auto& definition) {
        for (auto& binding : definition.bindings) {
            if (binding.control.empty()) {
                outError = "Input binding control cannot be empty";
                return false;
            }
            binding.controlCode = ResolveInputControl(binding.type, binding.control);
            if (binding.controlCode < 0) {
                outError = "Unknown input control: " + binding.control;
                return false;
            }
            if (binding.deadzone < 0.0f || binding.deadzone >= 1.0f) {
                outError = "Input binding deadzone must be in [0, 1)";
                return false;
            }
        }
        return true;
    };

    for (auto& action : actions) {
        if (action.name.empty() || !names.insert(action.name).second) {
            outError = action.name.empty() ? "Action name cannot be empty"
                                           : "Duplicate input name: " + action.name;
            return false;
        }
        if (!validateBindings(action)) return false;
    }
    for (auto& axis : axes) {
        if (axis.name.empty() || !names.insert(axis.name).second) {
            outError = axis.name.empty() ? "Axis name cannot be empty"
                                         : "Duplicate input name: " + axis.name;
            return false;
        }
        if (!validateBindings(axis)) return false;
    }
    return true;
}

} // namespace Hephaestus
