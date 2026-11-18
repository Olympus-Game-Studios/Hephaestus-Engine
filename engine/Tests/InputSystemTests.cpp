#include "Input/InputMap.h"
#include "Input/InputSystem.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return condition;
}

bool Near(float left, float right, float epsilon = 0.001f)
{
    return std::abs(left - right) <= epsilon;
}

bool TestActionSemantics()
{
    bool ok = true;
    Hephaestus::InputSystem input;

    input.BeginFrame();
    input.SetKeyState(GLFW_KEY_SPACE, true);
    ok &= Check(!input.IsActionDown("MissingAction"), "Unknown action is safely false");
    ok &= Check(Near(input.GetAxis("MissingAxis"), 0.0f), "Unknown axis is safely zero");
    ok &= Check(input.IsActionDown("Jump"), "Jump is down on first held frame");
    ok &= Check(input.IsActionPressed("Jump"), "Jump rising edge");
    ok &= Check(!input.IsActionReleased("Jump"), "Jump not released on rising edge");

    input.BeginFrame();
    input.SetKeyState(GLFW_KEY_SPACE, true);
    ok &= Check(input.IsActionDown("Jump"), "Jump remains down");
    ok &= Check(!input.IsActionPressed("Jump"), "Jump pressed fires once");

    input.BeginFrame();
    input.SetKeyState(GLFW_KEY_SPACE, false);
    ok &= Check(!input.IsActionDown("Jump"), "Jump no longer down");
    ok &= Check(input.IsActionReleased("Jump"), "Jump falling edge");
    return ok;
}

bool TestAxesAndMultipleBindings()
{
    bool ok = true;
    Hephaestus::InputSystem input;

    input.BeginFrame();
    input.SetKeyState(GLFW_KEY_W, true);
    ok &= Check(Near(input.GetAxis("MoveForward"), 1.0f), "W produces +MoveForward");
    input.SetKeyState(GLFW_KEY_S, true);
    ok &= Check(Near(input.GetAxis("MoveForward"), 0.0f), "Opposing keys cancel");

    input.SetKeyState(GLFW_KEY_W, false);
    input.SetKeyState(GLFW_KEY_S, false);
    input.SetGamepadConnected(true);
    input.SetGamepadAxisState(GLFW_GAMEPAD_AXIS_LEFT_Y, -0.575f);
    ok &= Check(Near(input.GetAxis("MoveForward"), 0.5f), "Stick deadzone is normalized");

    input.BeginFrame();
    input.SetGamepadConnected(true);
    input.SetGamepadButtonState(GLFW_GAMEPAD_BUTTON_A, true);
    ok &= Check(input.IsActionPressed("Jump"), "Gamepad binding triggers Jump");

    input.BeginFrame();
    input.SetMouseDelta(12.0, -3.0);
    ok &= Check(Near(input.GetAxis("LookX"), 12.0f), "Unclamped mouse X axis");
    ok &= Check(Near(input.GetAxis("LookY"), -3.0f), "Unclamped mouse Y axis");
    return ok;
}

bool TestMapRoundTrip()
{
    bool ok = true;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "hephaestus_input_system_tests";
    std::error_code errorCode;
    std::filesystem::remove_all(root, errorCode);

    Hephaestus::InputMap source = Hephaestus::InputMap::CreateDefault();
    const std::filesystem::path path = root / "Default.hpinput";
    std::string error;
    ok &= Check(source.Save(path, error), "Save default input map: " + error);

    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ok &= Check(text.find("\"context\"") == std::string::npos,
                ".hpinput v1 contains no unused context field");

    Hephaestus::InputMap loaded;
    error.clear();
    ok &= Check(Hephaestus::InputMap::Load(path, loaded, error), "Load input map: " + error);
    ok &= Check(loaded.version == 1, "Input map version");
    ok &= Check(loaded.actions.size() == source.actions.size(), "Action count round-trips");
    ok &= Check(loaded.axes.size() == source.axes.size(), "Axis count round-trips");

    loaded.actions.push_back(loaded.actions.front());
    error.clear();
    ok &= Check(!loaded.Validate(error), "Duplicate names are rejected");
    std::filesystem::remove_all(root, errorCode);
    return ok;
}

bool TestRuntimeRebinding()
{
    using Hephaestus::InputBinding;
    using Hephaestus::InputBindingType;
    bool ok = true;
    Hephaestus::InputSystem input;
    std::string error;

    InputBinding j;
    j.type = InputBindingType::Keyboard;
    j.control = "J";
    ok &= Check(input.SetBindings("Jump", {j}, error), "Jump rebinds to J: " + error);
    input.BeginFrame();
    input.SetKeyState(GLFW_KEY_SPACE, true);
    ok &= Check(!input.IsActionDown("Jump"), "the old key no longer triggers a rebound action");
    input.SetKeyState(GLFW_KEY_J, true);
    ok &= Check(input.IsActionDown("Jump") && input.IsActionPressed("Jump"), "the new key triggers it");
    ok &= Check(input.GetBindings("Jump")->front().control == "J" &&
                    input.GetDefaultBindings("Jump")->front().control == "SPACE",
                "the loaded map's bindings stay available for a reset");

    InputBinding upKey = j;
    upKey.control = "UP";
    ok &= Check(input.SetBindings("MoveForward", {upKey}, error) && Near(input.GetAxis("MoveForward"), 0.0f),
                "an axis takes new bindings");
    input.SetKeyState(GLFW_KEY_UP, true);
    ok &= Check(Near(input.GetAxis("MoveForward"), 1.0f), "the axis follows its new key");

    InputBinding bad = j;
    bad.control = "NOT_A_KEY";
    ok &= Check(!input.SetBindings("Jump", {bad}, error) && !error.empty() &&
                    input.GetBindings("Jump")->front().control == "J",
                "an unknown control is refused and leaves the bindings alone");
    ok &= Check(!input.SetBindings("Missing", {j}, error) && input.GetBindings("Missing") == nullptr,
                "an unknown action is refused");

    input.BeginFrame();
    InputBindingType type = InputBindingType::Keyboard;
    int code = -1;
    ok &= Check(!input.GetPressedControl(type, code), "nothing newly pressed while keys are only held");
    input.SetMouseButtonState(GLFW_MOUSE_BUTTON_RIGHT, true);
    ok &= Check(input.GetPressedControl(type, code) && type == InputBindingType::MouseButton &&
                    Hephaestus::InputControlName(type, code) == "RIGHT",
                "a newly pressed mouse button is reported by name");
    input.BeginFrame();
    input.SetKeyState(GLFW_KEY_LEFT_CONTROL, true);
    ok &= Check(input.GetPressedControl(type, code) && type == InputBindingType::Keyboard &&
                    Hephaestus::InputControlName(type, code) == "LEFT_CONTROL",
                "a newly pressed key is reported by its canonical name");
    for (const char* name : {"A", "7", "SPACE", "ESCAPE", "ENTER", "LEFT_SHIFT", "F12", "RIGHT_CONTROL"}) {
        const int key = Hephaestus::ResolveInputControl(InputBindingType::Keyboard, name);
        ok &= Check(Hephaestus::InputControlName(InputBindingType::Keyboard, key) == name,
                    std::string("key name round-trips: ") + name);
    }
    ok &= Check(Hephaestus::InputControlName(InputBindingType::GamepadButton, GLFW_GAMEPAD_BUTTON_A) == "SOUTH",
                "gamepad buttons use the face names");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= TestActionSemantics();
    ok &= TestAxesAndMultipleBindings();
    ok &= TestMapRoundTrip();
    ok &= TestRuntimeRebinding();
    if (ok) {
        std::cout << "InputSystemTests passed\n";
        return 0;
    }
    return 1;
}
