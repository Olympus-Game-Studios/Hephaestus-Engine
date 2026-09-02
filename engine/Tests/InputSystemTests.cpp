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

} // namespace

int main()
{
    bool ok = true;
    ok &= TestActionSemantics();
    ok &= TestAxesAndMultipleBindings();
    ok &= TestMapRoundTrip();
    if (ok) {
        std::cout << "InputSystemTests passed\n";
        return 0;
    }
    return 1;
}
