#pragma once

#include "InputMap.h"

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace Hephaestus {

class InputSystem {
public:
    static constexpr int MAX_KEYS = 512;
    static constexpr int MAX_MOUSE_BUTTONS = 8;
    static constexpr int MAX_GAMEPAD_BUTTONS = 15;
    static constexpr int MAX_GAMEPAD_AXES = 6;

    InputSystem();

    void BeginFrame();
    void Reset();

    void SetKeyState(int key, bool down);
    void SetMouseButtonState(int button, bool down);
    void SetMousePosition(double x, double y);
    void SetMouseDelta(double x, double y);
    void AddMouseDelta(double x, double y);
    void AddScrollDelta(double x, double y);
    void SetGamepadConnected(bool connected);
    void SetGamepadButtonState(int button, bool down);
    void SetGamepadAxisState(int axis, float value);

    bool IsKeyDown(int key) const;
    bool IsMouseButtonDown(int button) const;
    double GetMouseX() const;
    double GetMouseY() const;
    double GetMouseDeltaX() const;
    double GetMouseDeltaY() const;
    double GetScrollDeltaX() const;
    double GetScrollDeltaY() const;
    bool IsGamepadConnected() const;

    bool IsActionDown(const std::string& name) const;
    bool IsActionPressed(const std::string& name) const;
    bool IsActionReleased(const std::string& name) const;
    float GetAxis(const std::string& name) const;
    bool HasAction(const std::string& name) const;
    bool HasAxis(const std::string& name) const;

    void SetMap(InputMap map);
    const InputMap& GetMap() const { return m_Map; }
    bool LoadMap(const std::filesystem::path& path, std::string& outError);
    const std::filesystem::path& GetMapPath() const { return m_MapPath; }

    void SetCursorModeRequest(int mode) { m_CursorModeRequest = mode == 1 ? 1 : 0; }
    int GetCursorModeRequest() const { return m_CursorModeRequest; }

private:
    struct DeviceState {
        std::array<bool, MAX_KEYS> keys{};
        std::array<bool, MAX_MOUSE_BUTTONS> mouseButtons{};
        std::array<bool, MAX_GAMEPAD_BUTTONS> gamepadButtons{};
        std::array<float, MAX_GAMEPAD_AXES> gamepadAxes{};
        double mouseX = 0.0;
        double mouseY = 0.0;
        double mouseDeltaX = 0.0;
        double mouseDeltaY = 0.0;
        double scrollDeltaX = 0.0;
        double scrollDeltaY = 0.0;
        bool gamepadConnected = false;
    };

    bool EvaluateAction(const InputActionDefinition& action, const DeviceState& state) const;
    float EvaluateAxis(const InputAxisDefinition& axis, const DeviceState& state) const;
    float EvaluateBinding(const InputBinding& binding, const DeviceState& state) const;
    void RebuildLookup();

    InputMap m_Map;
    std::filesystem::path m_MapPath;
    DeviceState m_Current;
    DeviceState m_Previous;
    std::unordered_map<std::string, std::size_t> m_ActionLookup;
    std::unordered_map<std::string, std::size_t> m_AxisLookup;
    int m_CursorModeRequest = 0;
};

} // namespace Hephaestus
